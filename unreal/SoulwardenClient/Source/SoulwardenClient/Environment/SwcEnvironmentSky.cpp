// The regional sky and fog of the environment (stage E3, docs/ENVIRONMENT.md): the blend of the time-of-day presets, the height fog
// and the volumetric fog, the clouds, and the sky sphere with the stars and the moon. A part of USwcEnvironmentSubsystem.

#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/LocalFogVolumeComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "Engine/StaticMesh.h"
#include "Async/Async.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureCube.h"
#include "Environment/SwcEnvironmentSubsystem.h"
#include "Environment/SwcProbeShading.h"
#include "Environment/SwcWorldTimeSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/Paths.h"

namespace
{

namespace env = swc::environment;

// The look values of E3 (the Unreal way, the decision of the user): console variables, so that one run of the matched views can compare
// them (tools/match-view.sh --variant).
TAutoConsoleVariable<int32> CVarFog(TEXT("swc.Fog"), 1, TEXT("The height fog of stage E3: 1 on, 0 off."));
TAutoConsoleVariable<int32> CVarFogColorMode(TEXT("swc.FogColorMode"), 0,
	TEXT("The color of the height fog: 0 the SkyAtmosphere of Unreal (the default, the Unreal way), 1 the fog colors and the sun glow of the data (10,000 cd/m2 a unit), 2 the SkyAtmosphere with the hue of the data."));
TAutoConsoleVariable<int32> CVarFogLayers(TEXT("swc.FogLayers"), 3,
	TEXT("The fog sets of the data as the layers of the height fog: 1 the legacy layers, 2 the second set (Volumetric fog 2), 3 both."));
TAutoConsoleVariable<float> CVarFogDensityScale(TEXT("swc.FogDensityScale"), 0.1f,
	TEXT("The height fog density property for one unit of the legacy fog layer density of the data."));
TAutoConsoleVariable<float> CVarFogDensityScale2(TEXT("swc.FogDensityScale2"), 0.0144f,
	TEXT("The second fog density property for one unit of the global density of the second fog set of the data (0.0144: the density as per km)."));
TAutoConsoleVariable<int32> CVarVolumetricFog(TEXT("swc.VolumetricFog"), 1, TEXT("The volumetric fog of Unreal: 1 on, 0 off."));
TAutoConsoleVariable<int32> CVarSourceFog(TEXT("swc.SourceFog"), 1,
	TEXT("The fog (stage E5 step 3): 1 the \"Volumetric fog 2\" set of the data by the rule of the source; 0 the fog of stage E3 (swc.FogLayers, swc.FogColorMode)."));
TAutoConsoleVariable<float> CVarSourceFogScale(TEXT("swc.SourceFogScale"), 1.0f, TEXT("A look scale of the density of the source fog (1: the source)."));
TAutoConsoleVariable<int32> CVarClouds(TEXT("swc.Clouds"), 1, TEXT("The volumetric clouds of Unreal: 1 on, 0 off."));
TAutoConsoleVariable<float> CVarCloudCover(TEXT("swc.CloudCover"), 2.0f,
	TEXT("The change of the coverage of the cloud material for one unit of the cloud cover of the data above the Everfall sky."));
TAutoConsoleVariable<float> CVarCloudBias(TEXT("swc.CloudBias"), 0.1f,
	TEXT("An offset of the coverage of the cloud material (0.1: the thin clouds of the Everfall noon of the retail pictures; a look value)."));
TAutoConsoleVariable<float> CVarStarScale(TEXT("swc.StarScale"), 200.0f,
	TEXT("The luminance (cd/m2) of the brightest star for one unit of the star intensity of the data."));
TAutoConsoleVariable<float> CVarMoonScale(TEXT("swc.MoonScale"), 15000.0f,
	TEXT("The luminance (cd/m2) of the moon disc and its coronas for one unit of their colors of the data."));
TAutoConsoleVariable<float> CVarMoonSize(TEXT("swc.MoonSize"), 3.5f,
	TEXT("The angular radius of the moon disc in degrees for one unit of the Size of the Moon element of the mission."));
TAutoConsoleVariable<float> CVarRainScale(TEXT("swc.RainScale"), 1.0f, TEXT("The rain amount of the data times this (1: the source; 0: no rain)."));
TAutoConsoleVariable<float> CVarSnowScale(TEXT("swc.SnowScale"), 1.0f, TEXT("The snow veil for one unit of the snow amount of the data."));
TAutoConsoleVariable<float> CVarDropLight(TEXT("swc.DropLight"), 0.25f,
	TEXT("The luminance of the snowflakes: this part of the luminance of a white surface in the light of the sun or the moon."));
TAutoConsoleVariable<float> CVarLocalFogScale(TEXT("swc.LocalFogScale"), 0.01f,
	TEXT("The extinction (1/m) of a fog volume for one unit of its GlobalDensity of the data."));
TAutoConsoleVariable<int32> CVarGrade(TEXT("swc.Grade"), 1,
	TEXT("The color grade of the source (stage E5): 1 the saturation, color balance, film curve, and photo filter of the data; 0 the Unreal tone mapper."));
TAutoConsoleVariable<int32> CVarGradeDebug(TEXT("swc.GradeDebug"), 0,
	TEXT("A check of the inputs of the grade: 1 the exposed value only (no curve, no grade), 2 the exposure (0.5 + log2 / 32)."));
TAutoConsoleVariable<float> CVarGradeBloom(TEXT("swc.GradeBloom"), 1.0f,
	TEXT("The part of the Unreal bloom in the grade: 1 as the Unreal tone mapper adds it, 0 no bloom."));
TAutoConsoleVariable<int32> CVarSourceBloom(TEXT("swc.SourceBloom"), 1,
	TEXT("The bloom (stage E5, research/color-pipeline.md section 6): 1 the mix of the source, exposure x ((1 - w) x scene + w x blur) with ")
	TEXT("w = saturate(\"Bloom amount\" x 0.3 x 0.125) and the Unreal bloom as a blur of gain 1 (the default); 0 the Unreal bloom added (swc.GradeBloom)."));
TAutoConsoleVariable<int32> CVarSkyWavelengths(TEXT("swc.SkyWavelengths"), 1,
	TEXT("The Rayleigh scattering color of the sky from the wavelengths of the data (\"Sky light: Wavelength (R, G, B)\"): 1 on, 0 the Unreal default."));
TAutoConsoleVariable<float> CVarSkyRayleigh(TEXT("swc.SkyRayleigh"), 0.0f,
	TEXT("A part of \"Sky light: Rayleigh scattering\" of the data in the Rayleigh scale of the sky (0: the Unreal scale; 1: the scale times the value)."));
TAutoConsoleVariable<float> CVarCloudTint(TEXT("swc.CloudTint"), 1.0f,
	TEXT("The albedo of the clouds from the cloud sun color of the data over the main light (stage E5): 1 the data, 0 white."));
TAutoConsoleVariable<int32> CVarSourceSky(TEXT("swc.SourceSky"), 1,
	TEXT("The sky color (stage E5): 1 the sky model of the source (the Nishita sky of the data and the night sky gradient), 0 the SkyAtmosphere of Unreal."));
TAutoConsoleVariable<int32> CVarSkyLightSource(TEXT("swc.SkyLightSource"), 1,
	TEXT("The sky light (the ambient): 1 the world probe of the data with the time-of-day probe multipliers (stage E5), 0 the real-time capture of the Unreal sky."));
TAutoConsoleVariable<float> CVarProbeScale(TEXT("swc.ProbeScale"), 1.0f, TEXT("A look scale of the world probe light (1: the units of the source)."));
TAutoConsoleVariable<int32> CVarLocalProbes(TEXT("swc.LocalProbes"), 0,
	TEXT("The local probes of the source (stage E5, a test): 2 the probes of each pixel (a pass after the base pass scales the material AO by the light of the probes of the pixel over the world probe); 1 the sky light blends the world probe with the local probes at the camera by the weights of the source; 0 the world probe only."));
TAutoConsoleVariable<float> CVarLocalProbeReach(TEXT("swc.LocalProbeReach"), 500.0f,
	TEXT("swc.LocalProbes 2: the probes whose box sphere is within this distance of the camera (m); the nearest 64 go to the pass."));
TAutoConsoleVariable<int32> CVarNightSkyTint(TEXT("swc.NightSkyTint"), 1,
	TEXT("The night sky: 1 the hue of the night horizon color of the data, 0 the color of the SkyAtmosphere."));

/** The luminance of one unit of a color of the data (cd/m2): the light unit of the public engine (Environment/SwcLocalLightRules.h). */
constexpr double SourceUnitLuminance = 10000.0;
/** The radius of the sky sphere (cm): beyond the far terrain of the world. */
constexpr double SkySphereRadius = 5.0e6;
/** The volumetric fog distance of Unreal: at most this (cm), whatever the ray-march range of the data. */
constexpr double VolumetricFogMaxDistance = 100000.0;
/** The cover of the Everfall sky of the data (sky/sky.json) maps to the default coverage of the cloud material of the engine (an
 *  offset of the coverage, -0.2 in m_SimpleVolumetricCloud_Inst); a sky with more cover of the data adds to it. */
constexpr double ReferenceCover = 0.6663;
/** The height fog does not cover the scene beyond this distance (cm): the sky sphere has its own sky. */
constexpr double FogCutoff = 4.0e6;

FLinearColor Linear(const std::array<double, 3>& C, double Scale = 1.0)
{
	return FLinearColor(static_cast<float>(C[0] * Scale), static_cast<float>(C[1] * Scale), static_cast<float>(C[2] * Scale));
}

/** @return The hue of a color: the color with its luminance 1 (Rec. 709), each channel at most 3; white for black. */
FLinearColor Hue(const std::array<double, 3>& C)
{
	const double Luminance = 0.2126 * C[0] + 0.7152 * C[1] + 0.0722 * C[2];
	if (Luminance <= 1e-6)
	{
		return FLinearColor::White;
	}
	return FLinearColor(static_cast<float>(FMath::Min(C[0] / Luminance, 3.0)), static_cast<float>(FMath::Min(C[1] / Luminance, 3.0)),
		static_cast<float>(FMath::Min(C[2] / Luminance, 3.0)));
}

std::array<double, 3> Mix(const std::array<double, 3>& A, const std::array<double, 3>& B, double T)
{
	return {A[0] + (B[0] - A[0]) * T, A[1] + (B[1] - A[1]) * T, A[2] + (B[2] - A[2]) * T};
}

std::array<double, 3> Times(const std::array<double, 3>& C, double S)
{
	return {C[0] * S, C[1] * S, C[2] * S};
}

/** @return An engine direction in Unreal (x, -y, z). */
FVector UnrealDirection(const env::Vec3& V)
{
	return FVector(V.x, -V.y, V.z);
}

}

void USwcEnvironmentSubsystem::UpdateBlend(const env::Vec3& Place, double WorldHour, double DeltaSeconds)
{
	// The menu mode: the preset of the menu scene is the one layer, at its fixed hour (no volumes of the world).
	if (Menu.IsSet())
	{
		Layers.Reset();
		FLayer Base;
		Base.Preset = Data->Presets.Find(Menu->Preset);
		Base.Name = FPaths::GetBaseFilename(Menu->Preset);
		Base.Hour = WorldHour;
		Base.Sky = Menu->Sky;
		Layers.Add(Base);
		LayerText = Base.Preset != nullptr ? FString::Printf(TEXT("%s (the menu)"), *Base.Name) : FString::Printf(TEXT("%s (the menu; not in presets.json)"), *Menu->Preset);
		EffectiveHour = WorldHour;
		return;
	}
	// The weights of the volumes: the target from the depth of the place (the feet of the player) in the shape (the blend distance),
	// moved in the blend time.
	for (size_t I = 0; I < Data->Volumes.size(); ++I)
	{
		const env::Volume& Volume = Data->Volumes[I];
		const double Target = env::volume_weight(Volume, Place);
		double* Weight = Weights.Find(static_cast<int32>(I));
		if (Weight == nullptr && Target <= 0.0)
		{
			continue;
		}
		const double Next = env::approach_weight(Weight != nullptr ? *Weight : 0.0, Target, Volume.blendTime, DeltaSeconds);
		if (Next <= 0.0 && Target <= 0.0)
		{
			Weights.Remove(static_cast<int32>(I));
		}
		else
		{
			Weights.Add(static_cast<int32>(I), Next);
		}
	}
	// The layers: the mission, then the volumes by priority (and by index for a tie).
	Layers.Reset();
	FLayer Base;
	Base.Preset = Data->Presets.Find(TEXT("mission"));
	Base.Name = TEXT("mission");
	Base.Hour = WorldHour;
	Layers.Add(Base);
	TArray<int32> Order;
	Weights.GetKeys(Order);
	Order.Sort([this](int32 A, int32 B)
	{
		const int PA = Data->Volumes[static_cast<size_t>(A)].priority;
		const int PB = Data->Volumes[static_cast<size_t>(B)].priority;
		return PA != PB ? PA < PB : A < B;
	});
	LayerText = TEXT("mission");
	for (const int32 Index : Order)
	{
		const env::Volume& Volume = Data->Volumes[static_cast<size_t>(Index)];
		const FString Name = UTF8_TO_TCHAR(Volume.preset.c_str());
		FLayer Layer;
		Layer.Preset = Data->Presets.Find(Name);
		if (Layer.Preset == nullptr)
		{
			continue;
		}
		Layer.Name = FPaths::GetBaseFilename(Name);
		Layer.Volume = Index;
		Layer.Weight = Weights[Index];
		Layer.Sky = UTF8_TO_TCHAR(Volume.sky.c_str());
		Layers.Add(Layer);
		LayerText += FString::Printf(TEXT(" + %s (volume %d, priority %d%s) %.2f"), *Layer.Name, Index, Volume.priority,
			Volume.hasHourOverride ? *FString::Printf(TEXT(", hour %.2f"), Volume.hourOverride) : TEXT(""), Layer.Weight);
	}
	// The hour: the world hour moved to the hour override of each volume by its weight, in the order of the priority (the retail
	// client: in the keep of Dunwood the hour goes from 12.05 to 0.0 in 1.07 s, the blend time 1.0 s of its volume; set4-weather
	// keep-walk-to-door). All layers use it.
	EffectiveHour = WorldHour;
	for (const FLayer& Layer : Layers)
	{
		const env::Volume* Volume = Layer.Volume >= 0 ? &Data->Volumes[static_cast<size_t>(Layer.Volume)] : nullptr;
		if (Volume != nullptr && Volume->hasHourOverride)
		{
			EffectiveHour += (Volume->hourOverride - EffectiveHour) * Layer.Weight;
		}
	}
	for (FLayer& Layer : Layers)
	{
		Layer.Hour = EffectiveHour;
	}
}

double USwcEnvironmentSubsystem::Blended(const TCHAR* Name, double Default, double FixedHour) const
{
	double Out = Default;
	bool bFirst = true;
	for (const FLayer& Layer : Layers)
	{
		if (Layer.Preset == nullptr || !Layer.Preset->Contains(Name))
		{
			continue;
		}
		const double Value = this->Value(*Layer.Preset, Name, env::curve_time(FixedHour >= 0.0 ? FixedHour : Layer.Hour), Default);
		Out = bFirst ? Value : Out + (Value - Out) * Layer.Weight;
		bFirst = false;
	}
	return Out;
}

std::array<double, 3> USwcEnvironmentSubsystem::BlendedColor(const TCHAR* Name, double FixedHour) const
{
	std::array<double, 3> Out{1.0, 1.0, 1.0};
	bool bFirst = true;
	for (const FLayer& Layer : Layers)
	{
		if (Layer.Preset == nullptr || !Layer.Preset->Contains(Name))
		{
			continue;
		}
		const std::array<double, 3> Value = Color(*Layer.Preset, Name, env::curve_time(FixedHour >= 0.0 ? FixedHour : Layer.Hour));
		Out = bFirst ? Value : Mix(Out, Value, Layer.Weight);
		bFirst = false;
	}
	return Out;
}

namespace
{
/**
 * Sets a local fog volume of Unreal for a fog volume of the data (a look mapping, the Unreal way): a sphere over the box (the unit
 * sphere of the component is GetBaseVolumeSize cm); a flat box (half its height under half its width) is a height layer at its
 * bottom that falls over half its height, else the radial fog of the sphere. The extinction of the data is GlobalDensity x Scale
 * per m (in the unit sphere: x the radius).
 */
void ApplyFogVolume(ULocalFogVolumeComponent* Fog, const USwcEnvironmentSubsystem::FData::FFogVolume& Volume, double Scale)
{
	const double HalfWidth = FMath::Max(Volume.Size.x, Volume.Size.y) / 2.0;
	const double HalfHeight = Volume.Size.z / 2.0;
	const bool bFlat = HalfHeight < 0.5 * HalfWidth;
	const double Radius = FMath::Max(bFlat ? HalfWidth * 1.05 : FMath::Max(HalfWidth, HalfHeight), 1.0);
	const double Extinction = Volume.Density * Scale * Radius;
	Fog->SetWorldLocation(FVector(Volume.Center.x * 100.0, -Volume.Center.y * 100.0, Volume.Center.z * 100.0));
	Fog->SetWorldScale3D(FVector(Radius * 100.0 / ULocalFogVolumeComponent::GetBaseVolumeSize()));
	Fog->SetRadialFogExtinction(bFlat ? 0.0f : static_cast<float>(Extinction));
	Fog->SetHeightFogExtinction(bFlat ? static_cast<float>(Extinction) : 0.0f);
	Fog->SetHeightFogFalloff(static_cast<float>(FMath::Clamp(Radius / FMath::Max(HalfHeight, 0.5), 1.0, 5000.0)));
	Fog->SetHeightFogOffset(static_cast<float>(-HalfHeight / Radius));
	Fog->SetFogAlbedo(FLinearColor(FMath::Clamp(static_cast<float>(Volume.Color[0]), 0.0f, 1.0f), FMath::Clamp(static_cast<float>(Volume.Color[1]), 0.0f, 1.0f),
		FMath::Clamp(static_cast<float>(Volume.Color[2]), 0.0f, 1.0f)));
}
}

void USwcEnvironmentSubsystem::CreateSkyAndFog(AActor* Owner, USceneComponent* Root)
{
	if (!Data->WorldProbe.IsEmpty())
	{
		const FString Name = TEXT("T_SwcProbe_") + Data->WorldProbe;
		WorldProbe = LoadObject<UTextureCube>(nullptr, *FString::Printf(TEXT("/SwcGameContent/Environment/Probes/%s.%s"), *Name, *Name), nullptr, LOAD_NoWarn);
		UE_LOG(LogSwcEnvironment, Log, TEXT("environment: the world probe %s%s"), *Name, WorldProbe != nullptr ? TEXT("") : TEXT(" is not imported (tools/import-sky.sh)"));
	}
	for (int32 I = 0; I < Data->FogVolumes.Num(); ++I)
	{
		ULocalFogVolumeComponent* Local = NewObject<ULocalFogVolumeComponent>(Owner);
		Local->SetupAttachment(Root);
		Local->SetUsingAbsoluteLocation(true);
		Local->SetUsingAbsoluteScale(true);
		Local->RegisterComponent();
		LocalFogs.Add(Local);
	}
	UExponentialHeightFogComponent* HeightFog = NewObject<UExponentialHeightFogComponent>(Owner, TEXT("SwcHeightFog"));
	HeightFog->SetupAttachment(Root);
	HeightFog->SetUsingAbsoluteLocation(true);
	HeightFog->RegisterComponent();
	Fog = HeightFog;
	if (UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineSky/VolumetricClouds/m_SimpleVolumetricCloud_Inst.m_SimpleVolumetricCloud_Inst")))
	{
		CloudMaterial = UMaterialInstanceDynamic::Create(Material, this);
		float Cover = DefaultCloudCover;
		if (CloudMaterial->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("Cloud_GlobalCoverage")), Cover))
		{
			DefaultCloudCover = Cover;
		}
		UVolumetricCloudComponent* Cloud = NewObject<UVolumetricCloudComponent>(Owner, TEXT("SwcClouds"));
		Cloud->SetMaterial(CloudMaterial);
		Cloud->SetupAttachment(Root);
		Cloud->RegisterComponent();
		Clouds = Cloud;
	}
	UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/EngineSky/SM_SkySphere.SM_SkySphere"));
	UMaterialInterface* SkyParent = LoadObject<UMaterialInterface>(nullptr, TEXT("/SwcGameContent/Environment/M_SwcSky.M_SwcSky"), nullptr, LOAD_NoWarn);
	if (Sphere != nullptr && SkyParent != nullptr)
	{
		SkyMaterial = UMaterialInstanceDynamic::Create(SkyParent, this);
		UStaticMeshComponent* Mesh = NewObject<UStaticMeshComponent>(Owner, TEXT("SwcSkySphere"));
		Mesh->SetStaticMesh(Sphere);
		Mesh->SetMaterial(0, SkyMaterial);
		Mesh->SetMobility(EComponentMobility::Movable);
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->SetCastShadow(false);
		Mesh->bAffectDistanceFieldLighting = false;
		Mesh->bVisibleInRayTracing = false;
		Mesh->SetUsingAbsoluteLocation(true);
		Mesh->SetUsingAbsoluteScale(true);
		const double Radius = FMath::Max(Sphere->GetBounds().SphereRadius, 1.0);
		Mesh->SetWorldScale3D(FVector(SkySphereRadius / Radius));
		Mesh->SetupAttachment(Root);
		Mesh->RegisterComponent();
		SkySphere = Mesh;
	}
	if (UMaterialInterface* Precipitation = LoadObject<UMaterialInterface>(nullptr, TEXT("/SwcGameContent/Environment/M_SwcPrecipitation.M_SwcPrecipitation"), nullptr, LOAD_NoWarn))
	{
		PrecipitationMaterial = UMaterialInstanceDynamic::Create(Precipitation, this);
		UPostProcessComponent* Post = NewObject<UPostProcessComponent>(Owner, TEXT("SwcPrecipitation"));
		Post->bUnbound = true;
		Post->Priority = 101.0f;
		Post->Settings.WeightedBlendables.Array.Add(FWeightedBlendable(1.0f, PrecipitationMaterial));
		Post->bEnabled = false;
		Post->SetupAttachment(Root);
		Post->RegisterComponent();
		PrecipitationPost = Post;
	}
	if (UMaterialInterface* Grade = LoadObject<UMaterialInterface>(nullptr, TEXT("/SwcGameContent/Environment/M_SwcGrade.M_SwcGrade"), nullptr, LOAD_NoWarn))
	{
		GradeMaterial = UMaterialInstanceDynamic::Create(Grade, this);
		UPostProcessComponent* Post = NewObject<UPostProcessComponent>(Owner, TEXT("SwcGrade"));
		Post->bUnbound = true;
		Post->Priority = 102.0f;
		Post->Settings.WeightedBlendables.Array.Add(FWeightedBlendable(1.0f, GradeMaterial));
		Post->SetupAttachment(Root);
		Post->RegisterComponent();
		GradePost = Post;
	}
	UE_LOG(LogSwcEnvironment, Log, TEXT("environment: the height fog, %d local fog volume(s), %s, %s, %d rain and snow volume(s)%s, %s, %d local probes (stage E3, E5)"), LocalFogs.Num(),
		Clouds.IsValid() ? *FString::Printf(TEXT("the volumetric clouds (the default cover %.3f)"), DefaultCloudCover) : TEXT("no clouds (no engine cloud material)"),
		SkySphere.IsValid() ? TEXT("the sky sphere with the stars and the moon") : TEXT("no sky sphere (tools/import-sky.sh)"),
		Data->Precipitation.Num(), PrecipitationPost.IsValid() ? TEXT("") : TEXT(" (no precipitation material: tools/import-sky.sh)"),
		GradePost.IsValid() ? TEXT("the color grade of the source") : TEXT("no color grade (tools/import-sky.sh)"), Data->LocalProbes.Num());
}

void USwcEnvironmentSubsystem::UpdateSkyAndFog(double Hour, const FVector& CameraLocation, double MoonWeight, double MainLux)
{
	const double CameraZ = CameraLocation.Z / 100.0;
	// The ambient (stage E5): the outdoor ambient of the source is the world probe (a cube of the radiance in the units of the source)
	// times its DiffuseMultiplier and the time-of-day "Environment Probe Diffuse Multiplier" (1 by day, 0.15 by night, by its
	// TODInfluence) and "Environment Probe Multiplier" (a color). The sky light shows that cube (its lower half too: the ground of the
	// probe); Lumen adds the bounce and the occlusion (the technique stays Unreal).
	if (USkyLightComponent* Ambient = SkyLight.Get())
	{
		const int32 Mode = WorldProbe != nullptr && CVarSkyLightSource.GetValueOnGameThread() != 0 ? 1 : 0;
		if (Mode != SkyLightMode)
		{
			SkyLightMode = Mode;
			Ambient->bRealTimeCapture = Mode == 0;
			Ambient->SourceType = Mode == 1 ? ESkyLightSourceType::SLS_SpecifiedCubemap : ESkyLightSourceType::SLS_CapturedScene;
			Ambient->bLowerHemisphereIsBlack = Mode == 0;
			Ambient->SetCubemap(Mode == 1 ? WorldProbe.Get() : nullptr);
			if (Mode == 0)
			{
				Ambient->SetIntensity(1.0f);
				Ambient->SetLightColor(FLinearColor::White);
			}
			Ambient->RecaptureSky();
		}
		if (Mode == 1)
		{
			const double Diffuse = FMath::Lerp(1.0, Blended(TEXT("Environment Probe Diffuse Multiplier"), 1.0), Data->WorldProbeTodInfluence);
			UpdateLocalProbes(Ambient, CameraLocation, SourceUnitLuminance * Data->WorldProbeDiffuse * Diffuse * CVarProbeScale.GetValueOnGameThread(),
				Linear(BlendedColor(TEXT("Environment Probe Multiplier"))));
		}
		UpdatePixelProbes(CameraLocation, Mode == 1 ? Linear(BlendedColor(TEXT("Environment Probe Multiplier"))) : FLinearColor::Black);
	}
	// The height fog: the legacy layers of the data (the bottom and the top density at their heights) and the second set (the
	// "Volumetric fog 2" layers) as the two layers of the Unreal height fog, with look scales of the density.
	if (UExponentialHeightFogComponent* HeightFog = Fog.Get())
	{
		const bool bOn = CVarFog.GetValueOnGameThread() != 0;
		HeightFog->SetVisibility(bOn);
		const int32 Sets = CVarFogLayers.GetValueOnGameThread();
		const double BottomHeight = Blended(TEXT("Fog height (bottom)"), 0.0);
		const double TopHeight = Blended(TEXT("Fog height (top)"), 400.0);
		const double BottomDensity = Blended(TEXT("Fog layer density (bottom)"), 0.05);
		const double TopDensity = Blended(TEXT("Fog layer density (top)"), 0.02);
		const double Global2 = Blended(TEXT("Volumetric fog 2: Global fog density"), 6.0);
		const double Bottom2 = Blended(TEXT("Volumetric fog 2: Fog height (bottom)"), -100.0);
		const double Top2 = Blended(TEXT("Volumetric fog 2: Fog height (top)"), 700.0);
		const double BottomDensity2 = Blended(TEXT("Volumetric fog 2: Fog layer density (bottom)"), 1.0);
		const double TopDensity2 = Blended(TEXT("Volumetric fog 2: Fog layer density (top)"), 0.0001);
		HeightFog->SetVolumetricFog(bOn && CVarVolumetricFog.GetValueOnGameThread() != 0);
		if (CVarSourceFog.GetValueOnGameThread() != 0)
		{
			UpdateSourceFog(HeightFog, CameraLocation, Global2, Bottom2, Top2, BottomDensity2, TopDensity2);
		}
		else
		{
			HeightFog->SetWorldLocation(FVector(CameraLocation.X, CameraLocation.Y, BottomHeight * 100.0));
			HeightFog->SetStartDistance(0.0f);
			HeightFog->SetVolumetricFogStartDistance(0.0f);
			HeightFog->SetVolumetricFogNearFadeInDistance(0.0f);
			HeightFog->SetVolumetricFogExtinctionScale(1.0f);
			HeightFog->SetFogMaxOpacity(1.0f);
			if (HeightFog->bOverrideLightColorsWithFogInscatteringColors)
			{
				HeightFog->bOverrideLightColorsWithFogInscatteringColors = false;
				HeightFog->MarkRenderStateDirty();
			}
			if (USkyAtmosphereComponent* Sky = Atmosphere.Get())
			{
				Sky->SetHeightFogContribution(1.0f);
			}
			HeightFog->SetFogDensity((Sets & 1) != 0 ? static_cast<float>(BottomDensity * CVarFogDensityScale.GetValueOnGameThread()) : 0.0f);
			HeightFog->SetFogHeightFalloff(static_cast<float>(FMath::Max(0.001, env::fog_height_falloff(BottomDensity, TopDensity, BottomHeight, TopHeight))));
			FExponentialHeightFogData Second;
			Second.FogDensity = (Sets & 2) != 0 ? static_cast<float>(Global2 * BottomDensity2 * CVarFogDensityScale2.GetValueOnGameThread()) : 0.0f;
			Second.FogHeightFalloff = static_cast<float>(FMath::Max(0.001, env::fog_height_falloff(BottomDensity2, TopDensity2, Bottom2, Top2)));
			Second.FogHeightOffset = static_cast<float>((Bottom2 - BottomHeight) * 100.0);
			HeightFog->SetSecondFogData(Second);
			// The color: at the camera height between the bottom and the top color of the data.
			const double T = TopHeight > BottomHeight ? FMath::Clamp((CameraZ - BottomHeight) / (TopHeight - BottomHeight), 0.0, 1.0) : 0.0;
			const std::array<double, 3> Bottom = Times(BlendedColor(TEXT("Fog color")), Blended(TEXT("Fog color multiplier"), 1.0));
			const std::array<double, 3> Top = Times(BlendedColor(TEXT("Fog color (top)")), Blended(TEXT("Fog color (top) multiplier"), 1.0));
			const std::array<double, 3> FogColor = Mix(Bottom, Top, T);
			const std::array<double, 3> Radial = Times(BlendedColor(TEXT("Fog color (radial)")), Blended(TEXT("Fog color (radial) multiplier"), 1.0));
			const double Lobe = Blended(TEXT("Fog radial lobe"), 0.5);
			const int32 Mode = CVarFogColorMode.GetValueOnGameThread();
			HeightFog->SetFogInscatteringColor(Mode == 1 ? Linear(FogColor, SourceUnitLuminance) : FLinearColor::Black);
			HeightFog->SetSkyAtmosphereAmbientContributionColorScale(Mode == 0 ? FLinearColor::White : Mode == 2 ? Hue(FogColor) : FLinearColor::Black);
			HeightFog->SetDirectionalInscatteringColor(Mode == 1 ? Linear(Radial, SourceUnitLuminance) : FLinearColor::Black);
			HeightFog->SetFogCutoffDistance(static_cast<float>(FogCutoff));
			HeightFog->SetDirectionalInscatteringExponent(static_cast<float>(4.0 / FMath::Max(1.0 - Lobe, 0.05)));
			// The volumetric fog of Unreal with the albedo and the anisotropy of the entities of the second set.
			HeightFog->SetVolumetricFogAlbedo(Linear(BlendedColor(TEXT("Volumetric fog 2: Fog albedo color (entities)"))).ToFColor(true));
			HeightFog->SetVolumetricFogScatteringDistribution(static_cast<float>(FMath::Clamp(Blended(TEXT("Volumetric fog 2: Anisotropy factor (entities)"), 0.5), -0.9, 0.9)));
			for (UDirectionalLightComponent* Light : {Sun.Get(), Moon.Get()})
			{
				if (Light != nullptr)
				{
					Light->SetVolumetricScatteringIntensity(1.0f);
				}
			}
			if (USkyLightComponent* Ambient = SkyLight.Get())
			{
				Ambient->SetVolumetricScatteringIntensity(1.0f);
			}
		}
		HeightFog->SetVolumetricFogDistance(static_cast<float>(FMath::Min(Blended(TEXT("Volumetric fog 2: Maximum range of ray-marching"), 1024.0) * 100.0, VolumetricFogMaxDistance)));
	}
	// The fog volumes of the data (their look scale applies at a change).
	const double LocalScale = CVarLocalFogScale.GetValueOnGameThread();
	if (LocalScale != LocalFogScaleApplied)
	{
		LocalFogScaleApplied = LocalScale;
		for (int32 I = 0; I < LocalFogs.Num() && I < Data->FogVolumes.Num(); ++I)
		{
			if (ULocalFogVolumeComponent* Local = LocalFogs[I].Get())
			{
				ApplyFogVolume(Local, Data->FogVolumes[I], LocalScale);
			}
		}
	}
	// The sky colors of the data (stage E5, part 2): the Rayleigh scattering color from the wavelengths of the source sky model by
	// 1 / lambda^4 (the Unreal default (0.175, 0.410, 1) is that ratio for 680, 550, and 440 nm; the data has 693, 596, and 450 nm in
	// Everfall: less green scattering, the violet sky of retail), its scale by (440 / blue)^4, and optionally the Rayleigh value of the data.
	if (USkyAtmosphereComponent* Sky = Atmosphere.Get())
	{
		const double R = FMath::Max(Blended(TEXT("Sky light: Wavelength (R)"), 680.0), 300.0);
		const double G = FMath::Max(Blended(TEXT("Sky light: Wavelength (G)"), 550.0), 300.0);
		const double B = FMath::Max(Blended(TEXT("Sky light: Wavelength (B)"), 440.0), 300.0);
		const bool bData = CVarSkyWavelengths.GetValueOnGameThread() != 0;
		const FLinearColor Color = bData ? FLinearColor(static_cast<float>(FMath::Pow(B / R, 4.0)), static_cast<float>(FMath::Pow(B / G, 4.0)), 1.0f)
			: FLinearColor(0.175287f, 0.409607f, 1.0f);
		const double RayleighPart = FMath::Clamp(static_cast<double>(CVarSkyRayleigh.GetValueOnGameThread()), 0.0, 1.0);
		const double Value = Blended(TEXT("Sky light: Rayleigh scattering"), 1.0);
		const double Scale = 0.0331 * (bData ? FMath::Pow(440.0 / B, 4.0) : 1.0) * FMath::Lerp(1.0, Value, RayleighPart);
		Sky->SetRayleighScattering(Color);
		Sky->SetRayleighScatteringScale(static_cast<float>(Scale));
	}
	// The night sky (E1, a look choice): the atmosphere lit by the moon is a day sky; the night factor darkens it as the moon fades in,
	// with the hue of the night horizon color of the data (E3).
	if (USkyAtmosphereComponent* Sky = Atmosphere.Get())
	{
		const FLinearColor Tint = CVarNightSkyTint.GetValueOnGameThread() != 0 ? Hue(BlendedColor(TEXT("Night sky: Horizon color"))) : FLinearColor::White;
		const FLinearColor Night = Tint * static_cast<float>(NightSkyFactor);
		Sky->SetSkyLuminanceFactor(FMath::Lerp(FLinearColor::White, Night, static_cast<float>(MoonWeight)));
	}
	// The clouds: the cover of the sky slices of the layers (a layer with no sky keeps the cover), the cloud shading color of the data.
	double Cover = 0.0;
	FString SkyName = TEXT("the mission sky (no cloud dome)");
	for (const FLayer& Layer : Layers)
	{
		if (const double* SkyCover = Layer.Sky.IsEmpty() ? nullptr : Data->SkyCovers.Find(Layer.Sky))
		{
			Cover += (*SkyCover - Cover) * Layer.Weight;
			SkyName = FPaths::GetBaseFilename(Layer.Sky);
		}
	}
	if (UVolumetricCloudComponent* Cloud = Clouds.Get())
	{
		const bool bOn = CVarClouds.GetValueOnGameThread() != 0 && Cover > 0.0;
		Cloud->SetVisibility(bOn);
		if (bOn && CloudMaterial != nullptr)
		{
			const double Unreal = FMath::Clamp(DefaultCloudCover + (Cover - ReferenceCover) * CVarCloudCover.GetValueOnGameThread()
				+ CVarCloudBias.GetValueOnGameThread(), -1.0, 1.0);
			CloudMaterial->SetScalarParameterValue(TEXT("Cloud_GlobalCoverage"), static_cast<float>(Unreal));
			// The color of the clouds (stage E5): the source lights its cloud layers with the cloud sun color (TimeOfDay.cpp:
			// lerp(the sun color x "Cloud shading: Sun light multiplier", "Sun custom color" x its multiplier, its influence), in the
			// units of the source); the Unreal clouds are lit by the main light. The albedo of the clouds is the ratio of the two
			// colors in each channel (swc.CloudTint 1; 0: white): the pink-lilac clouds of the Everfall noon, dark clouds by night.
			const UDirectionalLightComponent* Main = MoonWeight >= 0.5 ? Moon.Get() : Sun.Get();
			FLinearColor Albedo = FLinearColor::White;
			if (Main != nullptr && Main->Intensity > 0.0f)
			{
				const FLinearColor Light = Main->GetLightColor() * (Main->Intensity / static_cast<float>(SourceUnitLuminance * PI));
				const std::array<double, 3> Custom = Times(BlendedColor(TEXT("Cloud shading: Sun custom color")),
					Blended(TEXT("Cloud shading: Sun custom color multiplier"), 1.0));
				const double Influence = FMath::Clamp(Blended(TEXT("Cloud shading: Sun custom color influence"), 0.0), 0.0, 1.0);
				const double SunPart = Blended(TEXT("Cloud shading: Sun light multiplier"), 1.0);
				const float Channels[3] = {Light.R, Light.G, Light.B};
				float Ratio[3];
				for (int32 C = 0; C < 3; ++C)
				{
					const double CloudSun = FMath::Lerp(SunPart * Channels[C], Custom[C], Influence);
					Ratio[C] = static_cast<float>(FMath::Clamp(CloudSun / FMath::Max(static_cast<double>(Channels[C]), 1e-6), 0.0, 1.5));
				}
				Albedo = FMath::Lerp(FLinearColor::White, FLinearColor(Ratio[0], Ratio[1], Ratio[2]), CVarCloudTint.GetValueOnGameThread());
			}
			CloudMaterial->SetVectorParameterValue(TEXT("Cloud_AlbedoColor"), Albedo);
		}
	}
	// The sky sphere: the stars turn with the sun; the moon disc and its coronas of the data.
	if (UStaticMeshComponent* Mesh = SkySphere.Get())
	{
		Mesh->SetWorldLocation(CameraLocation);
		if (SkyMaterial != nullptr)
		{
			UpdateSourceSky(Hour, MoonWeight);
			const FCelestial Bodies = Celestial();
			const env::Rotation R = env::celestial_from_engine(Hour, Bodies.SunRotation, Bodies.Longitude);
			// The rows of the rotation from the Unreal world (x, -y, z of the engine) to the celestial frame.
			auto Row = [&R](int Index)
			{
				return FLinearColor(static_cast<float>(R[Index][0]), static_cast<float>(-R[Index][1]), static_cast<float>(R[Index][2]), 0.0f);
			};
			SkyMaterial->SetVectorParameterValue(TEXT("CelestialX"), Row(0));
			SkyMaterial->SetVectorParameterValue(TEXT("CelestialY"), Row(1));
			SkyMaterial->SetVectorParameterValue(TEXT("CelestialZ"), Row(2));
			SkyMaterial->SetScalarParameterValue(TEXT("StarIntensity"),
				static_cast<float>(FMath::Max(0.0, Blended(TEXT("Night sky: Star intensity"), 0.0)) * CVarStarScale.GetValueOnGameThread()));
			const FVector MoonDirection = UnrealDirection(env::moon_toward(Bodies.MoonLatitude, Bodies.MoonLongitude));
			SkyMaterial->SetVectorParameterValue(TEXT("MoonDirection"), FLinearColor(MoonDirection.X, MoonDirection.Y, MoonDirection.Z, 0.0f));
			SkyMaterial->SetScalarParameterValue(TEXT("MoonSize"),
				static_cast<float>(FMath::Tan(FMath::DegreesToRadians(Bodies.MoonSize * CVarMoonSize.GetValueOnGameThread()))));
			const double MoonScale = CVarMoonScale.GetValueOnGameThread();
			SkyMaterial->SetVectorParameterValue(TEXT("MoonColor"),
				Linear(Times(BlendedColor(TEXT("Night sky: Moon color")), Blended(TEXT("Night sky: Moon color multiplier"), 0.0)), MoonScale));
			FLinearColor Inner = Linear(Times(BlendedColor(TEXT("Night sky: Moon inner corona color")), Blended(TEXT("Night sky: Moon inner corona color multiplier"), 0.0)), MoonScale);
			Inner.A = static_cast<float>(Blended(TEXT("Night sky: Moon inner corona scale"), 0.1));
			FLinearColor Outer = Linear(Times(BlendedColor(TEXT("Night sky: Moon outer corona color")), Blended(TEXT("Night sky: Moon outer corona color multiplier"), 0.0)), MoonScale);
			Outer.A = static_cast<float>(Blended(TEXT("Night sky: Moon outer corona scale"), 0.07));
			// The coronas follow the moon at night only (the multipliers of the data are not 0 by day).
			SkyMaterial->SetVectorParameterValue(TEXT("InnerCorona"), Inner * static_cast<float>(MoonWeight));
			SkyMaterial->SetVectorParameterValue(TEXT("OuterCorona"), Outer * static_cast<float>(MoonWeight));
		}
	}
	// The color grade of the source.
	if (UPostProcessComponent* Post = GradePost.Get())
	{
		Post->bEnabled = CVarGrade.GetValueOnGameThread() != 0;
		if (GradeMaterial != nullptr)
		{
			GradeMaterial->SetVectorParameterValue(TEXT("FilmCurve"), FLinearColor(static_cast<float>(Blended(TEXT("Film curve shoulder scale"), 1.0)),
				static_cast<float>(Blended(TEXT("Film curve midtones scale"), 1.0)), static_cast<float>(Blended(TEXT("Film curve toe scale"), 1.0)),
				static_cast<float>(Blended(TEXT("Film curve whitepoint"), 4.0))));
			FLinearColor Balance = Linear(BlendedColor(TEXT("Color balance")));
			Balance.A = static_cast<float>(Blended(TEXT("Saturation"), 1.0));
			GradeMaterial->SetVectorParameterValue(TEXT("Balance"), Balance);
			FLinearColor Photo = Linear(BlendedColor(TEXT("Filters: photofilter color")));
			Photo.A = static_cast<float>(FMath::Clamp(Blended(TEXT("Filters: photofilter density"), 0.0), 0.0, 1.0));
			GradeMaterial->SetVectorParameterValue(TEXT("PhotoFilter"), Photo);
			GradeMaterial->SetScalarParameterValue(TEXT("Debug"), static_cast<float>(CVarGradeDebug.GetValueOnGameThread()));
			// The source mixes a blur of the HDR scene with the scene before the curve: w = saturate(BloomAmount x 0.3 x 0.125) (0.045 at
			// Everfall noon). The grade adds the bloom input times BloomScale: with the bloom as a blur of gain 1 (below), w / (1 - w),
			// and the exposure target of the meter takes the factor 1 - w (UpdateEnvironment).
			const bool bSourceBloom = CVarSourceBloom.GetValueOnGameThread() != 0;
			BloomMix = bSourceBloom ? FMath::Clamp(Blended(TEXT("Bloom amount"), 0.0) * 0.3 * 0.125, 0.0, 0.99) : 0.0;
			GradeMaterial->SetScalarParameterValue(TEXT("BloomScale"), bSourceBloom ? static_cast<float>(BloomMix / (1.0 - BloomMix)) : CVarGradeBloom.GetValueOnGameThread());
			if (UPostProcessComponent* BloomPost = Exposure.Get())
			{
				// The blur of the source: its 15-tap kernel (the binomial C(18, k), sigma 2.12 taps) on a quarter of the screen is a
				// Gaussian of about 8.5 pixels at 1,256 pixels (an inference: the audit leaves the scale open); its two images (the weights
				// 0.0174 and 0.192 over their sum) as the stages 3 and 4 of the Unreal bloom (sigma = the view width x size % / 1156: 2 x 4
				// and 3.9 x 4 % give 8.7 and 17 pixels at 1,256). A stage adds its Gaussian times tint x intensity / 6: the tints are 6 x
				// the weights, the gain is 1. No threshold, no lens flare, no dirt mask.
				FPostProcessSettings& Bloom = BloomPost->Settings;
				Bloom.bOverride_BloomMethod = Bloom.bOverride_BloomIntensity = Bloom.bOverride_BloomThreshold = Bloom.bOverride_BloomSizeScale = bSourceBloom;
				Bloom.bOverride_Bloom1Size = Bloom.bOverride_Bloom2Size = Bloom.bOverride_Bloom3Size = bSourceBloom;
				Bloom.bOverride_Bloom4Size = Bloom.bOverride_Bloom5Size = Bloom.bOverride_Bloom6Size = bSourceBloom;
				Bloom.bOverride_Bloom3Tint = Bloom.bOverride_Bloom4Tint = Bloom.bOverride_BloomGaussianIntensity = bSourceBloom;
				Bloom.bOverride_LensFlareIntensity = Bloom.bOverride_BloomDirtMaskIntensity = bSourceBloom;
				Bloom.BloomMethod = EBloomMethod::BM_SOG;
				Bloom.BloomIntensity = 1.0f;
				Bloom.BloomGaussianIntensity = 1.0f;
				Bloom.BloomThreshold = -1.0f;
				Bloom.BloomSizeScale = 4.0f;
				Bloom.Bloom1Size = Bloom.Bloom2Size = Bloom.Bloom5Size = Bloom.Bloom6Size = 0.0f;
				Bloom.Bloom3Size = 2.0f;
				Bloom.Bloom4Size = 3.9f;
				const float Small = 0.0174f / (0.0174f + 0.192f);
				Bloom.Bloom3Tint = FLinearColor::White * (6.0f * (1.0f - Small));
				Bloom.Bloom4Tint = FLinearColor::White * (6.0f * Small);
				Bloom.LensFlareIntensity = 0.0f;
				Bloom.BloomDirtMaskIntensity = 0.0f;
			}
		}
	}
	// The rain and the snow at the camera: the largest amount of the volumes (the full amount within the inner radius).
	const env::Vec3 At{CameraLocation.X / 100.0, -CameraLocation.Y / 100.0, CameraZ};
	double Rain = 0.0;
	double Snow = 0.0;
	const FData::FPrecipitation* RainVolume = nullptr;
	FString Weather;
	for (const FData::FPrecipitation& Volume : Data->Precipitation)
	{
		const double Horizontal = FMath::Sqrt(FMath::Square(At.x - Volume.Center.x) + FMath::Square(At.y - Volume.Center.y));
		if (Volume.Amount <= 0.0 || Horizontal > Volume.Outer || FMath::Abs(At.z - Volume.Center.z) > Volume.HalfHeight)
		{
			continue;
		}
		const double Weight = Volume.Outer > Volume.Inner ? FMath::Clamp((Volume.Outer - Horizontal) / (Volume.Outer - Volume.Inner), 0.0, 1.0) : 1.0;
		double& Amount = Volume.bSnow ? Snow : Rain;
		if (!Volume.bSnow && Weight * Volume.Amount > Rain)
		{
			RainVolume = &Volume;
		}
		Amount = FMath::Max(Amount, Weight * Volume.Amount);
		Weather += FString::Printf(TEXT(" %s %s %.2f"), Volume.bSnow ? TEXT("snow") : TEXT("rain"), *Volume.Name, Weight);
	}
	if (UPostProcessComponent* Post = PrecipitationPost.Get())
	{
		// The rain: the scene rain of the source (posteffectsgame@scenerainps; PostProcessRain.cpp, CSceneRain): the amount of the
		// volume at the camera, its drops amount, lighting, and speed, and the EV rule of the exposure (the adapted luminance).
		const double RainVeil = Rain * CVarRainScale.GetValueOnGameThread();
		const double SnowVeil = Snow * CVarSnowScale.GetValueOnGameThread();
		Post->bEnabled = RainVeil > 0.0 || SnowVeil > 0.0;
		if (Post->bEnabled && PrecipitationMaterial != nullptr)
		{
			PrecipitationMaterial->SetScalarParameterValue(TEXT("RainAmount"), static_cast<float>(RainVeil));
			PrecipitationMaterial->SetVectorParameterValue(TEXT("RainDrops"), RainVolume != nullptr
				? FLinearColor(static_cast<float>(RainVolume->DropsAmount), static_cast<float>(RainVolume->DropsLighting), static_cast<float>(RainVolume->DropsSpeed), 0.0f)
				: FLinearColor(1.0f, 1.0f, 1.0f, 0.0f));
			PrecipitationMaterial->SetScalarParameterValue(TEXT("AutoCompensation"), static_cast<float>(Blended(TEXT("EV Auto compensation"), 1.5)));
			PrecipitationMaterial->SetScalarParameterValue(TEXT("SnowAmount"), static_cast<float>(SnowVeil));
			const float Light = static_cast<float>(MainLux / PI * CVarDropLight.GetValueOnGameThread());
			PrecipitationMaterial->SetVectorParameterValue(TEXT("DropLight"), FLinearColor(Light, Light, Light, 0.0f));
		}
	}
	SkyText = FString::Printf(TEXT("sky %s, cloud cover %.3f; stars %.2f; rain %.2f, snow %.2f%s; probes%s"), *SkyName, Cover,
		Blended(TEXT("Night sky: Star intensity"), 0.0), Rain, Snow, Weather.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" (%s)"), *Weather.TrimStart()),
		ProbeText.IsEmpty() ? TEXT(" world 1.00") : *ProbeText);
}

/** A dome of the source sky on a worker thread: the Mie texels, then the Rayleigh texels, and the done mark. */
struct FSwcSourceSkyJob
{
	TArray<FLinearColor> Texels;
	TAtomic<bool> bDone{false};
};

namespace {
constexpr int32 SourceSkyWidth = 64;
constexpr int32 SourceSkyHeight = 32;

UTexture2D* MakeSkyDome()
{
	UTexture2D* Texture = UTexture2D::CreateTransient(SourceSkyWidth, SourceSkyHeight, PF_A32B32G32R32F);
	Texture->SRGB = false;
	Texture->Filter = TF_Bilinear;
	Texture->AddressX = TA_Wrap;
	Texture->AddressY = TA_Clamp;
	Texture->UpdateResource();
	return Texture;
}

void FillSkyDome(UTexture2D* Texture, const FLinearColor* Texels)
{
	FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
	void* Data = Mip.BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(Data, Texels, sizeof(FLinearColor) * SourceSkyWidth * SourceSkyHeight);
	Mip.BulkData.Unlock();
	Texture->UpdateResource();
}
} // namespace

void USwcEnvironmentSubsystem::UpdateSourceSky(double Hour, double MoonWeight)
{
	// The light of the sky model: the sun by day; by night the moon (the source puts the moon direction into the sky model at night).
	const FCelestial Bodies = Celestial();
	const env::Vec3 Toward = MoonWeight >= 0.5 ? env::moon_toward(Bodies.MoonLatitude, Bodies.MoonLongitude) : env::sun_toward(SunPathHour, Bodies.SunRotation, Bodies.Longitude);
	const std::array<double, 3> Intensity = Times(BlendedColor(TEXT("Sky light: Sun intensity")), Blended(TEXT("Sky light: Sun intensity multiplier"), 100.0));
	env::SkyModel Model;
	Model.sunToward = Toward;
	Model.sunIntensity = Intensity;
	Model.rayleigh = Blended(TEXT("Sky light: Rayleigh scattering"), 1.0);
	Model.mie = Blended(TEXT("Sky light: Mie scattering"), 4.8);
	Model.g = Blended(TEXT("Sky light: Sun anisotropy factor"), -0.99);
	Model.waveLengths = {Blended(TEXT("Sky light: Wavelength (R)"), 693.0), Blended(TEXT("Sky light: Wavelength (G)"), 596.0),
		Blended(TEXT("Sky light: Wavelength (B)"), 450.0)};
	if (SourceMie == nullptr)
	{
		SourceMie = MakeSkyDome();
		SourceRayleigh = MakeSkyDome();
	}
	// A finished dome goes into the textures.
	if (SourceSkyJob.IsValid() && SourceSkyJob->bDone.Load())
	{
		FillSkyDome(SourceMie, SourceSkyJob->Texels.GetData());
		FillSkyDome(SourceRayleigh, SourceSkyJob->Texels.GetData() + SourceSkyWidth * SourceSkyHeight);
		SourceSkyJob.Reset();
	}
	// A new dome when the sun moves by 0.2 degrees or a value changes by 0.5 %.
	const TArray<double> Key{Toward.x, Toward.y, Toward.z, Intensity[0], Intensity[1], Intensity[2], Model.rayleigh, Model.mie, Model.g,
		Model.waveLengths[0], Model.waveLengths[1], Model.waveLengths[2]};
	bool bChanged = SourceSkyKey.Num() != Key.Num();
	for (int32 I = 0; !bChanged && I < Key.Num(); ++I)
	{
		const double Tolerance = I < 3 ? 0.0035 : 0.005 * FMath::Max(1e-3, FMath::Abs(SourceSkyKey[I]));
		bChanged = FMath::Abs(Key[I] - SourceSkyKey[I]) > Tolerance;
	}
	if (bChanged && !SourceSkyJob.IsValid())
	{
		SourceSkyKey = Key;
		TSharedPtr<FSwcSourceSkyJob, ESPMode::ThreadSafe> Job = MakeShared<FSwcSourceSkyJob, ESPMode::ThreadSafe>();
		SourceSkyJob = Job;
		Async(EAsyncExecution::ThreadPool, [Model, Job]()
		{
			TArray<FLinearColor>& Texels = Job->Texels;
			Texels.SetNumZeroed(SourceSkyWidth * SourceSkyHeight * 2);
			for (int32 Y = 0; Y < SourceSkyHeight; ++Y)
			{
				// The rows: the zenith angle 0 to 90 degrees (the dome of the source); the columns: the azimuth from north to east.
				const double Theta = (Y + 0.5) / SourceSkyHeight * 0.5 * PI;
				for (int32 X = 0; X < SourceSkyWidth; ++X)
				{
					const double Phi = (X + 0.5) / SourceSkyWidth * 2.0 * PI;
					const env::Vec3 View{FMath::Sin(Theta) * FMath::Sin(Phi), FMath::Sin(Theta) * FMath::Cos(Phi), FMath::Cos(Theta)};
					const env::SkyParts Parts = env::source_sky(Model, View);
					Texels[Y * SourceSkyWidth + X] = FLinearColor(static_cast<float>(Parts.mie[0]), static_cast<float>(Parts.mie[1]), static_cast<float>(Parts.mie[2]), 1.0f);
					Texels[SourceSkyWidth * SourceSkyHeight + Y * SourceSkyWidth + X] = FLinearColor(static_cast<float>(Parts.rayleigh[0]),
						static_cast<float>(Parts.rayleigh[1]), static_cast<float>(Parts.rayleigh[2]), 1.0f);
				}
			}
			Job->bDone = true;
		});
	}
	// The material: the dome, the phases (toward the sun or the moon of the model), and the night sky gradient of the source
	// (SkyDome_NightSkyColBase = the horizon color, SkyDome_NightSkyColDelta = the zenith color - the horizon color, and the
	// zenith shift of the public engine: 1 / (shift + 0.1) and 0.1 / (shift + 0.1); an inference from the public CREHDRSky code).
	const FVector SkySun = UnrealDirection(Toward);
	SkyMaterial->SetScalarParameterValue(TEXT("SourceSky"), CVarSourceSky.GetValueOnGameThread() != 0 ? 1.0f : 0.0f);
	SkyMaterial->SetTextureParameterValue(TEXT("SourceMie"), SourceMie);
	SkyMaterial->SetTextureParameterValue(TEXT("SourceRayleigh"), SourceRayleigh);
	SkyMaterial->SetVectorParameterValue(TEXT("SkySun"), FLinearColor(SkySun.X, SkySun.Y, SkySun.Z, static_cast<float>(FMath::Clamp(Model.g, -0.9995, 0.9995))));
	const std::array<double, 3> Horizon = Times(BlendedColor(TEXT("Night sky: Horizon color")), Blended(TEXT("Night sky: Horizon color multiplier"), 0.0));
	const std::array<double, 3> Zenith = Times(BlendedColor(TEXT("Night sky: Zenith color")), Blended(TEXT("Night sky: Zenith color multiplier"), 0.0));
	const double Shift = Blended(TEXT("Night sky: Zenith shift"), 0.5);
	SkyMaterial->SetVectorParameterValue(TEXT("NightBase"), Linear(Horizon));
	SkyMaterial->SetVectorParameterValue(TEXT("NightDelta"), FLinearColor(static_cast<float>(Zenith[0] - Horizon[0]), static_cast<float>(Zenith[1] - Horizon[1]),
		static_cast<float>(Zenith[2] - Horizon[2]), 0.0f));
	SkyMaterial->SetVectorParameterValue(TEXT("NightShift"), FLinearColor(static_cast<float>(1.0 / (Shift + 0.1)), static_cast<float>(0.1 / (Shift + 0.1)), 0.0f, 0.0f));
	SkyMaterial->SetScalarParameterValue(TEXT("SourceUnit"), static_cast<float>(SourceUnitLuminance));
}

void USwcEnvironmentSubsystem::UpdateSourceFog(UExponentialHeightFogComponent* HeightFog, const FVector& CameraLocation, double Global, double Bottom, double Top,
	double BottomDensity, double TopDensity)
{
	// The volumetric fog of the source (stage E5 step 3; the static study ~/Games/soulwarden-client/research/e5-source-fog.md:
	// the retail fog shaders and NewWorld.exe, sections 1 to 3) and its stock Unreal fit (section 5):
	// - the density rho = GlobalDensity x 0.01 x exp of the line through ln(the bottom density) at the bottom height and ln(the top
	//   density) at the top height; the extinction e x rho, the light J = s x rho x [Ce x E + B] (s "In-scattering factor", e
	//   "Extinction factor", Ce "Fog albedo color (entities)", E the probes of the fog cell / 12.566, B the two sun lobes);
	// - B = S x [Ca x P(k1) x (1 - blend x mode) + Cr x P(k2) x blend], P the Schlick phase (1 - k^2) / (12.566 (1 - k mu)^2),
	//   S = the sun color / its luminance x the sun lux / (10000 pi) (the source unit is 10,000 cd/m2);
	// - the ramp: the light of the cells fades in from "Ramp start" to "Ramp end" (the camera distance).
	// The Unreal fit: the albedo Ce, the extinction of the two Unreal paths (the density 10 e rho / ln2^2, the extinction scale
	// 2 ln2^2), the sun scattering intensity s / (e pi) with the override of the light colors (the directional inscattering color
	// nSun x V / (Ce x Y(the sun proxy color)), V the sum of the lobe colors), one Henyey-Greenstein g (the mean cosine of the two
	// Schlick lobes by their light). The fog ambient of Unreal is the Lumen translucency volume (no gain); without that volume
	// (r.Lumen.TranslucencyVolume.Enable 0) the override takes the fog inscattering color x the sky light scattering intensity s / e.
	const double Extinction = FMath::Max(Blended(TEXT("Volumetric fog 2: Extinction factor"), 1.0), 1e-4);
	const double InScattering = Blended(TEXT("Volumetric fog 2: In-scattering factor"), 1.0);
	const double DensityBottom = FMath::Clamp(BottomDensity, 1e-5, 1.0);
	const double DensityTop = FMath::Clamp(TopDensity, 1e-5, 1.0);
	const double TopHeight = FMath::Max(Top, Bottom + 1.0);
	const double Ln2Squared = FMath::Square(UE_LN2);
	const double RhoBottom = Global * 0.01 * DensityBottom;
	HeightFog->SetWorldLocation(FVector(CameraLocation.X, CameraLocation.Y, Bottom * 100.0));
	HeightFog->SetFogDensity(static_cast<float>(FMath::Min(10.0 * Extinction * RhoBottom / Ln2Squared * CVarSourceFogScale.GetValueOnGameThread(), 10.0)));
	HeightFog->SetVolumetricFogExtinctionScale(static_cast<float>(2.0 * Ln2Squared));
	HeightFog->SetFogHeightFalloff(static_cast<float>(FMath::Max(0.001, env::fog_height_falloff(DensityBottom, DensityTop, Bottom, TopHeight))));
	HeightFog->SetFogMaxOpacity(static_cast<float>(FMath::Clamp(Blended(TEXT("Volumetric fog 2: Final density clamp"), 1.0), 0.0, 1.0)));
	FExponentialHeightFogData Second;
	Second.FogDensity = 0.0f;
	HeightFog->SetSecondFogData(Second);
	const double RampStart = FMath::Max(0.0, Blended(TEXT("Volumetric fog 2: Ramp start"), 0.0));
	const double RampEnd = FMath::Max(RampStart + 0.1, Blended(TEXT("Volumetric fog 2: Ramp end"), 100.0));
	HeightFog->SetStartDistance(static_cast<float>(RampStart * 100.0));
	HeightFog->SetVolumetricFogStartDistance(static_cast<float>(RampStart * 100.0));
	HeightFog->SetVolumetricFogNearFadeInDistance(static_cast<float>((RampEnd - RampStart) * 100.0));
	const std::array<double, 3> Entities = BlendedColor(TEXT("Volumetric fog 2: Fog albedo color (entities)"));
	// FLinearColor(FColor) of the fog proxy decodes sRGB.
	HeightFog->SetVolumetricFogAlbedo(Linear(Entities).ToFColor(true));
	// The sun lobes: the light of the fog is the sun by day and the moon by night (the stronger visible light).
	UDirectionalLightComponent* Main = nullptr;
	for (UDirectionalLightComponent* Light : {Sun.Get(), Moon.Get()})
	{
		if (Light != nullptr && Light->IsVisible() && (Main == nullptr || Light->Intensity > Main->Intensity))
		{
			Main = Light;
		}
	}
	auto Luminance = [](double R, double G, double B) { return 0.212671 * R + 0.715160 * G + 0.072169 * B; };
	const FLinearColor Color = Main != nullptr ? Main->GetLightColor() : FLinearColor::White;
	const double ColorY = FMath::Max(Luminance(Color.R, Color.G, Color.B), 1e-6);
	const std::array<double, 3> Normal{Color.R / ColorY, Color.G / ColorY, Color.B / ColorY};
	const double ProxyY = FMath::Max(ColorY * (Main != nullptr ? Main->Intensity : 0.0), 1e-6);
	const double Blend = FMath::Clamp(Blended(TEXT("Volumetric fog 2: Blend factor for sun scattering"), 0.5), 0.0, 1.0);
	const double Mode = FMath::Clamp(Blended(TEXT("Volumetric fog 2: Blend mode for sun scattering"), 0.0), 0.0, 1.0);
	const std::array<double, 3> AtmosphereLobe = Times(BlendedColor(TEXT("Volumetric fog 2: Fog albedo color (atmosphere)")), 1.0 - Blend * Mode);
	const std::array<double, 3> RadialLobe = Times(BlendedColor(TEXT("Volumetric fog 2: Fog albedo color (sun radial)")), Blend);
	FLinearColor Directional;
	for (int32 C = 0; C < 3; ++C)
	{
		Directional.Component(C) = static_cast<float>(Normal[C] * (AtmosphereLobe[C] + RadialLobe[C]) / (FMath::Max(Entities[C], 1e-4) * ProxyY));
	}
	Directional.A = 1.0f;
	HeightFog->SetDirectionalInscatteringColor(Directional);
	if (!HeightFog->bOverrideLightColorsWithFogInscatteringColors)
	{
		HeightFog->bOverrideLightColorsWithFogInscatteringColors = true;
		HeightFog->MarkRenderStateDirty();
	}
	for (UDirectionalLightComponent* Light : {Sun.Get(), Moon.Get()})
	{
		if (Light != nullptr)
		{
			Light->SetVolumetricScatteringIntensity(static_cast<float>(InScattering / (Extinction * PI)));
		}
	}
	// The mean cosine of a Schlick lobe: 1/k + (1 - k^2) / (2 k^2) ln((1 - k) / (1 + k)) (0 at k 0).
	auto MeanCosine = [](double K)
	{
		K = FMath::Clamp(K, -0.99999, 0.99999);
		return FMath::Abs(K) < 1e-4 ? 0.0 : 1.0 / K + (1.0 - K * K) / (2.0 * K * K) * FMath::Loge((1.0 - K) / (1.0 + K));
	};
	const double WeightAtmosphere = Luminance(Normal[0] * AtmosphereLobe[0], Normal[1] * AtmosphereLobe[1], Normal[2] * AtmosphereLobe[2]);
	const double WeightRadial = Luminance(Normal[0] * RadialLobe[0], Normal[1] * RadialLobe[1], Normal[2] * RadialLobe[2]);
	const double G = (WeightAtmosphere * MeanCosine(Blended(TEXT("Volumetric fog 2: Anisotropy factor (atmosphere)"), 0.0))
		+ WeightRadial * MeanCosine(Blended(TEXT("Volumetric fog 2: Anisotropy factor (sun radial)"), 0.95))) / FMath::Max(WeightAtmosphere + WeightRadial, 1e-6);
	HeightFog->SetVolumetricFogScatteringDistribution(static_cast<float>(FMath::Clamp(G, -0.9, 0.9)));
	// The ambient: the world probe (the mean radiance of its side faces x its multipliers) x Ce / 12.566 in the fog inscattering
	// color (the analytical tail of the source; with the override also the sky term of the volume without the Lumen volume).
	if (USkyLightComponent* Ambient = SkyLight.Get())
	{
		Ambient->SetVolumetricScatteringIntensity(static_cast<float>(InScattering / Extinction));
	}
	const double Diffuse = FMath::Lerp(1.0, Blended(TEXT("Environment Probe Diffuse Multiplier"), 1.0), Data->WorldProbeTodInfluence) * Data->WorldProbeDiffuse;
	const FLinearColor ProbeMultiplier = Linear(BlendedColor(TEXT("Environment Probe Multiplier")));
	const double AmbientScale = SourceUnitLuminance * Diffuse / 12.566;
	HeightFog->SetFogInscatteringColor(FLinearColor(static_cast<float>(AmbientScale * Data->WorldProbeHorizon[0] * Entities[0] * ProbeMultiplier.R),
		static_cast<float>(AmbientScale * Data->WorldProbeHorizon[1] * Entities[1] * ProbeMultiplier.G),
		static_cast<float>(AmbientScale * Data->WorldProbeHorizon[2] * Entities[2] * ProbeMultiplier.B)));
	HeightFog->SetSkyAtmosphereAmbientContributionColorScale(FLinearColor::Black);
	HeightFog->SetFogCutoffDistance(static_cast<float>(FogCutoff));
	if (USkyAtmosphereComponent* Sky = Atmosphere.Get())
	{
		Sky->SetHeightFogContribution(0.0f);
	}
}

void USwcEnvironmentSubsystem::UpdateLocalProbes(USkyLightComponent* Ambient, const FVector& CameraLocation, double WorldIntensity, const FLinearColor& Multiplier)
{
	// The local probes (stage E5 step b; tiledshading.cfib ApplyProbe of the source): each pixel takes the probes of its tile in the
	// list order, each with its fade of the part that is left, and the world probe takes the rest. The client takes the weights at
	// the camera (one place for the whole view, as the feet rule of the time-of-day volumes): the sky light blends the world cube with
	// the cube of the local probe of the largest light (SetCubemapBlend: no new capture for a new fraction), the intensity is the sum
	// of the weighted lights, and the color is their weighted tint.
	struct FHit
	{
		int32 Index;
		double Fade;
	};
	TArray<FHit> Hits;
	// The local probes of the world (a test, swc.LocalProbes), or the probes of the menu scene in the menu mode (stage F4b of
	// docs/FRONTEND.md: always; its scene probe has a box of 5 km, the probe of the whole menu level).
	const TArray<FData::FLocalProbe>& LocalProbes = Menu.IsSet() ? Menu->Probes : Data->LocalProbes;
	if (CVarLocalProbes.GetValueOnGameThread() == 1 || Menu.IsSet())
	{
		const env::Vec3 At{CameraLocation.X / 100.0, -CameraLocation.Y / 100.0, CameraLocation.Z / 100.0};
		for (int32 Index = 0; Index < LocalProbes.Num(); ++Index)
		{
			const FData::FLocalProbe& Probe = LocalProbes[Index];
			const env::Vec3 D{At.x - Probe.Center.x, At.y - Probe.Center.y, At.z - Probe.Center.z};
			if (D.x * D.x + D.y * D.y + D.z * D.z > Probe.Radius * Probe.Radius)
			{
				continue;
			}
			auto Dot = [&D](const env::Vec3& Row) { return Row.x * D.x + Row.y * D.y + Row.z * D.z; };
			const double Fade = env::probe_fade({Dot(Probe.Rows[0]), Dot(Probe.Rows[1]), Dot(Probe.Rows[2])}, Probe.Falloff, 1.0);
			if (Fade > 0.0)
			{
				Hits.Add({Index, Fade});
			}
		}
		// The order of the list: the higher SortPriority first, then the smaller box (an inference until the light list of the source
		// is read; the world probe has priority 0 and the largest box, so it is last).
		Hits.Sort([&LocalProbes](const FHit& A, const FHit& B)
		{
			const FData::FLocalProbe& PA = LocalProbes[A.Index];
			const FData::FLocalProbe& PB = LocalProbes[B.Index];
			return PA.Priority != PB.Priority ? PA.Priority > PB.Priority : PA.Volume < PB.Volume;
		});
	}
	std::vector<double> Fades;
	for (const FHit& Hit : Hits)
	{
		Fades.push_back(Hit.Fade);
	}
	const std::vector<double> ProbeWeights = env::probe_weights(Fades);
	const double TodDiffuse = Blended(TEXT("Environment Probe Diffuse Multiplier"), 1.0);
	double Total = ProbeWeights.back() * WorldIntensity;
	double Local = 0.0;
	std::array<double, 3> Tint{Total, Total, Total};
	int32 Best = -1;
	double BestLight = 0.0;
	ProbeText = FString::Printf(TEXT(" world %.2f"), ProbeWeights.back());
	for (int32 Hit = 0; Hit < Hits.Num(); ++Hit)
	{
		const FData::FLocalProbe& Probe = LocalProbes[Hits[Hit].Index];
		const double Luminance = FMath::Max(0.2126 * Probe.Color[0] + 0.7152 * Probe.Color[1] + 0.0722 * Probe.Color[2], 1e-6);
		const double Light = ProbeWeights[Hit] * SourceUnitLuminance * Luminance * FMath::Lerp(1.0, TodDiffuse, Probe.TodInfluence)
			* CVarProbeScale.GetValueOnGameThread();
		Total += Light;
		Local += Light;
		for (int32 C = 0; C < 3; ++C)
		{
			Tint[C] += Light * Probe.Color[C] / Luminance;
		}
		if (Light > BestLight)
		{
			BestLight = Light;
			Best = Hits[Hit].Index;
		}
		ProbeText += FString::Printf(TEXT(" %s %.2f"), *Probe.Cube, ProbeWeights[Hit]);
	}
	UTextureCube* Cube = nullptr;
	if (Best >= 0)
	{
		const FString& Name = LocalProbes[Best].Cube;
		if (const TObjectPtr<UTextureCube>* Found = ProbeCubes.Find(Name))
		{
			Cube = Found->Get();
		}
		else
		{
			const FString Asset = TEXT("T_SwcProbe_") + Name;
			Cube = LoadObject<UTextureCube>(nullptr, *FString::Printf(TEXT("/SwcGameContent/Environment/Probes/%s.%s"), *Asset, *Asset), nullptr, LOAD_NoWarn);
			ProbeCubes.Add(Name, Cube);
			UE_LOG(LogSwcEnvironment, Log, TEXT("environment: the local probe cube %s%s"), *Asset, Cube != nullptr ? TEXT("") : TEXT(" is not imported (tools/import-sky.sh)"));
		}
	}
	const float Fraction = Cube != nullptr && Total > 0.0 ? static_cast<float>(Local / Total) : 0.0f;
	Ambient->SetCubemapBlend(WorldProbe.Get(), Cube, Fraction);
	ProbeBlendCube = Cube != nullptr ? LocalProbes[Best].Cube : FString();
	Ambient->SetIntensity(static_cast<float>(Total));
	const double Scale = Total > 0.0 ? 1.0 / Total : 1.0;
	Ambient->SetLightColor(Multiplier * FLinearColor(static_cast<float>(Tint[0] * Scale), static_cast<float>(Tint[1] * Scale), static_cast<float>(Tint[2] * Scale)));
}

void USwcEnvironmentSubsystem::UpdatePixelProbes(const FVector& CameraLocation, const FLinearColor& Multiplier)
{
	// The local probes of each pixel (stage E5 step 3, research/e5-local-probes.md): the probes near the camera go to the pass after
	// the base pass in the order of the light list of the source. Off (an empty list) in the menu mode, with the real-time sky
	// light (no world probe: Multiplier black), and for swc.LocalProbes 0 and 1.
	FSwcProbeShadingList List;
	const double Gain = 0.2126 * Multiplier.R + 0.7152 * Multiplier.G + 0.0722 * Multiplier.B;
	if (CVarLocalProbes.GetValueOnGameThread() != 2 || Menu.IsSet() || !Data.IsValid() || Gain <= 0.0)
	{
		SwcProbeShading::SetList(MoveTemp(List));
		return;
	}
	// The time-of-day gain of a probe: lerp(1, G x D, TODInfluence), G the luminance of "Environment Probe Multiplier" and D
	// "Environment Probe Diffuse Multiplier" (the CPU rule of the source). The source unit and swc.ProbeScale are in both the world
	// and the local light: the ratio of the pass has no need of them.
	const double TodGain = Gain * Blended(TEXT("Environment Probe Diffuse Multiplier"), 1.0);
	auto Faces = [](const std::array<double, 6>& F, double Scale, FVector3f& Positive, FVector3f& Negative)
	{
		// The source axes east, north, up are Unreal +X, -Y, +Z.
		Positive = FVector3f(static_cast<float>(F[0] * Scale), static_cast<float>(F[3] * Scale), static_cast<float>(F[4] * Scale));
		Negative = FVector3f(static_cast<float>(F[1] * Scale), static_cast<float>(F[2] * Scale), static_cast<float>(F[5] * Scale));
	};
	const double WorldScale = Data->WorldProbeDiffuse * FMath::Lerp(1.0, TodGain, Data->WorldProbeTodInfluence);
	Faces(Data->WorldProbeFaces, WorldScale, List.WorldPositive, List.WorldNegative);
	double WorldMean = 0.0;
	for (double F : Data->WorldProbeFaces)
	{
		WorldMean += F * WorldScale / 6.0;
	}
	// The nearest probes (the gap from the camera to the box sphere), then the order of the source: the higher priority first (its
	// low byte), then the smaller radius (the length of Area / 2), then the higher entity id (here the later record).
	struct FNear
	{
		int32 Index;
		double Gap;
	};
	TArray<FNear> Near;
	const env::Vec3 At{CameraLocation.X / 100.0, -CameraLocation.Y / 100.0, CameraLocation.Z / 100.0};
	const double Reach = CVarLocalProbeReach.GetValueOnGameThread();
	for (int32 Index = 0; Index < Data->LocalProbes.Num(); ++Index)
	{
		const FData::FLocalProbe& Probe = Data->LocalProbes[Index];
		const double Dx = At.x - Probe.Center.x, Dy = At.y - Probe.Center.y, Dz = At.z - Probe.Center.z;
		const double Gap = FMath::Max(0.0, FMath::Sqrt(Dx * Dx + Dy * Dy + Dz * Dz) - Probe.Radius);
		const bool bLight = Probe.Faces[0] + Probe.Faces[1] + Probe.Faces[2] + Probe.Faces[3] + Probe.Faces[4] + Probe.Faces[5] > 0.0;
		if (Gap <= Reach && bLight)
		{
			Near.Add({Index, Gap});
		}
	}
	Near.Sort([](const FNear& A, const FNear& B) { return A.Gap < B.Gap; });
	const int32 Found = Near.Num();
	Near.SetNum(FMath::Min(Near.Num(), FSwcProbeShadingList::MaxProbes));
	Near.Sort([this](const FNear& A, const FNear& B)
	{
		const FData::FLocalProbe& PA = Data->LocalProbes[A.Index];
		const FData::FLocalProbe& PB = Data->LocalProbes[B.Index];
		if ((PA.Priority & 0xFF) != (PB.Priority & 0xFF))
		{
			return (PA.Priority & 0xFF) > (PB.Priority & 0xFF);
		}
		if (FMath::Abs(PA.Radius - PB.Radius) > UE_KINDA_SMALL_NUMBER)
		{
			return PA.Radius < PB.Radius;
		}
		return PA.Order > PB.Order;
	});
	int32 Brighter = 0;
	for (const FNear& Item : Near)
	{
		const FData::FLocalProbe& Probe = Data->LocalProbes[Item.Index];
		FSwcProbeShadingProbe& Out = List.Probes.AddDefaulted_GetRef();
		Out.Center = FVector(Probe.Center.x * 100.0, -Probe.Center.y * 100.0, Probe.Center.z * 100.0);
		for (int32 Row = 0; Row < 3; ++Row)
		{
			Out.Rows[Row] = FVector3f(static_cast<float>(Probe.Rows[Row].x / 100.0), static_cast<float>(-Probe.Rows[Row].y / 100.0),
				static_cast<float>(Probe.Rows[Row].z / 100.0));
		}
		// AttenuationFalloffMax in 1/255 steps, at least 0.001 (the light setup of the source).
		Out.Falloff = static_cast<float>(FMath::Max(FMath::RoundToDouble(255.0 * Probe.Falloff) / 255.0, 0.001));
		const double Color = 0.2126 * Probe.Color[0] + 0.7152 * Probe.Color[1] + 0.0722 * Probe.Color[2];
		const double Scale = Color * FMath::Lerp(1.0, TodGain, Probe.TodInfluence);
		Faces(Probe.Faces, Scale, Out.Positive, Out.Negative);
		double Mean = 0.0;
		for (double F : Probe.Faces)
		{
			Mean += F * Scale / 6.0;
		}
		Brighter += Mean > WorldMean ? 1 : 0;
	}
	const double Now = FPlatformTime::Seconds();
	if (Now >= NextPixelProbeLog)
	{
		NextPixelProbeLog = Now + 5.0;
		const FData::FLocalProbe* First = Near.IsEmpty() ? nullptr : &Data->LocalProbes[Near[0].Index];
		UE_LOG(LogSwcEnvironment, Log, TEXT("environment: the local probes of the pixels: %d of %d within %.0f m, %d with more light than the world probe (the AO cannot add it); the first %s (priority %d, radius %.1f m)"),
			List.Probes.Num(), Found, Reach, Brighter, First != nullptr ? *First->Cube : TEXT("none"), First != nullptr ? First->Priority & 0xFF : 0,
			First != nullptr ? First->Radius : 0.0);
	}
	SwcProbeShading::SetList(MoveTemp(List));
}
