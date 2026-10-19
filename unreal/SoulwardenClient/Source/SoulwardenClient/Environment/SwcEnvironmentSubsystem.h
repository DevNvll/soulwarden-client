#pragma once

#include "CoreMinimal.h"
#include "Environment/SwcEnvironmentRules.h"
#include "Subsystems/WorldSubsystem.h"

#include "SwcEnvironmentSubsystem.generated.h"

class UDirectionalLightComponent;
class UPostProcessComponent;
class USkyAtmosphereComponent;
class USkyLightComponent;

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
 * - The exposure: an unbound post process over the fixed exposure of the level: the Unreal auto exposure with the bounds EV100 3 to
 *   16 and a compensation of -2.5 by night (a night look). Stage E5 refines it.
 * The level actors of the world track (its sun, sky atmosphere, and sky light) are turned off while this environment runs; its fog
 * and its ocean stay.
 *
 * Development aids: -SwcEnvironmentProbe logs the state each 1 s (else each 60 s and at each change of the preset); -SwcNoEnvironment
 * keeps the environment of the level; -SwcNightSky=FACTOR and -SwcNightExposure=EV change the two look values of the night;
 * -SwcAmbientOnly turns the sun and the moon off (a check of the ambient).
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
		FString Report;
		bool bOk = false;
	};

private:
	void StartLoad();
	bool CreateEnvironment();
	void TurnOffLevelEnvironment();
	double Value(const TMap<FString, swc::environment::Curve>& Preset, const TCHAR* Name, double Time, double Default) const;
	std::array<double, 3> Color(const TMap<FString, swc::environment::Curve>& Preset, const TCHAR* Name, double Time) const;

	TSharedPtr<FData, ESPMode::ThreadSafe> Data;
	bool bLoading = false;
	bool bDisabled = false;
	bool bProbe = false;
	/** -SwcAmbientOnly: no sun and no moon (a development check of the ambient). */
	bool bAmbientOnly = false;
	TWeakObjectPtr<AActor> Actor;
	TWeakObjectPtr<UDirectionalLightComponent> Sun;
	TWeakObjectPtr<UDirectionalLightComponent> Moon;
	TWeakObjectPtr<USkyAtmosphereComponent> Atmosphere;
	TWeakObjectPtr<USkyLightComponent> SkyLight;
	TWeakObjectPtr<UPostProcessComponent> Exposure;
	FString PresetName;
	int32 VolumeIndex = -2;
	double NextLog = 0.0;
	double NextRecapture = 0.0;
	/** The two look values of the night: the sky luminance factor (-SwcNightSky=FACTOR) and the compensation of the exposure
	 *  (-SwcNightExposure=EV), both faded in with the moon. */
	double NightSkyFactor = 0.2;
	double NightExposure = -2.5;
	double NightFactor = 0.0;
};
