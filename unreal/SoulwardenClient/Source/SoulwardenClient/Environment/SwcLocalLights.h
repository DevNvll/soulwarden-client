#pragma once

#include "CoreMinimal.h"
#include "Environment/SwcLocalLightRules.h"
#include "Subsystems/WorldSubsystem.h"

#include <vector>

#include "SwcLocalLights.generated.h"

class ASwcCreature;
class ULocalLightComponent;
class UMaterialInterface;

/** The counts of the local lights (the exit check of stage E2: the active lights and the shadows). */
struct FSwcLocalLightStats
{
	/** The light components in the scene (the lights near the camera), by type. */
	int32 Active = 0;
	int32 Points = 0;
	int32 Spots = 0;
	int32 Rects = 0;
	/** The active lights in the draw distance and in the view cone of the camera (the lights that the frame can shade). */
	int32 InView = 0;
	/** The active lights with shadows, and those of them in the view. */
	int32 Shadows = 0;
	int32 ShadowsInView = 0;
	/** The active lights of NPC slices (bound to their creature). */
	int32 Bound = 0;
	/** The active spot lights with a cookie (a light function). */
	int32 Cookies = 0;
	/** All lights of the data (the static ones and the NPC ones). */
	int32 Loaded = 0;
};

/**
 * The local lights of the world (stage E2, docs/ENVIRONMENT.md): the point, spot (projector), and rect (area) lights of the slices
 * (SWC_CONTENT/environment/lights/REGION.json of tools/convert-environment.sh write, read on a worker thread at the start).
 *
 * - Places, axes, colors, ranges, the bulb, the field of view, the area size, and the start state come from the data. Hidden lights
 *   (Visible false: the light rigs of the mission NPC masters and helpers) are not in the data; OnInitially false stays off.
 * - The intensity (Environment/SwcLocalLightRules.h): the source attenuation of the bulb is the falloff of an Unreal sphere light
 *   with the bulb as its source radius, in candela from the light unit of the public engine, times a look scale (3 by default: the
 *   balance of the retail night pictures; -SwcLightScale=, the console variable swc.LightScale).
 * - Streaming: the lights near the camera are components of one actor (a grid of 64 m cells; a light comes in its draw distance and
 *   goes 30 m after it; the Unreal fade at the draw distance).
 * - Shadows: the source choice (CastShadowsSpec not Never); -SwcLightShadows=none|source|all, the console variable swc.LightShadows.
 * - Cookies: a spot light with a projector texture gets its light function material instance (/SwcGameContent/Environment/Cookies
 *   of tools/import-cookies.sh; SWC_CONTENT/environment/cookies.json); Unreal maps it over the outer cone (ProjectorFOV / 2).
 * - The lights of an NPC slice follow their creature (the same slice path, near the static place of the slice root): a light with
 *   no creature stays off. The camp lights are static props of the level data (always on).
 * - No switch by the hour: the data has none (TODInfluence alone is not a switch; the retail pictures show the lamps on at noon and
 *   at night). The light styles (AnimIndex) are in the client code, not in the data: the lights are steady.
 *
 * Development aids: -SwcNoLocalLights; -SwcLightScale=FACTOR (the console variable swc.LightScale); swc.LocalLights 0 hides the
 * lights (a compare of the cost); -SwcLightShadows=MODE; -SwcNoCookies; -SwcLightProbe logs the counts each 1 s (else each 30 s).
 */
UCLASS()
class SOULWARDENCLIENT_API USwcLocalLightSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	/** @return The counts of the lights at the camera now. */
	FSwcLocalLightStats GetStats() const;

	/** The data of the lights (the result of the worker thread). */
	struct FData
	{
		std::vector<swc::environment::LocalLight> Lights;
		swc::environment::LightGrid Grid{64.0};
		/** The indexes of the lights of NPC slices (not in the grid). */
		TArray<int32> NpcLights;
		/** The game path of each cookie texture to its material instance (object path). */
		TMap<FString, FString> CookieMaterials;
		FString Report;
		bool bOk = false;
	};

private:
	/** The values of swc.LightShadows. */
	enum class EShadows : uint8 { None = 0, Source = 1, All = 2 };

	void StartLoad();
	void UpdateStatic(const FVector& Camera);
	void UpdateBound();
	ULocalLightComponent* MakeLight(int32 Index, USceneComponent* Parent, const FTransform& Relative);
	bool CastsShadow(const swc::environment::LocalLight& Light) const;

	TSharedPtr<FData, ESPMode::ThreadSafe> Data;
	bool bLoading = false;
	bool bDisabled = false;
	bool bProbe = false;
	bool bCookies = true;
	/** The look scale applied to the active lights (swc.LightScale), and the visibility (swc.LocalLights). */
	double Scale = 3.0;
	bool bVisible = true;
	EShadows Shadows = EShadows::Source;
	TWeakObjectPtr<AActor> Actor;
	/** The light components by the index of the light. */
	TMap<int32, TWeakObjectPtr<ULocalLightComponent>> Active;
	/** The loaded cookie material instances by the game path of the texture (null: it does not load). */
	UPROPERTY()
	TMap<FString, TObjectPtr<UMaterialInterface>> CookieCache;
	/** The creature of each bound NPC light. */
	TMap<int32, TWeakObjectPtr<ASwcCreature>> Owners;
	double NextUpdate = 0.0;
	double NextBound = 0.0;
	double NextLog = 0.0;
	int32 Created = 0;
	int32 Removed = 0;
};
