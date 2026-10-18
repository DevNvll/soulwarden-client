#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "SwcPlayerCharacter.generated.h"

struct FSwcPlayerLook;
class USkeletalMeshComponent;

/**
 * The character of a player: the modular body of the retail client (player_<gender>.cdf). The mesh of the character is
 * the base of the gender (the leader: it has the skeleton and the clip, and it is not drawn); each visible part (head,
 * body parts, hair, armor) is one more skeletal mesh component that follows the pose of the leader (Leader Pose).
 * docs/CHARACTER.md has the rules.
 */
UCLASS()
class SOULWARDENCLIENT_API ASwcPlayerCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	ASwcPlayerCharacter();

	/** Builds the body again: the base of the gender, the idle clip, and the parts that have a mesh. */
	void ApplyLook(const FSwcPlayerLook& Look, const FString& BaseMesh, const FString& IdleAnimation);

	/** The number of drawn parts (a log line and a check). */
	int32 PartCount() const { return Parts.Num(); }

private:
	UPROPERTY()
	TArray<TObjectPtr<USkeletalMeshComponent>> Parts;
};
