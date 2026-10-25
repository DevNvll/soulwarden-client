#include "Effects/SwcFragmentEffects.h"

#include "Components/MeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "NiagaraComponent.h"
#include "Character/SwcActions.h"
#include "Effects/SwcParticleEffects.h"

DEFINE_LOG_CATEGORY_STATIC(LogSwcFragmentEffects, Log, All);

namespace
{
/** A rotation of CryEngine Euler angles (radians; x first, then y, then z: Quat::CreateRotationXYZ) in Unreal through the space map of
 *  stage 1.1: (x, y, z, w) -> (-x, y, -z, w). The order of rotationDeg of the weapon scripts is the same [I]. */
FQuat UnrealEuler(const FVector& Radians)
{
	const FQuat Game = FQuat(FVector::ZAxisVector, Radians.Z) * FQuat(FVector::YAxisVector, Radians.Y) * FQuat(FVector::XAxisVector, Radians.X);
	return FQuat(-Game.X, Game.Y, -Game.Z, Game.W).GetNormalized();
}

/** A position of the game (m) in Unreal (cm): (x, -y, z) x 100. */
FVector UnrealOffset(const FVector& Meters)
{
	return FVector(Meters.X, -Meters.Y, Meters.Z) * 100.0;
}

/** @return The socket of a joint on a mesh: the bone, or NAME_None (the component) for the root joint Xform when the mesh has no bone
 *  of that name. bFound: false when the joint is not on the mesh. */
FName SocketOf(const UMeshComponent* Mesh, FName Joint, bool& bFound)
{
	bFound = true;
	if (Joint.IsNone())
	{
		return NAME_None;
	}
	if (const USkinnedMeshComponent* Skinned = Cast<USkinnedMeshComponent>(Mesh); Skinned != nullptr && Skinned->GetBoneIndex(Joint) != INDEX_NONE)
	{
		return Joint;
	}
	if (Mesh != nullptr && Mesh->DoesSocketExist(Joint))
	{
		return Joint;
	}
	// The root of the skeleton and the root of a weapon (a static weapon mesh has its pivot at master_root) [I].
	if (Joint == FName(TEXT("Xform")) || Joint == FName(TEXT("master_root")))
	{
		return NAME_None;
	}
	bFound = false;
	return NAME_None;
}
} // namespace

bool SwcWeaponHelperPlace(const FSwcActions& Data, const FString& WeaponFile, FName HelperName, const UMeshComponent* Weapon, FName& OutSocket,
                          FTransform& OutRelative)
{
	const TMap<FName, FSwcWeaponHelper>* Helpers = Data.WeaponHelpers(WeaponFile);
	const FSwcWeaponHelper* Helper = Helpers != nullptr ? Helpers->Find(HelperName) : nullptr;
	if (Helper == nullptr)
	{
		return false;
	}
	const FQuat Rotation(-Helper->Rotation.X, Helper->Rotation.Y, -Helper->Rotation.Z, Helper->Rotation.W);
	OutRelative = FTransform(Rotation.GetNormalized(), UnrealOffset(Helper->Position));
	bool bFound = false;
	OutSocket = SocketOf(Weapon, Helper->Bone, bFound);
	return bFound;
}

void FSwcFragmentEffects::Note(const FString& Text)
{
	if (!Logged.Contains(Text))
	{
		Logged.Add(Text);
		UE_LOG(LogSwcFragmentEffects, Log, TEXT("fragment effects: %s"), *Text);
	}
}

void FSwcFragmentEffects::Update(const FSwcActions& Data, const FSwcEffectOwner& Owner, int32 Channel, const FSwcActionFragment* Fragment,
                                 const FSwcActionFragment* WeaponRecord, uint32 Serial, float Clock)
{
	if (Channel < 0 || Channel > 1)
	{
		return;
	}
	FChannel& State = Channels[Channel];
	if (State.Fragment != Fragment || State.Serial != Serial)
	{
		StopChannel(Owner.World, Channel);
		State.Fragment = Fragment;
		State.Weapon = WeaponRecord;
		State.Serial = Serial;
		// A clip at 0 enters at the first update of its fragment.
		State.Clock = -1.0e-4f;
	}
	if (Fragment == nullptr || Clock <= State.Clock)
	{
		return;
	}
	const float From = State.Clock;
	State.Clock = Clock;
	for (const FSwcActionFragment* Record : {Fragment, WeaponRecord})
	{
		if (Record == nullptr)
		{
			continue;
		}
		for (const FSwcEffectClip& Clip : Record->Effects)
		{
			if (Clip.Start > From && Clip.Start <= Clock)
			{
				// The log of each entry (the timing evidence of the checks): the fragment, the clip, its start, and the clock of the frame.
				UE_LOG(LogSwcFragmentEffects, Log, TEXT("fragment effects: %s %s %s at %.4f (clock %.4f, serial %u)"), *Record->Name, *Clip.Type,
					*Clip.Name, Clip.Start, Clock, Serial);
				Enter(Data, Owner, Channel, Clip, Record == WeaponRecord && Record != Fragment);
			}
			if (Clip.End >= 0.0f && Clip.End > From && Clip.End <= Clock)
			{
				ExitClip(Owner.World, Channel, Clip);
			}
		}
	}
}

void FSwcFragmentEffects::Enter(const FSwcActions& Data, const FSwcEffectOwner& Owner, int32 Channel, const FSwcEffectClip& Clip,
                                bool bWeaponRecord)
{
	if (!Clip.Condition.IsEmpty())
	{
		// The named condition of the action lists (the clip resolves and tests it before the spawn, research/skill-effects.md [R]): the
		// Mannequin tags of the weapon and of the fragment records, the learned nodes. An unknown name or fact is false.
		FSwcConditionFacts Facts;
		TArray<FString> Parts;
		Owner.WeaponTags.ParseIntoArray(Parts, TEXT(","));
		for (const FSwcActionFragment* Record : {Channels[Channel].Fragment, Channels[Channel].Weapon})
		{
			TArray<FString> RecordTags;
			if (Record != nullptr && Record->Tags.ParseIntoArray(RecordTags, TEXT("+")) > 0)
			{
				Parts.Append(RecordTags);
			}
		}
		for (const FString& Tag : Parts)
		{
			Facts.Tags.Add(Tag.TrimStartAndEnd().ToLower());
		}
		Facts.LearnedNodes = Owner.LearnedNodes;
		const bool bKnown = Data.HasCondition(Clip.Condition);
		if (!bKnown || !Data.Holds(Clip.Condition, Facts))
		{
			Note(FString::Printf(TEXT("%s %s: the condition %s is %s: not played"), *Clip.Type, *Clip.Name, *Clip.Condition,
				bKnown ? TEXT("false") : TEXT("not in the action lists")));
			return;
		}
		Note(FString::Printf(TEXT("%s %s: the condition %s is true"), *Clip.Type, *Clip.Name, *Clip.Condition));
	}
	if (Clip.Kind == FSwcEffectClip::EKind::Other)
	{
		Note(FString::Printf(TEXT("%s %s: not played yet"), *Clip.Type, *Clip.Name));
		return;
	}
	if (Clip.Kind == FSwcEffectClip::EKind::Attachment)
	{
		ShowAmmo(Data, Owner, Channel, Clip);
		return;
	}
	if (Clip.Kind == FSwcEffectClip::EKind::WeaponGroup)
	{
		if (Clip.Enter != TEXT("Enable"))
		{
			Note(FString::Printf(TEXT("WeaponEffects %s: OptionOnEnter %s"), *Clip.Name, *Clip.Enter));
			return;
		}
		if (Owner.Weapon == nullptr || Owner.WeaponTags.IsEmpty())
		{
			Note(FString::Printf(TEXT("WeaponEffects %s: no weapon in the main hand"), *Clip.Name));
			return;
		}
		const TArray<FSwcWeaponEffectEntry>* Entries = Data.WeaponEffectGroup(Owner.WeaponTags, Owner.Element, Clip.Name);
		if (Entries == nullptr)
		{
			Note(FString::Printf(TEXT("WeaponEffects %s: no group for %s %s"), *Clip.Name, *Owner.WeaponTags, *Owner.Element));
			return;
		}
		for (int32 Index = 0; Index < Entries->Num(); ++Index)
		{
			StartEntry(Data, Owner, Channel, Clip, Index, (*Entries)[Index]);
		}
		return;
	}
	USwcEffectSubsystem* Effects = Owner.World != nullptr ? Owner.World->GetSubsystem<USwcEffectSubsystem>() : nullptr;
	if (Effects == nullptr || Owner.Body == nullptr)
	{
		return;
	}
	// The parent: the weapon for a clip of the weapon scope; else the joint of the body; else (an attachment and no joint of the body) the
	// helper of the weapon in the main hand [I]. The offset of the clip is in the frame of the joint or of the helper.
	USceneComponent* Parent = Owner.Body;
	FName Socket = NAME_None;
	FTransform HelperPlace = FTransform::Identity;
	bool bFound = false;
	bool bBodyJoint = false;
	if (!bWeaponRecord && !Clip.Joint.IsNone())
	{
		Socket = SocketOf(Owner.Body, Clip.Joint, bBodyJoint);
		bFound = bBodyJoint;
	}
	if (!bBodyJoint && (bWeaponRecord || !Clip.Attachment.IsNone()))
	{
		Parent = Owner.Weapon;
		if (Owner.Weapon == nullptr)
		{
			Note(FString::Printf(TEXT("ParticleEffect %s: no weapon in the main hand"), *Clip.Name));
			return;
		}
		if (!Clip.Attachment.IsNone())
		{
			bFound = SwcWeaponHelperPlace(Data, Owner.WeaponFile, Clip.Attachment, Owner.Weapon, Socket, HelperPlace);
		}
		else
		{
			Socket = SocketOf(Owner.Weapon, Clip.Joint, bFound);
		}
	}
	if (!bFound)
	{
		Note(FString::Printf(TEXT("ParticleEffect %s: no joint %s and no helper %s (%s)"), *Clip.Name, *Clip.Joint.ToString(), *Clip.Attachment.ToString(),
			bWeaponRecord ? *Owner.WeaponFile : TEXT("the body")));
		return;
	}
	FSwcEffectSettings Settings;
	Settings.AlphaScale = Clip.AlphaScale;
	Settings.TimeScale = Clip.TimeScale;
	Settings.Tint = Clip.Tint;
	const FTransform Relative = FTransform(UnrealEuler(Clip.Rotation), UnrealOffset(Clip.Position), FVector(Clip.Scale)) * HelperPlace;
	UNiagaraComponent* Component = Effects->StartAttached(Clip.Name, Parent, Socket, Relative, Clip.bFollow, Settings);
	if (Component == nullptr)
	{
		Note(FString::Printf(TEXT("ParticleEffect %s: no system"), *Clip.Name));
		return;
	}
	FLive& Item = Live.AddDefaulted_GetRef();
	Item.Channel = Channel;
	Item.Clip = &Clip;
	Item.Component = Component;
	Item.Exit = Clip.bKill ? EExit::Kill : (Clip.bKeep ? EExit::Keep : EExit::StopEmission);
}

void FSwcFragmentEffects::StartEntry(const FSwcActions& Data, const FSwcEffectOwner& Owner, int32 Channel, const FSwcEffectClip& Clip,
                                     int32 Index, const FSwcWeaponEffectEntry& Entry)
{
	USwcEffectSubsystem* Effects = Owner.World != nullptr ? Owner.World->GetSubsystem<USwcEffectSubsystem>() : nullptr;
	if (Effects == nullptr || Entry.Particle.IsEmpty())
	{
		return;
	}
	// WeaponEffectBase:StartParticle stops the emitter of the entry first.
	for (int32 LiveIndex = Live.Num() - 1; LiveIndex >= 0; --LiveIndex)
	{
		if (Live[LiveIndex].Clip != nullptr && Live[LiveIndex].Clip->Name == Clip.Name && Live[LiveIndex].Entry == Index)
		{
			ExitLive(Owner.World, Live[LiveIndex]);
			Live.RemoveAt(LiveIndex);
		}
	}
	UMeshComponent* Parent = Entry.bOnOwner ? static_cast<UMeshComponent*>(Owner.Body) : Owner.Weapon;
	FTransform Relative = Entry.bOffset
		? FTransform(UnrealEuler(FVector(FMath::DegreesToRadians(Entry.RotationDeg.X), FMath::DegreesToRadians(Entry.RotationDeg.Y),
		                                 FMath::DegreesToRadians(Entry.RotationDeg.Z))), UnrealOffset(Entry.Position), FVector(Entry.Scale))
		: FTransform::Identity;
	FName Socket = NAME_None;
	bool bFound = true;
	if (!Entry.Joint.IsNone())
	{
		Socket = SocketOf(Parent, Entry.Joint, bFound);
	}
	else if (!Entry.Attachment.IsNone())
	{
		// A helper of the weapon file: its bone and its offset in the frame of the bone (the space map of stage 1.1).
		FTransform HelperPlace;
		if (!SwcWeaponHelperPlace(Data, Owner.WeaponFile, Entry.Attachment, Parent, Socket, HelperPlace))
		{
			Note(FString::Printf(TEXT("WeaponEffects %s: no helper %s in %s"), *Clip.Name, *Entry.Attachment.ToString(), *Owner.WeaponFile));
			return;
		}
		Relative = Relative * HelperPlace;
	}
	else
	{
		Note(FString::Printf(TEXT("WeaponEffects %s: %s has no joint and no attachment: not played"), *Clip.Name, *Entry.Particle));
		return;
	}
	if (!bFound || Parent == nullptr)
	{
		Note(FString::Printf(TEXT("WeaponEffects %s: %s: no joint %s"), *Clip.Name, *Entry.Particle, *Entry.Joint.ToString()));
		return;
	}
	UNiagaraComponent* Component = Effects->StartAttached(Entry.Particle, Parent, Socket, Relative, Entry.bFollow, FSwcEffectSettings());
	if (Component == nullptr)
	{
		Note(FString::Printf(TEXT("WeaponEffects %s: %s: no system"), *Clip.Name, *Entry.Particle));
		return;
	}
	FLive& Item = Live.AddDefaulted_GetRef();
	Item.Channel = Channel;
	Item.Clip = &Clip;
	Item.Entry = Index;
	Item.Component = Component;
	Item.Exit = Entry.bKill ? EExit::Kill : EExit::StopEmission;
}

void FSwcFragmentEffects::ExitClip(UWorld* World, int32 Channel, const FSwcEffectClip& Clip)
{
	// A weapon group with OptionOnExit Disable stops its entries; another option leaves them.
	if (Clip.Kind == FSwcEffectClip::EKind::WeaponGroup && Clip.Exit != TEXT("Disable"))
	{
		return;
	}
	for (int32 Index = Live.Num() - 1; Index >= 0; --Index)
	{
		if (Live[Index].Channel == Channel && Live[Index].Clip == &Clip)
		{
			ExitLive(World, Live[Index]);
			Live.RemoveAt(Index);
		}
	}
}

void FSwcFragmentEffects::ShowAmmo(const FSwcActions& Data, const FSwcEffectOwner& Owner, int32 Channel, const FSwcEffectClip& Clip)
{
	if (Owner.Weapon == nullptr || Owner.Weapon->GetOwner() == nullptr)
	{
		Note(FString::Printf(TEXT("CAGE-Attachment %s: no weapon in the main hand"), *Clip.Attachment.ToString()));
		return;
	}
	const FString MeshPath = Owner.AmmoMesh ? Owner.AmmoMesh() : FString();
	UStaticMesh* Mesh = MeshPath.IsEmpty() ? nullptr : LoadObject<UStaticMesh>(nullptr, *MeshPath);
	FName Socket;
	FTransform Relative;
	// The attachment: a helper of the weapon file (arrowattach_tip of the bow), else a socket of the body (righthandattach: the arrow in
	// the drawing hand before the nock, the clips of player_bow.adb).
	USceneComponent* Parent = Owner.Weapon;
	const bool bWeaponHelper = Mesh != nullptr && SwcWeaponHelperPlace(Data, Owner.WeaponFile, Clip.Attachment, Owner.Weapon, Socket, Relative);
	if (!bWeaponHelper && Mesh != nullptr && Owner.Body != nullptr && Owner.BodySocket && Owner.BodySocket(Clip.Attachment, Socket, Relative))
	{
		Parent = Owner.Body;
	}
	else if (!bWeaponHelper)
	{
		Note(FString::Printf(TEXT("CAGE-Attachment %s of %s: %s"), *Clip.Attachment.ToString(), *Owner.WeaponFile,
			Mesh == nullptr ? *FString::Printf(TEXT("no ammunition mesh (%s)"), MeshPath.IsEmpty() ? TEXT("none") : *MeshPath)
			                : TEXT("no such helper of the weapon file and no such socket of the body")));
		return;
	}
	UStaticMeshComponent* Prop = AmmoProp.Get();
	if (Prop == nullptr || Prop->GetOwner() != Owner.Weapon->GetOwner())
	{
		if (Prop != nullptr)
		{
			Prop->DestroyComponent();
		}
		Prop = NewObject<UStaticMeshComponent>(Owner.Weapon->GetOwner());
		Prop->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Prop->SetGenerateOverlapEvents(false);
		Prop->RegisterComponent();
		AmmoProp = Prop;
	}
	if (Prop->GetStaticMesh() != Mesh)
	{
		Prop->SetStaticMesh(Mesh);
	}
	// The mesh of the game and the helper are both in the space map of stage 1.1: the helper transform places the arrow (its tip at the
	// origin, the shaft along the game -Y) on the joint arrowAttach_tip_jnt, which the bow prop clips move along the draw.
	if (Prop->GetAttachParent() != Parent || Prop->GetAttachSocketName() != Socket)
	{
		Prop->AttachToComponent(Parent, FAttachmentTransformRules::KeepRelativeTransform, Socket);
	}
	Prop->SetRelativeTransform(Relative);
	FLive& Item = Live.AddDefaulted_GetRef();
	Item.Channel = Channel;
	Item.Clip = &Clip;
	Item.bAmmo = true;
	UpdateAmmoVisibility();
	Note(FString::Printf(TEXT("CAGE-Attachment %s: the ammunition %s on %s"), *Clip.Attachment.ToString(), *MeshPath, *Socket.ToString()));
}

void FSwcFragmentEffects::UpdateAmmoVisibility()
{
	if (UStaticMeshComponent* Prop = AmmoProp.Get())
	{
		const bool bShown = Live.ContainsByPredicate([](const FLive& Item) { return Item.bAmmo; });
		if (Prop->IsVisible() != bShown)
		{
			Prop->SetVisibility(bShown);
		}
	}
}

void FSwcFragmentEffects::ExitLive(UWorld* World, FLive& Item)
{
	if (Item.bAmmo)
	{
		Item.bAmmo = false;
		UpdateAmmoVisibility();
		return;
	}
	UNiagaraComponent* Component = Item.Component.Get();
	USwcEffectSubsystem* Effects = World != nullptr ? World->GetSubsystem<USwcEffectSubsystem>() : nullptr;
	if (Component == nullptr || Effects == nullptr)
	{
		return;
	}
	switch (Item.Exit)
	{
	case EExit::Kill:
		Effects->Stop(Component, true);
		break;
	case EExit::StopEmission:
		Effects->Stop(Component, false);
		break;
	case EExit::Keep:
		Effects->Keep(Component);
		break;
	}
}

void FSwcFragmentEffects::StopChannel(UWorld* World, int32 Channel)
{
	for (int32 Index = Live.Num() - 1; Index >= 0; --Index)
	{
		if (Live[Index].Channel == Channel)
		{
			// A weapon group that stays after its clip (OptionOnExit not Disable) ends with its fragment too [I].
			ExitLive(World, Live[Index]);
			Live.RemoveAt(Index);
		}
	}
	if (Channel >= 0 && Channel <= 1)
	{
		Channels[Channel] = FChannel();
	}
}

void FSwcFragmentEffects::StopAll(UWorld* World)
{
	StopChannel(World, 0);
	StopChannel(World, 1);
	if (UStaticMeshComponent* Prop = AmmoProp.Get())
	{
		Prop->DestroyComponent();
	}
	AmmoProp.Reset();
}
