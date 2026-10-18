#include "Creatures/SwcCreatureAnimInstance.h"

#include "Animation/AnimSequence.h"
#include "Creatures/SwcCreatureData.h"

namespace
{
/** Samples with a smaller weight are not evaluated (the value of the player animation). */
constexpr float MinimumWeight = 0.005f;
/** A layer below this weight goes away. */
constexpr float LayerMinimum = 0.001f;
} // namespace

FAnimInstanceProxy* USwcCreatureAnimInstance::CreateAnimInstanceProxy()
{
	return new FSwcPlayerAnimProxy(this);
}

void USwcCreatureAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy)
{
	delete static_cast<FSwcPlayerAnimProxy*>(InProxy);
}

void USwcCreatureAnimInstance::SetAnimations(const TSharedPtr<FSwcCreatureAnimations>& Data)
{
	Animations = Data;
	Sequences.Reset();
	Layers.Reset();
	if (!Animations.IsValid())
	{
		return;
	}
	for (const FSwcClip& Clip : Animations->Clips)
	{
		UAnimSequence* Sequence = Clip.AssetPath.IsEmpty() ? nullptr
			: LoadObject<UAnimSequence>(nullptr, *Clip.AssetPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (Sequence == nullptr && !Clip.AssetPath.IsEmpty())
		{
			// Interchange names some clips STEM + "Null" + NAME (101 of 5,097 clips of the world): the second form.
			FString Folder, Name;
			Clip.AssetPath.Split(TEXT("/"), &Folder, &Name, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
			FString Package, Object;
			Name.Split(TEXT("."), &Package, &Object);
			const int32 Cut = Package.Find(TEXT("_"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
			if (Cut > 0)
			{
				const FString Other = Package.Left(Cut) + TEXT("Null") + Package.Mid(Cut);
				Sequence = LoadObject<UAnimSequence>(nullptr, *(Folder + TEXT("/") + Other + TEXT(".") + Other), nullptr, LOAD_NoWarn | LOAD_Quiet);
			}
		}
		Sequences.Add(Sequence);
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

float USwcCreatureAnimInstance::ClipTime(const FSwcCreatureFragment& Fragment, int32 Item, float Time, float Scale) const
{
	const FSwcFragmentItem& Data = Fragment.Items[Item];
	const FSwcClip& Clip = Animations->Clips[Data.Index];
	float ClipTime = Data.ClipStart + (Time - Data.Start) * Data.Speed;
	// A clip of the same name as the clip before it continues the time of that clip (research R3.2.5).
	if (Item > 0)
	{
		const FSwcFragmentItem& Before = Fragment.Items[Item - 1];
		if (Before.Kind == FSwcFragmentItem::EKind::Clip && Before.Index == Data.Index)
		{
			ClipTime = ClipTime - Data.ClipStart + this->ClipTime(Fragment, Item - 1, Data.Start, Scale);
		}
	}
	const float Duration = FMath::Max(Clip.Duration, 0.001f);
	return Data.bLoop ? FMath::Fmod(FMath::Max(ClipTime, 0.0f), Duration) : FMath::Clamp(ClipTime, 0.0f, Duration);
}

void USwcCreatureAnimInstance::AddItem(const FSwcCreatureFragment& Fragment, int32 Item, const FLayer& Layer, float Time, float Weight,
	TMap<UAnimSequence*, FSwcPoseSample>& Out) const
{
	const auto Add = [this, &Out](int32 Clip, float ClipTime, float SampleWeight, bool bLoop)
	{
		if (!Sequences.IsValidIndex(Clip) || Sequences[Clip] == nullptr || SampleWeight <= 0.0f)
		{
			return;
		}
		FSwcPoseSample& Sample = Out.FindOrAdd(Sequences[Clip]);
		Sample.Sequence = Sequences[Clip];
		if (SampleWeight > Sample.Weight)
		{
			Sample.Time = ClipTime;
			Sample.bLoop = bLoop;
		}
		Sample.Weight += SampleWeight;
	};
	const FSwcFragmentItem& Data = Fragment.Items[Item];
	if (Data.Kind == FSwcFragmentItem::EKind::Clip)
	{
		Add(Data.Index, ClipTime(Fragment, Item, Time, Layer.Scale), Weight * FMath::Max(Data.Weight, 0.0f), Data.bLoop);
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
		Add(Clip, Phase * Animations->Clips[Clip].Duration, Weight * Example.Value, Data.bLoop);
	}
}

void USwcCreatureAnimInstance::AddSamples(const FLayer& Layer, float Weight, TMap<UAnimSequence*, FSwcPoseSample>& Out) const
{
	const FSwcCreatureFragment* Fragment = Animations->Fragment(Layer.Fragment);
	if (Fragment == nullptr)
	{
		return;
	}
	// The item of the fragment time: the last item that started. Its blend-in crosses from the item before it. The end of the
	// layer (an empty animation) keeps the pose of the item before it at the time of the end: the client keeps the last pose
	// of an action of the server after its fragment (protocol notes 4.14, UT2-2); the server then sends its idle record.
	const TArray<FSwcFragmentItem>& Items = Fragment->Items;
	int32 Current = 0;
	for (int32 Index = 1; Index < Items.Num(); ++Index)
	{
		if (Items[Index].Start <= Layer.Time)
		{
			Current = Index;
		}
	}
	if (Items[Current].Kind == FSwcFragmentItem::EKind::End || Items[Current].Kind == FSwcFragmentItem::EKind::Missing)
	{
		int32 Before = Current - 1;
		while (Before >= 0 && (Items[Before].Kind == FSwcFragmentItem::EKind::End || Items[Before].Kind == FSwcFragmentItem::EKind::Missing))
		{
			--Before;
		}
		if (Before >= 0)
		{
			AddItem(*Fragment, Before, Layer, Items[Current].Kind == FSwcFragmentItem::EKind::End ? Items[Current].Start : Layer.Time, Weight, Out);
		}
		return;
	}
	const float BlendIn = Items[Current].BlendIn;
	const float Alpha = Current == 0 || BlendIn <= 0.001f ? 1.0f : FMath::Clamp((Layer.Time - Items[Current].Start) / BlendIn, 0.0f, 1.0f);
	if (Alpha < 1.0f && Current > 0)
	{
		const int32 Before = Current - 1;
		if (Items[Before].Kind != FSwcFragmentItem::EKind::End && Items[Before].Kind != FSwcFragmentItem::EKind::Missing)
		{
			AddItem(*Fragment, Before, Layer, Layer.Time, Weight * (1.0f - Alpha), Out);
		}
	}
	AddItem(*Fragment, Current, Layer, Layer.Time, Weight * Alpha, Out);
}

void USwcCreatureAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);
	if (!Animations.IsValid() || Layers.IsEmpty())
	{
		Samples.Reset();
		GetProxyOnGameThread<FSwcPlayerAnimProxy>().Samples = Samples;
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
	// The times: a fragment time for the clips; one phase for each blend space item.
	for (FLayer& Layer : Layers)
	{
		Layer.Time += DeltaSeconds * Layer.Scale;
		const FSwcCreatureFragment* Fragment = Animations->Fragment(Layer.Fragment);
		if (Fragment == nullptr)
		{
			continue;
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
	TMap<UAnimSequence*, FSwcPoseSample> Merged;
	for (const FLayer& Layer : Layers)
	{
		AddSamples(Layer, Layer.Weight, Merged);
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
