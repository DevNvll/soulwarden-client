#include "Character/SwcPlayerAnimInstance.h"

#include "Animation/AnimSequence.h"
#include "AnimationRuntime.h"
#include "Character/SwcCharacterMovement.h"
#include "Character/SwcLocomotion.h"
#include "Character/SwcSmooth.h"
#include "GameFramework/Character.h"

namespace
{
/** The motion parameter smoothing of the action list component of the player slice [D m_motionParameterSmoothingSettings]:
 *  TravelSpeedConvergeTime and TravelAngleConvergeTime 0.1 s, TurnSpeedConvergeTime 0.75 s, MovementSpeedEpsilon 0.02. */
constexpr float SpeedConvergeTime = 0.1f;
constexpr float AngleConvergeTime = 0.1f;
constexpr float TurnConvergeTime = 0.75f;
constexpr float MovementSpeedEpsilon = 0.02f;
/** Samples with a smaller weight are not evaluated. */
constexpr float MinimumWeight = 0.005f;

} // namespace

bool FSwcPlayerAnimProxy::Evaluate(FPoseContext& Output)
{
	if (Samples.IsEmpty())
	{
		return false;
	}
	const int32 Count = Samples.Num();
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
		const FSwcPoseSample& Sample = Samples[Index];
		Poses[Index].SetBoneContainer(&Output.Pose.GetBoneContainer());
		Curves[Index].InitFrom(Output.Curve);
		FAnimationPoseData Data(Poses[Index], Curves[Index], Attributes[Index]);
		Sample.Sequence->GetAnimationPose(Data, FAnimExtractContext(static_cast<double>(Sample.Time), false, FDeltaTimeRecord(), Sample.bLoop));
		Weights[Index] = Sample.Weight;
	}
	FAnimationPoseData Result(Output);
	FAnimationRuntime::BlendPosesTogether(Poses, Curves, Attributes, Weights, Result);
	return true;
}

FAnimInstanceProxy* USwcPlayerAnimInstance::CreateAnimInstanceProxy()
{
	return new FSwcPlayerAnimProxy(this);
}

void USwcPlayerAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy)
{
	delete static_cast<FSwcPlayerAnimProxy*>(InProxy);
}

void USwcPlayerAnimInstance::SetLocomotion(const TSharedPtr<FSwcLocomotion>& Data)
{
	Locomotion = Data;
	Sequences.Reset();
	Layers.Reset();
	if (!Locomotion.IsValid())
	{
		return;
	}
	for (const FSwcClip& Clip : Locomotion->Clips)
	{
		Sequences.Add(Clip.AssetPath.IsEmpty() ? nullptr : LoadObject<UAnimSequence>(nullptr, *Clip.AssetPath));
	}
}

void USwcPlayerAnimInstance::AddSamples(const FLayer& Layer, TMap<UAnimSequence*, FSwcPoseSample>& Out) const
{
	const FSwcFragment* Fragment = Locomotion->Fragment(Layer.Fragment);
	if (Fragment == nullptr)
	{
		return;
	}
	const auto Add = [this, &Out](int32 Clip, float Time, float Weight, bool bLoop)
	{
		if (!Sequences.IsValidIndex(Clip) || Sequences[Clip] == nullptr || Weight <= 0.0f)
		{
			return;
		}
		FSwcPoseSample& Sample = Out.FindOrAdd(Sequences[Clip]);
		Sample.Sequence = Sequences[Clip];
		// One time for each clip: the layer with the larger weight gives it.
		if (Weight > Sample.Weight)
		{
			Sample.Time = Time;
			Sample.bLoop = bLoop;
		}
		Sample.Weight += Weight;
	};
	if (Fragment->Clip != INDEX_NONE)
	{
		const float Duration = FMath::Max(Locomotion->Clips[Fragment->Clip].Duration, 0.001f);
		const float Time = Layer.Elapsed * Fragment->Speed;
		Add(Fragment->Clip, Fragment->bLoop ? FMath::Fmod(Time, Duration) : FMath::Min(Time, Duration), Layer.Weight, Fragment->bLoop);
		return;
	}
	const FSwcBlendSpace& Space = Locomotion->BlendSpaces[Fragment->BlendSpace];
	TArray<float> Values;
	for (const FSwcBlendSpace::FParam& Param : Space.Params)
	{
		Values.Add(Param.Name == TEXT("MoveSpeed") ? MoveSpeed : Param.Name == TEXT("TravelAngle") ? TravelAngle : Param.Name == TEXT("TurnSpeed") ? TurnSpeed : 0.0f);
	}
	TMap<int32, float> Weights;
	Space.Evaluate(Values, Weights);
	for (const TPair<int32, float>& Example : Weights)
	{
		const int32 Clip = Space.ExampleClips.IsValidIndex(Example.Key) ? Space.ExampleClips[Example.Key] : INDEX_NONE;
		if (Clip == INDEX_NONE)
		{
			continue;
		}
		const float Duration = Locomotion->Clips[Clip].Duration;
		const float Phase = Fragment->bLoop ? FMath::Fmod(Layer.Phase, 1.0f) : FMath::Min(Layer.Phase, 1.0f);
		Add(Clip, Phase * Duration, Layer.Weight * Example.Value, Fragment->bLoop);
	}
}

void USwcPlayerAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);
	const ACharacter* Character = Cast<ACharacter>(TryGetPawnOwner());
	const USwcCharacterMovement* Movement = Character != nullptr ? Cast<USwcCharacterMovement>(Character->GetCharacterMovement()) : nullptr;
	if (!Locomotion.IsValid() || Movement == nullptr)
	{
		Samples.Reset();
		return;
	}
	// The motion parameters: speed (m/s), the direction of the travel to the facing (radians, positive to the left), and
	// the turn speed (rad/s, positive to the left), each with its smoothing.
	const FVector Velocity = Movement->Velocity;
	const float Speed = static_cast<float>(Velocity.Size2D() / 100.0);
	SwcSmoothCD(MoveSpeed, MoveSpeedRate, Speed < MovementSpeedEpsilon ? 0.0f : Speed, SpeedConvergeTime, DeltaSeconds);
	if (Speed >= MovementSpeedEpsilon)
	{
		const FVector Forward = Character->GetActorForwardVector();
		const FVector Left = -Character->GetActorRightVector();
		const float Angle = static_cast<float>(FMath::Atan2(FVector::DotProduct(Velocity, Left), FVector::DotProduct(Velocity, Forward)));
		// The shortest way to the new angle (the parameter goes from -pi to pi).
		const float Target = TravelAngle + FMath::FindDeltaAngleRadians(TravelAngle, Angle);
		SwcSmoothCD(TravelAngle, TravelAngleRate, Target, AngleConvergeTime, DeltaSeconds);
		TravelAngle = FMath::UnwindRadians(TravelAngle);
	}
	SwcSmoothCD(TurnSpeed, TurnSpeedRate, Movement->GetTurnSpeed(), TurnConvergeTime, DeltaSeconds);

	// The fragment of the movement state, with the blend-in time of the data.
	const FString Fragment = Movement->GetFragment();
	if ((Layers.IsEmpty() || Layers.Last().Fragment != Fragment) && Locomotion->Fragment(Fragment) != nullptr)
	{
		FLayer& Layer = Layers.AddDefaulted_GetRef();
		Layer.Fragment = Fragment;
		Layer.BlendIn = Locomotion->Fragment(Fragment)->BlendIn;
		Layer.Weight = Layers.Num() == 1 ? 1.0f : 0.0f;
	}
	if (Layers.IsEmpty())
	{
		Samples.Reset();
		return;
	}
	FLayer& Top = Layers.Last();
	Top.Weight = Top.BlendIn <= 0.001f ? 1.0f : FMath::Min(1.0f, Top.Weight + DeltaSeconds / Top.BlendIn);
	float Others = 0.0f;
	for (int32 Index = 0; Index < Layers.Num() - 1; ++Index)
	{
		Others += Layers[Index].Weight;
	}
	for (int32 Index = Layers.Num() - 2; Index >= 0; --Index)
	{
		Layers[Index].Weight = Others > 0.0f ? Layers[Index].Weight / Others * (1.0f - Top.Weight) : 0.0f;
		if (Layers[Index].Weight < 0.001f)
		{
			Layers.RemoveAt(Index);
		}
	}
	// The times: a clip from its start; a blend space with one cycle for its examples (the weighted cycle length of the
	// examples: the clips of different lengths stay in step).
	for (FLayer& Layer : Layers)
	{
		Layer.Elapsed += DeltaSeconds;
		const FSwcFragment* Data = Locomotion->Fragment(Layer.Fragment);
		if (Data == nullptr || Data->BlendSpace == INDEX_NONE)
		{
			continue;
		}
		const FSwcBlendSpace& Space = Locomotion->BlendSpaces[Data->BlendSpace];
		TArray<float> Values;
		for (const FSwcBlendSpace::FParam& Param : Space.Params)
		{
			Values.Add(Param.Name == TEXT("MoveSpeed") ? MoveSpeed : Param.Name == TEXT("TravelAngle") ? TravelAngle : Param.Name == TEXT("TurnSpeed") ? TurnSpeed : 0.0f);
		}
		TMap<int32, float> Weights;
		Space.Evaluate(Values, Weights);
		float Cycle = 0.0f;
		float Total = 0.0f;
		for (const TPair<int32, float>& Example : Weights)
		{
			const int32 Clip = Space.ExampleClips.IsValidIndex(Example.Key) ? Space.ExampleClips[Example.Key] : INDEX_NONE;
			if (Clip != INDEX_NONE)
			{
				Cycle += Example.Value * Locomotion->Clips[Clip].Duration / FMath::Max(Space.ExampleScales[Example.Key], 0.01f);
				Total += Example.Value;
			}
		}
		if (Total > 0.0f && Cycle > 0.0f)
		{
			Layer.Phase += DeltaSeconds * Data->Speed / (Cycle / Total);
		}
	}
	TMap<UAnimSequence*, FSwcPoseSample> Merged;
	for (const FLayer& Layer : Layers)
	{
		AddSamples(Layer, Merged);
	}
	Samples.Reset();
	float Sum = 0.0f;
	for (const TPair<UAnimSequence*, FSwcPoseSample>& Pair : Merged)
	{
		if (Pair.Value.Weight >= MinimumWeight)
		{
			Samples.Add(Pair.Value);
			Sum += Pair.Value.Weight;
		}
	}
	for (FSwcPoseSample& Sample : Samples)
	{
		Sample.Weight /= Sum;
	}
	// The evaluation runs after this update (on a worker thread): the proxy keeps a copy.
	GetProxyOnGameThread<FSwcPlayerAnimProxy>().Samples = Samples;
}
