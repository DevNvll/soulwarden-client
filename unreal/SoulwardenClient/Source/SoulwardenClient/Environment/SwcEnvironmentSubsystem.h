#pragma once

#include "CoreMinimal.h"
#include "Environment/SwcEnvironmentRules.h"
#include "Environment/SwcSnow.h"
#include "Subsystems/WorldSubsystem.h"

#include "SwcEnvironmentSubsystem.generated.h"

class UDirectionalLightComponent;
class UExponentialHeightFogComponent;
class UMaterialInstanceDynamic;
class UPostProcessComponent;
class USceneComponent;
class USkyAtmosphereComponent;
class USkyLightComponent;
class UStaticMeshComponent;
class UVolumetricCloudComponent;

/**
 * The runtime environment of the world (stage E1, docs/ENVIRONMENT.md): the sun, the moon, and the sky of the world clock. The decision
 * of the user (2026-10-18): the content follows the retail client (the hour of the backend clock, the sun and moon path, the colors
 * of the data where they look good); the rendering keeps the Unreal way where it looks better (the physical sky, Lumen, its shadows,
 * the auto exposure).
 *
 * - The data: SWC_CONTENT/environment (tools/convert-environment.sh write: mission.json, presets.json, volumes.json), read on a worker
 *   thread at the start.
 * - The preset: the time-of-day volume at the camera with the highest priority (its hour override, if any), else the base TimeOfDay
 *   of the main-world mission. The blend of volumes is stage E3.
 * - The hour: the world clock (USwcWorldTimeSubsystem); the time of a curve is hour / 24 (float32 steps).
 * - The sun: a movable directional light (atmosphere light 0), always on, on the sun path of the mission (SunRotation, Longitude),
 *   with the noon "Sun intensity" (lux) and "Sun color" of the preset; the SkyAtmosphere makes the sunrise, the sunset, and the
 *   twilight (its transmittance).
 * - The moon: a movable directional light (atmosphere light 1) at the fixed direction of the Moon element, with the night "Sun
 *   intensity" and "Sun color" of the hour; its weight is the larger of the fade of the source at the visual dawn and dusk of the
 *   record and a fade with the sun below the horizon (full at 4 degrees below it): the moon comes when the physical sun goes.
 * - A SkyAtmosphere (Unreal defaults; by night its sky luminance is scaled by a factor that the moon fades in: a look choice), and a
 *   SkyLight with a real-time capture of the sky at intensity 1.
 * - The exposure and the color grade (stage E5; the decision of the user, 2026-10-19: the color result copies retail): the Unreal auto
 *   exposure with the EV bounds and the auto compensation of the data (MeterAlbedo in the source file), and M_SwcGrade in place of
 *   the Unreal tone mapper: the film curve, the saturation, the color balance, and the photo filter of the data.
 * Stage E3 (the regional sky and fog; Environment/SwcEnvironmentSky.cpp):
 * - The blend: the base preset of the mission, then the time-of-day volumes at the camera by priority, each with a weight from its
 *   blend distance (the depth inside its shape) that moves in its blend time; each variable is the blend of the layers, all at one
 *   hour: the world hour moved to the hour override of each volume by its weight (as the retail client blends the hour).
 * - The fog: an exponential height fog of Unreal with the data as inputs (the two fog sets of the preset: the legacy layers and the
 *   second set of the volumetric fog), its color from the SkyAtmosphere of Unreal (the Unreal way: the fog colors and the sun glow
 *   of the data burned the views; swc.FogColorMode), and the volumetric fog of Unreal with the albedo and the anisotropy of the data.
 * - The sky: a sky sphere around the camera with M_SwcSky (tools/import-sky.sh): the SkyAtmosphere of Unreal, the stars of the
 *   engine catalog turning with the sun, the moon disc and its coronas of the data; by night the sky has the hue of the night horizon
 *   color of the data.
 * - The clouds: the volumetric clouds of Unreal (the engine material) with the cover of the sky slice of the preset (its cloud masks)
 *   and the cloud shading color of the data.
 * - The fog volumes of the data: local fog volumes of Unreal (a sphere over the box; a flat box is a thin height layer in it).
 * - The rain: a post-process material (M_SwcPrecipitation: the rain layer texture of the source falling over the screen) with the
 *   amount of the rain volumes of the data at the camera. The snow: flakes in the world (FSwcSnow, AST-257), not on the screen.
 * The level actors of the world track (its sun, sky atmosphere, sky light, height fog, and clouds) are turned off while this
 * environment runs; its ocean stays.
 *
 * The menu mode (stage F4 of docs/FRONTEND.md): the front end has no world clock. While the menu sets SetMenuScene (the token of PLAY
 * clears it), the environment runs with no record of the world clock: the preset of the TOD entity of the menu scene at its fixed hour is
 * the one layer (no time-of-day volumes of the world), with the sun path, the moon, and the sky slice of the menu level. All rules of
 * E1 to E5 are the ones of the world.
 *
 * Development aids: -SwcEnvironmentProbe logs the state each 1 s (else each 60 s and at each change of the preset); -SwcNoEnvironment
 * keeps the environment of the level; -SwcNightSky=FACTOR and -SwcNightExposure=EV change the two look values of the night;
 * -SwcAmbientOnly turns the sun and the moon off (a check of the ambient); the console variables swc.Fog, swc.FogColorMode,
 * swc.FogLayers, swc.FogDensityScale, swc.FogDensityScale2, swc.VolumetricFog, swc.Clouds, swc.CloudCover, swc.CloudBias, swc.StarScale,
 * swc.MoonScale, swc.MoonSize, swc.NightSkyTint, swc.RainScale, swc.DropLight, swc.LocalFogScale change the look values of E3 at run
 * time (the variants of the matched views).
 */
UCLASS()
class SOULWARDENCLIENT_API USwcEnvironmentSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	/** @return The night factor (0 by day, 1 by night): the weight of the moon. A read value for the emissive of the fixtures (the
	 *  local lights of stage E2 do not switch by the hour: the data has no switch). */
	double GetNightFactor() const { return NightFactor; }


	/** The data of the environment (the result of the worker thread). */
	struct FData
	{
		double SunRotation = 215.0;
		double Longitude = 130.0;
		double MoonLatitude = 240.0;
		double MoonLongitude = 35.0;
		double MoonSize = 0.8;
		/** Each preset: its variables by name (the first variable of a name). */
		TMap<FString, TMap<FString, swc::environment::Curve>> Presets;
		std::vector<swc::environment::Volume> Volumes;
		/** The cloud cover of each sky slice (sky/sky.json of tools/convert-environment.sh sky), and the sky of the mission. */
		TMap<FString, double> SkyCovers;
		FString MissionSky;
		/** The world probe of the outdoor ambient (stage E5; probes.json of tools/convert-environment.sh probes): the name of its cube
		 *  texture (T_SwcProbe_NAME), its DiffuseMultiplier, and its TODInfluence (the part of the time-of-day probe multipliers). */
		FString WorldProbe;
		double WorldProbeDiffuse = 1.0;
		double WorldProbeTodInfluence = 1.0;
		/** The SpecularMultiplier of the world probe (the specular term of the probes, swc.ProbeSpecular 1). */
		double WorldProbeSpecular = 1.0;
		/** The mean radiance of the four side faces of the world probe (the units of the source): the ambient of the far fog. */
		std::array<double, 3> WorldProbeHorizon{0.3, 0.33, 0.35};
		/** The ambient cube of the world probe (probes.json "diffuseMeans": the Rec. 709 luminance of the mean of each face of its
		 *  diffuse cube, in the order +east, -east, +north, -north, +up, -up): the light of the pixels with no local probe. */
		std::array<double, 6> WorldProbeFaces{1.0, 1.0, 1.0, 1.0, 1.0, 1.0};
		/** A local probe of the source (stage E5 step b; probes.json "local"): its box (the center in the units of the source, m,
		 *  and the rows that give the box space -1 to 1: the unit axes of the entity over the half sizes of Area X,Y,Z), the order
		 *  (SortPriority, then the smaller box first), the fade at its edge (AttenuationFalloffMax), its color (COLOR x
		 *  DiffuseMultiplier), its TODInfluence, and the name of its cube texture (T_SwcProbe_NAME). */
		struct FLocalProbe
		{
			swc::environment::Vec3 Center;
			std::array<swc::environment::Vec3, 3> Rows;
			double Radius = 0.0;
			double Volume = 0.0;
			int32 Priority = 0;
			double Falloff = 1.0;
			std::array<double, 3> Color{1.0, 1.0, 1.0};
			double TodInfluence = 1.0;
			FString Cube;
			/** The specular term (swc.ProbeSpecular 1): COLOR x DiffuseMultiplier x SpecMultiplier, the box projection of the reflection and the size of
			 *  its proxy box on the world axes (BoxWidth, BoxLength, BoxHeight of the record, m; the source: x Length, y Width, z Height). */
			std::array<double, 3> SpecColor{1.0, 1.0, 1.0};
			bool bBoxProject = false;
			std::array<double, 3> BoxSize{1.0, 1.0, 1.0};
			/** The ambient cube of its diffuse cube (as WorldProbeFaces; all 0 when the cube has no means: no light of its own). */
			std::array<double, 6> Faces{0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
			/** The place of the record in probes.json: the last key of the order (a stand-in for the entity id of the source). */
			int32 Order = 0;
		};
		TArray<FLocalProbe> LocalProbes;
		/** A rain or snow volume (stage E3): the full amount within the inner radius (m, horizontal), 0 at the outer radius, within
		 *  the half height about the center (a capsule or a cylinder; no shape: no limit). */
		struct FPrecipitation
		{
			swc::environment::Vec3 Center;
			double Inner = 0.0;
			double Outer = 0.0;
			double HalfHeight = 1e9;
			double Amount = 0.0;
			/** The rain drops of the data (the scene rain of the source): RainDropsAmount, RainDropsLighting, RainDropsSpeed. */
			double DropsAmount = 1.0;
			double DropsLighting = 1.0;
			double DropsSpeed = 1.0;
			bool bSnow = false;
			FString Name;
		};
		TArray<FPrecipitation> Precipitation;
		/** The Snow volumes (AST-257; research/snow.md section 1). */
		TArray<FSwcSnowVolume> Snow;
		/** A fog volume of the data (stage E3): the box of its entity (m), its color (COLOR), and its density (GlobalDensity). */
		struct FFogVolume
		{
			swc::environment::Vec3 Center;
			swc::environment::Vec3 Size;
			std::array<double, 3> Color{1.0, 1.0, 1.0};
			double Density = 0.0;
			FString Name;
		};
		TArray<FFogVolume> FogVolumes;
		FString Report;
		bool bOk = false;
	};

	/** Reads a local probe in the record form of probes.json "local" (also the probes of the menu scene) with the name of its cube
	 *  picture (T_SwcProbe_NAME). @return False for a record with no place, area, or rows. */
	static bool ReadLocalProbe(const TSharedPtr<class FJsonObject>& Probe, const FString& Picture, FData::FLocalProbe& Out);
	/** Reads the ambient cube of a cube record of probes.json ("diffuseMeans": the luminance of each face mean, in the order of
	 *  WorldProbeFaces). @return False when the record has no means. */
	static bool ReadCubeFaces(const TSharedPtr<class FJsonObject>& Cube, std::array<double, 6>& Out);

	/** The environment of the menu scene (the scene record of tools/convert-frontend.sh scene: "environment"). */
	struct FMenuScene
	{
		/** The preset of the TOD entity (a key of presets.json) and its fixed hour (the hour override). */
		FString Preset;
		double Hour = 12.0;
		/** The sky slice of the TOD entity (its cloud cover; empty: no cloud dome). */
		FString Sky;
		/** The Lighting and Moon elements of the mission of the menu level. */
		double SunRotation = 215.0;
		double Longitude = 130.0;
		double MoonLatitude = 240.0;
		double MoonLongitude = 35.0;
		double MoonSize = 0.8;
		/** The dawn and dusk fields of the Lighting element of the menu mission (minutes; the dusk with the 12-hour offset of the
		 *  engine): the edges of the sun path in the menu (the night delay); a record with no fields keeps the default edges. */
		bool bMissionEdges = false;
		double DawnTime = 360.0;
		double DawnDuration = 0.0;
		double DuskTime = 420.0;
		double DuskDuration = 0.0;
		/** The environment probes of the menu scene, in place of the local probes of the world (stage F4b). */
		TArray<FData::FLocalProbe> Probes;
		/** The factor of the ambient lights of the scene on its ambient (RGB; stage Q3: the creation level; 1: none). */
		std::array<double, 3> AmbientScale{1.0, 1.0, 1.0};
		/** The scene has ambient lights: the fog takes the ambient of its probes (MenuFogAmbient), not the world horizon. */
		bool bAmbientLights = false;
	};
	/** Sets the menu mode: the environment runs with no record of the world clock, with the menu scene. */
	void SetMenuScene(const FMenuScene& Scene);
	/** Clears the menu mode: the environment waits for the world clock again (it keeps its last state until the first record). */
	void ClearMenuScene();
	bool IsMenuScene() const { return Menu.IsSet(); }
	/** @return B of the particle effects: the evaluated "Particle Brightness Multiplier" of the scene preset with the volume blend and
	 *  the clock (particle-rules2.md a; the later of two entries with that name wins, as the file order of the source). */
	double ParticleBrightness() const { return Blended(TEXT("Particle Brightness Multiplier"), 1.0); }
	/** The source sun of the hour on the ground (swc.SunSource 1): "Sun color" / its Rec. 709 luminance x "Sun intensity" (lux), the
	 *  one light of the source (the moon keys at night); A = 1 when set, 0 with swc.SunSource 0 (then a user takes the Unreal light). */
	FLinearColor GetSourceSunLight() const
	{
		return SourceSunLight;
	}
	/** @return The light of the hour: the sun, or the moon at night (the brighter of the two directional lights of the environment). */
	const UDirectionalLightComponent* GetSkyLightSource() const;
	/** CloudSun of the source (CLOUDSHADING_SUNCOLOR, research/e5-distance-clouds.md "Color inputs and units"), in source units:
	 *  lerp("Cloud shading: Sun light multiplier" x the main light, "Sun custom color" x its multiplier, its influence); the main light
	 *  = Sun color / Y x Sun intensity / (10,000 pi) (the source sun of the hour), else the light of the hour (color x intensity / (10,000 pi)). */
	FLinearColor GetCloudSunSource() const;
	/** The exposure rule of the source at the place: "EV Min", "EV Max", and "EV Auto compensation" of the blend (the meter probe). */
	FVector GetExposureRule() const
	{
		return FVector(Blended(TEXT("EV Min"), 5.0), Blended(TEXT("EV Max"), 17.0), Blended(TEXT("EV Auto compensation"), 1.5));
	}

private:
	void StartLoad();
	bool CreateEnvironment();
	void TurnOffLevelEnvironment();
	double Value(const TMap<FString, swc::environment::Curve>& Preset, const TCHAR* Name, double Time, double Default) const;
	std::array<double, 3> Color(const TMap<FString, swc::environment::Curve>& Preset, const TCHAR* Name, double Time) const;

	/** One preset of the blend (stage E3): the base preset of the mission, then the time-of-day volumes at the camera by priority. */
	struct FLayer
	{
		const TMap<FString, swc::environment::Curve>* Preset = nullptr;
		FString Name;
		int32 Volume = -1;
		double Hour = 0.0;
		double Weight = 1.0;
		/** The sky slice of the layer (the slice of its volume, or of the menu scene; empty: none). */
		FString Sky;
	};
	/** The sun path and the moon of the mission: the world mission, or the menu level in the menu mode. */
	struct FCelestial
	{
		double SunRotation = 215.0;
		double Longitude = 130.0;
		double MoonLatitude = 240.0;
		double MoonLongitude = 35.0;
		double MoonSize = 0.8;
	};
	FCelestial Celestial() const;
	TOptional<FMenuScene> Menu;
	/** Updates the weights of the volumes at a place (engine m: the feet of the player) and the layers. */
	void UpdateBlend(const swc::environment::Vec3& Place, double WorldHour, double DeltaSeconds);
	/** @return A variable of the blend at the hour of each layer, or at a fixed hour (>= 0). */
	double Blended(const TCHAR* Name, double Default, double FixedHour = -1.0) const;
	std::array<double, 3> BlendedColor(const TCHAR* Name, double FixedHour = -1.0) const;
	void CreateSkyAndFog(AActor* Owner, USceneComponent* Root);
	/** Updates the fog, the sky, the clouds, and the precipitation; MainLux is the illuminance of the sun or the moon on the ground. */
	void UpdateSkyAndFog(double Hour, const FVector& CameraLocation, double MoonWeight, double MainLux);

	TSharedPtr<FData, ESPMode::ThreadSafe> Data;
	bool bLoading = false;
	bool bDisabled = false;
	bool bProbe = false;
	/** -SwcAmbientOnly: no sun and no moon (a development check of the ambient). */
	bool bAmbientOnly = false;
	TWeakObjectPtr<AActor> Actor;
	TWeakObjectPtr<UDirectionalLightComponent> Sun;
	TWeakObjectPtr<UDirectionalLightComponent> Moon;
	/** swc.SunShadowsOff: the saved shadow paths of the sun while the test turns them off (env::ShadowSwitch). */
	swc::environment::ShadowSwitch SunShadows;
	TWeakObjectPtr<USkyAtmosphereComponent> Atmosphere;
	TWeakObjectPtr<USkyLightComponent> SkyLight;
	TWeakObjectPtr<UPostProcessComponent> Exposure;
	TWeakObjectPtr<UExponentialHeightFogComponent> Fog;
	TWeakObjectPtr<UVolumetricCloudComponent> Clouds;
	TWeakObjectPtr<UStaticMeshComponent> SkySphere;
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> SkyMaterial;
	/** The sky of the source (stage E5 step 2b): the dome of the upper half of the sky (64 azimuths x 32 zenith angles), the Mie and
	 *  the Rayleigh part (env::source_sky), made on a worker thread when the sun or the values change. */
	UPROPERTY(Transient)
	TObjectPtr<class UTexture2D> SourceMie;
	UPROPERTY(Transient)
	TObjectPtr<class UTexture2D> SourceRayleigh;
	/** The values of the last dome (the sun direction, the intensity, Kr, Km, g, the wavelengths) and the dome work in progress. */
	TArray<double> SourceSkyKey;
	TSharedPtr<struct FSwcSourceSkyJob, ESPMode::ThreadSafe> SourceSkyJob;
	void UpdateSourceSky(double Hour, double MoonWeight);
	/** The fog of the source (stage E5 step 3): the second fog set of the data on the height fog and the volumetric fog of Unreal. */
	void UpdateSourceFog(class UExponentialHeightFogComponent* HeightFog, const FVector& CameraLocation, double Global, double Bottom, double Top,
		double BottomDensity, double TopDensity);
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> CloudMaterial;
	TWeakObjectPtr<UPostProcessComponent> PrecipitationPost;
	/** The local fog volumes of Unreal for the fog volumes of the data (by index), and the density scale that they have. */
	TArray<TWeakObjectPtr<class ULocalFogVolumeComponent>> LocalFogs;
	double LocalFogScaleApplied = -1.0;
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> PrecipitationMaterial;
	/** The falling snow in the world (AST-257): the flakes of the Snow volumes and the roof map. */
	TUniquePtr<FSwcSnow> SnowFall;
	/** The default cover of the cloud material (the cover of the Everfall sky maps to it). */
	float DefaultCloudCover = 0.5f;
	TArray<FLayer> Layers;
	/** The weight of each time-of-day volume in the blend (by its index). */
	TMap<int32, double> Weights;
	/** The hour of the blend: the world hour moved to the hour overrides of the volumes by their weights. */
	double EffectiveHour = 12.0;
	FString LayerText;
	FString SkyText;
	/** The layer text of the last log line. */
	FString PresetName;
	double NextLog = 0.0;
	double NextRecapture = 0.0;
	/** The two look values of the night: the sky luminance factor (-SwcNightSky=FACTOR) and the compensation of the exposure
	 *  (-SwcNightExposure=EV), both faded in with the moon. */
	double NightSkyFactor = 0.2;
	/** The night compensation of E1 (a look value): 0 since the decision of the user to copy the retail colors (the exposure of the
	 *  source sets the night; -SwcNightExposure=EV for a compare). */
	double NightExposure = 0.0;
	/** The bias curve of the auto compensation of the source, and the "EV Auto compensation" that it has. */
	UPROPERTY()
	TObjectPtr<class UCurveFloat> ExposureCurve;
	/** The rule of the curve: EV Min, EV Max, the auto compensation, and the meter scale (0: the luminance meter). */
	FVector4 CurveRule = FVector4(0.0, 0.0, 0.0, -1.0);
	/** The color grade of the source (M_SwcGrade: the tone mapper of the source in place of the Unreal one). */
	TWeakObjectPtr<UPostProcessComponent> GradePost;
	/** The cube of the world probe (stage E5): the sky light shows it with the multipliers of the data (swc.SkyLightSource 1). */
	UPROPERTY(Transient)
	TObjectPtr<class UTextureCube> WorldProbe;
	int32 SkyLightMode = -1;
	/** The bias (EV) that moves the target of the Unreal meter to the source meter of the frame (swc.SourceMeter 2), and the serial of
	 *  the last measure of the meter probe. */
	double MeterCorrection = 0.0;
	uint32 MeterSerial = 0;
	double NextMeterLog = 0.0;
	/** The last difference of the applied EV and the source EV, and the count of measures in a row within 0.05 EV. */
	double MeterGap = 0.0;
	int32 MeterSettled = 0;
	/** The ground transmittance of the atmosphere toward the sun of the last frame (swc.SunSource 1; for the environment log line). */
	FLinearColor SunTransmittance = FLinearColor::White;
	FLinearColor SourceSunLight = FLinearColor::Transparent;
	/** The hour of the sun path of the last frame (the night delay of the source: env::sun_path_hour), for the sky. */
	double SunPathHour = 12.0;
	/** The direction toward the sun of the light (swc.SunStep: it changes only in steps) and if it has a value. */
	FVector SunTowardApplied = FVector::ZeroVector;
	/** The turns of the sun light since the last environment log line, and the time of that line (swc.SunStep). */
	int32 SunTurns = 0;
	double SunTurnTime = -1.0;
	/** The bloom mix of the source (swc.SourceBloom 1): w of exposure x ((1 - w) x scene + w x blur); 0 when off. */
	double BloomMix = 0.0;
	/** The cubes of the local probes (stage E5 step b), loaded at their first use; the cube of the blend and its fraction. */
	UPROPERTY(Transient)
	TMap<FString, TObjectPtr<class UTextureCube>> ProbeCubes;
	/** The cubes of the specular term of the probes (swc.ProbeSpecular 1; T_SwcProbeSpec_NAME, the pre-filtered mips of the source),
	 *  loaded at their first use (null: not imported). */
	UPROPERTY(Transient)
	TMap<FString, TObjectPtr<class UTextureCube>> ProbeSpecCubes;
	class UTextureCube* ProbeSpecCube(const FString& Name);
	/** The environment BRDF table of the source (T_SwcEnvironmentBrdf), loaded at the first use of the specular term. */
	UPROPERTY(Transient)
	TObjectPtr<class UTexture2D> BrdfTable;
	bool bBrdfTableTried = false;
	/** The Unreal reflections that swc.ProbeSpecular 1 turns off (Lumen reflections, the reflection environment with the sky light
	 *  specular, the screen-space reflections) and their values before (-1: not changed). */
	void ApplyProbeSpecular(bool bOn);
	int32 ProbeSpecularApplied = -1;
	std::array<int32, 3> ReflectionsBefore{1, 1, 3};
	FString ProbeBlendCube;
	/** The menu mode: the ambient radiance at the camera (source units, luminance) of the probes that light it (the world part and
	 *  the local probes with their weights, their color, and the side faces of their cube) x the factor of the ambient lights of the
	 *  scene: the ambient of the fog in place of the horizon of the world probe (stage Q3: the closed ship hold). < 0: not set. */
	double MenuFogAmbient = -1.0;
	/** The local probes at the camera for the probe log line. */
	FString ProbeText;
	/** The local probes of each pixel (swc.LocalProbes 2): the probes near the camera, in the order of the source, for the pass
	 *  after the base pass (Environment/SwcProbeShading.cpp). */
	void UpdatePixelProbes(const FVector& CameraLocation, const FLinearColor& Multiplier);
	double NextPixelProbeLog = 0.0;
	void UpdateLocalProbes(class USkyLightComponent* Ambient, const FVector& CameraLocation, double WorldIntensity, const FLinearColor& Multiplier);
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> GradeMaterial;
	/** The sun rays of the source (AST-232, docs/ENVIRONMENT.md "The sun rays"): the TOD keys, the gates, the tint of the grade, and the
	 *  half-size target of the passes (Environment/SwcGodRays). */
	void UpdateGodRays(double MoonWeight);
	UPROPERTY()
	TObjectPtr<class UTextureRenderTarget2D> RayTarget;
	double NextRayLog = 0.0;
	int32 LastRayState = -1;
	double NightFactor = 0.0;
};
