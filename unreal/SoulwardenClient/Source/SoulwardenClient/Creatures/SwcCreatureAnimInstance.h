#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Character/SwcPlayerAnimInstance.h"
#include "SwcCreatureAnimInstance.generated.h"

struct FSwcCreatureAnimations;
struct FSwcCreatureFragment;
class UAnimSequence;

/**
 * The animation of a creature: the Mannequin fragment of the action of the server (the scope record of its movement state,
 * protocol notes 4.14) with the timing of its variant in the animation database (research feel-values.md R3.2.5): the items of
 * its first layer with their start, start time in the clip, blend-in time, speed, and loop flag; the blend spaces with the
 * weight grid of the data (FSwcBlendSpace of the player track). The blend proxy is the one of the player
 * (FSwcPlayerAnimProxy): no animation graph asset. The clips play in place: the server moves a creature.
 */
UCLASS(Transient)
class SOULWARDENCLIENT_API USwcCreatureAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	void SetAnimations(const TSharedPtr<FSwcCreatureAnimations>& Data);

	/**
	 * Starts a fragment. The new fragment blends in over the blend-in time of its first item; the fragments before it share
	 * the rest of the weight.
	 * @param FragmentTime The time of the fragment now (s): a client that sees an action late shows the pose of that time.
	 * @param PlaybackScale The playback scale of the record (1.0 from the backend).
	 * @return False when the animation set has no variant of the fragment (the pose of the fragment before it stays).
	 */
	bool PlayFragment(int32 Fragment, float FragmentTime, float PlaybackScale);

	/** The motion parameters of this frame: MoveSpeed (m/s), TurnSpeed (rad/s, positive to the left), TravelAngle (rad,
	 *  positive to the left). */
	void SetMotion(float InMoveSpeed, float InTurnSpeed, float InTravelAngle);

	int32 GetFragment() const { return Layers.IsEmpty() ? INDEX_NONE : Layers.Last().Fragment; }
	/** @return The time of the newest fragment (s), and its length (-1: it loops). */
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
		/** The time of the fragment (s) and the playback scale of the record. */
		float Time = 0.0f;
		float Scale = 1.0f;
		/** The normalized time of a blend space item (0 to 1 for one cycle). */
		float Phase = 0.0f;
	};

	void AddSamples(const FLayer& Layer, float Weight, TMap<UAnimSequence*, FSwcPoseSample>& Out) const;
	/** Adds the clips of one item of a fragment at a fragment time. */
	void AddItem(const FSwcCreatureFragment& Fragment, int32 Item, const FLayer& Layer, float Time, float Weight,
		TMap<UAnimSequence*, FSwcPoseSample>& Out) const;
	/** @return The clip time of a clip item at a fragment time (a clip of the same name as the item before it continues its time). */
	float ClipTime(const FSwcCreatureFragment& Fragment, int32 Item, float Time, float Scale) const;
	int32 SpaceOfItem(int32 Kind, int32 Index) const;
	float Param(const FString& Name) const;
	float CycleOf(int32 Space) const;

	TSharedPtr<FSwcCreatureAnimations> Animations;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UAnimSequence>> Sequences;
	TArray<FLayer> Layers;
	TArray<FSwcPoseSample> Samples;
	float MoveSpeed = 0.0f;
	float TurnSpeed = 0.0f;
	float TravelAngle = 0.0f;
};
