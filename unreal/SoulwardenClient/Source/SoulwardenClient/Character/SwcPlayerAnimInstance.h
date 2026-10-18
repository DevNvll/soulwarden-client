#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "SwcPlayerAnimInstance.generated.h"

class FSwcLocomotion;
class UAnimSequence;

/** One clip of the pose of a frame: the clip, its time, and its weight. */
struct FSwcPoseSample
{
	TObjectPtr<UAnimSequence> Sequence = nullptr;
	float Time = 0.0f;
	float Weight = 0.0f;
	bool bLoop = false;
};

/** The worker part of the anim instance: it blends the samples of the frame (no animation graph asset). */
struct FSwcPlayerAnimProxy : public FAnimInstanceProxy
{
	FSwcPlayerAnimProxy() = default;
	explicit FSwcPlayerAnimProxy(UAnimInstance* Instance) : FAnimInstanceProxy(Instance) {}

	virtual bool Evaluate(FPoseContext& Output) override;

	TArray<FSwcPoseSample> Samples;
};

/**
 * The animation of the player: the Mannequin fragments of the movement state (Character/SwcLocomotion, research
 * feel-values.md R3) with their blend-in times, the blend spaces with the weight grid of the data, and the motion
 * parameters of the action list with their smoothing (MoveSpeed and TravelAngle 0.1 s, TurnSpeed 0.75 s; research R2.3.3).
 * The clips play in place: the movement data moves the character.
 */
UCLASS(Transient)
class SOULWARDENCLIENT_API USwcPlayerAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	/** Gives the locomotion data of the gender of the body. */
	void SetLocomotion(const TSharedPtr<FSwcLocomotion>& Data);

	/** The samples of the last update. */
	const TArray<FSwcPoseSample>& GetSamples() const { return Samples; }

protected:
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override;

private:
	struct FLayer
	{
		FString Fragment;
		float Weight = 0.0f;
		float BlendIn = 0.0f;
		float Elapsed = 0.0f;
		/** The normalized time of a blend space (0 to 1 for one cycle). */
		float Phase = 0.0f;
	};

	void AddSamples(const FLayer& Layer, TMap<UAnimSequence*, FSwcPoseSample>& Out) const;

	TSharedPtr<FSwcLocomotion> Locomotion;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UAnimSequence>> Sequences;
	TArray<FLayer> Layers;
	TArray<FSwcPoseSample> Samples;
	/** The motion parameters (SmoothCD state: value and velocity). */
	float MoveSpeed = 0.0f;
	float MoveSpeedRate = 0.0f;
	float TravelAngle = 0.0f;
	float TravelAngleRate = 0.0f;
	float TurnSpeed = 0.0f;
	float TurnSpeedRate = 0.0f;
};
