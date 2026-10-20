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
 * movement (the water quadtree of each region). The content follows the source; the rendering is Single Layer Water (the base
 * material /SwcGameContent/Water/M_SwcWaterSource of tools/import-water.sh) with one dynamic instance for each surface.
 *
 * The surfaces build near the view (static meshes made at run time; a few each frame) and stay. The ocean is a flat grid at the
 * ocean level that follows the view. With no base material the surfaces do not show, and the queries still work.
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

	void Load();
	void UpdateBodies(const FVector& View);
	void UpdateOcean(const FVector& View, float DeltaTime);
	void HideWorldOcean();
	UStaticMesh* BuildMesh(const swc::water::Body& Body, const FVector& Center, UMaterialInterface* Material) const;
	UMaterialInstanceDynamic* MakeMaterial(const swc::water::Body& Body);
	/** Sets the sky cube of the reflection (the cube of the sky light) on the instances when it changes. */
	void UpdateSkyCube();
	/** The parameters of the ocean material instance for a material of the source, its animation, and its fog. */
	TMap<FName, float> OceanValues(int32 Material, const TSharedPtr<class FJsonObject>& Animation, const FVector3f& FogColor, float FogScale,
		float FogDensity, const FVector3f& NearFogColor) const;
	bool GetViewLocation(FVector& Out) const;

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
	TObjectPtr<class UTextureCube> SkyCube;
};
