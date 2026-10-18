#include "Character/SwcPlayerCharacter.h"

#include "Animation/AnimSequence.h"
#include "Character/SwcPlayerLooks.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "SwcBackendSubsystem.h"

namespace
{
constexpr double MeshYaw = 90.0;
} // namespace

ASwcPlayerCharacter::ASwcPlayerCharacter()
{
	PrimaryActorTick.bCanEverTick = false;
	// The capsule of the player slice comes with the movement (stage 1.7). The mesh stands on the bottom of the capsule.
	GetMesh()->SetRelativeLocation(FVector(0.0, 0.0, -GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight()));
	// The imported player mesh faces 90 degrees to the left of the actor (picture of stage 1.6 with the provisional space
	// map of Net/SwcSpace.h): the yaw of the mesh makes the body face the forward vector of the actor.
	GetMesh()->SetRelativeRotation(FRotator(0.0, MeshYaw, 0.0));
	// The leader is not drawn, but its pose is computed each frame for the parts.
	GetMesh()->SetVisibility(false);
	GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
}

void ASwcPlayerCharacter::ApplyLook(const FSwcPlayerLook& Look, const FString& BaseMesh, const FString& IdleAnimation)
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
	if (UAnimSequence* Idle = IdleAnimation.IsEmpty() ? nullptr : LoadObject<UAnimSequence>(nullptr, *IdleAnimation))
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
