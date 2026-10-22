#pragma once

#include "CoreMinimal.h"
#include "Environment/SwcEnvironmentRules.h"
#include "Environment/SwcWeatherOcclusion.h"
#include "Math/RandomStream.h"
#include "UObject/StrongObjectPtr.h"

class AActor;
class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialParameterCollection;
class UStaticMesh;
class USceneComponent;

/** A Snow volume of the data (AST-257; research/snow.md section 1: the Snow component and its options). The source axes (m). */
struct FSwcSnowVolume
{
	swc::environment::Vec3 Center;
	double Radius = 0.0;
	/** The surface: SnowAmount, FrostAmount, SurfaceFreezing, SnowBrightness, FrostBrightness. */
	double SnowAmount = 0.0;
	double FrostAmount = 0.0;
	double SurfaceFreezing = 0.0;
	double SnowBrightness = 0.0;
	double FrostBrightness = 0.0;
	/** The fall: SnowFlakeCount, SnowFlakeSize, SnowFallBrightness, SnowFallGravityScale, SnowFallWindScale, SnowFallTurbulence,
	 *  SnowFallTurbulenceFreq. */
	int32 FlakeCount = 0;
	double FlakeSize = 1.0;
	double FallBrightness = 1.0;
	double GravityScale = 0.0;
	double WindScale = 0.0;
	double Turbulence = 0.0;
	double TurbulenceFreq = 0.0;
	bool bEnabled = false;
	FString Name;
};

/**
 * The falling snow in the world (AST-257; research/snow.md sections 2 and 5): the legacy flakes of the source (SceneSnow) as one
 * instanced mesh. The mesh (built at run time) has the 500 flakes of one cluster: each flake a quad at a random local place of
 * -10 to 10 m, with its random scale (0.005 to 0.010), phase, atlas tile, and index. 100 instances are the clusters: each one spawns
 * at the camera (X, Y -15 to 15 m, Z -1 to 9 m), falls with gravity scale x 9.8 x its multiplier (0.2 to 0.4) m/s, drifts with the
 * wind, and respawns after its life (4 / max(gravity scale, 1) +- 2.5 s) or 30 m from the camera. The material M_SwcSnowFlake draws
 * the first N flakes of each cluster (N: the count of the volume x the weight at the camera), each flake facing the camera with the
 * source size (SnowFlakeSize x scale x depth^0.25, the clip expansion of the source), the turbulence of the source, the 2 x 2 atlas,
 * the soft depth fade (0.5 m), the roof map (FSwcWeatherOcclusion), and the sun term of the source.
 */
class FSwcSnow
{
public:
	static constexpr int32 FlakesPerCluster = 500;
	static constexpr int32 Clusters = 100;

	FSwcSnow();
	~FSwcSnow();

	/** Creates the component on the environment actor; false with no flake material (tools/import-snow.sh). */
	bool Create(AActor* Owner, USceneComponent* Root);

	struct FFrame
	{
		FVector Camera = FVector::ZeroVector;
		/** The horizontal field of view (degrees). */
		double HorizontalFov = 90.0;
		/** The direction toward the sun or the moon (Unreal space, unit). */
		FVector LightToward = FVector::UpVector;
		/** The light of the flakes: the color of the sun or the moon x the luminance of a white surface in it. */
		FLinearColor Light = FLinearColor::Black;
		/** The wind (Unreal space, m/s). */
		FVector Wind = FVector::ZeroVector;
		double DeltaSeconds = 0.0;
	};
	/** Each frame: the volume of the camera (the largest weight), the count, the clusters, and the material parameters. */
	void Update(UWorld& World, const TArray<FSwcSnowVolume>& Volumes, const FFrame& Frame, const AActor* Pawn);

	/** @return A short text for the log: the volume, the weight, the count, and the roof map. */
	FString Describe() const;
	const FSwcWeatherOcclusion& GetOcclusion() const { return Occlusion; }

private:
	struct FCluster
	{
		FVector Center = FVector::ZeroVector;
		double Age = 0.0;
		double Life = 0.0;
		double Multiplier = 0.3;
	};
	void Spawn(FCluster& Cluster, const FVector& Camera, const FSwcSnowVolume& Volume);
	UStaticMesh* BuildMesh(UObject* Outer);
	/** The values of MPC_SwcWeather for the surface snow (no volume: SnowAmounts.w 0). */
	void UpdateSurface(UWorld& World, const FSwcSnowVolume* Surface, bool bRoof);

	TArray<FCluster> ClusterStates;
	TWeakObjectPtr<UInstancedStaticMeshComponent> Flakes;
	TStrongObjectPtr<UMaterialInstanceDynamic> Material;
	FSwcWeatherOcclusion Occlusion;
	FRandomStream Random;
	FString VolumeName;
	FString SurfaceName;
	TWeakObjectPtr<UMaterialParameterCollection> Collection;
	bool bCollectionLoaded = false;
	double Weight = 0.0;
	int32 Count = 0;
	bool bSpawned = false;
	FVector LastCamera = FVector::ZeroVector;
};
