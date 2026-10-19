#include "Creatures/SwcCreatureAnimInstance.h"

#include "Animation/AnimSequence.h"
#include "AnimationRuntime.h"
#include "Components/SkeletalMeshComponent.h"
#include "Creatures/SwcCreatureData.h"
#include "Creatures/SwcCreatureSubsystem.h"
#include "Engine/SkeletalMesh.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace
{
/** Samples with a smaller weight are not evaluated (the value of the player animation). */
constexpr float MinimumWeight = 0.005f;
/** A layer below this weight goes away. */
constexpr float LayerMinimum = 0.001f;

/** -SwcAdditiveCheck (AST-197): a log line with the largest rotation of an additive layer and its bone, at most each 0.25 s for each
 * creature. */
bool AdditiveCheck()
{
	static const bool bOn = FParse::Param(FCommandLine::Get(), TEXT("SwcAdditiveCheck"));
	return bOn;
}

/** Blends the samples of one list into a pose (the weights add up to 1). @return False with no sample. */
bool BlendSamples(const TArray<FSwcCreatureClipSample>& Samples, const FPoseContext& Output, FCompactPose& OutPose, FBlendedCurve& OutCurve,
	UE::Anim::FStackAttributeContainer& OutAttributes)
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
		const FSwcCreatureClipSample& Sample = Samples[Index];
		Poses[Index].SetBoneContainer(&Output.Pose.GetBoneContainer());
		Curves[Index].InitFrom(Output.Curve);
		FAnimationPoseData Data(Poses[Index], Curves[Index], Attributes[Index]);
		Sample.Sequence->GetAnimationPose(Data, FAnimExtractContext(static_cast<double>(Sample.Time), false, FDeltaTimeRecord(), Sample.bLoop));
		Weights[Index] = Sample.Weight;
	}
	OutPose.SetBoneContainer(&Output.Pose.GetBoneContainer());
	OutCurve.InitFrom(Output.Curve);
	FAnimationPoseData Result(OutPose, OutCurve, OutAttributes);
	FAnimationRuntime::BlendPosesTogether(Poses, Curves, Attributes, Weights, Result);
	return true;
}
} // namespace

bool FSwcCreatureAnimProxy::Evaluate(FPoseContext& Output)
{
	if (Base.IsEmpty())
	{
		return false;
	}
	{
		FCompactPose Pose;
		FBlendedCurve Curve;
		UE::Anim::FStackAttributeContainer Attributes;
		if (!BlendSamples(Base, Output, Pose, Curve, Attributes))
		{
			return false;
		}
		Output.Pose = Pose;
		Output.Curve = Curve;
	}
	const FBoneContainer& Bones = Output.Pose.GetBoneContainer();
	for (const FSwcCreaturePoseLayer& Layer : Layers)
	{
		if (Layer.Weight <= 0.0f || Layer.Samples.IsEmpty() || Layer.MeshBones.IsEmpty())
		{
			continue;
		}
		FCompactPose Pose;
		FBlendedCurve Curve;
		UE::Anim::FStackAttributeContainer Attributes;
		if (!BlendSamples(Layer.Samples, Output, Pose, Curve, Attributes))
		{
			continue;
		}
		float LargestAngle = 0.0f;
		int32 LargestBone = INDEX_NONE;
		for (const int32 MeshBone : Layer.MeshBones)
		{
			const FCompactPoseBoneIndex Index = Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshBone));
			if (!Index.IsValid())
			{
				continue;
			}
			FTransform& Target = Output.Pose[Index];
			const FTransform& Value = Pose[Index];
			if (Layer.bAdditive)
			{
				// The keys of an additive clip are differences from the identity: its rotation (with the weight of the layer) goes
				// on top of the rotation of the pose. The translations of such clips are 0 or not keyed: they are not added.
				const FQuat Delta = FQuat::Slerp(FQuat::Identity, Value.GetRotation(), Layer.Weight);
				Target.SetRotation((Delta * Target.GetRotation()).GetNormalized());
				if (const float Angle = FMath::RadiansToDegrees(Delta.GetAngle()); Angle > LargestAngle)
				{
					LargestAngle = Angle;
					LargestBone = MeshBone;
				}
			}
			else
			{
				Target.BlendWith(Value, Layer.Weight);
			}
		}
		if (Layer.bAdditive && AdditiveCheck() && LargestBone != INDEX_NONE)
		{
			const double Now = FPlatformTime::Seconds();
			if (Now - LastAdditiveCheck >= 0.25)
			{
				LastAdditiveCheck = Now;
				UE_LOG(LogSwcCreatures, Log, TEXT("creature: additive check: the largest rotation %.1f degrees on %s (layer weight %.2f, %d bones)"),
					LargestAngle, *Bones.GetReferenceSkeleton().GetBoneName(LargestBone).ToString(), Layer.Weight, Layer.MeshBones.Num());
			}
		}
	}
	return true;
}

FAnimInstanceProxy* USwcCreatureAnimInstance::CreateAnimInstanceProxy()
{
	return new FSwcCreatureAnimProxy(this);
}

void USwcCreatureAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy)
{
	delete static_cast<FSwcCreatureAnimProxy*>(InProxy);
}

void USwcCreatureAnimInstance::RefreshClips()
{
	if (!Animations.IsValid())
	{
		return;
	}
	for (int32 ClipIndex = 0; ClipIndex < Animations->Clips.Num() && ClipIndex < Sequences.Num(); ++ClipIndex)
	{
		const FString& Path = Animations->Clips[ClipIndex].AssetPath;
		if (Sequences[ClipIndex] != nullptr || Path.IsEmpty())
		{
			continue;
		}
		UAnimSequence* Sequence = FindObject<UAnimSequence>(nullptr, *Path);
		if (Sequence == nullptr)
		{
			const FString Other = AlternateClipPath(Path);
			Sequence = Other.IsEmpty() ? nullptr : FindObject<UAnimSequence>(nullptr, *Other);
		}
		Sequences[ClipIndex] = Sequence;
	}
}

FString USwcCreatureAnimInstance::AlternateClipPath(const FString& AssetPath)
{
	FString Folder, Name;
	AssetPath.Split(TEXT("/"), &Folder, &Name, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
	FString Package, Object;
	Name.Split(TEXT("."), &Package, &Object);
	const int32 Cut = Package.Find(TEXT("_"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
	if (Cut <= 0)
	{
		return FString();
	}
	const FString Other = Package.Left(Cut) + TEXT("Null") + Package.Mid(Cut);
	return Folder + TEXT("/") + Other + TEXT(".") + Other;
}

void USwcCreatureAnimInstance::SetAnimations(const TSharedPtr<FSwcCreatureAnimations>& Data)
{
	Animations = Data;
	Sequences.Reset();
	ClipMeshBones.Reset();
	ClipAdditive.Reset();
	Layers.Reset();
	Overlays.Reset();
	if (!Animations.IsValid())
	{
		return;
	}
	const USkeletalMeshComponent* Mesh = GetSkelMeshComponent();
	const USkeletalMesh* Asset = Mesh != nullptr ? Mesh->GetSkeletalMeshAsset() : nullptr;
	for (int32 ClipIndex = 0; ClipIndex < Animations->Clips.Num(); ++ClipIndex)
	{
		const FSwcClip& Clip = Animations->Clips[ClipIndex];
		// The clips are in memory (the async load of ASwcCreature::Setup): a find, not a load (a load would wait for the async loads).
		UAnimSequence* Sequence = Clip.AssetPath.IsEmpty() ? nullptr : FindObject<UAnimSequence>(nullptr, *Clip.AssetPath);
		if (Sequence == nullptr && !Clip.AssetPath.IsEmpty())
		{
			const FString Other = AlternateClipPath(Clip.AssetPath);
			Sequence = Other.IsEmpty() ? nullptr : FindObject<UAnimSequence>(nullptr, *Other);
		}
		Sequences.Add(Sequence);
		TArray<int32>& MeshBones = ClipMeshBones.AddDefaulted_GetRef();
		if (Asset != nullptr && Animations->ClipBones.IsValidIndex(ClipIndex))
		{
			for (const FName& Bone : Animations->ClipBones[ClipIndex])
			{
				const int32 Index = Asset->GetRefSkeleton().FindBoneIndex(Bone);
				if (Index != INDEX_NONE)
				{
					MeshBones.Add(Index);
				}
			}
		}
		ClipAdditive.Add(Animations->ClipAdditive.IsValidIndex(ClipIndex) && Animations->ClipAdditive[ClipIndex]);
	}
}

bool USwcCreatureAnimInstance::PlayFragment(int32 Fragment, float FragmentTime, float PlaybackScale)
{
	const FSwcCreatureFragment* Data = Animations.IsValid() ? Animations->Fragment(Fragment) : nullptr;
	if (Data == nullptr || Data->Items.IsEmpty())
	{
		return false;
	}
	FLayer& Layer = Layers.AddDefaulted_GetRef();
	Layer.Fragment = Fragment;
	// The blend-in time of the fragment: the Duration of the Blend of its first item.
	Layer.BlendIn = Data->Items[0].BlendIn;
	// The playback scale of the record scales the whole fragment (its items and their clips).
	Layer.Scale = PlaybackScale > 0.0f ? PlaybackScale : 1.0f;
	Layer.Time = FMath::Max(FragmentTime, 0.0f) * Layer.Scale;
	// A fragment that the client sees late is already blended in.
	Layer.Weight = Layers.Num() == 1 || Layer.BlendIn <= 0.001f || Layer.Time >= Layer.BlendIn ? 1.0f : Layer.Time / Layer.BlendIn;
	return true;
}

bool USwcCreatureAnimInstance::PlayOverlay(int32 Scope, int32 Fragment, float FragmentTime, float PlaybackScale)
{
	for (FOverlay& Overlay : Overlays)
	{
		if (Overlay.Scope == Scope)
		{
			Overlay.bStopping = true;
		}
	}
	if (Fragment == INDEX_NONE)
	{
		return true;
	}
	const FSwcCreatureFragment* Data = Animations.IsValid() ? Animations->Fragment(Fragment) : nullptr;
	if (Data == nullptr || Data->Items.IsEmpty())
	{
		return false;
	}
	FOverlay& Overlay = Overlays.AddDefaulted_GetRef();
	Overlay.Scope = Scope;
	Overlay.Layer.Fragment = Fragment;
	Overlay.Layer.BlendIn = Data->Items[0].BlendIn;
	Overlay.Layer.Scale = PlaybackScale > 0.0f ? PlaybackScale : 1.0f;
	Overlay.Layer.Time = FMath::Max(FragmentTime, 0.0f) * Overlay.Layer.Scale;
	Overlay.Weight = Overlay.Layer.BlendIn <= 0.001f ? 1.0f : FMath::Min(1.0f, Overlay.Layer.Time / Overlay.Layer.BlendIn);
	return true;
}

void USwcCreatureAnimInstance::Stop()
{
	Layers.Reset();
	Overlays.Reset();
}

void USwcCreatureAnimInstance::SetMotion(float InMoveSpeed, float InTurnSpeed, float InTravelAngle)
{
	MoveSpeed = InMoveSpeed;
	TurnSpeed = InTurnSpeed;
	TravelAngle = InTravelAngle;
}

float USwcCreatureAnimInstance::Param(const FString& Name) const
{
	// The parameters of the creature blend spaces (research feel-values.md R3.2.7): the server sends none; the client takes
	// them from the motion of the creature. TravelSlope is 0 (no slope measure yet); DesiredFacing is the turn speed sign.
	if (Name == TEXT("MoveSpeed"))
	{
		return MoveSpeed;
	}
	if (Name == TEXT("TurnSpeed"))
	{
		return TurnSpeed;
	}
	if (Name == TEXT("TravelAngle"))
	{
		return TravelAngle;
	}
	if (Name == TEXT("DesiredFacing") || Name == TEXT("TurnAngle"))
	{
		return TurnSpeed;
	}
	return 0.0f;
}

int32 USwcCreatureAnimInstance::SpaceOfItem(int32 Kind, int32 Index) const
{
	if (Kind == static_cast<int32>(FSwcFragmentItem::EKind::BlendSpace))
	{
		return Animations->BlendSpaces.IsValidIndex(Index) ? Index : INDEX_NONE;
	}
	if (Kind == static_cast<int32>(FSwcFragmentItem::EKind::Comb) && Animations->Combs.IsValidIndex(Index))
	{
		// A combination chooses one of its blend spaces by its parameter: the first one covers 0 to its maximum (to the left),
		// the second one its minimum to 0 (research R3.2.7, the wolf turn).
		const FSwcComb& Comb = Animations->Combs[Index];
		const int32 Choice = Comb.Spaces.Num() > 1 && Param(Comb.Param) < 0.0f ? 1 : 0;
		return Comb.Spaces.IsValidIndex(Choice) && Animations->BlendSpaces.IsValidIndex(Comb.Spaces[Choice]) ? Comb.Spaces[Choice] : INDEX_NONE;
	}
	return INDEX_NONE;
}

float USwcCreatureAnimInstance::CycleOf(int32 Space) const
{
	const FSwcBlendSpace& Data = Animations->BlendSpaces[Space];
	TArray<float> Values;
	for (const FSwcBlendSpace::FParam& Parameter : Data.Params)
	{
		Values.Add(Param(Parameter.Name));
	}
	TMap<int32, float> Weights;
	Data.Evaluate(Values, Weights);
	float Cycle = 0.0f;
	float Total = 0.0f;
	for (const TPair<int32, float>& Example : Weights)
	{
		const int32 Clip = Data.ExampleClips.IsValidIndex(Example.Key) ? Data.ExampleClips[Example.Key] : INDEX_NONE;
		if (Clip != INDEX_NONE)
		{
			// One phase for all examples: the weighted mean of their durations divided by their playback scale (the rule of
			// the player locomotion).
			Cycle += Example.Value * Animations->Clips[Clip].Duration / FMath::Max(Data.ExampleScales[Example.Key], 0.01f);
			Total += Example.Value;
		}
	}
	return Total > 0.0f && Cycle > 0.0f ? Cycle / Total : 0.0f;
}

float USwcCreatureAnimInstance::ClipTime(const TArray<FSwcFragmentItem>& Items, int32 Item, float Time) const
{
	const FSwcFragmentItem& Data = Items[Item];
	const FSwcClip& Clip = Animations->Clips[Data.Index];
	float Value = Data.ClipStart + (Time - Data.Start) * Data.Speed;
	// A clip of the same name as the clip before it continues the time of that clip (research R3.2.5).
	if (Item > 0)
	{
		const FSwcFragmentItem& Before = Items[Item - 1];
		if (Before.Kind == FSwcFragmentItem::EKind::Clip && Before.Index == Data.Index)
		{
			Value = Value - Data.ClipStart + ClipTime(Items, Item - 1, Data.Start);
		}
	}
	const float Duration = FMath::Max(Clip.Duration, 0.001f);
	return Data.bLoop ? FMath::Fmod(FMath::Max(Value, 0.0f), Duration) : FMath::Clamp(Value, 0.0f, Duration);
}

void USwcCreatureAnimInstance::AddItem(const FSwcCreatureFragment& Fragment, const TArray<FSwcFragmentItem>& Items, int32 Item,
	const FLayer& Layer, float Time, float Weight, TMap<UAnimSequence*, FSwcCreatureClipSample>& Out, TSet<int32>* OutClips) const
{
	const auto Add = [this, &Out, OutClips](int32 Clip, float Value, float SampleWeight, bool bLoop)
	{
		if (!Sequences.IsValidIndex(Clip) || Sequences[Clip] == nullptr || SampleWeight <= 0.0f)
		{
			return;
		}
		if (OutClips != nullptr)
		{
			OutClips->Add(Clip);
		}
		FSwcCreatureClipSample& Sample = Out.FindOrAdd(Sequences[Clip]);
		Sample.Sequence = Sequences[Clip];
		if (SampleWeight > Sample.Weight)
		{
			Sample.Time = Value;
			Sample.bLoop = bLoop;
		}
		Sample.Weight += SampleWeight;
	};
	const FSwcFragmentItem& Data = Items[Item];
	if (Data.Kind == FSwcFragmentItem::EKind::Clip)
	{
		Add(Data.Index, ClipTime(Items, Item, Time), Weight * FMath::Max(Data.Weight, 0.0f), Data.bLoop);
		return;
	}
	const int32 Space = SpaceOfItem(static_cast<int32>(Data.Kind), Data.Index);
	if (Space == INDEX_NONE)
	{
		return;
	}
	const FSwcBlendSpace& Blend = Animations->BlendSpaces[Space];
	TArray<float> Values;
	for (const FSwcBlendSpace::FParam& Parameter : Blend.Params)
	{
		Values.Add(Param(Parameter.Name));
	}
	TMap<int32, float> Weights;
	Blend.Evaluate(Values, Weights);
	for (const TPair<int32, float>& Example : Weights)
	{
		const int32 Clip = Blend.ExampleClips.IsValidIndex(Example.Key) ? Blend.ExampleClips[Example.Key] : INDEX_NONE;
		if (Clip == INDEX_NONE)
		{
			continue;
		}
		const float Phase = Data.bLoop ? FMath::Fmod(Layer.Phase, 1.0f) : FMath::Min(Layer.Phase, 1.0f);
		Add(Clip, Phase * Animations->Clips[Clip].Duration, Weight * Example.Value * FMath::Max(Data.Weight, 0.0f), Data.bLoop);
	}
}

void USwcCreatureAnimInstance::AddItems(const FSwcCreatureFragment& Fragment, const TArray<FSwcFragmentItem>& Items, const FLayer& Layer,
	float Weight, TMap<UAnimSequence*, FSwcCreatureClipSample>& Out, TSet<int32>* OutClips) const
{
	if (Items.IsEmpty())
	{
		return;
	}
	// The item of the fragment time: the last item that started. Its blend-in crosses from the item before it. The end of the
	// layer (an empty animation) keeps the pose of the item before it at the time of the end: the client keeps the last pose
	// of an action of the server after its fragment (protocol notes 4.14, UT2-2); the server then sends its idle record.
	int32 Current = 0;
	for (int32 Index = 1; Index < Items.Num(); ++Index)
	{
		if (Items[Index].Start <= Layer.Time)
		{
			Current = Index;
		}
	}
	const auto Playable = [&Items](int32 Index)
	{ return Items[Index].Kind != FSwcFragmentItem::EKind::End && Items[Index].Kind != FSwcFragmentItem::EKind::Missing; };
	if (!Playable(Current))
	{
		int32 Before = Current - 1;
		while (Before >= 0 && !Playable(Before))
		{
			--Before;
		}
		if (Before >= 0)
		{
			AddItem(Fragment, Items, Before, Layer, Items[Current].Kind == FSwcFragmentItem::EKind::End ? Items[Current].Start : Layer.Time, Weight,
				Out, OutClips);
		}
		return;
	}
	const float BlendIn = Items[Current].BlendIn;
	const float Alpha = Current == 0 || BlendIn <= 0.001f ? 1.0f : FMath::Clamp((Layer.Time - Items[Current].Start) / BlendIn, 0.0f, 1.0f);
	if (Alpha < 1.0f && Current > 0 && Playable(Current - 1))
	{
		AddItem(Fragment, Items, Current - 1, Layer, Layer.Time, Weight * (1.0f - Alpha), Out, OutClips);
	}
	AddItem(Fragment, Items, Current, Layer, Layer.Time, Weight * Alpha, Out, OutClips);
}

TArray<FSwcCreatureClipSample> USwcCreatureAnimInstance::Normalize(TMap<UAnimSequence*, FSwcCreatureClipSample>& Merged)
{
	TArray<FSwcCreatureClipSample> Out;
	float Sum = 0.0f;
	for (const TPair<UAnimSequence*, FSwcCreatureClipSample>& Pair : Merged)
	{
		if (Pair.Value.Weight >= MinimumWeight)
		{
			Out.Add(Pair.Value);
			Sum += Pair.Value.Weight;
		}
	}
	for (FSwcCreatureClipSample& Sample : Out)
	{
		Sample.Weight /= Sum;
	}
	return Out;
}

void USwcCreatureAnimInstance::AddPoseLayers(const FLayer& Layer, float Weight, bool bWholeFragment, TArray<FSwcCreaturePoseLayer>& Out) const
{
	const FSwcCreatureFragment* Fragment = Animations->Fragment(Layer.Fragment);
	if (Fragment == nullptr || Weight < LayerMinimum)
	{
		return;
	}
	const auto AddList = [this, Fragment, &Layer, Weight, &Out](const TArray<FSwcFragmentItem>& Items)
	{
		TMap<UAnimSequence*, FSwcCreatureClipSample> Merged;
		TSet<int32> Clips;
		AddItems(*Fragment, Items, Layer, 1.0f, Merged, &Clips);
		if (Merged.IsEmpty())
		{
			return;
		}
		float Strength = 0.0f;
		for (const TPair<UAnimSequence*, FSwcCreatureClipSample>& Pair : Merged)
		{
			Strength += Pair.Value.Weight >= MinimumWeight ? Pair.Value.Weight : 0.0f;
		}
		FSwcCreaturePoseLayer& Pose = Out.AddDefaulted_GetRef();
		Pose.Samples = Normalize(Merged);
		Pose.Weight = Weight;
		TSet<int32> Bones;
		for (const int32 Clip : Clips)
		{
			Pose.bAdditive = Pose.bAdditive || (ClipAdditive.IsValidIndex(Clip) && ClipAdditive[Clip]);
			if (ClipMeshBones.IsValidIndex(Clip))
			{
				Bones.Append(ClipMeshBones[Clip]);
			}
		}
		if (Pose.bAdditive)
		{
			// The weight of the items of an additive layer is its strength (the R0 reactions of the wolf: 0.7): the normalized samples
			// would make it 1 (AST-197).
			Pose.Weight *= FMath::Min(Strength, 1.0f);
		}
		Pose.MeshBones = Bones.Array();
		Pose.MeshBones.Sort();
	};
	// An overlay of a different scope: its first layer too (its own bones, or additive).
	if (bWholeFragment)
	{
		AddList(Fragment->Items);
	}
	for (const TArray<FSwcFragmentItem>& Items : Fragment->ExtraLayers)
	{
		AddList(Items);
	}
}

void USwcCreatureAnimInstance::AdvanceLayer(FLayer& Layer, float DeltaSeconds) const
{
	Layer.Time += DeltaSeconds * Layer.Scale;
	const FSwcCreatureFragment* Fragment = Animations->Fragment(Layer.Fragment);
	if (Fragment == nullptr || Fragment->Items.IsEmpty())
	{
		return;
	}
	int32 Current = 0;
	for (int32 Index = 1; Index < Fragment->Items.Num(); ++Index)
	{
		if (Fragment->Items[Index].Start <= Layer.Time)
		{
			Current = Index;
		}
	}
	const FSwcFragmentItem& Item = Fragment->Items[Current];
	const int32 Space = SpaceOfItem(static_cast<int32>(Item.Kind), Item.Index);
	if (Space != INDEX_NONE)
	{
		const float Cycle = CycleOf(Space);
		if (Cycle > 0.0f)
		{
			Layer.Phase += DeltaSeconds * Item.Speed * Layer.Scale / Cycle;
		}
	}
}

void USwcCreatureAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);
	FSwcCreatureAnimProxy& Proxy = GetProxyOnGameThread<FSwcCreatureAnimProxy>();
	if (!Animations.IsValid() || Layers.IsEmpty())
	{
		Proxy.Base.Reset();
		Proxy.Layers.Reset();
		return;
	}
	// The weights: the newest layer goes to 1 in its blend-in time, the layers before it share the rest.
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
		if (Layers[Index].Weight < LayerMinimum)
		{
			Layers.RemoveAt(Index);
		}
	}
	for (FLayer& Layer : Layers)
	{
		AdvanceLayer(Layer, DeltaSeconds);
	}
	// The overlays of the other scopes: in with their blend-in, out with the blend-out of their fragment at their end or at a
	// stop (a record of the scope with no bit).
	for (int32 Index = Overlays.Num() - 1; Index >= 0; --Index)
	{
		FOverlay& Overlay = Overlays[Index];
		AdvanceLayer(Overlay.Layer, DeltaSeconds);
		const FSwcCreatureFragment* Fragment = Animations->Fragment(Overlay.Layer.Fragment);
		const bool bEnded = Fragment == nullptr || (Fragment->Length > 0.0f && Overlay.Layer.Time >= Fragment->Length);
		const float BlendOut = Fragment != nullptr ? FMath::Max(Fragment->BlendOut, 0.01f) : 0.2f;
		if (Overlay.bStopping || bEnded)
		{
			Overlay.Weight -= DeltaSeconds / BlendOut;
		}
		else
		{
			Overlay.Weight = Overlay.Layer.BlendIn <= 0.001f ? 1.0f : FMath::Min(1.0f, Overlay.Weight + DeltaSeconds / Overlay.Layer.BlendIn);
		}
		if (Overlay.Weight <= 0.0f)
		{
			Overlays.RemoveAt(Index);
		}
	}
	TMap<UAnimSequence*, FSwcCreatureClipSample> Merged;
	for (const FLayer& Layer : Layers)
	{
		const FSwcCreatureFragment* Fragment = Animations->Fragment(Layer.Fragment);
		if (Fragment != nullptr)
		{
			AddItems(*Fragment, Fragment->Items, Layer, Layer.Weight, Merged, nullptr);
		}
	}
	Proxy.Base = Normalize(Merged);
	Proxy.Layers.Reset();
	// The later layers of the fragment of scope 0 with the largest weight (the copies of layer 0 change nothing), then the
	// overlays of the other scopes.
	const FLayer* Main = nullptr;
	for (const FLayer& Layer : Layers)
	{
		Main = Main == nullptr || Layer.Weight > Main->Weight ? &Layer : Main;
	}
	if (Main != nullptr)
	{
		AddPoseLayers(*Main, Main->Weight, false, Proxy.Layers);
	}
	for (const FOverlay& Overlay : Overlays)
	{
		AddPoseLayers(Overlay.Layer, FMath::Clamp(Overlay.Weight, 0.0f, 1.0f), true, Proxy.Layers);
	}
}
