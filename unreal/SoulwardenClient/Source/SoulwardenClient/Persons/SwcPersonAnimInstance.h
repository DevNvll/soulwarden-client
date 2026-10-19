#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"

#include "SwcPersonAnimInstance.generated.h"

class UAnimSequence;

/** One clip of a person in a frame: the clip, its time, its weight, its loop flag. */
struct FSwcPersonClipSample
{
	TObjectPtr<UAnimSequence> Sequence = nullptr;
	float Time = 0.0f;
	float Weight = 0.0f;
	bool bLoop = false;
};

/** The worker part of the anim instance: it blends the clips of the frame (no animation graph asset). */
struct FSwcPersonAnimProxy : public FAnimInstanceProxy
{
	FSwcPersonAnimProxy() = default;
	explicit FSwcPersonAnimProxy(UAnimInstance* Instance) : FAnimInstanceProxy(Instance) {}
	virtual bool Evaluate(FPoseContext& Output) override;

	TArray<FSwcPersonClipSample> Samples;
};

/**
 * The animation of a person (stage P4.8, docs/PERSONS.md): one clip of a timeline at a time (the PlayAnim layer of the idle, greet,
 * interact, and bye timelines), with the blend-in time of the clip of the timeline: the new clip goes to the weight 1 in that time,
 * the clips before it share the rest. A clip with no loop holds its last frame until the next clip.
 */
UCLASS(Transient)
class SOULWARDENCLIENT_API USwcPersonAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	/** Starts a clip with its speed (the playbackSpeed of the timeline) and its blend-in time (s; 0: at once). */
	void PlayClip(UAnimSequence* Sequence, bool bLoop, float Speed, float BlendIn);
	/** @return The time of the current clip (s), and the length of the current clip (s; 0 with no clip). */
	float GetClipTime() const { return Clips.IsEmpty() ? 0.0f : Clips.Last().Time; }
	float GetClipLength() const;

protected:
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override;

private:
	struct FClip
	{
		TObjectPtr<UAnimSequence> Sequence = nullptr;
		float Time = 0.0f;
		float Speed = 1.0f;
		float BlendIn = 0.0f;
		float Weight = 0.0f;
		bool bLoop = false;
	};
	TArray<FClip> Clips;
	/** The clips that the person played: a reference for the garbage collector. */
	UPROPERTY()
	TArray<TObjectPtr<UAnimSequence>> Held;
};
