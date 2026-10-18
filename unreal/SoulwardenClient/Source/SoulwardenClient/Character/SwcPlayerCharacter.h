#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "SwcPlayerCharacter.generated.h"

struct FSwcPlayerLook;
class FSwcLocomotion;
class UCameraComponent;
class USkeletalMeshComponent;
class USpringArmComponent;
class USwcCharacterMovement;

/**
 * The character of a player: the modular body of the retail client (player_<gender>.cdf). The mesh of the character is
 * the base of the gender (the leader: it has the skeleton and the animation, and it is not drawn); each visible part
 * (head, body parts, hair, armor) is one more skeletal mesh component that follows the pose of the leader (Leader Pose).
 * The movement (USwcCharacterMovement) and the animation (USwcPlayerAnimInstance) use the values of the game data.
 * docs/CHARACTER.md and docs/MOVEMENT.md have the rules.
 */
UCLASS()
class SOULWARDENCLIENT_API ASwcPlayerCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	explicit ASwcPlayerCharacter(const FObjectInitializer& ObjectInitializer);

	/**
	 * Builds the body again: the base of the gender, its animation, and the parts that have a mesh.
	 * @param Locomotion The locomotion data of the gender, or null: then the base plays only the idle clip.
	 */
	void ApplyLook(const FSwcPlayerLook& Look, const FString& BaseMesh, const FString& IdleAnimation,
		const TSharedPtr<FSwcLocomotion>& Locomotion);

	/** The number of drawn parts (a log line and a check). */
	int32 PartCount() const { return Parts.Num(); }

	USwcCharacterMovement* GetSwcMovement() const;
	UCameraComponent* GetCamera() const { return Camera; }
	/** The feet of the character (the bottom of the capsule). */
	FVector GetFeetLocation() const;

private:
	UPROPERTY()
	TArray<TObjectPtr<USkeletalMeshComponent>> Parts;
	UPROPERTY()
	TObjectPtr<USpringArmComponent> CameraArm;
	UPROPERTY()
	TObjectPtr<UCameraComponent> Camera;
};
