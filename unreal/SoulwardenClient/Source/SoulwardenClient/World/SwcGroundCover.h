#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"

#include "SwcGroundCover.generated.h"

class UHierarchicalInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
struct FStreamableHandle;

/**
 * The ground cover of the game near the view (world fidelity T3, docs/WORLD.md): the records of region.vegetation (the
 * grass, the ferns, the leaf piles, the small plants), in the converted file SWC_CONTENT/regions/NAME/cover.bin
 * (converter/swc_convert/cover.py: the descriptor table, then the packed records of the 128 x 128 tiles of 16 m,
 * unchanged). Each batch of 4 x 4 tiles (64 m) within RangeCm of the view gets one instanced component for each
 * descriptor, with the source transforms (no new scatter), the override materials of its descriptor, and its bending
 * as the custom primitive data 0 "SwcWind". The file of a region is read off the game thread; its meshes and materials
 * load async. On by default (stage T3): -SwcNoGroundCover or SWC_GROUND_COVER=0 turns it off; -SwcCoverRange=M sets the
 * range (default 100 m). swc.GroundCover.Show 0 hides it (the batches stay);
 * swc.GroundCover.Shadow and swc.GroundCover.BackLight are the test switches of the color check.
 */
UCLASS()
class SOULWARDENCLIENT_API USwcGroundCover : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& World) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	/**
	 * The folder of the cover files (REGIONS/r_+XX_+YY/cover.bin). The menu scene sets the folder of the terrain of the main
	 * menu level (SWC_CONTENT/frontend/terrain/regions, docs/FRONTEND.md F4b) with its view place (the menu has no pawn, so
	 * the view of the loading gate is not there), and an empty text at its end: the world folder again, with no fixed view.
	 * The batches and the regions that are read go; the next update reads the regions of the new folder.
	 */
	void SetRegionsFolder(const FString& RegionsFolder, const TOptional<FVector>& FixedView = TOptional<FVector>());
	virtual TStatId GetStatId() const override;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	struct FDescriptor
	{
		FSoftObjectPath Mesh;
		TArray<TPair<FName, FSoftObjectPath>> Slots;   // the override materials by slot name
		float Bending = 0.0f;
	};
	/** The data of one region (read off the game thread). */
	struct FRegionData
	{
		TArray<uint8> Bytes;       // the whole cover.bin
		int64 Source = 0;          // the start of the bytes of region.vegetation in Bytes
		TArray<FDescriptor> Descriptors;
		TArray<uint32> TileOffsets;   // 128 x 128, from Source
	};
	struct FRegion
	{
		bool bStarted = false;
		TSharedPtr<FRegionData, ESPMode::ThreadSafe> Data;
		TSharedPtr<FThreadSafeBool, ESPMode::ThreadSafe> bRead;
		TSharedPtr<FStreamableHandle> Assets;
		bool bNoFile = false;
	};
	struct FBatch
	{
		TArray<TWeakObjectPtr<UHierarchicalInstancedStaticMeshComponent>> Components;
		int32 Instances = 0;
	};

	static bool ReadRegionFile(const FString& Path, FRegionData& Data);
	FRegion& RegionAt(const FIntPoint& Key);
	bool BuildBatch(const FIntPoint& Batch, FBatch& Out);
	void Update(const FVector& View, double MarginM, double Budget);
	/** The batches within the range of View (plus MarginM) that are not built, the nearest first. */
	void WantedBatches(const FVector& View, double MarginM, TArray<TPair<double, FIntPoint>>& Wanted) const;
	AActor* OwnerActor();
	void ApplyBackLight(UHierarchicalInstancedStaticMeshComponent* Component);

	TMap<FIntPoint, FRegion> Regions;
	TMap<FIntPoint, FBatch> Batches;
	TWeakObjectPtr<AActor> Owner;
	FString Folder;
	/** The view of the menu scene (SetRegionsFolder): used when there is no loading place and no pawn. */
	TOptional<FVector> FixedView;
	double RangeCm = 10000.0;
	double NextUpdate = 0.0;
	double NextReport = 0.0;
	bool bEnabled = false;
	bool bShown = true;
	bool bShadow = false;
	double LongestBuild = 0.0;   // seconds, since the last report line
	float BackLight = 1.0f;
	/** The dynamic instances of the back light test, by the original material. */
	UPROPERTY()
	TMap<TObjectPtr<UMaterialInterface>, TObjectPtr<UMaterialInstanceDynamic>> BackLightInstances;
};
