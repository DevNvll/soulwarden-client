#pragma once

#include "CoreMinimal.h"
#include "Engine/StreamableManager.h"
#include "GameFramework/Actor.h"
#include "Creatures/SwcEntityTable.h"
#include "Creatures/SwcHitShapes.h"
#include "SwcCreature.generated.h"

class FSwcCreatureLibrary;
class FSwcStatusEffects;
class UCapsuleComponent;
class USkeletalMeshComponent;
class UStaticMeshComponent;
class USwcCreatureAnimInstance;
struct FSwcCreatureAnimations;
struct FSwcCreatureSlice;
struct FSwcHitVolume;

/** A hit volume of a creature at the pose that it shows: the volume of its slice and its shape in Unreal world space. */
struct FSwcCreatureHitShape
{
	const FSwcHitVolume* Volume = nullptr;
	FSwcHitShape Shape;
};

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
	/** The assets of a slice (the body, the skeleton, the variant meshes, the weapons; bFirst: the clips of the Idle and Move
	 *  fragments, else each clip): the async load of a creature and the preload of the kinds near an arrival (AST-216). */
	static void CollectSliceAssets(FSwcCreatureLibrary& Library, const FSwcCreatureSlice* Slice, TArray<FSoftObjectPath>& Out, bool bFirst);
	ASwcCreature();

	/**
	 * Makes the body of an entity: the model of its slice, or a placeholder when the slice is not converted. The assets (the meshes,
	 * the weapons, the clips of the animation sets) load async with Streamer: the actor stays hidden and follows its movement until
	 * they are ready, then the body is made from the assets in memory. No load on the game thread waits for the async loads (a load
	 * of an asset that is not in memory flushes them: 5.5 s in a run of the player track).
	 */
	void Setup(FSwcCreatureLibrary& Library, const swc::creatures::Entity& Entity, FStreamableManager& Streamer);
	/** The creature waits for its animation sets (read on a worker thread) before Setup: hidden, its movement only. */
	void BeginLoading(uint64 InSerial);
	/** @return True while the assets of the body load. */
	bool IsLoading() const { return bLoading; }
	/** @return True one time when the body is made (the creature view logs the creature then); Seconds: the time of the load. */
	bool TakeReady(double& Seconds);

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
	/** AST-230 (plan step 10): the active statuses (the index of each status row, its potency not zero): their effects on the body. */
	void UpdateStatusEffects(const TArray<TPair<uint32, bool>>& Active);
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	/** @return The point of the name plate: above the bounds of the body. */
	FVector GetPlatePoint() const;
	/** @return The probe text of this frame (-SwcCreatureProbe): the place (engine m), the yaw (engine degrees: counterclockwise from
	 *  north, as the server yaw), the motion parameters of the blend spaces, and the fragments of the pose. */
	FString ProbeText() const;
	/** The time since the death mark (s), or a negative value. */
	double GetDeadSeconds() const;
	/** Adds the hit volumes of the slice at the pose of this frame (the bones of the leader), in Unreal world space (cm). A
	 *  placeholder has none. */
	void GetHitShapes(TArray<FSwcCreatureHitShape>& Out) const;
	/**
	 * @return The lock point of the camera (Unreal world space) from the CameraLockTargetComponent of the slice: the feet and
	 *         m_Offset in the frame of the creature (with its scale) when the offset is not zero; else the joint of the lock icon
	 *         and its vertical offset; else 1.0 m above the feet. The combination of the offset and the joint in the retail
	 *         client is not known (an inference).
	 */
	FVector GetLockPoint() const;
	/** @return The body capsule that blocks the player (no collision: none, or a dead creature). */
	const UCapsuleComponent* GetBodyBlock() const { return BodyBlock; }
	/** @return The bounds of the body (Unreal world space), for a fast test before the hit volumes. */
	FBoxSphereBounds GetBodyBounds() const;

private:
	struct FPending
	{
		/** The start of the action on the server clock (us). */
		int64 StartUs = 0;
		bool bRecord = false;
		swc::creatures::ScopeRecord Record;
	};

	/** The records of one scope: the records that wait for their time, and the record that plays (its start on the server
	 *  clock). Scope 0 (FullBody) plays the main fragment; a different scope plays an overlay (the additive reaction of scope 4
	 *  of the wolf). */
	struct FScopeState
	{
		TArray<FPending> Pending;
		bool bPlaying = false;
		swc::creatures::ScopeRecord Playing;
		int64 PlayingStartUs = 0;
		int32 PlayingFragment = INDEX_NONE;
	};

	void SetupBodyBlock();
	/** The body from the assets in memory (after the async load). */
	void FinishSetup();
	void OnAssetsLoaded();
	/** The second stage: the other clips of the animation sets, after the body shows. */
	void LoadRest();
	void OnRestLoaded();
	/** The paths of the assets of the body. First: the meshes (the slice, its variants, its weapons) and the clips of the fragments
	 *  Idle and Move; else: each clip of the animation sets. */
	void CollectAssets(TArray<FSoftObjectPath>& Out, bool bFirst) const;
	void ApplyScope(const swc::creatures::Entity& Entity);
	void ApplyScopeRecord(uint32 Scope, const swc::creatures::Entity& Entity);
	FString StartScope(uint32 Scope, FScopeState& State, uint64 RenderTimeUs, bool bDead);
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
	TSharedPtr<FSwcStatusEffects> StatusEffects;
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Placeholder;
	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> Weapons;
	/** The animated weapons (the weapon scope): the skeletal mesh of the weapon skeleton, its body (or none: the skeleton mesh
	 *  is the body), and the animation set. Each fragment of scope 0 plays its variant of the weapon database on them. */
	struct FAnimatedWeapon
	{
		USkeletalMeshComponent* Leader = nullptr;
		TSharedPtr<FSwcCreatureAnimations> Animations;
	};
	TArray<FAnimatedWeapon> AnimatedWeapons;
	UPROPERTY()
	TArray<TObjectPtr<USkeletalMeshComponent>> WeaponMeshes;
	/** Plays the weapon scope of a fragment of scope 0 on each animated weapon (or stops it: no variant).
	 *  @return The number of weapons that play the fragment. */
	int32 PlayWeaponScope(int32 Fragment, float FragmentTime, float PlaybackScale);

	/** The body capsule that blocks the movement of the player (only the channel Pawn): none for a dead creature. */
	UPROPERTY()
	TObjectPtr<UCapsuleComponent> BodyBlock;

	const FSwcCreatureSlice* Slice = nullptr;
	TSharedPtr<FSwcCreatureAnimations> Animations;
	/** The async load of the assets: the library and the manager of the creature view, the handles (they keep the assets in memory),
	 *  and the marks. */
	FSwcCreatureLibrary* PendingLibrary = nullptr;
	FStreamableManager* Streamer = nullptr;
	TSharedPtr<FStreamableHandle> LoadHandle;
	TSharedPtr<FStreamableHandle> RestHandle;
	bool bLoading = false;
	bool bReady = false;
	double LoadStart = 0.0;
	double LoadSeconds = 0.0;
	uint64 Serial = 0;
	FString SlicePath;
	FString DisplayName;
	bool bHasModel = false;
	bool bHidesLevel = false;
	bool bLooksApplied = false;
	uint32 AppliedCategory = 0;
	float BodyTop = 180.0f;

	/** The scope records: the revision of the entity that the actor read, and the state of each scope (scope index -> state). */
	uint32 ScopeRevision = 0;
	TMap<uint32, FScopeState> Scopes;

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
