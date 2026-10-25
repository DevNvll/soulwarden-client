#pragma once

#include "CoreMinimal.h"

#include <array>

#include "Engine/StreamableManager.h"
#include "Subsystems/WorldSubsystem.h"

#include "SwcQuestObjects.generated.h"

struct FSwcCreaturePlate;

/**
 * The destructible quest objects (stage Q9, Linear AST-244; Soulwarden docs/systems/quests.md "The destructible quest objects" of stage
 * 1.8e): the corruption nodules of 06_survivors, the monolith of 06_demonstorm2, and the other placements of the variation table
 * Gatherable_Quest_Damageable. The client makes them from the level data (SWC_CONTENT/world-objects/quest-objects.json,
 * converter/swc_convert/quest_objects.py). The server decides each hit (the slice has no HitVolume: the client reports no contact; the
 * normal attacks of the character are the input), and the LootTracker entry of the object (its managed actor, state Looted) destroys it
 * for the character until the respawn removes the entry.
 *
 * The state of an object within DrawM of the player:
 *   - an object with InteractOnlyOnQuest and no Active task with its InteractTag (USwcBackendSubsystem::GetQuestTags): nothing (the
 *     retail client shows nothing before the task; live I0 of backend 1.8e), no collision;
 *   - active: the meshes of its placed slice and of the variant visuals Visual_Healthy and Visual_Basemesh, a collision for the movement
 *     of the player only (the object type and the channel Pawn: the camera and the traces do not see it), and a plate of the AI pool
 *     (the marker type DamageableGatherable, the health bar of the variant at 1 of 1: the fallback of the Lua code);
 *   - destroyed (a Looted claim of its actor): the meshes of Visual_Depleted (often none), no collision, no plate.
 * -SwcNoQuestObjects turns it off; swc.QuestObjects 0 hides the actors in a run.
 */
UCLASS()
class SOULWARDENCLIENT_API USwcQuestObjectSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& World) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	/** The plates of the active objects within the plate range (the AI pool of the markers; UI/SwcUiSubsystem adds them to the lines of
	 *  the creatures). Interest numbers from PlateInterestBase. */
	void GetPlates(TArray<FSwcCreaturePlate>& Out) const;

	static constexpr uint32 PlateInterestBase = 0xF0000000u;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	enum class EState : uint8 { Hidden, Active, Destroyed };
	struct FPart
	{
		FSoftObjectPath Mesh;
		FSoftObjectPath Collision;
		TArray<TPair<FName, FSoftObjectPath>> Slots;
		bool bDecal = false;
		TArray<FTransform> Transforms;
	};
	struct FObject
	{
		FString Key;
		std::array<uint8, 16> Actor{};
		FString Tag;
		bool bOnlyOnQuest = true;
		FString Name;
		FVector Place = FVector::ZeroVector;
		FQuat Rotation = FQuat::Identity;
		/** The slices of each state: active (placed, healthy, base) and destroyed (depleted). */
		TArray<FString> ActiveSlices;
		TArray<FString> DestroyedSlices;
		FString MarkerType;
		FString MarkerOverride;
		bool bForceHealthBar = false;
		EState State = EState::Hidden;
		TWeakObjectPtr<AActor> Shown;
		TSharedPtr<FStreamableHandle> Load;
		uint32 LoadSerial = 0;
	};

	bool Read();
	void Show(int32 Index, EState State);
	void Build(int32 Index, uint32 Serial);
	void Clear(FObject& Object);

	TMap<FString, TArray<FPart>> Slices;
	TArray<FObject> Objects;
	FStreamableManager Streamer;
	FVector PlayerPlace = FVector::ZeroVector;
	double NextUpdate = 0.0;
	int32 ShownMode = 1;
	bool bEnabled = false;
	static constexpr double DrawM = 150.0;
	static constexpr double PlateM = 100.0;
	static constexpr double UpdateSeconds = 0.25;
};
