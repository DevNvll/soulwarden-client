#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/WorldSubsystem.h"
#include "UObject/ObjectKey.h"
#include "SwcLevelInstances.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogSwcObjects, Log, All);

class UInstancedStaticMeshComponent;
class UStaticMesh;
class ULevel;

/** An instance of a static mesh of the world level: its component (an instanced static mesh component of a cell actor) and its
 *  index. */
struct FSwcMeshInstanceRef
{
	TWeakObjectPtr<UInstancedStaticMeshComponent> Component;
	int32 Index = INDEX_NONE;
	/** The mesh of the component, and the origin of the instance (Unreal world space). */
	TWeakObjectPtr<UStaticMesh> Mesh;
	FVector Origin = FVector::ZeroVector;
};

/**
 * The instances of the world level at a place, for the state visuals of the world objects (stage P4.1 part 2).
 * Read-only use of the level of the world track (docs/WORLD.md): one actor for each cell of 256 m with one instanced static mesh
 * component for each mesh (converter/swc_convert/cells.py); the level build does not change. An index of the cell actors by their
 * cell comes from the streaming levels when they come and go; a search looks only at the cell of the place and its 8 neighbors.
 * A hidden instance has the scale 0; the level transform stays here and comes back at the show.
 */
UCLASS()
class SOULWARDENCLIENT_API USwcLevelInstances : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

	/**
	 * @return The number of instances of the static mesh components of the cell actors of a place (the cell and its neighbors) whose
	 *         origin is within the radius (cm) of the place (Unreal world space), in Out (Out is reset).
	 */
	int32 FindInstancesAt(const FVector& Place, double RadiusCm, TArray<FSwcMeshInstanceRef>& Out);
	/** Hides an instance (the scale 0) or shows it again with its level transform. */
	void SetInstanceHidden(const FSwcMeshInstanceRef& Instance, bool bHidden);
	bool IsInstanceHidden(const FSwcMeshInstanceRef& Instance) const;

	/** One more each time a streaming level with cell actors comes (also again after a stream out): its instances have the level
	 *  transforms, and a hide of an earlier load is gone. */
	uint32 GetRevision() const { return Revision; }

	/** The size of a cell of the level (m: cells.py CELL_M). */
	static constexpr double CellMeters = 256.0;

private:
	void AddLevel(ULevel* Level, UWorld* World);
	void RemoveLevel(ULevel* Level, UWorld* World);
	/** @return True when the actor is a cell actor (now in the index). */
	bool AddActor(AActor* Actor);
	static FIntPoint CellOf(const FVector& Place);
	/** The development aid -SwcInstanceProbe=METERS: each 2 s, hide or show again the instances near the pawn. With
	 *  -SwcInstanceProbeKeep: hide the instances near the first place of the pawn one time, and hide them again after each new
	 *  load of their cells (GetRevision), as the object layer does. */
	bool TickProbe(float DeltaSeconds);

	TMap<FIntPoint, TArray<TWeakObjectPtr<AActor>>> Cells;
	/** The level transforms of the hidden instances, by component and index. */
	TMap<TObjectKey<UInstancedStaticMeshComponent>, TMap<int32, FTransform>> Hidden;
	FDelegateHandle AddedHandle;
	FDelegateHandle RemovedHandle;
	uint32 Revision = 0;
	double ProbeMeters = 0.0;
	bool bProbeHidden = false;
	bool bProbeKeep = false;
	bool bProbePlaceSet = false;
	FVector ProbePlace = FVector::ZeroVector;
	uint32 ProbeRevision = 0;
	TArray<FSwcMeshInstanceRef> ProbeInstances;
	FTSTicker::FDelegateHandle ProbeTicker;
};
