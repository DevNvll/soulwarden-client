#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Creatures/SwcEntityTable.h"
#include "SwcCreature.generated.h"

class FSwcCreatureLibrary;
class USkeletalMeshComponent;
class UStaticMeshComponent;
class USwcCreatureAnimInstance;
struct FSwcCreatureAnimations;
struct FSwcCreatureSlice;

/**
 * A creature of the backend (docs/CREATURES.md): the body of its slice with the material variant of its category row, its
 * default weapons on their sockets, and the scale of its vitals row (research creature-visuals.md sections 3 and 4). It shows
 * the movement states of the server with a delay (interpolation between the time anchors), and it plays the actions of the
 * scope records at their time on the server clock.
 */
UCLASS(Transient)
class SOULWARDENCLIENT_API ASwcCreature : public AActor
{
	GENERATED_BODY()

public:
	ASwcCreature();

	/** Makes the body of an entity: the model of its slice, or a placeholder when the slice is not converted. */
	void Setup(FSwcCreatureLibrary& Library, const swc::creatures::Entity& Entity);

	/**
	 * Shows the entity at a time of the server clock (the render time: the server time now less the interpolation delay).
	 * @return The text of a new action of the server for the log, or an empty text.
	 */
	FString UpdateFromEntity(FSwcCreatureLibrary& Library, const swc::creatures::Entity& Entity, uint64 RenderTimeUs, float DeltaSeconds);

	/** The values of the name plate. */
	uint64 GetSerial() const { return Serial; }
	const FString& GetDisplayName() const { return DisplayName; }
	const FString& GetSlicePath() const { return SlicePath; }
	bool HasModel() const { return bHasModel; }
	bool HidesLevel() const { return bHidesLevel; }
	const FSwcCreatureSlice* GetSlice() const { return Slice; }
	/** @return The point of the name plate: above the bounds of the body. */
	FVector GetPlatePoint() const;
	/** The time since the death mark (s), or a negative value. */
	double GetDeadSeconds() const;

private:
	struct FPending
	{
		/** The start of the action on the server clock (us). */
		int64 StartUs = 0;
		bool bRecord = false;
		swc::creatures::ScopeRecord Record;
	};

	void ApplyScope(const swc::creatures::Entity& Entity);
	/** Shows a body mesh: the leader itself (the body of the skeleton file) or the follower. */
	void ShowBody(USkeletalMesh* Mesh);
	FString StartPending(uint64 RenderTimeUs, bool bDead);
	void SampleMotion(const swc::creatures::Entity& Entity, uint64 RenderTimeUs, FVector& OutPosition, FQuat& OutRotation) const;

	UPROPERTY()
	TObjectPtr<USceneComponent> Root;
	/** The skeleton mesh with the animation (the body of the skeleton file), and the body of the slice that follows its pose
	 *  (Leader Pose, by bone names). The leader is hidden when the body is a different mesh. */
	UPROPERTY()
	TObjectPtr<USkeletalMeshComponent> Leader;
	UPROPERTY()
	TObjectPtr<USkeletalMeshComponent> Body;
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Placeholder;
	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> Weapons;

	const FSwcCreatureSlice* Slice = nullptr;
	TSharedPtr<FSwcCreatureAnimations> Animations;
	uint64 Serial = 0;
	FString SlicePath;
	FString DisplayName;
	bool bHasModel = false;
	bool bHidesLevel = false;
	bool bLooksApplied = false;
	uint32 AppliedCategory = 0;
	float BodyTop = 180.0f;

	/** The scope records: the revision of the entity that the actor read, the records that wait for their time, and the record
	 *  that plays (its start on the server clock). */
	uint32 ScopeRevision = 0;
	TArray<FPending> Pending;
	bool bPlaying = false;
	swc::creatures::ScopeRecord Playing;
	int64 PlayingStartUs = 0;
	int32 PlayingFragment = INDEX_NONE;

	/** The motion parameters and their smoothing (MotionParameterSmoothing of the slices: 0.1 s speed and travel angle,
	 *  0.75 s turn speed; research feel-values.md R3.3.1). */
	bool bHasLast = false;
	FVector LastPosition = FVector::ZeroVector;
	float LastYaw = 0.0f;
	float MoveSpeed = 0.0f;
	float MoveSpeedRate = 0.0f;
	float TurnSpeed = 0.0f;
	float TurnSpeedRate = 0.0f;
	float TravelAngle = 0.0f;
	float TravelAngleRate = 0.0f;

	bool bDead = false;
	double DeadAt = 0.0;
};
