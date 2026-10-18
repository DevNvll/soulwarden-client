#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"

#include <memory>

#include "SwcPlayerActions.generated.h"

class ASwcPlayerCharacter;
class FSwcActions;
class USwcPlayerAnimInstance;
struct FSwcActionFragment;
struct FSwcConditionFacts;
struct FSwcWeaponActions;

namespace swc::combat
{
class Driver;
}

/**
 * The actions of the player with a weapon (stage P2.7, docs/ACTIONS.md):
 * - the attacks: the attack driver of the session (swc::combat::Driver) follows the attack graph of the weapon in the hand
 *   at the input steps, with the rules of the backend; this component plays the fragment of each visit at its clock on the
 *   FullBody channel of the animation, moves the character with the root track of the fragment (AnimationDrivenMotion,
 *   with the conditionals of the action list), and turns it (CameraRelativeFacing, InputRelativeFacing);
 * - the weapon keys and the sheathe key: the draw and the sheathe fragments on the UpperBody channel; each gear part moves
 *   at the mark of its action (unsheathe_weapon, Unsheathe_Offhand, sheathe_weapon); a key of the other weapon waits for
 *   the switch step of the driver (the switch marks of the fragment that plays);
 * - the stance: with the weapon in the hand, the movement fragments of its tag set (and of its shield).
 */
UCLASS()
class SOULWARDENCLIENT_API USwcPlayerActions : public UActorComponent
{
	GENERATED_BODY()

public:
	USwcPlayerActions();

	/**
	 * The action data, the gender of the body, and the attack driver of the session (null: no session; then a key draws at
	 * once and the attack button does nothing). Call it after each ApplyLook (the animation instance is new).
	 */
	void Setup(const TSharedPtr<FSwcActions>& Data, bool bFemale, const std::shared_ptr<swc::combat::Driver>& InDriver);
	/** The gear or the weapon state of the paperdoll changed: the attack graphs of the weapon slots go to the driver. */
	void OnGearChanged();
	/** A weapon key (slot 24 or 25; the controller puts its bits into the input report). */
	void OnWeaponKey(uint32 Slot);
	/** The sheathe key: the active weapon goes to its sheath or to the hand. */
	void OnSheatheKey();
	/** The attack button (the left mouse button) went down or up. */
	void OnAttackButton(bool bDown);

	/** The count of the attack fragments that started, and of the weapon switches of the driver (a log line and a check). */
	uint32 GetVisitCount() const { return Visits; }
	uint32 GetSwitchCount() const { return SeenSwitches; }

protected:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	/** A draw or a sheathe on the UpperBody channel. */
	struct FUpperAction
	{
		int32 Fragment = INDEX_NONE;
		uint32 Slot = 0;
		bool bDraw = false;
		/** The game time of its start (s), its last clock, its length (s), and its serial for the animation. */
		double Start = 0.0;
		float Last = -1.0f;
		float Length = 0.0f;
		uint32 Serial = 0;
		/** The draw fragment has the mark Unsheathe_Offhand (else the off hand moves at unsheathe_weapon). */
		bool bOffhandMark = false;
	};

	ASwcPlayerCharacter* Character() const;
	USwcPlayerAnimInstance* Animation() const;
	/** @return The actions of the weapon of a slot (24 to 26), or null. */
	const FSwcWeaponActions* WeaponOf(uint32 Slot) const;
	/** The logical change of the weapon state, with the draw and the sheathe actions (gear.md: the marks move the parts). */
	void Request(uint32 Slot, bool bDrawn);
	void StartUpper(uint32 Slot, bool bDraw, double Start);
	/** Places the parts of a weapon slot: its main hand part and (bOffhand) its off-hand part. */
	void PlaceSlot(uint32 Slot, bool bInHand, bool bMain, bool bOffhand);
	/** The weapon of a slot goes to the hand at once (an attack starts), the other weapons to their sheaths. */
	void DrawAtOnce(uint32 Slot);
	void UpdateUpper(double Now);
	void UpdateAttack(float DeltaTime);
	void Move(const FSwcActionFragment& Fragment, const FSwcWeaponActions& Weapon, float From, float To, bool bStart, float DeltaTime);
	void FillFacts(const FSwcActionFragment& Fragment, const FSwcWeaponActions& Weapon, float Time, FSwcConditionFacts& Facts) const;

	TSharedPtr<FSwcActions> Data;
	std::shared_ptr<swc::combat::Driver> Driver;
	bool bFemale = false;
	/** The graphs that the driver has, and the weapon in the hand that it got. */
	const void* DriverGraphs[2] = {nullptr, nullptr};
	uint32 DriverHeld = 0;
	TArray<FUpperAction> Upper;
	uint32 UpperSerial = 0;
	/** The weapon in the hand of the pose: its slot (0: none) and its shield in the hand. */
	uint32 VisualSlot = 0;
	bool bVisualOffhand = false;
	/** The attack: the visit that plays, its last clock, the inherited velocity (cm/s, x right, y forward of the actor). */
	uint32 Visit = 0;
	float LastClock = 0.0f;
	bool bPlaying = false;
	FVector Inherited = FVector::ZeroVector;
	uint32 Visits = 0;
	uint32 SeenSwitches = 0;
	uint32 SeenLost = 0;
	bool bButtonDown = false;
};
