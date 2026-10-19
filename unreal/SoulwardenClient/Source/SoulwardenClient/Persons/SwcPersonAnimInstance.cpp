#include "Persons/SwcPersonAnimInstance.h"

#include "Animation/AnimSequence.h"
#include "AnimationRuntime.h"

namespace
{
/** A clip below this weight goes away (the value of the creature view). */
constexpr float ClipMinimum = 0.001f;
} // namespace

bool FSwcPersonAnimProxy::Evaluate(FPoseContext& Output)
{
	const int32 Count = Samples.Num();
	if (Count == 0)
	{
		return false;
	}
	TArray<FCompactPose> Poses;
	TArray<FBlendedCurve> Curves;
	TArray<UE::Anim::FStackAttributeContainer> Attributes;
	TArray<float> Weights;
	Poses.SetNum(Count);
	Curves.SetNum(Count);
	Attributes.SetNum(Count);
	Weights.SetNum(Count);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FSwcPersonClipSample& Sample = Samples[Index];
		Poses[Index].SetBoneContainer(&Output.Pose.GetBoneContainer());
		Curves[Index].InitFrom(Output.Curve);
		FAnimationPoseData Data(Poses[Index], Curves[Index], Attributes[Index]);
		Sample.Sequence->GetAnimationPose(Data, FAnimExtractContext(static_cast<double>(Sample.Time), false, FDeltaTimeRecord(), Sample.bLoop));
		Weights[Index] = Sample.Weight;
	}
	UE::Anim::FStackAttributeContainer OutAttributes;
	FAnimationPoseData Result(Output.Pose, Output.Curve, OutAttributes);
	FAnimationRuntime::BlendPosesTogether(Poses, Curves, Attributes, Weights, Result);
	return true;
}

void USwcPersonAnimInstance::PlayClip(UAnimSequence* Sequence, bool bLoop, float Speed, float BlendIn)
{
	if (Sequence == nullptr)
	{
		return;
	}
	Held.AddUnique(Sequence);
	FClip Clip;
	Clip.Sequence = Sequence;
	Clip.bLoop = bLoop;
	Clip.Speed = Speed > 0.0f ? Speed : 1.0f;
	Clip.BlendIn = BlendIn;
	Clip.Weight = Clips.IsEmpty() || BlendIn <= 0.001f ? 1.0f : 0.0f;
	if (Clip.Weight >= 1.0f)
	{
		Clips.Reset();
	}
	Clips.Add(Clip);
}

float USwcPersonAnimInstance::GetClipLength() const
{
	return Clips.IsEmpty() || Clips.Last().Sequence == nullptr ? 0.0f : Clips.Last().Sequence->GetPlayLength();
}

void USwcPersonAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);
	FSwcPersonAnimProxy& Proxy = GetProxyOnGameThread<FSwcPersonAnimProxy>();
	Proxy.Samples.Reset();
	if (Clips.IsEmpty())
	{
		return;
	}
	// The weights: the newest clip goes to 1 in its blend-in time, the clips before it share the rest.
	FClip& Top = Clips.Last();
	Top.Weight = Top.BlendIn <= 0.001f ? 1.0f : FMath::Min(1.0f, Top.Weight + DeltaSeconds / Top.BlendIn);
	float Others = 0.0f;
	for (int32 Index = 0; Index < Clips.Num() - 1; ++Index)
	{
		Others += Clips[Index].Weight;
	}
	for (int32 Index = Clips.Num() - 2; Index >= 0; --Index)
	{
		Clips[Index].Weight = Others > 0.0f ? Clips[Index].Weight / Others * (1.0f - Top.Weight) : 0.0f;
		if (Clips[Index].Weight < ClipMinimum)
		{
			Clips.RemoveAt(Index);
		}
	}
	for (FClip& Clip : Clips)
	{
		const float Length = Clip.Sequence->GetPlayLength();
		Clip.Time += DeltaSeconds * Clip.Speed;
		// A loop wraps; a clip with no loop holds its last frame.
		Clip.Time = Clip.bLoop && Length > 0.0f ? FMath::Fmod(Clip.Time, Length) : FMath::Min(Clip.Time, Length);
		FSwcPersonClipSample Sample;
		Sample.Sequence = Clip.Sequence;
		Sample.Time = Clip.Time;
		Sample.Weight = Clip.Weight;
		Sample.bLoop = Clip.bLoop;
		Proxy.Samples.Add(Sample);
	}
}

FAnimInstanceProxy* USwcPersonAnimInstance::CreateAnimInstanceProxy()
{
	return new FSwcPersonAnimProxy(this);
}

void USwcPersonAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy)
{
	delete static_cast<FSwcPersonAnimProxy*>(InProxy);
}
