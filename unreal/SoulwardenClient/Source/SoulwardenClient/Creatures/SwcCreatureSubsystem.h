#pragma once

#include "CoreMinimal.h"
#include "Creatures/SwcCreatureData.h"
#include "Creatures/SwcEntityTable.h"
#include "Subsystems/WorldSubsystem.h"
#include "SwcCreatureSubsystem.generated.h"

class APlayerController;
class ASwcCreature;
class UCanvas;

DECLARE_LOG_CATEGORY_EXTERN(LogSwcCreatures, Log, All);

/** A creature for the target lock of the camera: its interest number, its feet (Unreal cm), and the dead mark. */
struct FSwcCreatureTarget
{
	uint32 Interest = 0;
	FVector Position = FVector::ZeroVector;
	bool bDead = false;
};

/**
 * The creatures of the backend in the world of the game (docs/CREATURES.md): the entity table of the other entities (the
 * records and the stops of USwcBackendSubsystem::TakeOtherUpdates), one ASwcCreature for each entity with a slice and a
 * position, the movement states shown with a delay of InterpolationDelay on the server clock, and the name plates (a simple
 * plate drawn on the canvas of the game viewport; the original is the AiMarker template of
 * lyshineui/markers/markermanager.dynamicuicanvas, for the UI track).
 *
 * Command line: -SwcPlates=all (development pictures) draws the plate of each creature within 40 m.
 */
UCLASS()
class SOULWARDENCLIENT_API USwcCreatureSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	/** The read path of the camera (the target lock): the creatures of the table with a position. */
	void GetTargets(TArray<FSwcCreatureTarget>& Out) const;
	const swc::creatures::EntityTable& GetTable() const { return Table; }

	/** The delay of the shown movement states on the server clock: the backend sends a state for each step of 200 ms. */
	static constexpr double InterpolationDelaySeconds = 0.25;

private:
	void Clear(const TCHAR* Cause);
	void Spawn(const swc::creatures::Entity& Entity);
	void DrawPlates(UCanvas* Canvas, APlayerController* Controller);

	swc::creatures::EntityTable Table;
	FSwcCreatureLibrary Library;
	TMap<uint32, TWeakObjectPtr<ASwcCreature>> Actors;
	FDelegateHandle DrawHandle;
	bool bAllPlates = false;
	bool bConnected = false;
	/** The counts of a session for the log line at its end. */
	int32 Spawned = 0;
	int32 Removed = 0;
	int32 Actions = 0;
	int32 Deaths = 0;
	int32 Placeholders = 0;
	TSet<FString> Kinds;
};
