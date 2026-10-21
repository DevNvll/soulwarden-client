#include "Environment/SwcLocalLights.h"

#include "Async/Async.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/PointLightComponent.h"
#include "Components/RectLightComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SpotLightComponent.h"
#include "Creatures/SwcCreature.h"
#include "Creatures/SwcCreatureData.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Environment/SwcWorldTimeSubsystem.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{

namespace env = swc::environment;

/** The look scale of the intensities (-SwcLightScale= sets it at the start; a change applies to the active lights). The default 1 is
 *  the source unit (stage E5, research/color-pipeline.md rule 6: one conversion for the whole scene, the decision of 2026-10-20); 3 was
 *  the look choice of stage E2 (the lamps against the moon and the sky of the retail night pictures of Oxboro). */
constexpr float DefaultLookScale = 1.0f;
TAutoConsoleVariable<float> CVarLightScale(TEXT("swc.LightScale"), DefaultLookScale,
	TEXT("The look scale of the local lights and the Illum emission (1: the source light unit, the default; 3: the look of stage E2)."));
/** The local lights on (1) or hidden (0): a compare of the cost in one run. */
TAutoConsoleVariable<int32> CVarLocalLights(TEXT("swc.LocalLights"), 1, TEXT("The local lights of stage E2: 1 on, 0 hidden."));
/** The shadows of the local lights: 0 none, 1 the source flags (the default), 2 all (-SwcLightShadows=none|source|all at the start). */
TAutoConsoleVariable<int32> CVarLightShadows(TEXT("swc.LightShadows"), 1, TEXT("The shadows of the local lights of stage E2: 0 none, 1 the source flags, 2 all."));
/** The rule of the point lights (-SwcPointLightRule= at the start): 1 the source rule of research/lights-emission.md (the default),
 *  0 the old rule (the bulb as the sphere radius, pi x 10,000 x D x bulb^2), for a compare in one run. */
TAutoConsoleVariable<int32> CVarPointLightRule(TEXT("swc.PointLightRule"), 1,
	TEXT("The point lights: 1 the source rule (radius 0, the near clamp and the range fade of M_SwcPointFalloff), 0 the old sphere rule."));
/** The distance of the falloff function from the camera (m): the per-light pass of a light function costs about 1 ms of GPU for each
 *  100 point lights (p5-light, the Oxboro town hall and inn); beyond it a point light has the plain inverse-square falloff. */
TAutoConsoleVariable<float> CVarPointFalloffDistance(TEXT("swc.PointFalloffDistance"), 40.0f,
	TEXT("The point lights within this distance of the camera (m) have the falloff function of the source rule (M_SwcPointFalloff)."));
const TCHAR* PointFalloffPath = TEXT("/SwcGameContent/Environment/M_SwcPointFalloff.M_SwcPointFalloff");

/** The update of the static lights (s), of the bound NPC lights (s), and the log of the counts (s; -SwcLightProbe: 1 s). */
constexpr double UpdateSeconds = 0.25;
constexpr double BoundSeconds = 0.5;
constexpr double LogSeconds = 30.0;
/** A light comes in its draw distance plus this margin and goes at its draw distance plus the hysteresis (m). */
constexpr double ComeMargin = 10.0;
constexpr double GoMargin = 30.0;
/** The largest number of new light components in one update (a teleport adds several hundred: spread over a few updates). */
constexpr int32 CreateBudget = 128;
/** The Unreal fade at the draw distance (a part of it). */
constexpr float FadePart = 0.2f;

double NumberOf(const TSharedPtr<FJsonObject>& Object, const TCHAR* Name, double Default)
{
	double Value = Default;
	return Object->TryGetNumberField(Name, Value) ? Value : Default;
}

env::Vec3 VectorOf(const TSharedPtr<FJsonObject>& Object, const TCHAR* Name, const env::Vec3& Default)
{
	const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
	if (!Object->TryGetArrayField(Name, Values) || Values->Num() < 3)
	{
		return Default;
	}
	return {(*Values)[0]->AsNumber(), (*Values)[1]->AsNumber(), (*Values)[2]->AsNumber()};
}

env::LocalLight ReadLight(const TSharedPtr<FJsonObject>& Object)
{
	env::LocalLight Light;
	const FString Type = Object->GetStringField(TEXT("type"));
	Light.type = Type == TEXT("spot") ? env::LocalLight::Type::spot : Type == TEXT("rect") ? env::LocalLight::Type::rect : env::LocalLight::Type::point;
	Light.p = VectorOf(Object, TEXT("p"), Light.p);
	Light.x = VectorOf(Object, TEXT("x"), Light.x);
	Light.z = VectorOf(Object, TEXT("z"), Light.z);
	const env::Vec3 Color = VectorOf(Object, TEXT("color"), {1.0, 1.0, 1.0});
	Light.color = {Color.x, Color.y, Color.z};
	Light.diffuse = NumberOf(Object, TEXT("diffuse"), 1.0);
	Light.spec = NumberOf(Object, TEXT("spec"), 1.0);
	Light.range = NumberOf(Object, TEXT("range"), 0.0);
	Light.bulb = NumberOf(Object, TEXT("bulb"), 0.0);
	Light.fov = NumberOf(Object, TEXT("fov"), 90.0);
	Light.width = NumberOf(Object, TEXT("w"), 0.0);
	Light.height = NumberOf(Object, TEXT("h"), 0.0);
	Light.cap = NumberOf(Object, TEXT("cap"), 0.0);
	Object->TryGetBoolField(TEXT("on"), Light.on);
	Object->TryGetBoolField(TEXT("shadow"), Light.shadow);
	Object->TryGetBoolField(TEXT("volumetric"), Light.volumetric);
	FString Text;
	if (Object->TryGetStringField(TEXT("cookie"), Text) && !Text.IsEmpty())
	{
		Text = Text.ToLower().Replace(TEXT("\\"), TEXT("/"));
		Text.RemoveFromStart(TEXT("@assets@/"));   // the key of cookies.json (converter cookie_key)
		Light.cookie = TCHAR_TO_UTF8(*Text);
	}
	if (Object->TryGetStringField(TEXT("npc"), Text))
	{
		Light.npc = TCHAR_TO_UTF8(*Text);
		Light.root = VectorOf(Object, TEXT("root"), Light.p);
		const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
		if (Object->TryGetArrayField(TEXT("local"), Rows))
		{
			for (int32 R = 0; R < 4 && R < Rows->Num(); ++R)
			{
				const TArray<TSharedPtr<FJsonValue>>& Row = (*Rows)[R]->AsArray();
				for (int32 C = 0; C < 4 && C < Row.Num(); ++C)
				{
					Light.local[static_cast<size_t>(R * 4 + C)] = Row[C]->AsNumber();
				}
			}
		}
	}
	return Light;
}

/** @return The lights of SWC_CONTENT/environment/lights (a worker thread). */
TSharedPtr<USwcLocalLightSubsystem::FData, ESPMode::ThreadSafe> LoadData(const FString& Folder)
{
	TSharedPtr<USwcLocalLightSubsystem::FData, ESPMode::ThreadSafe> Out = MakeShared<USwcLocalLightSubsystem::FData, ESPMode::ThreadSafe>();
	const double Start = FPlatformTime::Seconds();
	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *FPaths::Combine(Folder, TEXT("*.json")), true, false);
	int32 Off = 0;
	for (const FString& File : Files)
	{
		FString Text;
		TSharedPtr<FJsonObject> Object;
		if (!FFileHelper::LoadFileToString(Text, *FPaths::Combine(Folder, File)) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Object)
			|| !Object.IsValid())
		{
			continue;
		}
		for (const TSharedPtr<FJsonValue>& Value : Object->GetArrayField(TEXT("lights")))
		{
			env::LocalLight Light = ReadLight(Value->AsObject());
			if (!Light.on)
			{
				++Off;   // OnInitially false: no script of the client turns it on
				continue;
			}
			const int32 Index = static_cast<int32>(Out->Lights.size());
			if (Light.npc.empty())
			{
				Out->Grid.add(Index, Light.p);
			}
			else
			{
				Out->NpcLights.Add(Index);
			}
			Out->Lights.push_back(MoveTemp(Light));
		}
	}
	// The cookies: the material instance of each converted projector texture (tools/import-cookies.sh).
	FString CookieText;
	TSharedPtr<FJsonObject> CookieObject;
	const TSharedPtr<FJsonObject>* Cookies = nullptr;
	if (FFileHelper::LoadFileToString(CookieText, *FPaths::Combine(FPaths::GetPath(Folder), TEXT("cookies.json")))
		&& FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(CookieText), CookieObject) && CookieObject.IsValid()
		&& CookieObject->TryGetObjectField(TEXT("cookies"), Cookies))
	{
		for (const auto& Pair : (*Cookies)->Values)
		{
			const FString Name = TEXT("MI_") + Pair.Value->AsObject()->GetStringField(TEXT("name"));
			Out->CookieMaterials.Add(FString(Pair.Key), FString::Printf(TEXT("/SwcGameContent/Environment/Cookies/%s.%s"), *Name, *Name));
		}
	}
	Out->bOk = !Out->Lights.empty();
	Out->Report = Out->bOk
		? FString::Printf(TEXT("local lights: %d light(s) of %d region file(s) (%d static, %d of NPC slices; %d off at the start), %d cookie(s) (%.0f ms)"),
			static_cast<int32>(Out->Lights.size()), Files.Num(), static_cast<int32>(Out->Grid.size()), Out->NpcLights.Num(), Off, Out->CookieMaterials.Num(),
			(FPlatformTime::Seconds() - Start) * 1000.0)
		: FString::Printf(TEXT("local lights: no light in %s (tools/convert-environment.sh write)"), *Folder);
	return Out;
}

FVector UnrealPlace(const env::Vec3& P)
{
	return FVector(P.x * 100.0, -P.y * 100.0, P.z * 100.0);
}

/** @return The Unreal rotation of the engine axes X and Z of an entity (the map C = diag(1, -1, 1) of the space keeps a rotation). */
FQuat UnrealRotation(const env::Vec3& X, const env::Vec3& Z)
{
	return FRotationMatrix::MakeFromXZ(FVector(X.x, -X.y, X.z), FVector(Z.x, -Z.y, Z.z)).ToQuat();
}

double DistanceTo(const FVector& Camera, const env::Vec3& P)
{
	return FVector::Dist(Camera, UnrealPlace(P)) / 100.0;
}

}

void USwcLocalLightSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	bDisabled = FParse::Param(FCommandLine::Get(), TEXT("SwcNoLocalLights"));
	bProbe = FParse::Param(FCommandLine::Get(), TEXT("SwcLightProbe"));
	bCookies = !FParse::Param(FCommandLine::Get(), TEXT("SwcNoCookies"));
	if (FParse::Value(FCommandLine::Get(), TEXT("SwcLightScale="), Scale))
	{
		CVarLightScale->Set(static_cast<float>(Scale), ECVF_SetByCommandline);
	}
	if (FParse::Value(FCommandLine::Get(), TEXT("SwcPointLightRule="), PointRule))
	{
		CVarPointLightRule->Set(PointRule, ECVF_SetByCommandline);
	}
	FString Mode;
	if (FParse::Value(FCommandLine::Get(), TEXT("SwcLightShadows="), Mode))
	{
		Shadows = Mode == TEXT("none") ? EShadows::None : Mode == TEXT("all") ? EShadows::All : EShadows::Source;
		CVarLightShadows->Set(static_cast<int32>(Shadows), ECVF_SetByCommandline);
	}
}

void USwcLocalLightSubsystem::Deinitialize()
{
	if (Actor.IsValid())
	{
		Actor->Destroy();
	}
	Super::Deinitialize();
}

bool USwcLocalLightSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId USwcLocalLightSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USwcLocalLightSubsystem, STATGROUP_Tickables);
}

void USwcLocalLightSubsystem::StartLoad()
{
	bLoading = true;
	const FString Folder = FPaths::Combine(FSwcCreatureLibrary::ContentFolder(), TEXT("environment"), TEXT("lights"));
	TWeakObjectPtr<USwcLocalLightSubsystem> Self(this);
	Async(EAsyncExecution::ThreadPool, [Self, Folder]()
	{
		TSharedPtr<FData, ESPMode::ThreadSafe> Loaded = LoadData(Folder);
		AsyncTask(ENamedThreads::GameThread, [Self, Loaded]()
		{
			if (USwcLocalLightSubsystem* Subsystem = Self.Get())
			{
				Subsystem->Data = Loaded;
				Subsystem->bLoading = false;
				UE_LOG(LogSwcEnvironment, Log, TEXT("%s"), *Loaded->Report);
			}
		});
	});
}

bool USwcLocalLightSubsystem::CastsShadow(const env::LocalLight& Light) const
{
	return Shadows == EShadows::All || (Shadows == EShadows::Source && Light.shadow);
}

double USwcLocalLightSubsystem::Candela(const env::LocalLight& Light, bool bFalloff) const
{
	if (Light.type != env::LocalLight::Type::point)
	{
		return env::light_candela(Light, Scale);
	}
	if (PointRule == 0)
	{
		const double R = FMath::Max(Light.bulb, env::kMinBulb);
		return PI * env::kSourceUnitLuminance * Light.diffuse * R * R * Scale;
	}
	return env::light_candela(Light, Scale) * (bFalloff ? env::kPointFalloffGain : 1.0);
}

UMaterialInterface* USwcLocalLightSubsystem::PointFalloff(double Bulb, double Range)
{
	if (!bPointFalloffLoaded)
	{
		bPointFalloffLoaded = true;
		PointFalloffParent = LoadObject<UMaterialInterface>(nullptr, PointFalloffPath, nullptr, LOAD_NoWarn);
		UE_LOG(LogSwcEnvironment, Log, TEXT("local lights: the point falloff function %s"),
			PointFalloffParent != nullptr ? TEXT("is loaded") : TEXT("is missing (tools/import-cookies.sh): the points have no near clamp and no source fade"));
	}
	if (PointFalloffParent == nullptr)
	{
		return nullptr;
	}
	const FString Key = FString::Printf(TEXT("%.4f_%.3f"), Bulb, Range);
	if (TObjectPtr<UMaterialInterface>* Cached = PointFalloffCache.Find(Key))
	{
		return *Cached;
	}
	UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(PointFalloffParent, this);
	Instance->SetScalarParameterValue(TEXT("Bulb"), static_cast<float>(Bulb));
	Instance->SetScalarParameterValue(TEXT("Range"), static_cast<float>(Range));
	Instance->SetScalarParameterValue(TEXT("Gain"), static_cast<float>(env::kPointFalloffGain));
	PointFalloffCache.Add(Key, Instance);
	return Instance;
}

bool USwcLocalLightSubsystem::WantsFalloff(const UPointLightComponent* Point) const
{
	const double Limit = FMath::Max(0.0f, CVarPointFalloffDistance.GetValueOnGameThread()) * 100.0 + (Point->LightFunctionMaterial != nullptr ? 500.0 : 0.0);
	return FVector::DistSquared(Point->GetComponentLocation(), LastCamera) < Limit * Limit;
}

void USwcLocalLightSubsystem::UpdateFalloff()
{
	if (PointRule == 0)
	{
		return;
	}
	for (const auto& Pair : Active)
	{
		const env::LocalLight& Light = Data->Lights[static_cast<size_t>(Pair.Key)];
		UPointLightComponent* Point = Cast<UPointLightComponent>(Pair.Value.Get());
		if (Point == nullptr || Light.type != env::LocalLight::Type::point)
		{
			continue;
		}
		const bool bWants = WantsFalloff(Point);
		if (bWants != (Point->LightFunctionMaterial != nullptr))
		{
			ApplyPointRule(Point, Light, bWants);
		}
	}
}

void USwcLocalLightSubsystem::ApplyPointRule(UPointLightComponent* Point, const env::LocalLight& Light, bool bFalloff)
{
	UMaterialInterface* Falloff = PointRule != 0 && bFalloff ? PointFalloff(env::point_bulb(Light), FMath::Max(Light.range, 0.1)) : nullptr;
	Point->SetSourceRadius(PointRule != 0 ? 0.0f : static_cast<float>(FMath::Max(Light.bulb, env::kMinBulb) * 100.0));
	Point->SetSoftSourceRadius(0.0f);
	Point->SetSourceLength(0.0f);
	Point->SetLightFunctionMaterial(Falloff);
	Point->SetLightFunctionScale(FVector(100.0));   // the light vector of the function in m
	Point->SetLightFunctionDisabledBrightness(static_cast<float>(1.0 / env::kPointFalloffGain));
	Point->SetLightFunctionFadeDistance(Point->MaxDrawDistance > 0.0f ? Point->MaxDrawDistance : 100000.0f);
	Point->SetIntensity(static_cast<float>(Candela(Light, Falloff != nullptr)));
}

ULocalLightComponent* USwcLocalLightSubsystem::MakeLight(int32 Index, USceneComponent* Parent, const FTransform& Relative)
{
	const env::LocalLight& Light = Data->Lights[static_cast<size_t>(Index)];
	AActor* Owner = Actor.Get();
	ULocalLightComponent* Component = nullptr;
	if (Light.type == env::LocalLight::Type::rect)
	{
		URectLightComponent* Rect = NewObject<URectLightComponent>(Owner);
		Rect->SetSourceWidth(static_cast<float>(Light.width * 100.0));
		Rect->SetSourceHeight(static_cast<float>(Light.height * 100.0));
		Rect->SetBarnDoorLength(0.0f);   // the source area light lights its whole front half space
		Component = Rect;
	}
	else
	{
		UPointLightComponent* Point = nullptr;
		if (Light.type == env::LocalLight::Type::spot)
		{
			USpotLightComponent* Spot = NewObject<USpotLightComponent>(Owner);
			Spot->SetOuterConeAngle(static_cast<float>(FMath::Clamp(Light.fov * 0.5, 1.0, 80.0)));   // ProjectorFOV is the full angle
			Spot->SetInnerConeAngle(0.0f);
			if (bCookies && !Light.cookie.empty())
			{
				const FString Key = UTF8_TO_TCHAR(Light.cookie.c_str());
				TObjectPtr<UMaterialInterface>* Cached = CookieCache.Find(Key);
				if (Cached == nullptr)
				{
					const FString* Path = Data->CookieMaterials.Find(Key);
					Cached = &CookieCache.Add(Key, Path != nullptr ? LoadObject<UMaterialInterface>(nullptr, **Path, nullptr, LOAD_NoWarn) : nullptr);
				}
				if (*Cached != nullptr)
				{
					Spot->SetLightFunctionMaterial(*Cached);   // Unreal maps the UV of a spot light function over the outer cone
				}
			}
			Point = Spot;
		}
		else
		{
			Point = NewObject<UPointLightComponent>(Owner);
		}
		Point->SetSourceRadius(static_cast<float>(env::source_radius(Light) * 100.0));
		Point->SetUseInverseSquaredFalloff(true);
		Component = Point;
	}
	Component->SetMobility(EComponentMobility::Movable);
	Component->SetIntensityUnits(ELightUnits::Candelas);
	Component->SetIntensity(static_cast<float>(Candela(Light, false)));
	Component->SetLightColor(FLinearColor(static_cast<float>(Light.color[0]), static_cast<float>(Light.color[1]), static_cast<float>(Light.color[2])));
	Component->SetAttenuationRadius(static_cast<float>(FMath::Max(Light.range, 0.1) * 100.0));
	Component->SetSpecularScale(static_cast<float>(FMath::Clamp(Light.spec, 0.0, 4.0)));
	Component->SetCastShadows(CastsShadow(Light));
	Component->SetVolumetricScatteringIntensity(Light.volumetric ? 1.0f : 0.0f);
	Component->MaxDrawDistance = static_cast<float>(env::draw_distance(Light) * 100.0);
	Component->MaxDistanceFadeRange = Component->MaxDrawDistance * FadePart;

	Component->SetupAttachment(Parent);
	Component->SetRelativeTransform(Relative);
	Component->SetVisibility(bVisible);
	Component->RegisterComponent();
	if (Light.type == env::LocalLight::Type::point)
	{
		UPointLightComponent* Point = CastChecked<UPointLightComponent>(Component);
		ApplyPointRule(Point, Light, WantsFalloff(Point));
	}
	++Created;
	return Component;
}

void USwcLocalLightSubsystem::UpdateStatic(const FVector& Camera)
{
	LastCamera = Camera;
	UpdateFalloff();
	const env::Vec3 At{Camera.X / 100.0, -Camera.Y / 100.0, Camera.Z / 100.0};
	// Away: the lights out of their draw distance plus the hysteresis.
	for (auto It = Active.CreateIterator(); It; ++It)
	{
		const env::LocalLight& Light = Data->Lights[static_cast<size_t>(It.Key())];
		if (!Light.npc.empty())
		{
			continue;   // UpdateBound
		}
		if (!It.Value().IsValid() || DistanceTo(Camera, Light.p) > env::draw_distance(Light) + GoMargin)
		{
			if (ULocalLightComponent* Component = It.Value().Get())
			{
				Component->DestroyComponent();
			}
			It.RemoveCurrent();
			++Removed;
		}
	}
	// Near: the lights in their draw distance plus the margin, the nearest first.
	std::vector<int> Candidates;
	Data->Grid.query(At, env::kDrawMax + ComeMargin, Candidates);
	TArray<TPair<double, int32>> New;
	for (const int Index : Candidates)
	{
		if (Active.Contains(Index))
		{
			continue;
		}
		const env::LocalLight& Light = Data->Lights[static_cast<size_t>(Index)];
		const double Distance = DistanceTo(Camera, Light.p);
		if (Distance <= env::draw_distance(Light) + ComeMargin)
		{
			New.Emplace(Distance, Index);
		}
	}
	New.Sort([](const TPair<double, int32>& A, const TPair<double, int32>& B) { return A.Key < B.Key; });
	USceneComponent* Root = Actor->GetRootComponent();
	for (int32 I = 0; I < New.Num() && I < CreateBudget; ++I)
	{
		const env::LocalLight& Light = Data->Lights[static_cast<size_t>(New[I].Value)];
		Active.Add(New[I].Value, MakeLight(New[I].Value, Root, FTransform(UnrealRotation(Light.x, Light.z), UnrealPlace(Light.p))));
	}
}

void USwcLocalLightSubsystem::UpdateBound()
{
	// The lights whose creature went away.
	for (auto It = Owners.CreateIterator(); It; ++It)
	{
		if (!It.Value().IsValid())
		{
			if (const TWeakObjectPtr<ULocalLightComponent>* Found = Active.Find(It.Key()))
			{
				if (ULocalLightComponent* Component = Found->Get())
				{
					Component->DestroyComponent();
				}
			}
			Active.Remove(It.Key());
			It.RemoveCurrent();
			++Removed;
		}
	}
	if (Data->NpcLights.IsEmpty())
	{
		return;
	}
	for (TActorIterator<ASwcCreature> It(GetWorld()); It; ++It)
	{
		ASwcCreature* Creature = *It;
		const FString& Path = Creature->GetSlicePath();
		if (Path.IsEmpty())
		{
			continue;
		}
		const std::string Slice = TCHAR_TO_UTF8(*Path);
		const FVector Place = Creature->GetActorLocation();
		const env::Vec3 At{Place.X / 100.0, -Place.Y / 100.0, Place.Z / 100.0};
		for (const int32 Index : Data->NpcLights)
		{
			const env::LocalLight& Light = Data->Lights[static_cast<size_t>(Index)];
			if (Owners.Contains(Index) || !env::owns_lights(Light.npc, Light.root, Slice, At))
			{
				continue;
			}
			// The local matrix of the light in the frame of the slice root, in Unreal (the same map of the space).
			const std::array<double, 16>& L = Light.local;
			const FTransform Relative(UnrealRotation({L[0], L[4], L[8]}, {L[2], L[6], L[10]}), UnrealPlace({L[3], L[7], L[11]}));
			ULocalLightComponent* Component = MakeLight(Index, Creature->GetRootComponent(), Relative);
			Component->SetUsingAbsoluteScale(true);
			Active.Add(Index, Component);
			Owners.Add(Index, Creature);
		}
	}
}

FSwcLocalLightStats USwcLocalLightSubsystem::GetStats() const
{
	FSwcLocalLightStats Stats;
	if (!Data.IsValid())
	{
		return Stats;
	}
	Stats.Loaded = static_cast<int32>(Data->Lights.size());
	const UWorld* World = GetWorld();
	const APlayerController* Controller = World != nullptr ? World->GetFirstPlayerController() : nullptr;
	const APlayerCameraManager* Camera = Controller != nullptr ? Controller->PlayerCameraManager.Get() : nullptr;
	const FMinimalViewInfo View = Camera != nullptr ? Camera->GetCameraCacheView() : FMinimalViewInfo();
	// The view cone: the half angle of the diagonal of the view (the horizontal field of view and the aspect ratio).
	const double TanX = FMath::Tan(FMath::DegreesToRadians(View.FOV * 0.5));
	const double TanY = TanX / FMath::Max(View.AspectRatio, 0.1f);
	const double ConeHalf = FMath::Atan(FMath::Sqrt(TanX * TanX + TanY * TanY));
	const FVector Forward = View.Rotation.Vector();
	for (const auto& Pair : Active)
	{
		const ULocalLightComponent* Component = Pair.Value.Get();
		if (Component == nullptr)
		{
			continue;
		}
		if (!Component->IsVisible())
		{
			continue;   // hidden (swc.LocalLights 0)
		}
		++Stats.Active;
		Stats.Points += Component->IsA<USpotLightComponent>() ? 0 : Component->IsA<UPointLightComponent>() ? 1 : 0;
		Stats.Spots += Component->IsA<USpotLightComponent>() ? 1 : 0;
		Stats.Rects += Component->IsA<URectLightComponent>() ? 1 : 0;
		Stats.Bound += Owners.Contains(Pair.Key) ? 1 : 0;
		Stats.Cookies += Component->LightFunctionMaterial != nullptr ? 1 : 0;
		const bool bShadow = Component->CastShadows;
		Stats.Shadows += bShadow ? 1 : 0;
		const FVector ToLight = Component->GetComponentLocation() - View.Location;
		const double Distance = ToLight.Size();
		const double Radius = Component->AttenuationRadius;
		bool bInView = Distance <= Component->MaxDrawDistance + Radius;
		if (bInView && Distance > Radius)
		{
			const double Angle = FMath::Acos(FMath::Clamp(FVector::DotProduct(ToLight / Distance, Forward), -1.0, 1.0));
			bInView = Angle - FMath::Asin(FMath::Clamp(Radius / Distance, 0.0, 1.0)) <= ConeHalf;
		}
		Stats.InView += bInView ? 1 : 0;
		Stats.ShadowsInView += bInView && bShadow ? 1 : 0;
	}
	return Stats;
}

void USwcLocalLightSubsystem::Tick(float DeltaTime)
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
		return;   // in the world only (the world clock comes with the world)
	}
	if (!Actor.IsValid())
	{
		FActorSpawnParameters Parameters;
		Parameters.ObjectFlags |= RF_Transient;
		AActor* Owner = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Parameters);
		if (Owner == nullptr)
		{
			return;
		}
		USceneComponent* Root = NewObject<USceneComponent>(Owner, TEXT("SwcLocalLightsRoot"));
		Owner->SetRootComponent(Root);
		Root->SetMobility(EComponentMobility::Movable);
		Root->RegisterComponent();
		Actor = Owner;
	}
	const bool bNewVisible = CVarLocalLights.GetValueOnGameThread() != 0;
	if (bNewVisible != bVisible)
	{
		bVisible = bNewVisible;
		for (const auto& Pair : Active)
		{
			if (ULocalLightComponent* Component = Pair.Value.Get())
			{
				Component->SetVisibility(bVisible);
			}
		}
		UE_LOG(LogSwcEnvironment, Log, TEXT("local lights: %s"), bVisible ? TEXT("on") : TEXT("hidden (swc.LocalLights 0)"));
	}
	const EShadows NewShadows = static_cast<EShadows>(FMath::Clamp(CVarLightShadows.GetValueOnGameThread(), 0, 2));
	if (NewShadows != Shadows)
	{
		Shadows = NewShadows;
		for (const auto& Pair : Active)
		{
			if (ULocalLightComponent* Component = Pair.Value.Get())
			{
				Component->SetCastShadows(CastsShadow(Data->Lights[static_cast<size_t>(Pair.Key)]));
			}
		}
		UE_LOG(LogSwcEnvironment, Log, TEXT("local lights: the shadows are %s"), Shadows == EShadows::None ? TEXT("off") : Shadows == EShadows::All ? TEXT("on for all lights") : TEXT("the source flags"));
	}
	const int32 NewPointRule = CVarPointLightRule.GetValueOnGameThread() != 0 ? 1 : 0;
	if (NewPointRule != PointRule)
	{
		PointRule = NewPointRule;
		for (const auto& Pair : Active)
		{
			const env::LocalLight& Light = Data->Lights[static_cast<size_t>(Pair.Key)];
			if (UPointLightComponent* Point = Cast<UPointLightComponent>(Pair.Value.Get()); Point != nullptr && Light.type == env::LocalLight::Type::point)
			{
				ApplyPointRule(Point, Light, WantsFalloff(Point));
			}
		}
		UE_LOG(LogSwcEnvironment, Log, TEXT("local lights: the point rule is %s"), PointRule != 0 ? TEXT("the source rule") : TEXT("the old sphere rule"));
	}
	const double NewScale = FMath::Max(0.0, static_cast<double>(CVarLightScale.GetValueOnGameThread()));
	if (NewScale != Scale)
	{
		Scale = NewScale;
		for (const auto& Pair : Active)
		{
			if (ULocalLightComponent* Component = Pair.Value.Get())
			{
				Component->SetIntensity(static_cast<float>(Candela(Data->Lights[static_cast<size_t>(Pair.Key)], Component->LightFunctionMaterial != nullptr)));
			}
		}
		UE_LOG(LogSwcEnvironment, Log, TEXT("local lights: the look scale is %.3f"), Scale);
	}
	const double Now = FPlatformTime::Seconds();
	if (Now >= NextUpdate)
	{
		NextUpdate = Now + UpdateSeconds;
		UpdateStatic(Controller->PlayerCameraManager->GetCameraLocation());
	}
	if (Now >= NextBound)
	{
		NextBound = Now + BoundSeconds;
		UpdateBound();
	}
	if (Now >= NextLog)
	{
		NextLog = Now + (bProbe ? 1.0 : LogSeconds);
		const FSwcLocalLightStats Stats = GetStats();
		UE_LOG(LogSwcEnvironment, Log, TEXT("local lights: %d active (%d point, %d spot, %d rect; %d bound to NPCs; %d with a cookie), %d in the view; shadows %d (%d in the view); "
			"%d created, %d removed; scale %.3f"), Stats.Active, Stats.Points, Stats.Spots, Stats.Rects, Stats.Bound, Stats.Cookies, Stats.InView, Stats.Shadows,
			Stats.ShadowsInView, Created, Removed, Scale);
	}
}
