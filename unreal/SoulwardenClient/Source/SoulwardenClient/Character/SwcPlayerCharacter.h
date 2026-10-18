#pragma once

#include "CoreMinimal.h"
#include "Character/SwcPlayerLooks.h"
#include "GameFramework/Character.h"
#include "SwcPlayerCharacter.generated.h"

class FSwcLocomotion;
class USkeletalMeshComponent;
class UStaticMeshComponent;
class USwcCharacterMovement;

/**
 * The character of a player: the modular body of the retail client (player_<gender>.cdf). The mesh of the character is
 * the base of the gender (the leader: it has the skeleton and the animation, and it is not drawn); each visible part
 * (head, body parts, hair, armor) is one more skeletal mesh component that follows the pose of the leader (Leader Pose).
 * The movement (USwcCharacterMovement) and the animation (USwcPlayerAnimInstance) use the values of the game data; the
 * camera manager of the player controller (ASwcCameraManager) makes the view. docs/CHARACTER.md, docs/MOVEMENT.md, and
 * docs/CAMERA.md have the rules.
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

	/** The number of drawn parts and of the gear meshes (a log line and a check). */
	int32 PartCount() const { return Parts.Num(); }
	int32 GearCount() const { return Gear.Num(); }

	/**
	 * The weapon state: the active weapon slot (24 to 26) and drawn or sheathed. The gear of the active weapon (and of
	 * its off hand slot) goes to the hand when it is drawn and to its sheath place when it is sheathed; the other
	 * weapons are in their sheath places. docs/GEAR.md has the rules.
	 */
	void SetWeaponState(uint32 ActiveSlot, bool bDrawn);
	/**
	 * A draw or a sheathe of the keys: the logical state changes at once; each gear part goes to its new place at the
	 * marker time of the Mannequin data (gear.md "Active, sheathed, and attached are separate states"). A switch from a
	 * drawn weapon to another one sheathes the first one, then draws the second one after that sheathe marker.
	 */
	void RequestWeaponState(uint32 ActiveSlot, bool bDrawn);
	uint32 GetActiveWeaponSlot() const { return ActiveWeaponSlot; }
	bool IsWeaponDrawn() const { return bWeaponDrawn; }
	/** @return True when a weapon slot (24 to 26) has a weapon. */
	bool HasWeapon(uint32 Slot) const;

	USwcCharacterMovement* GetSwcMovement() const;
	/** The feet of the character (the bottom of the capsule). */
	FVector GetFeetLocation() const;

private:
	UPROPERTY()
	TArray<TObjectPtr<USkeletalMeshComponent>> Parts;
	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> Gear;
	TArray<FSwcGearPart> GearParts;
	/** The physical place of each gear part (true: the hand), and its timer of a move to come. */
	TArray<bool> GearInHand;
	TArray<FTimerHandle> GearTimers;
	void PlaceGear(int32 Index, bool bInHand);
	uint32 ActiveWeaponSlot = 0;
	bool bWeaponDrawn = false;
};
