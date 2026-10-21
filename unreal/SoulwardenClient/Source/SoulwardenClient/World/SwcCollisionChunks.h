#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "SwcCollisionChunks.generated.h"

class UInstancedStaticMeshComponent;
struct FStreamableHandle;

/**
 * The collision of the merged structures (AST-201, docs/WORLD.md "The collision of the game"): the colliders of the collision
 * chunks of the regions (CoATGen: the merged render meshes have no physics; the collision chunk of each part has the physics
 * file of each piece at its place), in the converted file SWC_CONTENT/regions/NAME/collision-chunks.bin
 * (converter/swc_convert/collision_chunks.py; the colliders that the cells already have are not in it). Each batch of 128 m
 * within RangeCm of the character gets one hidden instanced component for each collision mesh (/SwcGameContent/Collision/FOLDER/C_NAME,
 * import section 6), with the collision profile of the cells (BlockAll, tag SwcCollision). The file of a region is read off the
 * game thread (the region of the character at once); the meshes of a batch within NearCm load at once, the others async. On by default: -SwcNoCollisionChunks
 * turns it off.
 */
UCLASS()
class SOULWARDENCLIENT_API USwcCollisionChunks : public UTickableWorldSubsystem
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
	struct FInstance
	{
		uint32 Entry = 0;
		FTransform Transform;
	};
	/** The data of one region (read off the game thread): the collision meshes and the instances by batch. */
	struct FRegionData
	{
		TArray<FSoftObjectPath> Meshes;
		TMap<FIntPoint, TArray<FInstance>> Batches;
		int32 Instances = 0;
	};
	struct FRegion
	{
		bool bStarted = false;
		bool bNoFile = false;
		TSharedPtr<FRegionData, ESPMode::ThreadSafe> Data;
		TSharedPtr<FThreadSafeBool, ESPMode::ThreadSafe> bRead;
	};
	struct FBatch
	{
		/** The async load of the meshes of a far batch; a near batch loads them at once. */
		TSharedPtr<FStreamableHandle> Assets;
		bool bBuilt = false;
		TArray<TWeakObjectPtr<UInstancedStaticMeshComponent>> Components;
		int32 Instances = 0;
	};

	static bool ReadRegionFile(const FString& Path, FRegionData& Data);
	FRegion& RegionAt(const FIntPoint& Key, bool bNow);
	void Update(const FVector& View);
	void Build(const FRegionData& Data, const TArray<FInstance>& Instances, FBatch& Batch);
	AActor* OwnerActor();

	TMap<FIntPoint, FRegion> Regions;
	/** The batches by (batch x, batch y, region): two regions can have colliders in one batch square (a piece of a region can lie
	 *  outside its square, AST-216: before, the region read second lost those colliders). */
	TMap<FIntVector, FBatch> Batches;
	TWeakObjectPtr<AActor> Owner;
	FString Folder;
	bool bEnabled = true;
	/** The batch under the character is built (until then the update runs in each frame: no fall through a deck at the world entry). */
	bool bNearReady = false;
	/** The batch of the character at the last update (a new batch: an update at once, the travel and the world entry). */
	FIntPoint LastHere = FIntPoint(MAX_int32, MAX_int32);
	double NextUpdate = 0.0;
	double NextReport = 0.0;
	static constexpr double BatchCm = 12800.0;
	static constexpr double RangeCm = 20000.0;
	static constexpr double DropCm = 30000.0;
	/** A batch within this distance of the character loads its meshes at once (the collision under the character at the world entry). */
	static constexpr double NearCm = 16000.0;
	static constexpr double UpdateSeconds = 0.5;
};
