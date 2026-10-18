#include "Character/SwcPlayerCharacter.h"

#include "Animation/AnimSequence.h"
#include "Camera/CameraComponent.h"
#include "Character/SwcCharacterMovement.h"
#include "Character/SwcLocomotion.h"
#include "Character/SwcPlayerAnimInstance.h"
#include "Character/SwcPlayerLooks.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/SpringArmComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "SwcBackendSubsystem.h"

namespace
{
/** The capsule of the player slice: radius 0.41 m, half height 0.81 m (research feel-values.md R2.6). */
constexpr float CapsuleRadius = 41.0f;
constexpr float CapsuleHalfHeight = 81.0f;
/** The imported player mesh faces 90 degrees to the left of the actor (stage 1.6; the space map of stage 1.1 keeps it). */
constexpr double MeshYaw = 90.0;
/**
 * The free camera (MidCamera) of the retail client, research feel-values.md R1.6: the pivot 1.7 m above the feet and
 * 0.5 m to the right, the arm 4.0 m (distance 2.0 m + the default zoom 2.0 m), the collision sphere 0.084 m, no lag, the
 * player FOV 57 degrees (vertical) = 88.0 degrees horizontal at 16:9. Stage 1.8 adds the zoom, the blends, and the modes.
 */
constexpr double PivotHeight = 170.0;
constexpr double PivotRight = 50.0;
constexpr float ArmLength = 400.0f;
constexpr float ProbeRadius = 8.4f;
constexpr float HorizontalFov = 88.0f;
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

	CameraArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraArm"));
	CameraArm->SetupAttachment(GetCapsuleComponent());
	CameraArm->SetRelativeLocation(FVector(0.0, 0.0, PivotHeight - CapsuleHalfHeight));
	CameraArm->TargetArmLength = ArmLength;
	CameraArm->SocketOffset = FVector(0.0, PivotRight, 0.0);
	CameraArm->bUsePawnControlRotation = true;
	CameraArm->bDoCollisionTest = true;
	CameraArm->ProbeSize = ProbeRadius;
	CameraArm->ProbeChannel = ECC_Camera;
	CameraArm->bEnableCameraLag = false;
	CameraArm->bEnableCameraRotationLag = false;
	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(CameraArm, USpringArmComponent::SocketName);
	Camera->FieldOfView = HorizontalFov;
	Camera->bUsePawnControlRotation = false;
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
}
