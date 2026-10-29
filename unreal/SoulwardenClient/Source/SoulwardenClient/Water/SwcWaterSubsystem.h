#pragma once

#include <optional>

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Water/SwcWaterData.h"

#include "SwcWaterSubsystem.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMesh;
class UStaticMeshComponent;

DECLARE_LOG_CATEGORY_EXTERN(LogSwcWater, Log, All);

/**
 * The water of the world (stage WF5, docs/WATER.md): the surfaces of the water volumes and the rivers of the source (the data of
 * tools/convert-water.sh, SWC_CONTENT/water), the ocean at the ocean level with its local overrides, and the water queries of the
 * movement (the water quadtree of each region). The content follows the source. Two render paths:
 * - the source path (-SwcWaterMode=Source, swc.WaterMode 0): the source water shader (the translucent base material
 *   /SwcGameContent/Water/M_SwcWaterSource of tools/import-water.sh) with one dynamic instance for each surface. The surfaces build near
 *   the view (static meshes made at run time; a few each frame) and stay. The ocean is a flat grid at the ocean level that follows the
 *   view. With no base material the surfaces do not show, and the queries still work;
 * - the Unreal water (the default, swc.WaterMode 1; docs/WATER.md "The Unreal water", Water/SwcWaterUnreal.cpp): the bodies of the Unreal
 *   Water plugin from the specs of tools/convert-water.sh bodies. With no specs file the source path draws.
 * The queries, the loading gate, and the shore foam decals are the same in both paths.
 */
UCLASS()
class SOULWARDENCLIENT_API USwcWaterSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Tick(float DeltaTime) override;
	virtual void Deinitialize() override;
	virtual TStatId GetStatId() const override;

	/** @return The water at a point (engine m): the quadtree, or the ocean where it has no water (swc::water::with_ocean). */
	std::optional<swc::water::Sample> GetWaterAt(double X, double Y) const;
	/** @return The depth class of the feet (engine m; the rule of the retail client); OutDepth gets the depth (m). */
	swc::water::DepthClass GetDepthClass(double X, double Y, double FeetZ, float* OutDepth = nullptr) const;
	/** @return True when the data is loaded. */
	bool IsLoaded() const { return bLoaded; }
	/** @return True when the water draws with the Unreal Water plugin (-SwcWaterMode=Unreal). */
	bool IsUnrealWater() const { return bUnrealMode; }

private:
	/** The values of a water material of the source (.mtl: the shader, the public parameters). */
	struct FMaterialRecord
	{
		FString Path;
		FString Shader;
		TMap<FName, float> Params;
	};
	/** A surface of the data and its component when it is built. */
	struct FBodyEntry
	{
		swc::water::Body Body;
		FVector Center = FVector::ZeroVector;   // Unreal cm
		double Radius = 0.0;                    // cm
		bool bBuilt = false;
		/** The region of the surface and its index in REGION.water (the specs of the Unreal water use the same order). */
		FString Region;
		int32 RegionIndex = INDEX_NONE;
	};
	/** A body of the Unreal water (unreal-bodies.json): its spec, its surface (an index of Bodies), and its actor when it is spawned. */
	enum class EUnrealBody : uint8
	{
		River,
		Lake,
		Custom
	};
	struct FUnrealBody
	{
		FString Region;
		int32 RegionIndex = INDEX_NONE;
		int32 Surface = INDEX_NONE;
		EUnrealBody Kind = EUnrealBody::Custom;
		TSharedPtr<class FJsonObject> Spec;
		FVector Center = FVector::ZeroVector;   // Unreal cm
		double Radius = 0.0;                    // cm
		bool bSpawned = false;
	};
	/** A local ocean override (WaterOceanComponent): its shape in the frame of its entity and its settings. */
	struct FOceanOverride
	{
		FString Entity;
		/** The inverse of the rotation and scale of the entity matrix (rows) and its translation (engine m). */
		double Inverse[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
		FVector3d Translation = FVector3d::ZeroVector;
		FString Shape;
		double Radius = 0.0;
		double Height = 0.0;
		FVector3d Dimensions = FVector3d::ZeroVector;
		int32 Material = INDEX_NONE;
		double Transition = 0.0;
		TMap<FName, float> Values;   // the parameters of the ocean material instance
	};

	/** A decal sheet of a shore foam effect (water/shore.json, the particle library env_island.xml). */
	struct FShoreSheet
	{
		float Delay = 0.0f;
		float Life = 5.0f;
		FVector3f Offset = FVector3f::ZeroVector;        // LocalSpawnPosOffset (m, the frame of the emitter)
		FVector3f RandomOffset = FVector3f::ZeroVector;  // LocalSpawnPosRandomOffset
		float AngleZ = 0.0f;                              // LocalInitAngles z (degrees)
		float Thickness = 0.5f;
		swc::water::Curve SizeX;
		swc::water::Curve SizeY;
		swc::water::Curve Alpha;
		TArray<FVector4f> Color;                          // t, r, g, b
		TArray<FVector2f> Ranges;                         // the view distances (m) in which the sheet shows
	};
	/** A placed shore foam emitter: its effect and its frame (engine m, the rows of the matrix: x, y, z axes and the place). */
	struct FShoreEmitter
	{
		int32 Effect = INDEX_NONE;
		FVector3d Place = FVector3d::ZeroVector;
		FVector3d AxisX = FVector3d(1, 0, 0);
		FVector3d AxisY = FVector3d(0, 1, 0);
		FVector3d AxisZ = FVector3d(0, 0, 1);
		int32 FirstDecal = INDEX_NONE;   // its decals in ShoreDecals (one for each sheet), when they are made
	};

	void Load();
	void LoadShore(const FString& Folder);
	void UpdateShore(const FVector& View);
	void UpdateBodies(const FVector& View);
	void UpdateOcean(const FVector& View, float DeltaTime);
	/** The ocean of the place of the view (the first local override whose shape has the view, else the mission ocean) and the blend of
	 *  its values in its transition time: OceanNow (both render paths). */
	void UpdateOceanState(const FVector& View, float DeltaTime);
	void HideWorldOcean();
	UStaticMesh* BuildMesh(const swc::water::Body& Body, const FVector& Center, UMaterialInterface* Material) const;
	UMaterialInstanceDynamic* MakeMaterial(const swc::water::Body& Body);
	/** The sky cube of the reflection (a capture of only the sky) and the sun color of the source on the instances. */
	void UpdateSkyCube(const FVector& View);
	void UpdateSkyCubeClouds(const FVector& View, bool bShow);
	/** The parameters of the ocean material instance for a material of the source, its animation, and its fog. */
	TMap<FName, float> OceanValues(int32 Material, const TSharedPtr<class FJsonObject>& Animation, const FVector3f& FogColor, float FogScale,
		float FogDensity, const FVector3f& NearFogColor) const;
	bool GetViewLocation(FVector& Out) const;

	/** The Unreal water (Water/SwcWaterUnreal.cpp). @return False with no specs (then the source path draws). */
	bool LoadUnreal(const FString& Folder);
	void SpawnUnrealZone();
	AActor* SpawnUnrealBody(const FUnrealBody& Entry);
	void UpdateUnreal(const FVector& View, float DeltaTime);
	/** @return The bodies of the Unreal water to spawn in the near radius of the loading gate about a place (1 with no zone yet). */
	int32 UnrealBodiesToSpawnNear(const FVector& Place) const;
	/** @return The client instance of -SwcWaterMaterials (FOLDER/Instance) when it exists, else the material of the plugin. */
	UMaterialInterface* UnrealMaterial(const TCHAR* Instance, const TCHAR* PluginPath) const;
	/** The Unreal water draws (swc.WaterMode 1 and its specs are loaded); bUnrealAvailable: its specs are loaded. */
	bool bUnrealMode = false;
	bool bUnrealAvailable = false;
	/** -SwcWaterInfoMesh=Plugin: keep the water info meshes of the plugin (editor runs); else the client builds them (SwcBuildWaterInfoMeshes). */
	bool bPluginInfoMeshes = false;
	FString UnrealMaterialFolder;
	TArray<FUnrealBody> UnrealBodies;
	/** The extent of the world regions (Unreal cm). */
	FVector2D UnrealZoneMin = FVector2D::ZeroVector;
	FVector2D UnrealZoneMax = FVector2D::ZeroVector;
	/** The source tone of a body (docs/WATER.md "The source tone"): the water fog of the source and the plugin values of its instance. */
	struct FUnrealTone
	{
		TWeakObjectPtr<class UWaterBodyComponent> Body;
		/** The ocean: the values of OceanNow, the ocean rule of the tone (swc::water::ocean_tone), and the far mesh instance (Extra). */
		bool bOcean = false;
		TWeakObjectPtr<UMaterialInstanceDynamic> Extra;
		FLinearColor PluginFarAbsorption = FLinearColor::Black;
		FLinearColor PluginFarScattering = FLinearColor::Black;
		/** The instances that have the tone now (the water instance and, for a river, its two transition instances). */
		TArray<TWeakObjectPtr<UMaterialInstanceDynamic>, TInlineAllocator<3>> Materials;
		FLinearColor Fog = FLinearColor::Black;
		float Density = 0.0f;
		float Scale = 1.0f;
		FLinearColor PluginAbsorption = FLinearColor::Black;
		FLinearColor PluginScattering = FLinearColor::Black;
	};
	TArray<FUnrealTone> UnrealTones;
	int32 AppliedTone = -1;
	float AppliedAlbedoScale = -1.0f;
	void AddUnrealTone(class UWaterBodyComponent& Body, const swc::water::Body& Surface);
	void ApplyUnrealTone(FUnrealTone& Tone) const;
	/** The ocean tone of OceanNow (the ocean of the place, blended in the transition time of its override). */
	void UpdateUnrealOceanTone();
	/** The parameters of the near term of the source ocean on the ocean instances (swc.WaterOceanNear; the check parent M_SwcWaterNear). */
	void UpdateUnrealOceanNear(const FUnrealTone& Tone);
	/** Shows one render path and hides the other (swc.WaterMode). */
	void SetUnrealVisible(bool bUnreal);
	int32 OceanToneIndex = INDEX_NONE;
	/** The ocean waves of the source (swc.WaterOceanWaves 1): S is the blended WavesSize of OceanNow; M (WavesAmount) changes at the end of a
	 *  transition (the source copies it then). The plugin waves of the ocean come back with swc.WaterOceanWaves 0. */
	void UpdateUnrealOceanWaves();
	float WavesAmountNow = 2.0f;
	float AppliedWavesSize = -1.0f;
	float AppliedWavesAmount = -1.0f;
	bool bSourceWavesOn = false;
	double NextWavesUpdate = 0.0;
	double NextZoneRefresh = 0.0;
	bool bInfoTextureLogged = false;
	/** The tests of the near tone of the ocean that the ocean instance has now (swc.WaterOceanFoamScatter, ...; research/water-near-tone.md). */
	FVector3f AppliedOceanTests = FVector3f(-1.0f, -1.0f, -2.0f);
	/** The floor tests of the ocean that the ocean instance has now (swc.WaterOceanExtinction, swc.WaterOceanMaskOffset). */
	FVector2f AppliedOceanFloorTests = FVector2f(-1.0f, -9999.0f);
	/** The blue scattering gain of the ocean tone that the ocean instances have now (swc.WaterOceanScatterBlue). */
	float AppliedOceanBlue = 1.0f;
	/** The medium gain of the ocean tone that the ocean instances have now (swc.WaterOceanMediumGain). */
	float AppliedOceanMedium = 1.0f;
	/** The near term of the ocean that the ocean instances have now (gain, mode, debug, sigma) and the time of its next update. */
	FVector4f AppliedNear = FVector4f(0.0f, 0.0f, 0.0f, 0.0f);
	double NextNearUpdate = 0.0;
	/** The shore foam of the ocean that the ocean instances have now (gain, debug). */
	FVector2f AppliedShore = FVector2f(0.0f, 0.0f);
	/** The optics tests of the ocean that the ocean instances have now (the near density per m, the normal mix). */
	FVector2f AppliedOptics = FVector2f(0.0f, 1.0f);
	/** -SwcWaterOceanDepth=M: the channel depth of the plugin ocean in m (default 10; the plugin default 0 makes the ocean the minimum of the
	 *  water height range of the zone, and then the plugin draws it clear: docs/WATER.md "Stage 2a"). */
	float OceanChannelDepth = 10.0f;
	/** The far-ring test (research/water-far-ring.md; docs/WATER.md "The far ring"): -SwcWaterZoneMode=Global turns the local tessellation
	 *  of the zone off (one water mesh tree and one water info capture for the whole zone; the far mesh of the plugin starts at the zone
	 *  edge, where the detailed water ends); -SwcWaterInfoResolution=N sets the water info texture (default 2048). Default: local, 2048. */
	bool bGlobalZone = false;
	int32 InfoResolution = 2048;
	UPROPERTY()
	TObjectPtr<class UGerstnerWaterWaves> UnrealSourceWaves;
	UPROPERTY()
	TObjectPtr<class UWaterWavesBase> UnrealPluginWaves;
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> UnrealFarMaterial;
	double NextToneCheck = 0.0;
	UPROPERTY()
	TObjectPtr<class AWaterZone> UnrealZone;
	UPROPERTY()
	TObjectPtr<class AWaterBodyOcean> UnrealOcean;
	UPROPERTY()
	TArray<TObjectPtr<AActor>> UnrealActors;

	/** The parameter values of -SwcWater=Name=Value,... for each instance (the calibration of the matched views; docs/WATER.md). */
	TMap<FName, float> CalibrationValues;
	/** The last value of swc.WaterParams. */
	FString ConsoleParams;
	void ApplyOverrides(UMaterialInstanceDynamic* Material) const;

	bool bLoaded = false;
	bool bLoadTried = false;
	TArray<FMaterialRecord> Materials;
	TArray<FBodyEntry> Bodies;
	TMap<FIntPoint, TSharedPtr<swc::water::Tree>> Trees;
	double OceanLevel = 40.0;
	TMap<FName, float> OceanBase;
	TArray<FOceanOverride> Overrides;
	/** The ocean instance: the values now, the values of the target, and the time of the blend. */
	TMap<FName, float> OceanNow;
	TMap<FName, float> OceanFrom;
	TMap<FName, float> OceanTarget;
	int32 OceanSelected = -2;
	double OceanBlend = 1.0;
	double OceanBlendTime = 0.0;
	double NextWorldOceanCheck = 0.0;
	int32 WorldOceanChecks = 0;

	UPROPERTY()
	TObjectPtr<AActor> WaterActor;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> Parent;
	UPROPERTY()
	TArray<TObjectPtr<UStaticMesh>> Meshes;
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Ocean;
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> OceanMaterial;
	/** The instances of the surfaces (for the sky cube) and the sky cube now. */
	UPROPERTY()
	TArray<TObjectPtr<UMaterialInstanceDynamic>> SurfaceMaterials;
	UPROPERTY()
	TObjectPtr<class UTexture> SkyCube;
	UPROPERTY()
	TObjectPtr<class USceneCaptureComponentCube> SkyCapture;
	double NextSkyCapture = 0.0;
	/** The source cloud layers of the sky cube (only in the scene captures) and their dynamic instances. */
	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> SkyClouds;
	UPROPERTY()
	TArray<TObjectPtr<UMaterialInstanceDynamic>> SkyCloudMaterials;
	bool bSkyCloudsTried = false;
	FLinearColor SunColor = FLinearColor(0, 0, 0, 0);
	/** The shore foam (AST-200): the effects (their sheets), the emitters, the decal material, the decals and their instances. */
	TArray<TArray<FShoreSheet>> ShoreEffects;
	TArray<FShoreEmitter> ShoreEmitters;
	FLinearColor ShoreTint = FLinearColor::White;
	float ShoreOpacity = 0.7f;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> ShoreMaterial;
	UPROPERTY()
	TArray<TObjectPtr<class UDecalComponent>> ShoreDecals;
	UPROPERTY()
	TArray<TObjectPtr<UMaterialInstanceDynamic>> ShoreInstances;
};
