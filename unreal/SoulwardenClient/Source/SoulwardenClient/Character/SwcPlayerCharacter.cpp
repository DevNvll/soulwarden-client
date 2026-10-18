#include "Character/SwcPlayerCharacter.h"

#include "Animation/AnimSequence.h"
#include "Character/SwcCharacterMovement.h"
#include "Character/SwcLocomotion.h"
#include "Character/SwcPlayerAnimInstance.h"
#include "Character/SwcPlayerLooks.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "SwcBackendSubsystem.h"
#include "TimerManager.h"

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
	// The mesh stands on the bottom of the capsule.
	GetMesh()->SetRelativeLocation(FVector(0.0, 0.0, -CapsuleHalfHeight));
	GetMesh()->SetRelativeRotation(FRotator(0.0, MeshYaw, 0.0));
	// The leader is not drawn, but its pose is computed each frame for the parts.
	GetMesh()->SetVisibility(false);
	GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
}

USwcCharacterMovement* ASwcPlayerCharacter::GetSwcMovement() const
{
	return Cast<USwcCharacterMovement>(GetCharacterMovement());
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
	for (UStaticMeshComponent* Mesh : Gear)
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
	// The gear: one static mesh for each weapon and accessory with a mesh; SetWeaponState places them.
	for (const FSwcGearPart& Part : Look.Gear)
	{
		UStaticMesh* Mesh = Part.MeshPath.IsEmpty() ? nullptr : LoadObject<UStaticMesh>(nullptr, *Part.MeshPath);
		if (Mesh == nullptr || !Part.Sheathed.IsValid() || !Part.Hand.IsValid())
		{
			continue;
		}
		UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(this);
		Component->SetupAttachment(GetMesh(), Part.Sheathed.Bone);
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->RegisterComponent();
		Component->SetStaticMesh(Mesh);
		Gear.Add(Component);
		GearParts.Add(Part);
	}
	GearInHand.Init(false, Gear.Num());
	GearTimers.SetNum(Gear.Num());
	SetWeaponState(ActiveWeaponSlot, bWeaponDrawn);
}

bool ASwcPlayerCharacter::HasWeapon(uint32 Slot) const
{
	return GearParts.ContainsByPredicate([Slot](const FSwcGearPart& Part) { return Part.PaperdollSlot == Slot && !Part.bAccessory; });
}

void ASwcPlayerCharacter::PlaceGear(int32 Index, bool bInHand)
{
	const FSwcGearPart& Part = GearParts[Index];
	UStaticMeshComponent* Component = Gear[Index];
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
		// The off hand slots 27 to 29 go with the weapon slots 24 to 26.
		const uint32 WeaponSlot = Part.PaperdollSlot >= 27 ? Part.PaperdollSlot - 3 : Part.PaperdollSlot;
		GetWorldTimerManager().ClearTimer(GearTimers[Index]);
		PlaceGear(Index, bDrawn && WeaponSlot == ActiveSlot && !Part.bAccessory);
	}
}

void ASwcPlayerCharacter::RequestWeaponState(uint32 ActiveSlot, bool bDrawn)
{
	// The sheathe marker of the weapon that leaves the hand delays the draw of a new one.
	float SheatheDelay = 0.0f;
	for (int32 Index = 0; Index < Gear.Num(); ++Index)
	{
		const FSwcGearPart& Part = GearParts[Index];
		const uint32 WeaponSlot = Part.PaperdollSlot >= 27 ? Part.PaperdollSlot - 3 : Part.PaperdollSlot;
		if (GearInHand[Index] && !(bDrawn && WeaponSlot == ActiveSlot))
		{
			SheatheDelay = FMath::Max(SheatheDelay, Part.SheatheTime);
		}
	}
	ActiveWeaponSlot = ActiveSlot;
	bWeaponDrawn = bDrawn;
	for (int32 Index = 0; Index < Gear.Num(); ++Index)
	{
		const FSwcGearPart& Part = GearParts[Index];
		const uint32 WeaponSlot = Part.PaperdollSlot >= 27 ? Part.PaperdollSlot - 3 : Part.PaperdollSlot;
		const bool bInHand = bDrawn && WeaponSlot == ActiveSlot && !Part.bAccessory;
		if (bInHand == GearInHand[Index])
		{
			continue;
		}
		const float Delay = bInHand ? SheatheDelay + Part.DrawTime : Part.SheatheTime;
		UE_LOG(LogSwcBackend, Log, TEXT("weapon: slot %u %s to the %s at the marker, %.3f s"), Part.PaperdollSlot, *Part.Item,
			bInHand ? TEXT("hand") : TEXT("sheath"), Delay);
		GetWorldTimerManager().ClearTimer(GearTimers[Index]);
		if (Delay <= 0.0f)
		{
			PlaceGear(Index, bInHand);
			continue;
		}
		GetWorldTimerManager().SetTimer(GearTimers[Index], FTimerDelegate::CreateWeakLambda(this, [this, Index, bInHand]()
			{
				if (GearInHand.IsValidIndex(Index))
				{
					PlaceGear(Index, bInHand);
				}
			}), Delay, false);
	}
}
