#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "SwcCreatureAnimInstance.generated.h"

struct FSwcCreatureAnimations;
struct FSwcCreatureFragment;
struct FSwcFragmentItem;
class UAnimSequence;

/** One clip of a pose layer of a frame: the clip, its time, its weight. */
struct FSwcCreatureClipSample
{
	TObjectPtr<UAnimSequence> Sequence = nullptr;
	float Time = 0.0f;
	float Weight = 0.0f;
	bool bLoop = false;
};

/**
 * A pose layer after the base pose: the blend of its clips changes only the bones that its clips animate (a layer after the
 * first one of a variant: a hand pose, an arm blend space), or adds its rotations to the base pose (an additive clip: its keys
 * are differences from the identity; research feel-values.md R3.2.9).
 */
struct FSwcCreaturePoseLayer
{
	TArray<FSwcCreatureClipSample> Samples;
	/** The bones of the layer: mesh bone indexes of the skeleton mesh. */
	TArray<int32> MeshBones;
	float Weight = 0.0f;
	bool bAdditive = false;
};

/** The worker part of the anim instance: it blends the base clips, then the layers (no animation graph asset). */
struct FSwcCreatureAnimProxy : public FAnimInstanceProxy
{
	FSwcCreatureAnimProxy() = default;
	explicit FSwcCreatureAnimProxy(UAnimInstance* Instance) : FAnimInstanceProxy(Instance) {}

	virtual bool Evaluate(FPoseContext& Output) override;

	TArray<FSwcCreatureClipSample> Base;
	TArray<FSwcCreaturePoseLayer> Layers;
	/** The time of the last line of -SwcAdditiveCheck of this creature (AST-197). */
	double LastAdditiveCheck = 0.0;
};

/**
 * The animation of a creature: the Mannequin fragment of the action of the server (the scope record of its movement state,
 * protocol notes 4.14) with the timing of its variant in the animation database (research feel-values.md R3.2.5): the items of
 * each layer with their start, start time in the clip, blend-in time, speed, and loop flag; the blend spaces with the weight
 * grid of the data (FSwcBlendSpace of the player track). Layer 1 of a variant and the later ones change only the bones of
 * their clips. The records of the other scopes (the additive reaction of scope 4 of the wolf) play as overlays. The clips play
 * in place: the server moves a creature.
 */
UCLASS(Transient)
class SOULWARDENCLIENT_API USwcCreatureAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	/** @param ReferenceBones The bone names of the skeleton mesh in the order of its mesh bone indexes. */
	void SetAnimations(const TSharedPtr<FSwcCreatureAnimations>& Data);
	/** @return The second object path of a clip: Interchange names some clips STEM + "Null" + NAME (101 of 5,097 clips of the world). */
	static FString AlternateClipPath(const FString& AssetPath);
	/** Finds the clips that were not in memory at SetAnimations (they load after the first ones: ASwcCreature::Setup). */
	void RefreshClips();

	/**
	 * Starts a fragment of scope 0. The new fragment blends in over the blend-in time of its first item; the fragments before it
	 * share the rest of the weight.
	 * @param FragmentTime The time of the fragment now (s): a client that sees an action late shows the pose of that time.
	 * @param PlaybackScale The playback scale of the record (1.0 from the backend).
	 * @return False when the animation set has no variant of the fragment (the pose of the fragment before it stays).
	 */
	bool PlayFragment(int32 Fragment, float FragmentTime, float PlaybackScale);

	/** Starts a fragment in a different scope (an overlay over the pose of scope 0), or stops the overlay of a scope (Fragment
	 *  INDEX_NONE: it blends out). */
	bool PlayOverlay(int32 Scope, int32 Fragment, float FragmentTime, float PlaybackScale);

	/** Stops each fragment and overlay: the mesh shows its reference pose (the weapon scope with no fragment). */
	void Stop();

	/** The motion parameters of this frame: MoveSpeed (m/s), TurnSpeed (rad/s, positive to the left), TravelAngle (rad,
	 *  positive to the left). */
	void SetMotion(float InMoveSpeed, float InTurnSpeed, float InTravelAngle);

	int32 GetFragment() const { return Layers.IsEmpty() ? INDEX_NONE : Layers.Last().Fragment; }
	float GetFragmentTime() const { return Layers.IsEmpty() ? 0.0f : Layers.Last().Time; }

protected:
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override;

private:
	struct FLayer
	{
		int32 Fragment = INDEX_NONE;
		float Weight = 0.0f;
		float BlendIn = 0.0f;
		/** The time of the fragment (s, scaled by the playback scale). */
		float Time = 0.0f;
		float Scale = 1.0f;
		/** The normalized time of a blend space item (0 to 1 for one cycle). */
		float Phase = 0.0f;
	};

	struct FOverlay
	{
		int32 Scope = 0;
		FLayer Layer;
		/** The weight of the overlay: it goes to 1 in the blend-in time, and to 0 in the blend-out time at its end. */
		float Weight = 0.0f;
		bool bStopping = false;
	};

	/** The clips of one item list (a layer of a variant) at a fragment time, into a sample map. */
	void AddItems(const FSwcCreatureFragment& Fragment, const TArray<FSwcFragmentItem>& Items, const FLayer& Layer, float Weight,
		TMap<UAnimSequence*, FSwcCreatureClipSample>& Out, TSet<int32>* OutClips) const;
	void AddItem(const FSwcCreatureFragment& Fragment, const TArray<FSwcFragmentItem>& Items, int32 Item, const FLayer& Layer, float Time,
		float Weight, TMap<UAnimSequence*, FSwcCreatureClipSample>& Out, TSet<int32>* OutClips) const;
	float ClipTime(const TArray<FSwcFragmentItem>& Items, int32 Item, float Time) const;
	int32 SpaceOfItem(int32 Kind, int32 Index) const;
	float Param(const FString& Name) const;
	float CycleOf(int32 Space) const;
	void AdvanceLayer(FLayer& Layer, float DeltaSeconds) const;
	/** Adds the pose layers of the layers after the first one of a fragment instance, and of an overlay. */
	void AddPoseLayers(const FLayer& Layer, float Weight, bool bWholeFragment, TArray<FSwcCreaturePoseLayer>& Out) const;
	static TArray<FSwcCreatureClipSample> Normalize(TMap<UAnimSequence*, FSwcCreatureClipSample>& Merged);

	TSharedPtr<FSwcCreatureAnimations> Animations;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UAnimSequence>> Sequences;
	/** The mesh bones of each clip (the bones that it animates), and its additive mark. */
	TArray<TArray<int32>> ClipMeshBones;
	TArray<bool> ClipAdditive;
	TArray<FLayer> Layers;
	TArray<FOverlay> Overlays;
	float MoveSpeed = 0.0f;
	float TurnSpeed = 0.0f;
	float TravelAngle = 0.0f;
};
