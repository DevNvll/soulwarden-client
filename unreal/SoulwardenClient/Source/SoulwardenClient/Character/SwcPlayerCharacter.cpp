#include "Character/SwcPlayerCharacter.h"

#include "Animation/AnimSequence.h"
#include "Character/SwcCharacterMovement.h"
#include "Character/SwcLocomotion.h"
#include "Character/SwcPlayerActions.h"
#include "Character/SwcPlayerAnimInstance.h"
#include "Character/SwcPlayerLooks.h"
#include "Character/SwcRangedView.h"
#include "Character/SwcWeaponAnimInstance.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "SwcBackendSubsystem.h"
#include "Effects/SwcStatusEffects.h"
#include "Components/WorldPartitionStreamingSourceComponent.h"

namespace
{
/** The capsule of the player slice: radius 0.41 m, half height 0.81 m (research feel-values.md R2.6). */
constexpr float CapsuleRadius = 41.0f;
constexpr float CapsuleHalfHeight = 81.0f;
/** The imported player mesh faces 90 degrees to the left of the actor (stage 1.6; the space map of stage 1.1 keeps it). */
constexpr double MeshYaw = 90.0;
} // namespace

ASwcPlayerCharacter::ASwcPlayerCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<USwcCharacterMovement>(ACharacter::CharacterMovementComponentName))
{
	PrimaryActorTick.bCanEverTick = false;
	GetCapsuleComponent()->InitCapsuleSize(CapsuleRadius, CapsuleHalfHeight);
	// The character turns with its movement (strafe mode: to the camera yaw while it moves), not with the controller.
	bUseControllerRotationYaw = false;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;
	// The World Partition cells of the world level load around the character (stage 1.3).
	CreateDefaultSubobject<UWorldPartitionStreamingSourceComponent>(TEXT("WorldStreaming"));
	// The mesh stands on the bottom of the capsule.
	GetMesh()->SetRelativeLocation(FVector(0.0, 0.0, -CapsuleHalfHeight));
	GetMesh()->SetRelativeRotation(FRotator(0.0, MeshYaw, 0.0));
	// The leader is not drawn, but its pose is computed each frame for the parts.
	GetMesh()->SetVisibility(false);
	GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	Actions = CreateDefaultSubobject<USwcPlayerActions>(TEXT("Actions"));
	RangedView = CreateDefaultSubobject<USwcRangedView>(TEXT("RangedView"));
}

USwcCharacterMovement* ASwcPlayerCharacter::GetSwcMovement() const
{
	return Cast<USwcCharacterMovement>(GetCharacterMovement());
}

void ASwcPlayerCharacter::SetDummy(const FVector* Feet, double Yaw, bool bArmed)
{
	if (Feet != nullptr)
	{
		GetMesh()->SetWorldLocationAndRotation(*Feet, FRotator(0.0, Yaw + MeshYaw, 0.0));
		for (const TObjectPtr<UMeshComponent>& Component : Gear)
		{
			if (Component != nullptr && !bArmed)
			{
				Component->SetVisibility(false);
			}
		}
		return;
	}
	GetMesh()->SetRelativeLocationAndRotation(FVector(0.0, 0.0, -CapsuleHalfHeight), FRotator(0.0, MeshYaw, 0.0));
	for (int32 Index = 0; Index < Gear.Num(); ++Index)
	{
		PlaceGear(Index, GearInHand.IsValidIndex(Index) && GearInHand[Index]);
	}
}

FVector ASwcPlayerCharacter::GetFeetLocation() const
{
	return GetActorLocation() - FVector(0.0, 0.0, GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
}

void ASwcPlayerCharacter::ApplyLook(const FSwcPlayerLook& Look, const FString& BaseMesh, const FString& IdleAnimation,
	const TSharedPtr<FSwcLocomotion>& Locomotion)
{
	for (USkeletalMeshComponent* Part : Parts)
	{
		if (Part != nullptr)
		{
			Part->DestroyComponent();
		}
	}
	Parts.Reset();
	for (UMeshComponent* Mesh : Gear)
	{
		if (Mesh != nullptr)
		{
			Mesh->DestroyComponent();
		}
	}
	Gear.Reset();
	GearParts.Reset();
	USkeletalMesh* Base = BaseMesh.IsEmpty() ? nullptr : LoadObject<USkeletalMesh>(nullptr, *BaseMesh);
	if (Base == nullptr)
	{
		UE_LOG(LogSwcBackend, Warning, TEXT("character: no base mesh %s (tools/convert-player.sh base, tools/import-player.sh)"), *BaseMesh);
		return;
	}
	GetMesh()->SetSkeletalMesh(Base);
	GetSwcMovement()->SetLocomotion(Locomotion);
	if (Locomotion.IsValid())
	{
		GetMesh()->SetAnimationMode(EAnimationMode::AnimationBlueprint);
		GetMesh()->SetAnimInstanceClass(USwcPlayerAnimInstance::StaticClass());
		if (USwcPlayerAnimInstance* Animation = Cast<USwcPlayerAnimInstance>(GetMesh()->GetAnimInstance()))
		{
			Animation->SetLocomotion(Locomotion);
		}
	}
	else if (UAnimSequence* Idle = IdleAnimation.IsEmpty() ? nullptr : LoadObject<UAnimSequence>(nullptr, *IdleAnimation))
	{
		GetMesh()->PlayAnimation(Idle, true);
	}
	for (const FSwcLookPart& Part : Look.Parts)
	{
		USkeletalMesh* Mesh = Part.MeshPath.IsEmpty() ? nullptr : LoadObject<USkeletalMesh>(nullptr, *Part.MeshPath);
		if (Mesh == nullptr)
		{
			continue;
		}
		USkeletalMeshComponent* Component = NewObject<USkeletalMeshComponent>(this, Part.Slot);
		Component->SetupAttachment(GetMesh());
		Component->RegisterComponent();
		Component->SetSkeletalMesh(Mesh);
		Component->SetLeaderPoseComponent(GetMesh());
		if (Part.Tint.A > 0.0f)
		{
			// The hair color: a tint of the base color of each material of the part.
			for (int32 Index = 0; Index < Component->GetNumMaterials(); ++Index)
			{
				if (UMaterialInstanceDynamic* Tinted = Component->CreateAndSetMaterialInstanceDynamic(Index))
				{
					Tinted->SetVectorParameterValue(TEXT("BaseColorFactor"), Part.Tint);
				}
			}
		}
		Parts.Add(Component);
	}
	// The gear: one mesh for each weapon and accessory with a mesh; SetWeaponState places them. A weapon with weapon clips is a
	// skeletal mesh with the weapon animation (AST-202): it updates after the base (its leader gives the weapon visits).
	for (const FSwcGearPart& Part : Look.Gear)
	{
		if (!Part.Sheathed.IsValid() || !Part.Hand.IsValid())
		{
			continue;
		}
		UMeshComponent* Component = nullptr;
		if (USkeletalMesh* Skeletal = Part.bSkeletal ? LoadObject<USkeletalMesh>(nullptr, *Part.MeshPath) : nullptr)
		{
			USkeletalMeshComponent* Weapon = NewObject<USkeletalMeshComponent>(this);
			Weapon->SetupAttachment(GetMesh(), Part.Sheathed.Bone);
			Weapon->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Weapon->RegisterComponent();
			Weapon->SetSkeletalMesh(Skeletal);
			Weapon->SetAnimationMode(EAnimationMode::AnimationBlueprint);
			Weapon->SetAnimInstanceClass(USwcWeaponAnimInstance::StaticClass());
			Weapon->AddTickPrerequisiteComponent(GetMesh());
			Component = Weapon;
		}
		else if (UStaticMesh* Static = !Part.bSkeletal && !Part.MeshPath.IsEmpty() ? LoadObject<UStaticMesh>(nullptr, *Part.MeshPath) : nullptr)
		{
			UStaticMeshComponent* Weapon = NewObject<UStaticMeshComponent>(this);
			Weapon->SetupAttachment(GetMesh(), Part.Sheathed.Bone);
			Weapon->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Weapon->RegisterComponent();
			Weapon->SetStaticMesh(Static);
			Component = Weapon;
		}
		if (Component == nullptr)
		{
			continue;
		}
		Gear.Add(Component);
		GearParts.Add(Part);
	}
	GearInHand.Init(false, Gear.Num());
	GearAnimationData.Init(nullptr, Gear.Num());
	SetWeaponState(ActiveWeaponSlot, bWeaponDrawn);
}

bool ASwcPlayerCharacter::GetMainHandWeapon(UMeshComponent*& OutWeapon, FString& OutTags, FString& OutFile) const
{
	for (int32 Index = 0; Index < GearParts.Num(); ++Index)
	{
		if (GearParts[Index].PaperdollSlot == ActiveWeaponSlot && !GearParts[Index].bAccessory)
		{
			OutWeapon = Gear[Index];
			OutTags = GearParts[Index].Tags;
			TArray<FString> Fields;
			GearParts[Index].MeshKey.ParseIntoArray(Fields, TEXT("|"));
			OutFile = Fields.Num() > 1 ? Fields[1] : FString();
			return OutWeapon != nullptr;
		}
	}
	return false;
}

bool ASwcPlayerCharacter::HasWeapon(uint32 Slot) const
{
	return GearParts.ContainsByPredicate([Slot](const FSwcGearPart& Part) { return Part.PaperdollSlot == Slot && !Part.bAccessory; });
}

void ASwcPlayerCharacter::PlaceGear(int32 Index, bool bInHand)
{
	const FSwcGearPart& Part = GearParts[Index];
	UMeshComponent* Component = Gear[Index];
	const FSwcAttachPlace& Place = bInHand ? Part.Hand : Part.Sheathed;
	GearInHand[Index] = bInHand;
	Component->AttachToComponent(GetMesh(), FAttachmentTransformRules::KeepRelativeTransform, Place.Bone);
	Component->SetRelativeTransform(Place.Offset);
	Component->SetVisibility(bInHand || Part.bAlwaysVisible || !Part.bHideSheathed);
}

void ASwcPlayerCharacter::SetWeaponState(uint32 ActiveSlot, bool bDrawn)
{
	ActiveWeaponSlot = ActiveSlot;
	bWeaponDrawn = bDrawn;
	for (int32 Index = 0; Index < Gear.Num(); ++Index)
	{
		const FSwcGearPart& Part = GearParts[Index];
		// The off hand slots 27 to 29 go with the weapon slots 24 to 26. A tool (33 to 36) is in the hand only in a gather.
		const uint32 WeaponSlot = Part.PaperdollSlot >= 27 && Part.PaperdollSlot <= 29 ? Part.PaperdollSlot - 3 : Part.PaperdollSlot;
		PlaceGear(Index, bDrawn && WeaponSlot == ActiveSlot && !Part.bAccessory);
	}
}

void ASwcPlayerCharacter::SetToolInHand(uint32 Slot)
{
	for (int32 Index = 0; Index < Gear.Num(); ++Index)
	{
		const uint32 PartSlot = GearParts[Index].PaperdollSlot;
		if (PartSlot >= 33 && PartSlot <= 36 && !GearParts[Index].bAccessory)
		{
			PlaceGear(Index, PartSlot == Slot);
		}
	}
}

void ASwcPlayerCharacter::SetAnimatedWeapon(uint32 Slot, const TSharedPtr<FSwcActions>& Data, USwcPlayerAnimInstance* Leader)
{
	for (int32 Index = 0; Index < Gear.Num(); ++Index)
	{
		const USkeletalMeshComponent* Weapon = Cast<USkeletalMeshComponent>(Gear[Index]);
		USwcWeaponAnimInstance* Animation = Weapon != nullptr ? Cast<USwcWeaponAnimInstance>(Weapon->GetAnimInstance()) : nullptr;
		if (Animation == nullptr)
		{
			continue;
		}
		const FSwcGearPart& Part = GearParts[Index];
		if (GearAnimationData[Index] != Data.Get())
		{
			GearAnimationData[Index] = Data.Get();
			Animation->Setup(Data, Part.MeshPath, Part.ClipPaths);
			UE_LOG(LogSwcBackend, Log, TEXT("weapon: %s (slot %u) is a skeletal mesh with %d of %d weapon clips"), *Part.Item, Part.PaperdollSlot,
				Animation->GetClipCount(), Part.ClipPaths.Num());
		}
		const bool bFollow = Slot != 0 && Part.PaperdollSlot == Slot && !Part.bAccessory;
		if (bFollow != Animation->HasLeader())
		{
			Animation->SetLeader(bFollow ? Leader : nullptr);
		}
	}
}

void ASwcPlayerCharacter::SetLogicalWeaponState(uint32 ActiveSlot, bool bDrawn)
{
	ActiveWeaponSlot = ActiveSlot;
	bWeaponDrawn = bDrawn;
}

void ASwcPlayerCharacter::UpdateStatusEffects(const TArray<TPair<uint32, bool>>& Active)
{
	if (!StatusEffects.IsValid())
	{
		StatusEffects = MakeShared<FSwcStatusEffects>();
		StatusEffects->Setup(true, FString());
	}
	StatusEffects->Update(GetWorld(), GetMesh(), Active);
}

void ASwcPlayerCharacter::EndPlay(const EEndPlayReason::Type Reason)
{
	if (StatusEffects.IsValid())
	{
		StatusEffects->StopAll(GetWorld());
	}
	Super::EndPlay(Reason);
}
