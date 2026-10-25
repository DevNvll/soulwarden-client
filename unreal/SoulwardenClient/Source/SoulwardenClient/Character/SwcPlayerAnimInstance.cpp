#include "Character/SwcPlayerAnimInstance.h"

#include "Animation/AnimSequence.h"
#include "Net/SwcSpace.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "BonePose.h"
#include "AnimationRuntime.h"
#include "Character/SwcActions.h"
#include "Character/SwcCharacterMovement.h"
#include "Character/SwcLocomotion.h"
#include "Character/SwcPlayerActions.h"
#include "Character/SwcPlayerCharacter.h"
#include "Character/SwcPlayerLooks.h"
#include "Effects/SwcFragmentEffects.h"
#include "Engine/GameInstance.h"
#include "SwcBackendSubsystem.h"
#include "Character/SwcSmooth.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/Character.h"

namespace
{
/** The motion parameter smoothing of the action list component of the player slice [D m_motionParameterSmoothingSettings]:
 *  TravelSpeedConvergeTime and TravelAngleConvergeTime 0.1 s, TurnSpeedConvergeTime 0.75 s, MovementSpeedEpsilon 0.02. */
constexpr float SpeedConvergeTime = 0.1f;
constexpr float AngleConvergeTime = 0.1f;
constexpr float TurnConvergeTime = 0.75f;
constexpr float MovementSpeedEpsilon = 0.02f;
/** Samples and overlays with a smaller weight are not evaluated. */
constexpr float MinimumWeight = 0.005f;

/** Evaluates samples (weights that sum to 1) into one pose. */
void BlendSamples(const TArray<FSwcPoseSample>& Samples, FPoseContext& Context, FCompactPose& OutPose, FBlendedCurve& OutCurve,
	UE::Anim::FStackAttributeContainer& OutAttributes)
{
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
		Poses[Index].SetBoneContainer(&Context.Pose.GetBoneContainer());
		Curves[Index].InitFrom(Context.Curve);
		FAnimationPoseData Data(Poses[Index], Curves[Index], Attributes[Index]);
		Sample.Sequence->GetAnimationPose(Data, FAnimExtractContext(static_cast<double>(Sample.Time), false, FDeltaTimeRecord(), Sample.bLoop));
		Weights[Index] = Sample.Weight;
	}
	FAnimationPoseData Result(OutPose, OutCurve, OutAttributes);
	FAnimationRuntime::BlendPosesTogether(Poses, Curves, Attributes, Weights, Result);
}
} // namespace

bool FSwcPlayerAnimProxy::Evaluate(FPoseContext& Output)
{
	if (Samples.IsEmpty() && Overlays.IsEmpty())
	{
		return false;
	}
	if (Samples.IsEmpty())
	{
		Output.ResetToRefPose();
	}
	else
	{
		BlendSamples(Samples, Output, Output.Pose, Output.Curve, Output.CustomAttributes);
	}
	// The overlays in their order: an override layer moves its bones to its pose with its weight; an additive layer adds
	// its rotation (q = slerp(1, delta, w) x q; the additive clips of the player data have rotations only; the order of
	// the product is an inference, research feel-values.md R3.2.9).
	const FBoneContainer& Bones = Output.Pose.GetBoneContainer();
	for (const FSwcPoseOverlay& Overlay : Overlays)
	{
		FCompactPose Pose;
		FBlendedCurve Curve;
		UE::Anim::FStackAttributeContainer Attributes;
		Pose.SetBoneContainer(&Bones);
		Curve.InitFrom(Output.Curve);
		BlendSamples(Overlay.Samples, Output, Pose, Curve, Attributes);
		const float Weight = FMath::Clamp(Overlay.Weight, 0.0f, 1.0f);
		for (const int32 MeshBone : Overlay.Bones)
		{
			const FCompactPoseBoneIndex Index = Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshBone));
			if (Index == INDEX_NONE)
			{
				continue;
			}
			FTransform& Out = Output.Pose[Index];
			const FTransform& In = Pose[Index];
			if (Overlay.bAdditive)
			{
				Out.SetRotation((FQuat::Slerp(FQuat::Identity, In.GetRotation(), Weight) * Out.GetRotation()).GetNormalized());
				continue;
			}
			Out.SetRotation(FQuat::Slerp(Out.GetRotation(), In.GetRotation(), Weight).GetNormalized());
			Out.SetTranslation(FMath::Lerp(Out.GetTranslation(), In.GetTranslation(), static_cast<double>(Weight)));
			Out.SetScale3D(FMath::Lerp(Out.GetScale3D(), In.GetScale3D(), static_cast<double>(Weight)));
		}
	}
	// The bend of the upper body to the aim (usertest-1024 item 4): each joint turns by the bend in component space, parents first (a
	// child takes the turn of its parent, so the bend adds up along the spine).
	if (!RetargetBones.IsEmpty() && !RetargetBend.Equals(FQuat::Identity, 1e-6))
	{
		FCSPose<FCompactPose> Space;
		Space.InitPose(Output.Pose);
		for (const int32 MeshBone : RetargetBones)
		{
			const FCompactPoseBoneIndex Index = Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshBone));
			if (Index == INDEX_NONE)
			{
				continue;
			}
			FTransform Joint = Space.GetComponentSpaceTransform(Index);
			Joint.SetRotation((RetargetBend * Joint.GetRotation()).GetNormalized());
			Space.SetComponentSpaceTransform(Index, Joint);
		}
		FCSPose<FCompactPose>::ConvertComponentPosesToLocalPoses(MoveTemp(Space), Output.Pose);
	}
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

void USwcPlayerAnimInstance::SetActions(const TSharedPtr<FSwcActions>& Data, bool bFemale)
{
	bFemaleBody = bFemale;
	Actions = Data;
	ActionPaths.Reset();
	ActionSequences.Reset();
	ActionLoaded.Reset();
	BoneCache.Reset();
	Channels[0].Reset();
	Channels[1].Reset();
	if (!Actions.IsValid())
	{
		return;
	}
	Actions->ClipAssets(bFemale, ActionPaths);
	ActionSequences.SetNum(ActionPaths.Num());
	ActionLoaded.Init(false, ActionPaths.Num());
	// The clips load here, not at their first use (no hitch in an attack).
	for (int32 Clip = 0; Clip < ActionPaths.Num(); ++Clip)
	{
		ActionSequence(Clip);
	}
}

void USwcPlayerAnimInstance::SetStance(const FSwcWeaponActions* Weapon, bool bOffhand)
{
	Stance = Weapon;
	bOffhandStance = bOffhand;
}

void USwcPlayerAnimInstance::UpdateEffects(int32 Channel, int32 Fragment, uint32 Serial, float Time)
{
	if (!Actions.IsValid())
	{
		return;
	}
	if (!FragmentEffects.IsValid())
	{
		FragmentEffects = MakeShared<FSwcFragmentEffects>();
	}
	FSwcEffectOwner Owner;
	Owner.World = GetWorld();
	Owner.Body = GetSkelMeshComponent();
	if (const ASwcPlayerCharacter* Character = Cast<ASwcPlayerCharacter>(GetOwningActor()))
	{
		Character->GetMainHandWeapon(Owner.Weapon, Owner.WeaponTags, Owner.WeaponFile);
		if (const USwcPlayerActions* PlayerActions = Character->GetActions())
		{
			Owner.LearnedNodes = &PlayerActions->GetLearnedNodes();
		}
	}
	// The ammunition of the weapon of the stance (the nocked arrow of a CAGE-Attachment clip; usertest-1024): read only at such a clip.
	if (Stance != nullptr && Stance->Ranged.IsValid() && !Stance->Ranged->ammo.empty())
	{
		const TSharedPtr<FSwcActions> Data = Actions;
		const std::string Fallback = Stance->Ranged->ammo;
		USwcBackendSubsystem* Backend = GetWorld() != nullptr && GetWorld()->GetGameInstance() != nullptr
			? GetWorld()->GetGameInstance()->GetSubsystem<USwcBackendSubsystem>() : nullptr;
		Owner.AmmoMesh = [Data, Fallback, Backend]()
		{
			return SwcAmmoMesh(*Data, Fallback, Backend != nullptr ? Backend->GetEquippedAmmo() : std::vector<std::string>());
		};
		const bool bFemale = bFemaleBody;
		Owner.BodySocket = [Backend, bFemale](FName Name, FName& OutBone, FTransform& OutRelative)
		{
			const FSwcPlayerLooks* Looks = Backend != nullptr ? Backend->GetPlayerLooks() : nullptr;
			const FSwcAttachPlace Place = Looks != nullptr ? Looks->SocketPlace(bFemale, Name.ToString().ToLower()) : FSwcAttachPlace();
			OutBone = Place.Bone;
			OutRelative = Place.Offset;
			return Place.IsValid();
		};
	}
	const bool bValid = Fragment != INDEX_NONE && Actions->Fragments.IsValidIndex(Fragment);
	const int32 Record = bValid && Stance != nullptr ? Stance->WeaponRecord(Fragment) : INDEX_NONE;
	FragmentEffects->Update(*Actions, Owner, Channel, bValid ? &Actions->Fragments[Fragment] : nullptr,
		Actions->Fragments.IsValidIndex(Record) ? &Actions->Fragments[Record] : nullptr, Serial, Time);
}

void USwcPlayerAnimInstance::NativeUninitializeAnimation()
{
	if (FragmentEffects.IsValid())
	{
		FragmentEffects->StopAll(GetWorld());
	}
	Super::NativeUninitializeAnimation();
}

void USwcPlayerAnimInstance::SetAction(int32 Channel, int32 Fragment, uint32 Serial, float Time)
{
	if (Channel < 0 || Channel > 1)
	{
		return;
	}
	UpdateEffects(Channel, Fragment, Serial, Time);
	TArray<FActionLayer>& Layers0 = Channels[Channel];
	if (Fragment == INDEX_NONE || !Actions.IsValid() || !Actions->Fragments.IsValidIndex(Fragment))
	{
		if (!Layers0.IsEmpty())
		{
			Layers0.Last().bEnding = true;
		}
		return;
	}
	if (!Layers0.IsEmpty() && Layers0.Last().Serial == Serial && Layers0.Last().Fragment == Fragment && !Layers0.Last().bEnding)
	{
		Layers0.Last().Time = Time;
		return;
	}
	FActionLayer& Layer = Layers0.AddDefaulted_GetRef();
	Layer.Fragment = Fragment;
	Layer.Serial = Serial;
	Layer.Time = Time;
}

UAnimSequence* USwcPlayerAnimInstance::ActionSequence(int32 Clip) const
{
	if (!ActionPaths.IsValidIndex(Clip))
	{
		return nullptr;
	}
	if (!ActionLoaded[Clip])
	{
		ActionLoaded[Clip] = true;
		ActionSequences[Clip] = ActionPaths[Clip].IsEmpty() ? nullptr : LoadObject<UAnimSequence>(nullptr, *ActionPaths[Clip]);
	}
	return ActionSequences[Clip];
}

const TArray<int32>& USwcPlayerAnimInstance::ClipBones(int32 Clip) const
{
	if (const TArray<int32>* Found = BoneCache.Find(Clip))
	{
		return *Found;
	}
	TArray<int32>& Out = BoneCache.Add(Clip);
	const USkeletalMeshComponent* Mesh = GetSkelMeshComponent();
	const USkeletalMesh* Asset = Mesh != nullptr ? Mesh->GetSkeletalMeshAsset() : nullptr;
	if (Asset == nullptr || !Actions.IsValid() || !Actions->Clips.IsValidIndex(Clip))
	{
		return Out;
	}
	for (const FName& Bone : Actions->Clips[Clip].Bones)
	{
		// The root bone (Xform) stays locked: the root track moves the character.
		const int32 Index = Asset->GetRefSkeleton().FindBoneIndex(Bone);
		if (Index > 0)
		{
			Out.Add(Index);
		}
	}
	return Out;
}

TArray<float> USwcPlayerAnimInstance::ParamValues(const FSwcBlendSpace& Space) const
{
	TArray<float> Values;
	for (const FSwcBlendSpace::FParam& Param : Space.Params)
	{
		Values.Add(Param.Name == TEXT("MoveSpeed") ? MoveSpeed : Param.Name == TEXT("TravelAngle") ? TravelAngle : Param.Name == TEXT("TurnSpeed") ? TurnSpeed : 0.0f);
	}
	return Values;
}

void USwcPlayerAnimInstance::SpaceSamples(const FSwcBlendSpace& Space, float Phase, bool bLoop, float Weight, TArray<FSwcPoseSample>& Out) const
{
	TMap<int32, float> Weights;
	Space.Evaluate(ParamValues(Space), Weights);
	for (const TPair<int32, float>& Example : Weights)
	{
		const int32 Clip = Space.ExampleClips.IsValidIndex(Example.Key) ? Space.ExampleClips[Example.Key] : INDEX_NONE;
		UAnimSequence* Sequence = ActionSequence(Clip);
		if (Sequence == nullptr)
		{
			continue;
		}
		const float Duration = Actions->Clips[Clip].Duration;
		const float Cycle = bLoop ? FMath::Fmod(Phase, 1.0f) : FMath::Min(Phase, 1.0f);
		Out.Add({Sequence, Cycle * Duration, Weight * Example.Value, bLoop});
	}
}

float USwcPlayerAnimInstance::PhaseStep(const FSwcBlendSpace& Space, float DeltaSeconds, float Speed) const
{
	TMap<int32, float> Weights;
	Space.Evaluate(ParamValues(Space), Weights);
	float Cycle = 0.0f;
	float Total = 0.0f;
	for (const TPair<int32, float>& Example : Weights)
	{
		const int32 Clip = Space.ExampleClips.IsValidIndex(Example.Key) ? Space.ExampleClips[Example.Key] : INDEX_NONE;
		if (Clip != INDEX_NONE)
		{
			Cycle += Example.Value * Actions->Clips[Clip].Duration / FMath::Max(Space.ExampleScales[Example.Key], 0.01f);
			Total += Example.Value;
		}
	}
	return Total > 0.0f && Cycle > 0.0f ? DeltaSeconds * Speed / (Cycle / Total) : 0.0f;
}

void USwcPlayerAnimInstance::AddOverlays(const FSwcActionFragment& Fragment, int32 First, float Time, float Phase, float Weight)
{
	for (int32 LayerIndex = First; LayerIndex < Fragment.Layers.Num(); ++LayerIndex)
	{
		const TArray<FSwcActionItem>& Items = Fragment.Layers[LayerIndex];
		int32 Current = INDEX_NONE;
		for (int32 Index = 0; Index < Items.Num(); ++Index)
		{
			if (Items[Index].At <= Time + KINDA_SMALL_NUMBER)
			{
				Current = Index;
			}
		}
		if (Current == INDEX_NONE)
		{
			continue;
		}
		// The item blends in over its blend-in time from its start; the item before it blends out in that time.
		const FSwcActionItem& Item = Items[Current];
		const float Alpha = Item.BlendIn > KINDA_SMALL_NUMBER ? FMath::Clamp((Time - Item.At) / Item.BlendIn, 0.0f, 1.0f) : 1.0f;
		const auto Add = [&](const FSwcActionItem& Source, float SourceWeight)
		{
			// Weight 0 in a layer keeps the pose below it (layer 0 of the attacks: the base plays on).
			if (Source.IsEmpty() || SourceWeight * Source.Weight < MinimumWeight)
			{
				return;
			}
			FSwcPoseOverlay Overlay;
			Overlay.Weight = SourceWeight * Source.Weight;
			TSet<int32> BoneSet;
			if (Source.Clip != INDEX_NONE)
			{
				UAnimSequence* Sequence = ActionSequence(Source.Clip);
				if (Sequence == nullptr)
				{
					return;
				}
				const FSwcActionClip& Clip = Actions->Clips[Source.Clip];
				Overlay.Samples.Add({Sequence, Source.ClipTime(Time, Clip.Duration), 1.0f, Source.bLoop});
				Overlay.bAdditive = Clip.bAdditive;
				BoneSet.Append(ClipBones(Source.Clip));
			}
			else
			{
				const FSwcBlendSpace& Space = Actions->BlendSpaces[Source.BlendSpace];
				// A blend space of an action channel: one cycle from the start of the item (the stance gives its phase).
				float SourcePhase = Phase;
				if (SourcePhase < 0.0f)
				{
					const float Step = PhaseStep(Space, 1.0f, Source.Speed);
					SourcePhase = Step * FMath::Max(Time - Source.At, 0.0f);
				}
				SpaceSamples(Space, SourcePhase, Source.bLoop, 1.0f, Overlay.Samples);
				float Sum = 0.0f;
				for (const FSwcPoseSample& Sample : Overlay.Samples)
				{
					Sum += Sample.Weight;
				}
				for (FSwcPoseSample& Sample : Overlay.Samples)
				{
					Sample.Weight /= FMath::Max(Sum, KINDA_SMALL_NUMBER);
				}
				for (const int32 Clip : Space.ExampleClips)
				{
					if (Actions->Clips.IsValidIndex(Clip))
					{
						Overlay.bAdditive = Overlay.bAdditive || Actions->Clips[Clip].bAdditive;
						BoneSet.Append(ClipBones(Clip));
					}
				}
			}
			if (Overlay.Samples.IsEmpty())
			{
				return;
			}
			Overlay.Bones = BoneSet.Array();
			Overlays.Add(MoveTemp(Overlay));
		};
		if (Current > 0 && Alpha < 1.0f)
		{
			Add(Items[Current - 1], Weight * (1.0f - Alpha));
		}
		Add(Item, Weight * Alpha);
	}
}

void USwcPlayerAnimInstance::AddSamples(const FLayer& Layer, TMap<UAnimSequence*, FSwcPoseSample>& Out) const
{
	const auto Merge = [&Out](const FSwcPoseSample& Sample)
	{
		if (Sample.Sequence == nullptr || Sample.Weight <= 0.0f)
		{
			return;
		}
		FSwcPoseSample& Merged = Out.FindOrAdd(Sample.Sequence);
		Merged.Sequence = Sample.Sequence;
		// One time for each clip: the layer with the larger weight gives it.
		if (Sample.Weight > Merged.Weight)
		{
			Merged.Time = Sample.Time;
			Merged.bLoop = Sample.bLoop;
		}
		Merged.Weight += Sample.Weight;
	};
	if (Layer.Action != INDEX_NONE)
	{
		// A stance fragment of the weapon: layer 0 is the base.
		const FSwcActionFragment& Fragment = Actions->Fragments[Layer.Action];
		if (Fragment.Layers.IsEmpty() || Fragment.Layers[0].IsEmpty())
		{
			return;
		}
		// The item of layer 0 at the time (a jump: the jump clip, then its fall loop).
		const FSwcActionItem* Current = &Fragment.Layers[0][0];
		for (const FSwcActionItem& Candidate : Fragment.Layers[0])
		{
			if (!Candidate.IsEmpty() && Candidate.At <= Layer.Elapsed)
			{
				Current = &Candidate;
			}
		}
		const FSwcActionItem& Item = *Current;
		if (Item.Clip != INDEX_NONE)
		{
			Merge({ActionSequence(Item.Clip), Item.ClipTime(Layer.Elapsed, Actions->Clips[Item.Clip].Duration), Layer.Weight, Item.bLoop});
		}
		else if (Item.BlendSpace != INDEX_NONE)
		{
			TArray<FSwcPoseSample> Space;
			SpaceSamples(Actions->BlendSpaces[Item.BlendSpace], Layer.Phase, Item.bLoop, Layer.Weight, Space);
			for (const FSwcPoseSample& Sample : Space)
			{
				Merge(Sample);
			}
		}
		return;
	}
	const FSwcFragment* Fragment = Locomotion->Fragment(Layer.Fragment);
	if (Fragment == nullptr)
	{
		return;
	}
	const auto Add = [this, &Merge](int32 Clip, float Time, float Weight, bool bLoop)
	{
		if (Sequences.IsValidIndex(Clip))
		{
			Merge({Sequences[Clip], Time, Weight, bLoop});
		}
	};
	if (Fragment->Clip != INDEX_NONE)
	{
		const float Duration = FMath::Max(Locomotion->Clips[Fragment->Clip].Duration, 0.001f);
		const float Time = Layer.Elapsed * Fragment->Speed;
		Add(Fragment->Clip, Fragment->bLoop ? FMath::Fmod(Time, Duration) : FMath::Min(Time, Duration), Layer.Weight, Fragment->bLoop);
		return;
	}
	const FSwcBlendSpace& Space = Locomotion->BlendSpaces[Fragment->BlendSpace];
	TMap<int32, float> Weights;
	Space.Evaluate(ParamValues(Space), Weights);
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
		Overlays.Reset();
		WeaponVisits.Reset();
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

	// The fragment of the movement state: the stance of the weapon in the hand, else the unarmed fragment of locomotion.json.
	FString Fragment = Movement->GetFragment();
	// The variant of the stance (stage P3.G): the block (Idle+Blocking, Strafe+Blocking: rswrd_block_idle and rswrd_block_strafe_blend
	// of a 1H melee weapon), the aim of a bow (+Aim: the half draw, +ChargedAim: the full draw), when the weapon has one.
	const TCHAR* Variant = Movement->IsBlocking() ? TEXT("+Blocking") : StanceVariant;
	if (Variant != nullptr && *Variant != 0 && Stance != nullptr && Stance->Stance.Contains(Fragment + Variant))
	{
		Fragment += Variant;
	}
	const int32* StanceFragment = Stance != nullptr && Actions.IsValid() ? Stance->Stance.Find(Fragment) : nullptr;
	const int32 Action = StanceFragment != nullptr ? *StanceFragment : INDEX_NONE;
	const int32* OffhandFragment = Action != INDEX_NONE && bOffhandStance ? Stance->Offhand.Find(Fragment) : nullptr;
	if ((Layers.IsEmpty() || Layers.Last().Fragment != Fragment || Layers.Last().Action != Action)
		&& (Action != INDEX_NONE || Locomotion->Fragment(Fragment) != nullptr))
	{
		FLayer& Layer = Layers.AddDefaulted_GetRef();
		Layer.Fragment = Fragment;
		Layer.Action = Action;
		Layer.Offhand = OffhandFragment != nullptr ? *OffhandFragment : INDEX_NONE;
		const FSwcActionFragment* Data = Action != INDEX_NONE ? &Actions->Fragments[Action] : nullptr;
		Layer.BlendIn = Data != nullptr ? (Data->Layers.IsEmpty() || Data->Layers[0].IsEmpty() ? 0.2f : Data->Layers[0][0].BlendIn)
										: Locomotion->Fragment(Fragment)->BlendIn;
		Layer.Weight = Layers.Num() == 1 ? 1.0f : 0.0f;
	}
	if (Layers.IsEmpty())
	{
		Samples.Reset();
		Overlays.Reset();
		WeaponVisits.Reset();
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
		if (Layer.Action != INDEX_NONE)
		{
			const FSwcActionFragment& Data = Actions->Fragments[Layer.Action];
			const FSwcActionItem* Item = Data.Layers.IsEmpty() || Data.Layers[0].IsEmpty() ? nullptr : &Data.Layers[0][0];
			if (Item != nullptr && Item->BlendSpace != INDEX_NONE)
			{
				Layer.Phase += PhaseStep(Actions->BlendSpaces[Item->BlendSpace], DeltaSeconds, Item->Speed);
			}
			continue;
		}
		const FSwcFragment* Data = Locomotion->Fragment(Layer.Fragment);
		if (Data == nullptr || Data->BlendSpace == INDEX_NONE)
		{
			continue;
		}
		const FSwcBlendSpace& Space = Locomotion->BlendSpaces[Data->BlendSpace];
		TMap<int32, float> Weights;
		Space.Evaluate(ParamValues(Space), Weights);
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

	// The overlays: the other layers of the stance fragments and the fragments of the shield (with the phase of the base),
	// then the draw or the sheathe, then the attack over all of them.
	Overlays.Reset();
	if (Actions.IsValid())
	{
		for (const FLayer& Layer : Layers)
		{
			if (Layer.Action != INDEX_NONE)
			{
				AddOverlays(Actions->Fragments[Layer.Action], 1, Layer.Elapsed, Layer.Phase, Layer.Weight);
			}
			if (Layer.Offhand != INDEX_NONE)
			{
				AddOverlays(Actions->Fragments[Layer.Offhand], 0, Layer.Elapsed, Layer.Phase, Layer.Weight);
			}
		}
		for (const int32 Channel : {1, 0})
		{
			TArray<FActionLayer>& Played = Channels[Channel];
			for (int32 Index = 0; Index < Played.Num(); ++Index)
			{
				FActionLayer& Layer = Played[Index];
				const bool bNewest = Index == Played.Num() - 1;
				// The component gives the time of the newest fragment; an older or an ending one runs on.
				if (!bNewest || Layer.bEnding)
				{
					Layer.Time += DeltaSeconds;
				}
				if (Layer.bEnding)
				{
					const float BlendOut = Actions->Fragments[Layer.Fragment].BlendOut;
					Layer.Weight = BlendOut > 0.001f ? Layer.Weight - DeltaSeconds / BlendOut : 0.0f;
				}
			}
			// A fragment that blended in fully covers the ones before it (AST-202: also its weapon record, whose blend-in can be
			// longer: the half draw loop of the bow 0.5 s).
			for (int32 Index = Played.Num() - 1; Index > 0; --Index)
			{
				float FullIn = FSwcActions::FullIn(Actions->Fragments[Played[Index].Fragment]);
				const int32 Record = Stance != nullptr ? Stance->WeaponRecord(Played[Index].Fragment) : INDEX_NONE;
				if (Record != INDEX_NONE)
				{
					FullIn = FMath::Max(FullIn, FSwcActions::FullIn(Actions->Fragments[Record]));
				}
				if (Played[Index].Time >= FullIn)
				{
					Played.RemoveAt(0, Index);
					break;
				}
			}
			Played.RemoveAll([](const FActionLayer& Layer) { return Layer.Weight <= 0.0f; });
			for (const FActionLayer& Layer : Played)
			{
				AddOverlays(Actions->Fragments[Layer.Fragment], 0, Layer.Time, -1.0f, Layer.Weight);
			}
		}
	}
	// The weapon scope (AST-202): the weapon record of each fragment, on the clock and with the weight of the fragment. The fragments
	// of the stance (the movement: the flail idle and strafe) have weights that sum to 1: each one blends over the ones before it
	// with its share of the weights up to it; the action channels go over them with their own weights.
	WeaponVisits.Reset();
	if (Actions.IsValid() && Stance != nullptr && !Stance->WeaponRecords.IsEmpty())
	{
		float StanceSum = 0.0f;
		for (const FLayer& Layer : Layers)
		{
			StanceSum += Layer.Weight;
			const int32 Record = Layer.Action != INDEX_NONE ? Stance->WeaponRecord(Layer.Action) : INDEX_NONE;
			if (Record != INDEX_NONE && StanceSum > 0.0f)
			{
				WeaponVisits.Add({Record, Layer.Elapsed, Layer.Weight / StanceSum});
			}
		}
		for (const int32 Channel : {1, 0})
		{
			for (const FActionLayer& Layer : Channels[Channel])
			{
				if (const int32 Record = Stance->WeaponRecord(Layer.Fragment); Record != INDEX_NONE)
				{
					WeaponVisits.Add({Record, Layer.Time, Layer.Weight});
				}
			}
		}
	}
	// The evaluation runs after this update (on a worker thread): the proxy keeps a copy.
	FSwcPlayerAnimProxy& Proxy = GetProxyOnGameThread<FSwcPlayerAnimProxy>();
	Proxy.Samples = Samples;
	Proxy.Overlays = Overlays;
	UpdateRetarget(DeltaSeconds, Proxy);
}

void USwcPlayerAnimInstance::UpdateRetarget(float DeltaSeconds, FSwcPlayerAnimProxy& Proxy)
{
	// The bend of the upper body to the aim (the Mannequin clip AttackHeightRetargeting with the AttackHeightRetargetComponent of the
	// player slice; usertest-1024 item 4, research usertest-hitvolume.md section 4; the rule: Character/SwcAttackRetarget.h). The aim is
	// the camera direction of the controller (0x6781C60 asks the character for its aim direction when the input has no target). The
	// turn is applied in component space after the overlays (FSwcPlayerAnimProxy::Evaluate) [I: the space of the operator mode 3]: the
	// weapon on the hand and the strike shapes read that final pose.
	Proxy.RetargetBend = FQuat::Identity;
	Proxy.RetargetBones.Reset();
	const ACharacter* Character = Cast<ACharacter>(TryGetPawnOwner());
	USkeletalMeshComponent* Mesh = GetSkelMeshComponent();
	if (!Actions.IsValid() || Character == nullptr || Mesh == nullptr || Actions->GetRetargetJoints().IsEmpty())
	{
		return;
	}
	const FSwcEffectClip* Clip = FragmentEffects.IsValid() ? FragmentEffects->GetRetargetClip() : nullptr;
	swc::retarget::Settings Settings = Actions->GetRetargetDefaults();
	if (Clip != nullptr)
	{
		Settings.lockDir = Clip->bLockDir;
		Settings.blendIn = Clip->BlendIn >= 0.0f ? Clip->BlendIn : Settings.blendIn;
		Settings.blendOut = Clip->BlendOut >= 0.0f ? Clip->BlendOut : Settings.blendOut;
		if (Clip->bAngles)
		{
			Settings.minAngle = Clip->MinAngle;
			Settings.maxAngle = Clip->MaxAngle;
		}
	}
	FVector Aim = Character->GetActorForwardVector();
	if (const APlayerController* Controller = Cast<APlayerController>(Character->GetController()); Controller != nullptr && Controller->PlayerCameraManager != nullptr)
	{
		Aim = Controller->PlayerCameraManager->GetCameraRotation().Vector();
	}
	const auto Engine = [](const FVector& Unreal) { return swc::retarget::Vec3{Unreal.X, -Unreal.Y, Unreal.Z}; };
	const swc::retarget::Bend Bend = RetargetRule.update(DeltaSeconds, Clip != nullptr, Settings, Engine(Aim), Engine(Character->GetActorForwardVector()));
	// The log of a clip: its start (the aim pitch) and its end (the largest bend).
	if (Clip != RetargetClip)
	{
		if (RetargetClip != nullptr)
		{
			UE_LOG(LogTemp, Log, TEXT("retarget: the clip ends: the largest bend %.1f degrees"), RetargetPeak);
		}
		if (Clip != nullptr)
		{
			UE_LOG(LogTemp, Log, TEXT("retarget: AttackHeightRetargeting starts (aim pitch %.1f degrees, limits %.0f / %.0f, blend %.2f / %.2f s, lock %d)"),
				FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(Aim.GetSafeNormal().Z, -1.0, 1.0))), Settings.minAngle, Settings.maxAngle, Settings.blendIn,
				Settings.blendOut, Settings.lockDir ? 1 : 0);
		}
		RetargetClip = Clip;
		RetargetPeak = 0.0;
	}
	const double Degrees = FMath::RadiansToDegrees(Bend.angle);
	RetargetPeak = FMath::Abs(Degrees) > FMath::Abs(RetargetPeak) ? Degrees : RetargetPeak;
	if (FMath::Abs(Bend.angle) < 1e-5)
	{
		return;
	}
	for (const FName& Joint : Actions->GetRetargetJoints())
	{
		const int32 Bone = Mesh->GetBoneIndex(Joint);
		if (Bone != INDEX_NONE)
		{
			Proxy.RetargetBones.Add(Bone);
		}
	}
	if (Proxy.RetargetBones.IsEmpty())
	{
		return;
	}
	// The turn of each joint in engine space, then the space map of Net/SwcSpace.h, then the frame of the mesh component.
	const double Half = Bend.angle / Proxy.RetargetBones.Num() * 0.5;
	const double Sine = FMath::Sin(Half);
	const FQuat World = SwcEngineToUnreal(swc::net::Quaternion{static_cast<float>(Bend.axis[0] * Sine), static_cast<float>(Bend.axis[1] * Sine),
		static_cast<float>(Bend.axis[2] * Sine), static_cast<float>(FMath::Cos(Half))});
	const FQuat Component = Mesh->GetComponentQuat();
	Proxy.RetargetBend = (Component.Inverse() * World * Component).GetNormalized();
}
