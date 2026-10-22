#include "Environment/SwcEnvironmentSubsystem.h"
#include "Environment/SwcMeterProbe.h"

#include "World/SwcLoadingGate.h"

#include "Async/Async.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "Creatures/SwcCreatureData.h"
#include "Curves/CurveFloat.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Environment/SwcGodRays.h"
#include "Environment/SwcProbeShading.h"
#include "Environment/SwcWorldTimeSubsystem.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
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
/** The histogram of the auto exposure (log2 of the metered value): wide enough for the 100,000 lux sun of the data. */
constexpr float HistogramLogMin = -6.0f;
constexpr float HistogramLogMax = 16.0f;
/** The exposure of the source (stage E5; the decision of the user, 2026-10-19: the color result copies retail): the final scene shader
 *  of the game copy (HDRFinalScenePS, shaders/cache/d3d11/cgpshaders/hdrpostprocess@hdrfinalsceneps.fxcb) meters the illuminance
 *  E = pi x L / albedo of each pixel (the albedo of the G-buffer, at least 0.01; 0.2 for the sky; HDRSampleLumInitialPS), takes the
 *  geometric mean, and sets EV = log2(E x 100 / 330) - (clamp(log10(E + 1), 0.1, 5.2) - 3) / 2 x "EV Auto compensation", clamped to
 *  "EV Min" and "EV Max"; the exposure is 1 / (1.2 x 2^EV). The Unreal meter that ignores the materials
 *  (r.AutoExposure.IgnoreMaterials) meters M = L / albedo x lerp(0.18, 1, 0.01) with the Rec. 709 weights
 *  (r.AutoExposure.LuminanceMethod 2), so E = pi x M / MeterScale. Unreal sets the exposure 0.18 x 2^bias / mean M, and evaluates its
 *  bias curve at EV100 = log2(mean M / 0.18): the curve holds the whole rule of the source (env::source_exposure_bias; the bounds of
 *  Unreal stay wide). With the Unreal luminance meter (swc.SourceMeter 0) the mean luminance stands for E = pi x L / 0.18. */
/** The moon fades in as the sun goes below the horizon: full at this angle (degrees) below it. */
constexpr double TwilightDegrees = 4.0;
/** @return The scale of the Unreal meter that ignores the materials (its luminance scale over the albedo, with its albedo floor). */
double MeterScale()
{
	const IConsoleVariable* Scale = IConsoleManager::Get().FindConsoleVariable(TEXT("r.AutoExposure.IgnoreMaterials.LuminanceScale"));
	const IConsoleVariable* Floor = IConsoleManager::Get().FindConsoleVariable(TEXT("r.AutoExposure.IgnoreMaterials.MinBaseColorLuminance"));
	return FMath::Lerp(Scale != nullptr ? Scale->GetFloat() : 0.18, 1.0, Floor != nullptr ? Floor->GetFloat() : 0.01);
}

/** The bias of the grade exposure (EV, a look scale; 0: the rule of the source). */
TAutoConsoleVariable<float> CVarGradeBias(TEXT("swc.GradeBias"), 0.0f, TEXT("An offset of the exposure of the source rule (EV)."));
TAutoConsoleVariable<float> CVarNightDelay(TEXT("swc.NightDelay"), 0.7f,
	TEXT("The night delay of the sun path (hours; e_TimeOfDay_NightDelay of the source, default 0.7; research e5-menu-probe.md): the sun ")
	TEXT("path runs from dawnStart + delay to duskEnd - delay; the curves of the time of day keep the hour. 0: the hour."));
TAutoConsoleVariable<int32> CVarSunSource(TEXT("swc.SunSource"), 1,
	TEXT("The sun light (stage E5, research/color-pipeline.md rule 7): 1 the \"Sun intensity\" and \"Sun color\" of the hour (the source), the light ")
	TEXT("color divided by the ground transmittance of the atmosphere (the ground gets the source sun), 0 at the moon (the default); 0 the noon values, ")
	TEXT("the atmosphere makes the sunrise and the sunset (E1)."));
/** The meter of the exposure: 2 the source meter measured on the frame, 1 the Unreal meter that ignores the albedo, 0 the Unreal luminance. */
TAutoConsoleVariable<int32> CVarSourceMeter(TEXT("swc.SourceMeter"), 2,
	TEXT("The meter of the exposure: 2 the source meter of the frame (Environment/SwcMeterProbe: E = pi L / the G-buffer albedo, the geometric mean) ")
	TEXT("corrects the target of 1; 1 the illuminance of the Unreal meter that ignores the materials (r.AutoExposure.IgnoreMaterials); 0 the scene luminance."));
TAutoConsoleVariable<int32> CVarSsao(TEXT("swc.Ssao"), 0,
	TEXT("A test of the screen-space AO (stage E5 step 3, research/e5-ssdo.md): 1 the AO values of the post process for the SSAO of Lumen ")
	TEXT("(intensity 1, power 2, a world radius of 480 cm, bias 0, static fraction 0, quality 50; set r.Lumen.DiffuseIndirect.SSAO 1 and ")
	TEXT("r.Lumen.ScreenProbeGather.ShortRangeAO 0 with it); 0 the values of Unreal."));

/** Sets the console variables of the source meter (the engine reads them each frame). */
void ApplySourceMeter(bool bSourceMeter)
{
	auto Set = [](const TCHAR* Name, int32 Value)
	{
		if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			Variable->Set(Value, ECVF_SetByCode);
		}
	};
	Set(TEXT("r.AutoExposure.IgnoreMaterials"), bSourceMeter ? 1 : 0);
	Set(TEXT("r.AutoExposure.IgnoreMaterials.UsePrecalculatedIlluminance"), 0);   // the luminance over the albedo, as the source
	Set(TEXT("r.AutoExposure.LuminanceMethod"), 2);                                 // the Rec. 709 weights of the source
	Set(TEXT("r.EyeAdaptation.ExposureCompensationCurveLUT.NumSamples"), 256);      // the bends of the rule at the EV bounds
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
	Volume.blendDistance = NumberOf(Object, TEXT("blendDistance"), 0.0);
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

/** @return A rain or snow volume of volumes.json (stage E3). The rain: InnerRadius, OuterRadius, and Amount of "Rain Options"; the
 *  snow: the Radius of "Snow Options" (the amount falls in its last 10 %) and the amount SnowFlakeCount / 1000 x SnowFallBrightness / 3
 *  (an inference of this client: the source draws SnowFlakeCount flakes with that brightness). A disabled volume has no amount. */
USwcEnvironmentSubsystem::FData::FPrecipitation ReadPrecipitation(const TSharedPtr<FJsonObject>& Object, bool bSnow)
{
	USwcEnvironmentSubsystem::FData::FPrecipitation Out;
	Out.bSnow = bSnow;
	Out.Name = Object->GetStringField(TEXT("entity"));
	const TArray<TSharedPtr<FJsonValue>>& Rows = Object->GetArrayField(TEXT("matrix"));
	if (Rows.Num() >= 3)
	{
		Out.Center = {Rows[0]->AsArray()[3]->AsNumber(), Rows[1]->AsArray()[3]->AsNumber(), Rows[2]->AsArray()[3]->AsNumber()};
	}
	const TSharedPtr<FJsonObject>* Shape = nullptr;
	if (Object->TryGetObjectField(TEXT("shape"), Shape) && Shape != nullptr && (*Shape)->HasField(TEXT("height")))
	{
		Out.HalfHeight = NumberOf(*Shape, TEXT("height"), 2e9) / 2.0;
	}
	const TSharedPtr<FJsonObject>* Fields = nullptr;
	if (!Object->TryGetObjectField(TEXT("fields"), Fields) || Fields == nullptr)
	{
		return Out;
	}
	bool bEnabled = true;
	(*Fields)->TryGetBoolField(TEXT("Enabled"), bEnabled);
	const TSharedPtr<FJsonObject>* Options = nullptr;
	if (!bEnabled || !(*Fields)->TryGetObjectField(bSnow ? TEXT("Snow Options") : TEXT("Rain Options"), Options) || Options == nullptr)
	{
		return Out;
	}
	if (bSnow)
	{
		Out.Outer = NumberOf(*Options, TEXT("Radius"), 0.0);
		Out.Inner = Out.Outer * 0.9;
		Out.Amount = NumberOf(*Options, TEXT("SnowFlakeCount"), 0.0) / 1000.0 * NumberOf(*Options, TEXT("SnowFallBrightness"), 1.0) / 3.0;
	}
	else
	{
		Out.Inner = NumberOf(*Options, TEXT("InnerRadius"), 0.0);
		Out.Outer = FMath::Max(NumberOf(*Options, TEXT("OuterRadius"), 0.0), Out.Inner);
		Out.Amount = NumberOf(*Options, TEXT("Amount"), 0.0);
		Out.DropsAmount = NumberOf(*Options, TEXT("RainDropsAmount"), 1.0);
		Out.DropsLighting = NumberOf(*Options, TEXT("RainDropsLighting"), 1.0);
		Out.DropsSpeed = NumberOf(*Options, TEXT("RainDropsSpeed"), 1.0);
	}
	return Out;
}

/** Adds a fog volume of volumes.json (stage E3): the box of its shape, the COLOR, and the GlobalDensity of its FogVolumeConfiguration. */
void ReadFogVolume(const TSharedPtr<FJsonObject>& Object, TArray<USwcEnvironmentSubsystem::FData::FFogVolume>& Out)
{
	const TSharedPtr<FJsonObject>* Shape = nullptr;
	const TSharedPtr<FJsonObject>* Fields = nullptr;
	const TSharedPtr<FJsonObject>* Configuration = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* Dimensions = nullptr;
	if (!Object->TryGetObjectField(TEXT("shape"), Shape) || Shape == nullptr || !(*Shape)->TryGetArrayField(TEXT("dimensions"), Dimensions)
		|| Dimensions->Num() != 3 || !Object->TryGetObjectField(TEXT("fields"), Fields) || Fields == nullptr
		|| !(*Fields)->TryGetObjectField(TEXT("FogVolumeConfiguration"), Configuration) || Configuration == nullptr)
	{
		return;
	}
	USwcEnvironmentSubsystem::FData::FFogVolume Volume;
	Volume.Name = Object->GetStringField(TEXT("entity"));
	const TArray<TSharedPtr<FJsonValue>>& Rows = Object->GetArrayField(TEXT("matrix"));
	if (Rows.Num() >= 3)
	{
		Volume.Center = {Rows[0]->AsArray()[3]->AsNumber(), Rows[1]->AsArray()[3]->AsNumber(), Rows[2]->AsArray()[3]->AsNumber()};
	}
	Volume.Size = {(*Dimensions)[0]->AsNumber(), (*Dimensions)[1]->AsNumber(), (*Dimensions)[2]->AsNumber()};
	Volume.Density = NumberOf(*Configuration, TEXT("GlobalDensity"), 0.0);
	const TArray<TSharedPtr<FJsonValue>>* Color = nullptr;
	if ((*Configuration)->TryGetArrayField(TEXT("COLOR"), Color) && Color->Num() >= 3)
	{
		Volume.Color = {(*Color)[0]->AsNumber(), (*Color)[1]->AsNumber(), (*Color)[2]->AsNumber()};
	}
	if (Volume.Density > 0.0)
	{
		Out.Add(Volume);
	}
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
		if (!Object.IsValid())
		{
			continue;
		}
		const FString Kind = Object->GetStringField(TEXT("kind"));
		if (Kind == TEXT("timeOfDay"))
		{
			Out->Volumes.push_back(ReadVolume(Object));
		}
		else if (Kind == TEXT("rain") || Kind == TEXT("snow"))
		{
			Out->Precipitation.Add(ReadPrecipitation(Object, Kind == TEXT("snow")));
		}
		else if (Kind == TEXT("fog"))
		{
			ReadFogVolume(Object, Out->FogVolumes);
		}
	}
	// The cloud cover of each sky slice (stage E3; tools/convert-environment.sh sky).
	if (const TSharedPtr<FJsonObject> Sky = ReadJson(FPaths::Combine(Folder, TEXT("sky"), TEXT("sky.json"))))
	{
		const TSharedPtr<FJsonObject>* Skies = nullptr;
		if (Sky->TryGetObjectField(TEXT("skies"), Skies))
		{
			for (const auto& Pair : (*Skies)->Values)
			{
				Out->SkyCovers.Add(FString(Pair.Key), NumberOf(Pair.Value->AsObject(), TEXT("cover"), 0.0));
			}
		}
	}
	// The world probe of the outdoor ambient (stage E5; tools/convert-environment.sh probes): the first one (the world has one).
	if (const TSharedPtr<FJsonObject> Probes = ReadJson(FPaths::Combine(Folder, TEXT("probes.json"))))
	{
		const TArray<TSharedPtr<FJsonValue>>* World = nullptr;
		if (Probes->TryGetArrayField(TEXT("world"), World))
		{
			for (const TSharedPtr<FJsonValue>& Value : *World)
			{
				const TSharedPtr<FJsonObject> Probe = Value->AsObject();
				FString Picture;
				if (Probe.IsValid() && Probe->TryGetStringField(TEXT("hdr"), Picture))
				{
					Out->WorldProbe = FPaths::GetBaseFilename(Picture);
					Out->WorldProbeDiffuse = NumberOf(Probe, TEXT("diffuseMultiplier"), 1.0);
					Out->WorldProbeTodInfluence = NumberOf(Probe, TEXT("todInfluence"), 1.0);
					USwcEnvironmentSubsystem::ReadCubeFaces(Probe, Out->WorldProbeFaces);
					const TSharedPtr<FJsonObject>* Faces = nullptr;
					if (Probe->TryGetObjectField(TEXT("faceMeans"), Faces) && Faces != nullptr)
					{
						std::array<double, 3> Sum{0.0, 0.0, 0.0};
						int32 Count = 0;
						for (const TCHAR* Face : {TEXT("+east"), TEXT("-east"), TEXT("+north"), TEXT("-north")})
						{
							const TArray<TSharedPtr<FJsonValue>>* Mean = nullptr;
							if ((*Faces)->TryGetArrayField(Face, Mean) && Mean != nullptr && Mean->Num() == 3)
							{
								for (int32 C = 0; C < 3; ++C)
								{
									Sum[C] += (*Mean)[C]->AsNumber();
								}
								++Count;
							}
						}
						if (Count > 0)
						{
							Out->WorldProbeHorizon = {Sum[0] / Count, Sum[1] / Count, Sum[2] / Count};
						}
					}
					break;
				}
			}
		}
		// The local probes (stage E5 step b): the ones with a cube picture.
		const TSharedPtr<FJsonObject>* Cubes = nullptr;
		const TArray<TSharedPtr<FJsonValue>>* Local = nullptr;
		if (Probes->TryGetObjectField(TEXT("cubes"), Cubes) && Cubes != nullptr && Probes->TryGetArrayField(TEXT("local"), Local))
		{
			for (int32 Index = 0; Index < Local->Num(); ++Index)
			{
				const TSharedPtr<FJsonObject> Probe = (*Local)[Index]->AsObject();
				const TSharedPtr<FJsonObject>* Cube = nullptr;
				FString CubeName, Picture;
				if (!Probe.IsValid() || !Probe->TryGetStringField(TEXT("cubemap"), CubeName) || !(*Cubes)->TryGetObjectField(CubeName, Cube)
					|| Cube == nullptr || !(*Cube)->TryGetStringField(TEXT("hdr"), Picture))
				{
					continue;
				}
				USwcEnvironmentSubsystem::FData::FLocalProbe Item;
				if (USwcEnvironmentSubsystem::ReadLocalProbe(Probe, Picture, Item))
				{
					USwcEnvironmentSubsystem::ReadCubeFaces(*Cube, Item.Faces);
					Item.Order = Index;
					Out->LocalProbes.Add(MoveTemp(Item));
				}
			}
		}
	}
	Out->bOk = true;
	Out->Report = FString::Printf(TEXT("environment: %d preset(s), %d time-of-day volume(s), %d sky cover(s), %d fog volume(s); the sun rotation %.1f, longitude %.1f; the moon %.1f %.1f size %.2f (%.0f ms)"),
		Out->Presets.Num(), static_cast<int32>(Out->Volumes.size()), Out->SkyCovers.Num(), Out->FogVolumes.Num(), Out->SunRotation, Out->Longitude, Out->MoonLatitude, Out->MoonLongitude, Out->MoonSize,
		(FPlatformTime::Seconds() - Start) * 1000.0);
	return Out;
}

/** @return The rotation of a directional light whose light comes from an engine direction (toward the light). */
FRotator LightRotation(const env::Vec3& Toward)
{
	return FVector(-Toward.x, Toward.y, -Toward.z).Rotation();   // engine (x, y, z) to Unreal (x, -y, z), then the opposite
}

}

/** @return A local probe of the record form of probes.json "local" (the world, and the probes of the menu scene). */
bool USwcEnvironmentSubsystem::ReadLocalProbe(const TSharedPtr<FJsonObject>& Probe, const FString& Picture, FData::FLocalProbe& Out)
{
	auto NumbersOf = [](const TSharedPtr<FJsonObject>& Object, const TCHAR* Name, TArray<double>& Values)
	{
		Values.Reset();
		const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
		if (Object->TryGetArrayField(Name, Array))
		{
			for (const TSharedPtr<FJsonValue>& Item : *Array)
			{
				Values.Add(Item->AsNumber());
			}
		}
	};
	TArray<double> Place, Area, Color;
	NumbersOf(Probe, TEXT("place"), Place);
	NumbersOf(Probe, TEXT("area"), Area);
	NumbersOf(Probe, TEXT("color"), Color);
	const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
	if (Place.Num() != 3 || Area.Num() != 3 || !Probe->TryGetArrayField(TEXT("rows"), Rows) || Rows->Num() != 3)
	{
		return false;
	}
	Out.Center = {Place[0], Place[1], Place[2]};
	// The box space: the unit axes of the entity (the columns of its matrix) over the half sizes of the area.
	std::array<std::array<double, 3>, 3> M{};
	for (int32 R = 0; R < 3; ++R)
	{
		const TArray<TSharedPtr<FJsonValue>>& Values = (*Rows)[R]->AsArray();
		for (int32 C = 0; C < 3 && C < Values.Num(); ++C)
		{
			M[R][C] = Values[C]->AsNumber();
		}
	}
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		const double Length = FMath::Max(FMath::Sqrt(M[0][Axis] * M[0][Axis] + M[1][Axis] * M[1][Axis] + M[2][Axis] * M[2][Axis]), 1e-9);
		const double Half = FMath::Max(Area[Axis] * 0.5, 1e-3);
		Out.Rows[Axis] = {M[0][Axis] / Length / Half, M[1][Axis] / Length / Half, M[2][Axis] / Length / Half};
	}
	Out.Radius = 0.5 * FMath::Sqrt(Area[0] * Area[0] + Area[1] * Area[1] + Area[2] * Area[2]);
	Out.Volume = Area[0] * Area[1] * Area[2];
	Out.Priority = static_cast<int32>(NumberOf(Probe, TEXT("priority"), 0.0));
	Out.Falloff = NumberOf(Probe, TEXT("falloff"), 1.0);
	const double Diffuse = NumberOf(Probe, TEXT("diffuseMultiplier"), 1.0);
	for (int32 C = 0; C < 3; ++C)
	{
		Out.Color[C] = (C < Color.Num() ? Color[C] : 1.0) * Diffuse;
	}
	Out.TodInfluence = NumberOf(Probe, TEXT("todInfluence"), 1.0);
	Out.Cube = FPaths::GetBaseFilename(Picture);
	return true;
}

bool USwcEnvironmentSubsystem::ReadCubeFaces(const TSharedPtr<FJsonObject>& Cube, std::array<double, 6>& Out)
{
	const TSharedPtr<FJsonObject>* Means = nullptr;
	if (!Cube.IsValid() || !Cube->TryGetObjectField(TEXT("diffuseMeans"), Means) || Means == nullptr)
	{
		return false;
	}
	const TCHAR* Names[6] = {TEXT("+east"), TEXT("-east"), TEXT("+north"), TEXT("-north"), TEXT("+up"), TEXT("-up")};
	for (int32 Face = 0; Face < 6; ++Face)
	{
		const TArray<TSharedPtr<FJsonValue>>* Mean = nullptr;
		if (!(*Means)->TryGetArrayField(Names[Face], Mean) || Mean == nullptr || Mean->Num() != 3)
		{
			return false;
		}
		Out[Face] = 0.2126 * (*Mean)[0]->AsNumber() + 0.7152 * (*Mean)[1]->AsNumber() + 0.0722 * (*Mean)[2]->AsNumber();
	}
	return true;
}

void USwcEnvironmentSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	bDisabled = FParse::Param(FCommandLine::Get(), TEXT("SwcNoEnvironment"));
	bProbe = FParse::Param(FCommandLine::Get(), TEXT("SwcEnvironmentProbe"));
	bAmbientOnly = FParse::Param(FCommandLine::Get(), TEXT("SwcAmbientOnly"));
	FParse::Value(FCommandLine::Get(), TEXT("SwcNightExposure="), NightExposure);
	FParse::Value(FCommandLine::Get(), TEXT("SwcNightSky="), NightSkyFactor);
	// The loading gate (AST-203): a screen ends only when the source meter has set the exposure of the place (swc.SourceMeter 2).
	if (!bDisabled)
	{
		SwcLoadingGate::Register(GetWorld(), TEXT("meter"), [this](const FVector& Place, FString& Why)
		{
			if (CVarSourceMeter.GetValueOnGameThread() != 2 || MeterSettled >= 4)
			{
				return true;
			}
			Why = FString::Printf(TEXT("the exposure is %.2f EV from the source meter"), MeterGap);
			return false;
		});
	}
}

void USwcEnvironmentSubsystem::Deinitialize()
{
	SwcLoadingGate::Unregister(GetWorld(), TEXT("meter"));
	SwcProbeShading::SetList(FSwcProbeShadingList());
	SwcGodRays::SetSettings(SwcGodRays::FSettings());
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
		Light->bCastCloudShadows = true;   // the volumetric clouds shade the ground (stage E3: the overcast light of the rain skies)
		Light->SetupAttachment(Root);
		Light->RegisterComponent();
		return Light;
	};
	Sun = MakeLight(TEXT("SwcSun"), 0);
	Moon = MakeLight(TEXT("SwcMoon"), 1);
	Moon->SetLightSourceAngle(static_cast<float>(Celestial().MoonSize));   // an inference: the Size of the Moon element as degrees
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
	// The source takes the mean of all pixels: the widest window of Unreal; the bounds of Unreal stay wide (the curve has the EV bounds).
	Settings.bOverride_AutoExposureLowPercent = true;
	Settings.bOverride_AutoExposureHighPercent = true;
	Settings.AutoExposureLowPercent = 1.0f;
	Settings.AutoExposureHighPercent = 99.0f;
	Settings.AutoExposureMinBrightness = FMath::Exp2(HistogramLogMin) / 0.18f;
	Settings.AutoExposureMaxBrightness = FMath::Exp2(HistogramLogMax) / 0.18f;
	Post->SetupAttachment(Root);
	Post->RegisterComponent();
	Exposure = Post;
	Moon->SetAtmosphereSunDiskColorScale(FLinearColor::Black);   // the moon disc is the texture of the sky material (stage E3)
	CreateSkyAndFog(Owner, Root);
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
			if ((Component->IsA<UDirectionalLightComponent>() || Component->IsA<USkyAtmosphereComponent>() || Component->IsA<USkyLightComponent>()
					|| Component->IsA<UExponentialHeightFogComponent>() || Component->IsA<UVolumetricCloudComponent>())
				&& Component->IsRegistered())
			{
				Component->UnregisterComponent();
				++Count;
			}
		}
	}
	UE_LOG(LogSwcEnvironment, Log, TEXT("environment: %d sun, sky, sky light, fog, and cloud component(s) of the level turned off (the ocean of the level stays)"), Count);
}

void USwcEnvironmentSubsystem::SetMenuScene(const FMenuScene& Scene)
{
	Menu = Scene;
	Weights.Reset();   // the volumes of the world have no part in the menu
	UE_LOG(LogSwcEnvironment, Log, TEXT("environment: the menu mode: the preset %s at the hour %.2f, the sky %s, the sun rotation %.1f, longitude %.1f, the moon size %.2f, %d probe(s)"),
		*Scene.Preset, Scene.Hour, Scene.Sky.IsEmpty() ? TEXT("(none)") : *Scene.Sky, Scene.SunRotation, Scene.Longitude, Scene.MoonSize, Scene.Probes.Num());
}

void USwcEnvironmentSubsystem::ClearMenuScene()
{
	if (Menu.IsSet())
	{
		Menu.Reset();
		UE_LOG(LogSwcEnvironment, Log, TEXT("environment: the menu mode ends (the world clock again)"));
	}
}

USwcEnvironmentSubsystem::FCelestial USwcEnvironmentSubsystem::Celestial() const
{
	FCelestial Out;
	if (Menu.IsSet())
	{
		Out = {Menu->SunRotation, Menu->Longitude, Menu->MoonLatitude, Menu->MoonLongitude, Menu->MoonSize};
	}
	else if (Data.IsValid())
	{
		Out = {Data->SunRotation, Data->Longitude, Data->MoonLatitude, Data->MoonLongitude, Data->MoonSize};
	}
	return Out;
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
	// The menu mode needs no record of the world clock (its hour is fixed).
	const bool bMenu = Menu.IsSet();
	if ((!bMenu && (Clock == nullptr || !Clock->HasWorldTime())) || Controller == nullptr || Controller->PlayerCameraManager == nullptr)
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
	// The blend at the feet of the player (stage E3): the base preset of the mission, then the time-of-day volumes by priority and
	// weight. The source selects the volumes by the place of the player, not of the camera (set4-weather/keep-walk-to-door.json: the
	// hour of the keep interior starts when the feet pass y 5923.6, with the camera 4.3 m behind, outside the door). With no pawn: the
	// camera.
	const FVector CameraLocation = Controller->PlayerCameraManager->GetCameraLocation();
	FVector Selector = CameraLocation;
	// The loading gate (docs/WORLD.md "The loading"): during a loading screen the volumes of the place of the character, with no
	// blend from another place (the sky and the light changed 6 to 10 s after the screen ended).
	FVector Place;
	const bool bScreen = SwcLoadingGate::GetPlace(World, Place);
	if (bScreen)
	{
		Selector = Place;
	}
	else if (const APawn* Pawn = Controller->GetPawn())
	{
		Selector = Pawn->GetActorLocation() - FVector(0.0, 0.0, Pawn->GetSimpleCollisionHalfHeight());
	}
	const swc::net::WorldPosition Feet = SwcUnrealToEngine(Selector);
	const double WorldHour = bMenu ? Menu->Hour : Clock->GetHour();
	UpdateBlend(env::Vec3{Feet.x, Feet.y, Feet.z}, WorldHour, bScreen ? 1.0e6f : DeltaTime);
	if (Layers.IsEmpty() || Layers[0].Preset == nullptr)
	{
		return;
	}
	const double Hour = EffectiveHour;
	// The menu has no record: the default edges of the clock (06:00 and 19:00; the menu hour 16.7 is far from both).
	static const swc::net::TimeRecord NoRecord;
	const swc::net::TimeRecord& Record = bMenu ? NoRecord : Clock->GetRecord();
	// The menu: the dawn and dusk fields of its mission (research e5-menu-probe.md), else the default edges.
	const env::Edges Edges = bMenu && Menu->bMissionEdges ? env::mission_edges(Menu->DawnTime, Menu->DawnDuration, Menu->DuskTime, Menu->DuskDuration)
		: env::edges_of(Record.dawnTime, Record.dawnDuration, Record.duskTime, Record.duskDuration, Record.dayLength);
	const env::LightMultipliers Multipliers = env::light_multipliers(Hour, Edges);
	// The sun (stage E5, the decision of 2026-10-20; research/color-pipeline.md rule 7): the "Sun intensity" and "Sun color" of the
	// hour (the source has one light with these keys: the sun by day and the moon by night), on the ground as the source has it
	// (swc.SunSource 1). swc.SunSource 0 is the sun of stage E1 (the decision of the user then): the noon values, and the atmosphere
	// makes the sunrise, the sunset, and the twilight. The moon keeps the data: its direction, the night "Sun intensity" and "Sun
	// color" of the hour, and the fade at the visual dawn and dusk of the record.
	const bool bSunSource = CVarSunSource.GetValueOnGameThread() != 0;
	const double SunHour = bSunSource ? -1.0 : 12.0;
	const double SunLux = bAmbientOnly ? 0.0 : FMath::Max(0.0, Blended(TEXT("Sun intensity"), 100000.0, SunHour));
	const env::LightColor SunColor = env::light_color(BlendedColor(TEXT("Sun color"), SunHour));
	const double MoonLux = bAmbientOnly ? 0.0 : FMath::Max(0.0, Blended(TEXT("Sun intensity"), 0.0));
	const env::LightColor MoonColor = env::light_color(BlendedColor(TEXT("Sun color")));
	const FCelestial Bodies = Celestial();
	// The sun path takes the night delay of the source; the curves keep the hour.
	SunPathHour = env::sun_path_hour(Hour, Edges.dawnStart, Edges.duskEnd, FMath::Max(0.0f, CVarNightDelay.GetValueOnGameThread()));
	const env::Vec3 SunToward = env::sun_toward(SunPathHour, Bodies.SunRotation, Bodies.Longitude);
	const env::Vec3 MoonToward = env::moon_toward(Bodies.MoonLatitude, Bodies.MoonLongitude);
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
		if (bSunSource)
		{
			// The atmosphere of Unreal dims and colors the sun light on the ground by its transmittance toward the sun; the source sun has
			// no such term (Csun = SunColor / Y(SunColor) x SunLux / (N pi)). The light color over that transmittance (at least 0.05 a
			// channel) gives the source color and lux on the ground; the largest channel goes into the intensity (the light color is
			// 8-bit). The sun fades out as the moon (the same keys at night) fades in.
			const USkyAtmosphereComponent* Sky = Atmosphere.Get();
			const FLinearColor Transmittance = Sky != nullptr ? const_cast<USkyAtmosphereComponent*>(Sky)->GetAtmosphereTransmitanceOnGroundAtPlanetTop(Light) : FLinearColor::White;
			std::array<double, 3> Outside{};
			double Largest = 1e-9;
			for (int32 C = 0; C < 3; ++C)
			{
				Outside[C] = SunColor.color[C] / FMath::Max(static_cast<double>(Transmittance.Component(C)), 0.05);
				Largest = FMath::Max(Largest, Outside[C]);
			}
			Light->SetIntensity(static_cast<float>(SunLux * SunColor.intensityFactor * Largest * (1.0 - MoonWeight)));
			Light->SetLightColor(FLinearColor(static_cast<float>(Outside[0] / Largest), static_cast<float>(Outside[1] / Largest), static_cast<float>(Outside[2] / Largest)));
			SunTransmittance = Transmittance;
			// The same light for the effects (MPC_SwcEffects of World/SwcMaterialScale.cpp: the sun of the fog sheets): the source sun on
			// the ground, not the light before the atmosphere.
			const double Ground = SunLux * SunColor.intensityFactor;
			SourceSunLight = FLinearColor(static_cast<float>(SunColor.color[0] * Ground), static_cast<float>(SunColor.color[1] * Ground),
				static_cast<float>(SunColor.color[2] * Ground), 1.0f);
		}
		else
		{
			SourceSunLight = FLinearColor::Transparent;
			Light->SetIntensity(static_cast<float>(SunLux * SunColor.intensityFactor));
			Light->SetLightColor(Linear(SunColor));
		}
	}
	if (UDirectionalLightComponent* Light = Moon.Get())
	{
		Light->SetWorldRotation(LightRotation(MoonToward));
		Light->SetLightSourceAngle(static_cast<float>(Bodies.MoonSize));   // the menu level has its own moon size
		Light->SetIntensity(static_cast<float>(MoonLux * MoonWeight * MoonColor.intensityFactor));
		Light->SetLightColor(Linear(MoonColor));
		Light->SetVisibility(MoonWeight > 0.0);
	}
	// The exposure of the source: the EV bounds and the auto compensation of the data, in the bias curve of Unreal.
	const double EvMin = Blended(TEXT("EV Min"), 5.0);
	const double EvMax = Blended(TEXT("EV Max"), 17.0);
	const double AutoCompensation = Blended(TEXT("EV Auto compensation"), 1.5);
	const double Compensation = CVarGradeBias.GetValueOnGameThread() + FMath::Lerp(0.0, NightExposure, MoonWeight);
	if (UPostProcessComponent* Post = Exposure.Get())
	{
		// The source meter of the frame (swc.SourceMeter 2, stage E5): the Unreal meter ignores the albedo but meters it in its own way
		// (the histogram window, its albedo floor, the reflectance). Each measure of the frame (each 0.25 s, an asynchronous readback)
		// adds a tenth of the difference of the applied EV and the EV of the source rule to the bias: the exposure goes to the source
		// rule, slower than the eye adaptation of Unreal (no swing). While a loading screen shows, half of the difference: the gate
		// (the check "meter") ends the screen when four measures in a row are within 0.05 EV.
		const int32 MeterMode = CVarSourceMeter.GetValueOnGameThread();
		SwcMeterProbe::SetWatch(MeterMode == 2);
		SwcMeterProbe::FResult Measure;
		if (MeterMode != 2)
		{
			MeterCorrection = 0.0;
		}
		else if (SwcMeterProbe::GetLatest(Measure) && Measure.Serial != MeterSerial && Measure.Exposure > 0.0 && Measure.SourceE > 0.0)
		{
			MeterSerial = Measure.Serial;
			// The applied exposure as an EV of the source rule: exposure = 1 / (1.2 x 2^EV); a positive bias makes the picture brighter.
			const double AppliedEv = -FMath::Log2(1.2 * Measure.Exposure);
			// The target: the EV of the source rule, with the factor 1 - w of the bloom mix (swc.SourceBloom 1) in the exposure.
			const double SourceEv = env::source_ev(Measure.SourceE, EvMin, EvMax, AutoCompensation) - FMath::Log2(1.0 - BloomMix);
			FVector ScreenPlace;
			const double Gain = SwcLoadingGate::GetPlace(GetWorld(), ScreenPlace) ? 0.5 : 0.1;
			MeterGap = AppliedEv - SourceEv;
			MeterSettled = FMath::Abs(MeterGap) < 0.05 ? MeterSettled + 1 : 0;
			MeterCorrection = FMath::Clamp(MeterCorrection + Gain * MeterGap, -6.0, 6.0);
			if (FPlatformTime::Seconds() >= NextMeterLog)
			{
				NextMeterLog = FPlatformTime::Seconds() + 2.0;
				UE_LOG(LogSwcEnvironment, Log, TEXT("meter: the source E %.0f lux (EV %.2f), the applied EV %.2f, the bias %+.2f EV; measure %u, %u dropped"),
					Measure.SourceE, SourceEv, AppliedEv, MeterCorrection, Measure.Serial, Measure.Dropped);
			}
		}
		// The bloom mix of the source: in meter 2 the target has the factor 1 - w; else the bias takes it.
		Post->Settings.AutoExposureBias = static_cast<float>(Compensation + MeterCorrection + (MeterMode == 2 ? 0.0 : FMath::Log2(1.0 - BloomMix)));
		// The AO test (swc.Ssao 1): the first SSAO values of the SSDO study (its table "The values for the client").
		const bool bSsao = CVarSsao.GetValueOnGameThread() != 0;
		FPostProcessSettings& Ao = Post->Settings;
		Ao.bOverride_AmbientOcclusionIntensity = Ao.bOverride_AmbientOcclusionPower = Ao.bOverride_AmbientOcclusionRadiusInWS = bSsao;
		Ao.bOverride_AmbientOcclusionRadius = Ao.bOverride_AmbientOcclusionBias = Ao.bOverride_AmbientOcclusionStaticFraction = bSsao;
		Ao.bOverride_AmbientOcclusionQuality = bSsao;
		Ao.AmbientOcclusionIntensity = 1.0f;
		Ao.AmbientOcclusionPower = 2.0f;
		Ao.AmbientOcclusionRadiusInWS = true;
		Ao.AmbientOcclusionRadius = 480.0f;   // cm: the outer projection of r_ssdoRadius 1.2 m in the single full-size pass (section 6)
		Ao.AmbientOcclusionBias = 0.0f;
		Ao.AmbientOcclusionStaticFraction = 0.0f;
		Ao.AmbientOcclusionQuality = 50.0f;
		const bool bSourceMeter = MeterMode != 0;
		const FVector4 Rule(EvMin, EvMax, AutoCompensation, bSourceMeter ? MeterScale() : 0.0);
		if (ExposureCurve == nullptr || !Rule.Equals(CurveRule, 0.005))
		{
			ApplySourceMeter(bSourceMeter);
			// The bias curve of Unreal over the metered EV100 (each 0.25 EV over the range of its curve table, -10 to 20). A new curve
			// object each time: the renderer bakes the table again only for a new curve object (a key change of the same object
			// is seen only in the editor).
			ExposureCurve = NewObject<UCurveFloat>(this);
			for (double Ev = -10.0; Ev <= 20.0; Ev += 0.25)
			{
				const FKeyHandle Key = ExposureCurve->FloatCurve.AddKey(static_cast<float>(Ev),
					static_cast<float>(env::source_exposure_bias(Ev, EvMin, EvMax, AutoCompensation, bSourceMeter ? MeterScale() : 0.18)));
				ExposureCurve->FloatCurve.SetKeyInterpMode(Key, RCIM_Linear);
			}
			CurveRule = Rule;
			Post->Settings.bOverride_AutoExposureBiasCurve = true;
			Post->Settings.AutoExposureBiasCurve = ExposureCurve;
		}
	}
	const double MainLux = SunLux * FMath::Max(0.0, SunToward.z) + MoonLux * MoonWeight;
	UpdateSkyAndFog(Hour, CameraLocation, MoonWeight, MainLux);
	const double Now = FPlatformTime::Seconds();
	if (LayerText != PresetName || Now >= NextLog)
	{
		NextLog = Now + (bProbe ? 1.0 : LogSeconds);
		UE_LOG(LogSwcEnvironment, Log, TEXT("environment: hour %.4f%s, presets %s; sun %.0f lux%s, elevation %.1f (toward %.3f %.3f %.3f), color %.3f %.3f %.3f; "
			"moon %.0f lux x %.3f, color %.3f %.3f %.3f; night sky %.2f; exposure EV of the source %.1f to %.1f, bias %.2f; %s"),
			Hour, bMenu ? TEXT(" (the menu mode)") : FMath::Abs(Hour - WorldHour) > 1e-6 ? *FString::Printf(TEXT(" (the world hour %.4f with the hour overrides)"), WorldHour) : TEXT(""),
			*LayerText, SunLux, bSunSource ? *FString::Printf(TEXT(" (the hour; the transmittance %.3f %.3f %.3f)"), SunTransmittance.R, SunTransmittance.G, SunTransmittance.B) : TEXT(" (noon)"),
			Elevation, SunToward.x, SunToward.y, SunToward.z, SunColor.color[0], SunColor.color[1], SunColor.color[2],
			MoonLux, MoonWeight, MoonColor.color[0], MoonColor.color[1], MoonColor.color[2], NightSkyFactor, EvMin, EvMax, Compensation, *SkyText);
		PresetName = LayerText;
	}
}
