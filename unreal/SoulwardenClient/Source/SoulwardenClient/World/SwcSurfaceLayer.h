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
 * On by default (the M5 cost check); -SwcNoSurfaceLayer or SWC_SURFACE_LAYER=0 turns it off; swc.SurfaceLayer 0 hides it.
 */
UCLASS()
class SOULWARDENCLIENT_API USwcSurfaceLayer : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& World) override;
	virtual void Tick(float DeltaTime) override;
	virtual void Deinitialize() override;
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
		FLinearColor Color = FLinearColor::White;   // the entity Color, its alpha x the entity Opacity (M_SwcDecal DecalColor)
		TWeakObjectPtr<UDecalComponent> Component;
	};

	/** The roads, the decals, and the decal materials of a region file, read on a worker (AST-273 P8). */
	struct FParsed
	{
		TArray<FRoad> Roads;
		TArray<FDecal> Decals;
		TArray<FSoftObjectPath> Materials;
		FThreadSafeBool bDone = false;
	};

	struct FRegion
	{
		bool bRead = false;    // the read has started
		TSharedPtr<FParsed, ESPMode::ThreadSafe> Parsed;   // the read on a worker, until its data moves into the region
		TArray<FRoad> Roads;
		TArray<FDecal> Decals;
		TSharedPtr<FStreamableHandle> Materials;   // the decal materials of the region (one async load)
	};

	/** Reads the layer file of a region (any thread). @return False with no file. */
	static bool ReadRegion(const FString& RegionsFolder, const FIntPoint& Key, FParsed& Out);
	void Update(const FVector& View);
	FVector UpdatedAt = FVector(TNumericLimits<double>::Max());   // the view of the last update (the loading gate)
	AActor* OwnerActor();

	TMap<FIntPoint, FRegion> Regions;
	TWeakObjectPtr<AActor> Owner;
	FString Folder;
	double NextUpdate = 0.0;
	bool bEnabled = true;
	int32 RoadsShown = 0;
	int32 DecalsShown = 0;
};
