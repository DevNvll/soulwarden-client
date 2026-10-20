#include "Character/SwcWeaponAnimInstance.h"

#include "Animation/AnimSequence.h"
#include "Character/SwcActions.h"
#include "Character/SwcPlayerAnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"

namespace
{
/** Overlays with a smaller weight are not evaluated (the value of the player animation). */
constexpr float MinimumWeight = 0.005f;
} // namespace

FAnimInstanceProxy* USwcWeaponAnimInstance::CreateAnimInstanceProxy()
{
	return new FSwcPlayerAnimProxy(this);
}

void USwcWeaponAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy)
{
	delete static_cast<FSwcPlayerAnimProxy*>(InProxy);
}

void USwcWeaponAnimInstance::Setup(const TSharedPtr<FSwcActions>& Data, const FString& MeshPath, const TArray<FString>& ClipPaths)
{
	Actions = Data;
	Sequences.Reset();
	BoneCache.Reset();
	if (!Actions.IsValid())
	{
		return;
	}
	const FString Prefix = FPaths::GetBaseFilename(MeshPath).ToLower();
	// The name of an asset is the full name, or (only when no full name is equal) the full name cut in its middle at an underscore.
	const auto Cut = [](const FString& Name, const FString& Full)
	{
		for (int32 At = 0; At < Name.Len(); ++At)
		{
			if (Name[At] == TCHAR('_') && Name.Len() - 1 < Full.Len() && Full.StartsWith(Name.Left(At))
				&& Full.EndsWith(Name.RightChop(At + 1)))
			{
				return true;
			}
		}
		return false;
	};
	for (const FString& Path : ClipPaths)
	{
		const FString Name = FPaths::GetBaseFilename(Path).ToLower();
		int32 Found = INDEX_NONE;
		int32 Count = 0;
		for (int32 Pass = 0; Pass < 2 && Count == 0; ++Pass)
		{
			for (int32 Clip = 0; Clip < Actions->Clips.Num(); ++Clip)
			{
				const FString Full = Prefix + Actions->Clips[Clip].Name;
				if (Actions->Clips[Clip].bProp && (Pass == 0 ? Name == Full : Cut(Name, Full)))
				{
					Found = Clip;
					++Count;
				}
			}
		}
		if (Count != 1)
		{
			continue;
		}
		if (UAnimSequence* Sequence = LoadObject<UAnimSequence>(nullptr, *Path))
		{
			Sequences.Add(Found, Sequence);
		}
	}
}

void USwcWeaponAnimInstance::SetLeader(USwcPlayerAnimInstance* InLeader)
{
	Leader = InLeader;
}

const TArray<int32>& USwcWeaponAnimInstance::ClipBones(int32 Clip)
{
	if (const TArray<int32>* Found = BoneCache.Find(Clip))
	{
		return *Found;
	}
	TArray<int32>& Out = BoneCache.Add(Clip);
	const USkeletalMeshComponent* Mesh = GetSkelMeshComponent();
	const USkeletalMesh* Asset = Mesh != nullptr ? Mesh->GetSkeletalMeshAsset() : nullptr;
	if (Asset == nullptr)
	{
		return Out;
	}
	for (const FName& Bone : Actions->Clips[Clip].Bones)
	{
		// The weapon has no root motion: each animated bone, the root too.
		const int32 Index = Asset->GetRefSkeleton().FindBoneIndex(Bone);
		if (Index != INDEX_NONE)
		{
			Out.Add(Index);
		}
	}
	return Out;
}

void USwcWeaponAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);
	TArray<FSwcPoseOverlay> Overlays;
	const USwcPlayerAnimInstance* Source = Leader.Get();
	if (Actions.IsValid() && Source != nullptr)
	{
		// Each visit as the action channels of the player animation (USwcPlayerAnimInstance::AddOverlays): for each layer of the
		// record, its item at the time with its blend-in from the item before it. A weapon clip overrides its bones.
		for (const FSwcWeaponVisit& Visit : Source->GetWeaponVisits())
		{
			if (!Actions->Fragments.IsValidIndex(Visit.Fragment))
			{
				continue;
			}
			for (const TArray<FSwcActionItem>& Items : Actions->Fragments[Visit.Fragment].Layers)
			{
				int32 Current = INDEX_NONE;
				for (int32 Index = 0; Index < Items.Num(); ++Index)
				{
					if (Items[Index].At <= Visit.Time + KINDA_SMALL_NUMBER)
					{
						Current = Index;
					}
				}
				if (Current == INDEX_NONE)
				{
					continue;
				}
				const FSwcActionItem& Item = Items[Current];
				const float Alpha = Item.BlendIn > KINDA_SMALL_NUMBER ? FMath::Clamp((Visit.Time - Item.At) / Item.BlendIn, 0.0f, 1.0f) : 1.0f;
				const auto Add = [&](const FSwcActionItem& From, float Weight)
				{
					const TObjectPtr<UAnimSequence>* Sequence = From.Clip != INDEX_NONE ? Sequences.Find(From.Clip) : nullptr;
					if (Sequence == nullptr || Weight * From.Weight < MinimumWeight)
					{
						return;
					}
					FSwcPoseOverlay& Overlay = Overlays.AddDefaulted_GetRef();
					Overlay.Weight = Weight * From.Weight;
					Overlay.Samples.Add({*Sequence, From.ClipTime(Visit.Time, Actions->Clips[From.Clip].Duration), 1.0f, From.bLoop});
					Overlay.Bones = ClipBones(From.Clip);
				};
				if (Current > 0 && Alpha < 1.0f)
				{
					Add(Items[Current - 1], Visit.Weight * (1.0f - Alpha));
				}
				Add(Item, Visit.Weight * Alpha);
			}
		}
	}
	FSwcPlayerAnimProxy& Proxy = GetProxyOnGameThread<FSwcPlayerAnimProxy>();
	Proxy.Samples.Reset();
	Proxy.Overlays = MoveTemp(Overlays);
}
