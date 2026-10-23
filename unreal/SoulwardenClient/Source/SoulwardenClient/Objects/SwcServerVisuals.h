#pragma once

#include "CoreMinimal.h"
#include "Engine/StreamableManager.h"
#include "Subsystems/WorldSubsystem.h"
#include "SwcServerVisuals.generated.h"

/**
 * The visuals of the objects that the backend spawns (AST-275, class C2): the crafting stations of the settlements and their bodies.
 * A settlement places its stations with spawners that are not "on start": the server makes each station and its children, and the
 * client gets a record (the metadata state: the slice; the PositionInTheWorld state: the place and the rotation) within 150 m, and
 * the stop of the record at 200 m (Soulwarden protocol notes 4.36). The cells have no station (the census does not follow these
 * spawners), and the creature subsystem draws only records with movement. This subsystem reads the entity table of
 * USwcCreatureSubsystem: a record with a world place whose slice key has visuals, at a backend place of that key (within PlaceM),
 * gets one actor with the static meshes of the slice; the stop of the record (or a new serial) removes it. One path, no double draw.
 * The data: SWC_CONTENT/world-objects/server-visuals.json (converter/swc_convert/server_visuals.py).
 * -SwcNoServerVisuals turns it off; swc.ServerVisuals 0 hides the actors in a run.
 */
UCLASS()
class SOULWARDENCLIENT_API USwcServerVisualSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& World) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	struct FPart
	{
		FSoftObjectPath Mesh;
		FSoftObjectPath Collision;
		TArray<TPair<FName, FSoftObjectPath>> Slots;
		bool bDecal = false;
		TArray<FTransform> Transforms;
	};
	struct FVisual
	{
		uint64 Serial = 0;
		FString Key;
		TWeakObjectPtr<AActor> Actor;
		TSharedPtr<FStreamableHandle> Load;
	};

	bool Read();
	void Spawn(uint32 Interest, FVisual& Visual, const FVector& Place, const FQuat& Rotation);
	void Build(FVisual& Visual);

	/** The parts of each slice key (the text of Entity::slice_key). */
	TMap<FString, TArray<FPart>> Slices;
	/** The backend places of each key (Unreal cm). */
	TMultiMap<FString, FVector> Places;
	TMap<uint32, FVisual> Visuals;
	FStreamableManager Streamer;
	double NextUpdate = 0.0;
	int32 ShownMode = 1;
	bool bEnabled = false;
	/** The records with visuals but no backend place near them (logged once each). */
	TSet<FString> Unmatched;
	static constexpr double PlaceM = 2.0;
	static constexpr double UpdateSeconds = 0.25;
};
