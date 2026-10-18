#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "SwcSurfaceLayer.generated.h"

class UDecalComponent;
class UStaticMeshComponent;
struct FStreamableHandle;

/**
 * The surface layer of the world (stage M5 of the materials track with S1, docs/MATERIALS.md): the roads (the road
 * splines of the slices, rebuilt as ribbon meshes on the terrain with a decal material: mesh decals) and the projected
 * decals of the slices (Unreal decal components). The converter writes SWC_CONTENT/decals/NAME/layer.json for each
 * region (converter/swc_convert/decals.py); tools/import-surface-layer.sh imports the road meshes and the decal
 * materials into /SwcGameContent. This subsystem streams them near the view: the road meshes of the cells within
 * RoadRangeCm, the decals within their view distance (Max View Distance x View Distance Multiplier of the source).
 * Opt-in until the check of stage S1 passes: -SwcSurfaceLayer or SWC_SURFACE_LAYER=1 (-SwcNoSurfaceLayer turns it off).
 */
UCLASS()
class SOULWARDENCLIENT_API USwcSurfaceLayer : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& World) override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	struct FRoad
	{
		FSoftObjectPath Mesh;
		FVector Location = FVector::ZeroVector;   // Unreal cm: the cell corner of the mesh
		FVector Center = FVector::ZeroVector;     // the cell center (the range test)
		int32 SortPriority = 0;
		TWeakObjectPtr<UStaticMeshComponent> Component;
		TSharedPtr<FStreamableHandle> Loading;
	};

	struct FDecal
	{
		FSoftObjectPath Material;
		FTransform Transform;
		FVector Size = FVector::OneVector;        // half sizes, cm (X: the projection axis)
		int32 SortPriority = 0;
		double RangeCm = 0.0;
		TWeakObjectPtr<UDecalComponent> Component;
	};

	struct FRegion
	{
		bool bRead = false;
		TArray<FRoad> Roads;
		TArray<FDecal> Decals;
		TSharedPtr<FStreamableHandle> Materials;   // the decal materials of the region (one async load)
	};

	bool ReadRegion(const FIntPoint& Key, FRegion& Region) const;
	void Update(const FVector& View);
	AActor* OwnerActor();

	TMap<FIntPoint, FRegion> Regions;
	TWeakObjectPtr<AActor> Owner;
	FString Folder;
	double NextUpdate = 0.0;
	bool bEnabled = true;
	int32 RoadsShown = 0;
	int32 DecalsShown = 0;
};
