#include "Environment/SwcEnvironmentSubsystem.h"

#include "Async/Async.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Creatures/SwcCreatureData.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Environment/SwcWorldTimeSubsystem.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Net/SwcSpace.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{

namespace env = swc::environment;

/** The log of the state (s); -SwcEnvironmentProbe: each 1 s. */
constexpr double LogSeconds = 60.0;
/** The histogram of the auto exposure (log2 of the luminance, cd/m2): wide enough for the 100,000 lux sun of the data. */
constexpr float HistogramLogMin = -6.0f;
constexpr float HistogramLogMax = 16.0f;
/** The bounds of the auto exposure (EV100; the decision of the user: the Unreal exposure for a readable picture, stage E5 refines
 *  it): a twilight and a night adapt, a noon does not burn out. */
constexpr double ExposureMinEv = 3.0;
constexpr double ExposureMaxEv = 16.0;
/** The moon fades in as the sun goes below the horizon: full at this angle (degrees) below it. */
constexpr double TwilightDegrees = 4.0;
/** The luminance of an EV100 (cd/m2): 2^EV x 12.5 / 100. */
float LuminanceOfEv(double Ev)
{
	return static_cast<float>(FMath::Pow(2.0, Ev) * 0.125);
}

TSharedPtr<FJsonObject> ReadJson(const FString& Path)
{
	FString Text;
	TSharedPtr<FJsonObject> Object;
	if (!FFileHelper::LoadFileToString(Text, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Object))
	{
		return nullptr;
	}
	return Object;
}

double NumberOf(const TSharedPtr<FJsonObject>& Object, const TCHAR* Name, double Default)
{
	if (!Object.IsValid() || !Object->HasField(Name))
	{
		return Default;
	}
	const TSharedPtr<FJsonValue> Value = Object->TryGetField(Name);
	return Value->Type == EJson::String ? FCString::Atod(*Value->AsString()) : Value->AsNumber();
}

/** @return The variables of a preset (the first variable of a name): [time, value or [r, g, b], flags] keys, or the saved value. */
TMap<FString, env::Curve> ReadPreset(const TSharedPtr<FJsonObject>& Preset)
{
	TMap<FString, env::Curve> Out;
	const TArray<TSharedPtr<FJsonValue>>* Variables = nullptr;
	if (!Preset.IsValid() || !Preset->TryGetArrayField(TEXT("variables"), Variables))
	{
		return Out;
	}
	for (const TSharedPtr<FJsonValue>& Value : *Variables)
	{
		const TSharedPtr<FJsonObject> Variable = Value->AsObject();
		const FString Name = Variable->GetStringField(TEXT("name"));
		if (Out.Contains(Name))
		{
			continue;
		}
		env::Curve Curve;
		for (const TSharedPtr<FJsonValue>& KeyValue : Variable->GetArrayField(TEXT("keys")))
		{
			const TArray<TSharedPtr<FJsonValue>>& Key = KeyValue->AsArray();
			if (Key.Num() < 3)
			{
				continue;
			}
			env::Key Item;
			Item.time = Key[0]->AsNumber();
			Item.flags = static_cast<std::uint32_t>(Key[2]->AsNumber());
			if (Key[1]->Type == EJson::Array)
			{
				const TArray<TSharedPtr<FJsonValue>>& Rgb = Key[1]->AsArray();
				Curve.channels = 3;
				for (int32 C = 0; C < 3 && C < Rgb.Num(); ++C)
				{
					Item.value[C] = Rgb[C]->AsNumber();
				}
			}
			else
			{
				Item.value[0] = Key[1]->AsNumber();
			}
			Curve.keys.push_back(Item);
		}
		const TSharedPtr<FJsonValue> Saved = Variable->TryGetField(TEXT("saved"));
		if (Saved.IsValid() && Saved->Type == EJson::Array)
		{
			const TArray<TSharedPtr<FJsonValue>>& Rgb = Saved->AsArray();
			Curve.channels = 3;
			for (int32 C = 0; C < 3 && C < Rgb.Num(); ++C)
			{
				Curve.saved[C] = Rgb[C]->AsNumber();
			}
		}
		else if (Saved.IsValid() && Saved->Type == EJson::Number)
		{
			Curve.saved[0] = Saved->AsNumber();
		}
		Out.Add(Name, MoveTemp(Curve));
	}
	return Out;
}

env::Volume ReadVolume(const TSharedPtr<FJsonObject>& Object)
{
	env::Volume Volume;
	const TArray<TSharedPtr<FJsonValue>>& Rows = Object->GetArrayField(TEXT("matrix"));
	for (int32 R = 0; R < 4 && R < Rows.Num(); ++R)
	{
		const TArray<TSharedPtr<FJsonValue>>& Row = Rows[R]->AsArray();
		for (int32 C = 0; C < 4 && C < Row.Num(); ++C)
		{
			Volume.matrix[static_cast<size_t>(R * 4 + C)] = Row[C]->AsNumber();
		}
	}
	Volume.inverse = env::inverse_of(Volume.matrix);
	const TSharedPtr<FJsonObject>* Shape = nullptr;
	if (Object->TryGetObjectField(TEXT("shape"), Shape) && Shape != nullptr)
	{
		const FString Type = (*Shape)->GetStringField(TEXT("type"));
		Volume.shape = Type == TEXT("cylinder") ? env::Volume::Shape::cylinder : Type == TEXT("box") ? env::Volume::Shape::box
			: Type == TEXT("sphere") ? env::Volume::Shape::sphere : Type == TEXT("capsule") ? env::Volume::Shape::capsule
			: Type == TEXT("polygonPrism") ? env::Volume::Shape::polygonPrism : env::Volume::Shape::none;
		Volume.radius = NumberOf(*Shape, TEXT("radius"), 0.0);
		Volume.height = NumberOf(*Shape, TEXT("height"), 0.0);
		const TArray<TSharedPtr<FJsonValue>>* Dimensions = nullptr;
		if ((*Shape)->TryGetArrayField(TEXT("dimensions"), Dimensions) && Dimensions->Num() == 3)
		{
			Volume.dimensions = {(*Dimensions)[0]->AsNumber(), (*Dimensions)[1]->AsNumber(), (*Dimensions)[2]->AsNumber()};
		}
		const TArray<TSharedPtr<FJsonValue>>* Vertices = nullptr;
		if ((*Shape)->TryGetArrayField(TEXT("vertices"), Vertices))
		{
			for (const TSharedPtr<FJsonValue>& Vertex : *Vertices)
			{
				const TArray<TSharedPtr<FJsonValue>>& Xy = Vertex->AsArray();
				if (Xy.Num() >= 2)
				{
					Volume.vertices.push_back({Xy[0]->AsNumber(), Xy[1]->AsNumber()});
				}
			}
		}
	}
	Volume.preset = TCHAR_TO_UTF8(*Object->GetStringField(TEXT("preset")));
	Volume.priority = static_cast<int>(NumberOf(Object, TEXT("priority"), 0.0));
	Volume.blendTime = NumberOf(Object, TEXT("blendTime"), 0.0);
	const TSharedPtr<FJsonValue> Override = Object->TryGetField(TEXT("hourOverride"));
	Volume.hasHourOverride = Override.IsValid() && Override->Type == EJson::Number;
	Volume.hourOverride = Volume.hasHourOverride ? Override->AsNumber() : 0.0;
	FString Sky;
	if (Object->TryGetStringField(TEXT("sky"), Sky))
	{
		Volume.sky = TCHAR_TO_UTF8(*Sky);
	}
	return Volume;
}

/** @return The data of SWC_CONTENT/environment (a worker thread). */
TSharedPtr<USwcEnvironmentSubsystem::FData, ESPMode::ThreadSafe> LoadData(const FString& Folder)
{
	TSharedPtr<USwcEnvironmentSubsystem::FData, ESPMode::ThreadSafe> Out = MakeShared<USwcEnvironmentSubsystem::FData, ESPMode::ThreadSafe>();
	const double Start = FPlatformTime::Seconds();
	const TSharedPtr<FJsonObject> Mission = ReadJson(FPaths::Combine(Folder, TEXT("mission.json")));
	const TSharedPtr<FJsonObject> Presets = ReadJson(FPaths::Combine(Folder, TEXT("presets.json")));
	const TSharedPtr<FJsonObject> Volumes = ReadJson(FPaths::Combine(Folder, TEXT("volumes.json")));
	if (!Mission.IsValid() || !Presets.IsValid() || !Volumes.IsValid())
	{
		Out->Report = FString::Printf(TEXT("environment: the data of %s does not read (tools/convert-environment.sh write)"), *Folder);
		return Out;
	}
	const TSharedPtr<FJsonObject> Environment = Mission->GetObjectField(TEXT("environment"));
	const TSharedPtr<FJsonObject>* Lighting = nullptr;
	const TSharedPtr<FJsonObject>* Moon = nullptr;
	if (Environment.IsValid() && Environment->TryGetObjectField(TEXT("Lighting"), Lighting))
	{
		Out->SunRotation = NumberOf(*Lighting, TEXT("SunRotation"), Out->SunRotation);
		Out->Longitude = NumberOf(*Lighting, TEXT("Longitude"), Out->Longitude);
	}
	if (Environment.IsValid() && Environment->TryGetObjectField(TEXT("Moon"), Moon))
	{
		Out->MoonLatitude = NumberOf(*Moon, TEXT("Latitude"), Out->MoonLatitude);
		Out->MoonLongitude = NumberOf(*Moon, TEXT("Longitude"), Out->MoonLongitude);
		Out->MoonSize = NumberOf(*Moon, TEXT("Size"), Out->MoonSize);
	}
	const TSharedPtr<FJsonObject>* Base = nullptr;
	if (Mission->TryGetObjectField(TEXT("timeOfDay"), Base))
	{
		Out->Presets.Add(TEXT("mission"), ReadPreset(*Base));
	}
	for (const auto& Pair : Presets->Values)
	{
		Out->Presets.Add(FString(Pair.Key), ReadPreset(Pair.Value->AsObject()));
	}
	for (const TSharedPtr<FJsonValue>& Value : Volumes->GetArrayField(TEXT("volumes")))
	{
		const TSharedPtr<FJsonObject> Object = Value->AsObject();
		if (Object.IsValid() && Object->GetStringField(TEXT("kind")) == TEXT("timeOfDay"))
		{
			Out->Volumes.push_back(ReadVolume(Object));
		}
	}
	Out->bOk = true;
	Out->Report = FString::Printf(TEXT("environment: %d preset(s), %d time-of-day volume(s); the sun rotation %.1f, longitude %.1f; the moon %.1f %.1f size %.2f (%.0f ms)"),
		Out->Presets.Num(), static_cast<int32>(Out->Volumes.size()), Out->SunRotation, Out->Longitude, Out->MoonLatitude, Out->MoonLongitude, Out->MoonSize,
		(FPlatformTime::Seconds() - Start) * 1000.0);
	return Out;
}

/** @return The rotation of a directional light whose light comes from an engine direction (toward the light). */
FRotator LightRotation(const env::Vec3& Toward)
{
	return FVector(-Toward.x, Toward.y, -Toward.z).Rotation();   // engine (x, y, z) to Unreal (x, -y, z), then the opposite
}

}

void USwcEnvironmentSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	bDisabled = FParse::Param(FCommandLine::Get(), TEXT("SwcNoEnvironment"));
	bProbe = FParse::Param(FCommandLine::Get(), TEXT("SwcEnvironmentProbe"));
	bAmbientOnly = FParse::Param(FCommandLine::Get(), TEXT("SwcAmbientOnly"));
	FParse::Value(FCommandLine::Get(), TEXT("SwcNightExposure="), NightExposure);
	FParse::Value(FCommandLine::Get(), TEXT("SwcNightSky="), NightSkyFactor);
}

void USwcEnvironmentSubsystem::Deinitialize()
{
	if (Actor.IsValid())
	{
		Actor->Destroy();
	}
	Super::Deinitialize();
}

bool USwcEnvironmentSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId USwcEnvironmentSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USwcEnvironmentSubsystem, STATGROUP_Tickables);
}

void USwcEnvironmentSubsystem::StartLoad()
{
	bLoading = true;
	const FString Folder = FPaths::Combine(FSwcCreatureLibrary::ContentFolder(), TEXT("environment"));
	TWeakObjectPtr<USwcEnvironmentSubsystem> Self(this);
	Async(EAsyncExecution::ThreadPool, [Self, Folder]()
	{
		TSharedPtr<FData, ESPMode::ThreadSafe> Loaded = LoadData(Folder);
		AsyncTask(ENamedThreads::GameThread, [Self, Loaded]()
		{
			if (USwcEnvironmentSubsystem* Subsystem = Self.Get())
			{
				Subsystem->Data = Loaded;
				Subsystem->bLoading = false;
				UE_LOG(LogSwcEnvironment, Log, TEXT("%s"), *Loaded->Report);
			}
		});
	});
}

bool USwcEnvironmentSubsystem::CreateEnvironment()
{
	UWorld* World = GetWorld();
	FActorSpawnParameters Parameters;
	Parameters.ObjectFlags |= RF_Transient;
	AActor* Owner = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Parameters);
	if (Owner == nullptr)
	{
		return false;
	}
	USceneComponent* Root = NewObject<USceneComponent>(Owner, TEXT("SwcEnvironmentRoot"));
	Owner->SetRootComponent(Root);
	Root->SetMobility(EComponentMobility::Movable);
	Root->RegisterComponent();
	auto MakeLight = [&](const TCHAR* Name, int32 Index)
	{
		UDirectionalLightComponent* Light = NewObject<UDirectionalLightComponent>(Owner, Name);
		Light->SetMobility(EComponentMobility::Movable);
		Light->SetAtmosphereSunLight(true);
		Light->SetAtmosphereSunLightIndex(Index);
		Light->SetCastShadows(true);
		Light->SetupAttachment(Root);
		Light->RegisterComponent();
		return Light;
	};
	Sun = MakeLight(TEXT("SwcSun"), 0);
	Moon = MakeLight(TEXT("SwcMoon"), 1);
	Moon->SetLightSourceAngle(static_cast<float>(Data->MoonSize));   // an inference: the Size of the Moon element as degrees
	USkyAtmosphereComponent* Sky = NewObject<USkyAtmosphereComponent>(Owner, TEXT("SwcSkyAtmosphere"));
	Sky->SetupAttachment(Root);   // the physical sky of Unreal: the sunrise, the sunset, and the twilight come from the atmosphere
	Sky->RegisterComponent();
	Atmosphere = Sky;
	USkyLightComponent* Ambient = NewObject<USkyLightComponent>(Owner, TEXT("SwcSkyLight"));
	Ambient->SetMobility(EComponentMobility::Movable);
	Ambient->bRealTimeCapture = true;   // the renderer captures the current sky each frame (a static capture gave no light here)
	Ambient->SourceType = ESkyLightSourceType::SLS_CapturedScene;
	Ambient->SetupAttachment(Root);
	Ambient->RegisterComponent();
	SkyLight = Ambient;
	UPostProcessComponent* Post = NewObject<UPostProcessComponent>(Owner, TEXT("SwcExposure"));
	Post->bUnbound = true;
	Post->Priority = 100.0f;   // over the fixed exposure of the level
	FPostProcessSettings& Settings = Post->Settings;
	Settings.bOverride_AutoExposureMethod = true;
	Settings.AutoExposureMethod = EAutoExposureMethod::AEM_Histogram;
	Settings.bOverride_AutoExposureMinBrightness = true;
	Settings.bOverride_AutoExposureMaxBrightness = true;
	Settings.bOverride_AutoExposureBias = true;
	Settings.bOverride_HistogramLogMin = true;
	Settings.bOverride_HistogramLogMax = true;
	Settings.HistogramLogMin = HistogramLogMin;
	Settings.HistogramLogMax = HistogramLogMax;
	Post->SetupAttachment(Root);
	Post->RegisterComponent();
	Exposure = Post;
	Actor = Owner;
	return true;
}

void USwcEnvironmentSubsystem::TurnOffLevelEnvironment()
{
	int32 Count = 0;
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		if (*It == Actor.Get())
		{
			continue;
		}
		TInlineComponentArray<UActorComponent*> Components(*It);
		for (UActorComponent* Component : Components)
		{
			if ((Component->IsA<UDirectionalLightComponent>() || Component->IsA<USkyAtmosphereComponent>() || Component->IsA<USkyLightComponent>())
				&& Component->IsRegistered())
			{
				Component->UnregisterComponent();
				++Count;
			}
		}
	}
	UE_LOG(LogSwcEnvironment, Log, TEXT("environment: %d sun, sky, and sky light component(s) of the level turned off (the fog and the ocean of the level stay)"), Count);
}

double USwcEnvironmentSubsystem::Value(const TMap<FString, env::Curve>& Preset, const TCHAR* Name, double Time, double Default) const
{
	const env::Curve* Curve = Preset.Find(Name);
	return Curve != nullptr ? env::evaluate(*Curve, Time)[0] : Default;
}

std::array<double, 3> USwcEnvironmentSubsystem::Color(const TMap<FString, env::Curve>& Preset, const TCHAR* Name, double Time) const
{
	const env::Curve* Curve = Preset.Find(Name);
	return Curve != nullptr ? env::evaluate(*Curve, Time) : std::array<double, 3>{1.0, 1.0, 1.0};
}

void USwcEnvironmentSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (bDisabled)
	{
		return;
	}
	if (!Data.IsValid())
	{
		if (!bLoading)
		{
			StartLoad();
		}
		return;
	}
	if (!Data->bOk)
	{
		return;
	}
	UWorld* World = GetWorld();
	const USwcWorldTimeSubsystem* Clock = World->GetSubsystem<USwcWorldTimeSubsystem>();
	APlayerController* Controller = World->GetFirstPlayerController();
	if (Clock == nullptr || !Clock->HasWorldTime() || Controller == nullptr || Controller->PlayerCameraManager == nullptr)
	{
		return;   // before the first record of the world clock: the environment of the level
	}
	if (!Actor.IsValid())
	{
		if (!CreateEnvironment())
		{
			return;
		}
		TurnOffLevelEnvironment();
	}
	// The preset at the camera: the time-of-day volume of the highest priority, else the base preset of the mission.
	const swc::net::WorldPosition Camera = SwcUnrealToEngine(Controller->PlayerCameraManager->GetCameraLocation());
	const int32 Index = env::select_volume(Data->Volumes, env::Vec3{Camera.x, Camera.y, Camera.z});
	const env::Volume* Volume = Index >= 0 ? &Data->Volumes[static_cast<size_t>(Index)] : nullptr;
	const FString Name = Volume != nullptr ? FString(UTF8_TO_TCHAR(Volume->preset.c_str())) : FString(TEXT("mission"));
	const TMap<FString, env::Curve>* Preset = Data->Presets.Find(Name);
	if (Preset == nullptr)
	{
		Preset = Data->Presets.Find(TEXT("mission"));
	}
	if (Preset == nullptr)
	{
		return;
	}
	const double Hour = Volume != nullptr && Volume->hasHourOverride ? Volume->hourOverride : Clock->GetHour();
	const double Time = env::curve_time(Hour);
	const swc::net::TimeRecord& Record = Clock->GetRecord();
	const env::Edges Edges = env::edges_of(Record.dawnTime, Record.dawnDuration, Record.duskTime, Record.duskDuration, Record.dayLength);
	const env::LightMultipliers Multipliers = env::light_multipliers(Hour, Edges);
	// The sun (the Unreal way, the decision of the user): always on, on the path of the data, with the noon "Sun intensity" and "Sun
	// color" of the preset; the atmosphere makes the sunrise, the sunset, and the twilight. The moon keeps the data: its direction,
	// the night "Sun intensity" and "Sun color" of the hour, and the fade at the visual dawn and dusk of the record.
	const double Noon = env::curve_time(12.0);
	const double SunLux = bAmbientOnly ? 0.0 : FMath::Max(0.0, Value(*Preset, TEXT("Sun intensity"), Noon, 100000.0));
	const env::LightColor SunColor = env::light_color(Color(*Preset, TEXT("Sun color"), Noon));
	const double MoonLux = bAmbientOnly ? 0.0 : FMath::Max(0.0, Value(*Preset, TEXT("Sun intensity"), Time, 0.0));
	const env::LightColor MoonColor = env::light_color(Color(*Preset, TEXT("Sun color"), Time));
	const env::Vec3 SunToward = env::sun_toward(Hour, Data->SunRotation, Data->Longitude);
	const env::Vec3 MoonToward = env::moon_toward(Data->MoonLatitude, Data->MoonLongitude);
	// The weight of the moon: the fade of the source at the visual dawn and dusk of the record, or the sun below the horizon (0 at the
	// sunset, 1 at TwilightDegrees below it): the moon comes when the physical sun goes (the night values of "Sun intensity" start
	// at 18:05, just after the sunset of the path at 18:00).
	const double Elevation = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(SunToward.z, -1.0, 1.0)));
	const double MoonWeight = FMath::Max(Multipliers.moon, FMath::Clamp(-Elevation / TwilightDegrees, 0.0, 1.0));
	NightFactor = MoonWeight;
	auto Linear = [](const env::LightColor& In)
	{
		return FLinearColor(static_cast<float>(In.color[0]), static_cast<float>(In.color[1]), static_cast<float>(In.color[2]));
	};
	if (UDirectionalLightComponent* Light = Sun.Get())
	{
		Light->SetWorldRotation(LightRotation(SunToward));
		Light->SetIntensity(static_cast<float>(SunLux * SunColor.intensityFactor));
		Light->SetLightColor(Linear(SunColor));
	}
	if (UDirectionalLightComponent* Light = Moon.Get())
	{
		Light->SetWorldRotation(LightRotation(MoonToward));
		Light->SetIntensity(static_cast<float>(MoonLux * MoonWeight * MoonColor.intensityFactor));
		Light->SetLightColor(Linear(MoonColor));
		Light->SetVisibility(MoonWeight > 0.0);
	}
	// The night sky: the atmosphere lit by the moon is a day sky; the factor darkens it as the moon fades in (a look choice).
	if (USkyAtmosphereComponent* Sky = Atmosphere.Get())
	{
		const float Factor = static_cast<float>(FMath::Lerp(1.0, NightSkyFactor, MoonWeight));
		Sky->SetSkyLuminanceFactor(FLinearColor(Factor, Factor, Factor));
	}
	// The Unreal auto exposure with wide bounds; by night a darker compensation keeps a night look.
	const double Compensation = FMath::Lerp(0.0, NightExposure, MoonWeight);
	if (UPostProcessComponent* Post = Exposure.Get())
	{
		Post->Settings.AutoExposureMinBrightness = LuminanceOfEv(ExposureMinEv);
		Post->Settings.AutoExposureMaxBrightness = LuminanceOfEv(ExposureMaxEv);
		Post->Settings.AutoExposureBias = static_cast<float>(Compensation);
	}
	const double Now = FPlatformTime::Seconds();
	if (Name != PresetName || Index != VolumeIndex || Now >= NextLog)
	{
		NextLog = Now + (bProbe ? 1.0 : LogSeconds);
		UE_LOG(LogSwcEnvironment, Log, TEXT("environment: hour %.4f%s, preset %s%s; sun %.0f lux, elevation %.1f (toward %.3f %.3f %.3f), color %.3f %.3f %.3f; "
			"moon %.0f lux x %.3f, color %.3f %.3f %.3f; night sky %.2f; exposure EV100 %.1f to %.1f, compensation %.2f"),
			Hour, Volume != nullptr && Volume->hasHourOverride ? TEXT(" (the hour override of the volume)") : TEXT(""), *Name,
			Volume != nullptr ? *FString::Printf(TEXT(" (volume %d, priority %d)"), Index, Volume->priority) : TEXT(" (no volume: the mission)"),
			SunLux, Elevation, SunToward.x, SunToward.y, SunToward.z, SunColor.color[0], SunColor.color[1], SunColor.color[2],
			MoonLux, MoonWeight, MoonColor.color[0], MoonColor.color[1], MoonColor.color[2], NightSkyFactor, ExposureMinEv, ExposureMaxEv, Compensation);
		PresetName = Name;
		VolumeIndex = Index;
	}
}
