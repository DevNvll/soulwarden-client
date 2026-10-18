#pragma once

#include "CoreMinimal.h"
#include "Combat/SwcContactFilter.h"
#include "Combat/SwcMeleeShape.h"
#include "Components/ActorComponent.h"
#include "Combat/SwcAttackGraph.h"
#include "Combat/SwcLife.h"
#include "Net/Codec/SwcRepMessages.h"

#include <memory>
#include <vector>

#include "SwcPlayerActions.generated.h"

class ASwcPlayerCharacter;
class FSwcActions;
class USkeletalMeshComponent;
class USwcPlayerAnimInstance;
struct FSwcActionFragment;
struct FSwcConditionFacts;
struct FSwcStrikeShape;
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
 * - the stance: with the weapon in the hand, the movement fragments of its tag set (and of its shield);
 * - the melee contacts (stage P2.8): in each input step in the window of a strike of the attack fragment, the shapes of its
 *   damage clips at the shown pose go to the contact query of the creatures; one contact for each target for each strike
 *   goes into list 3 of the next input entry;
 * - the life (stage P2.10): the death plays r_Death and locks the input, the respawn plays Respawn (playeractions_death).
 *
 * Command line: -SwcDrawStrikes draws the shapes; -SwcContactProbe (development runs) sends one made-up contact for each
 * strike at the center of its first shape (a receiver that does not exist: the backend refuses it; a check of list 3).
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
	/** The dodge key (left Shift). */
	void OnDodgeKey();
	/** A key of the bar (the place 0 to 2: Q, R, F) went down or up (stage P3.5): the press bits at the step of the driver. */
	void OnAbilityKey(uint32 Place, bool bDown);
	/** The aim button (the right mouse button): the aim of a ranged weapon in the hand (AimDownSights). */
	void OnAimButton(bool bDown) { bAimHeld = bDown; }
	/** The aim plays (the AimCamera, the walk); the target lock can start (DisableCameraLock of the camera action list: not in
	 *  the aim, not in an attack before its mark CanTransition). */
	bool IsAiming() const { return bAiming; }
	bool IsLockAllowed() const { return bLockAllowed; }

	/**
	 * The life of the character (stage P2.10, docs/ACTIONS.md "The death and the respawn"): the dead mark of the vitals state,
	 * and the loading screen of a respawn or a far travel (from the request or message 3951 to message 3954). The death
	 * (the action Death of playeractions_death) sheathes the weapons, plays r_Death with the tag Unarmed, holds it, and locks
	 * the input; when the character lives again and no loading screen shows, the action Respawn plays the fragment Respawn,
	 * and the input is free at its mark CanTransition.
	 */
	void SetLife(bool bDead, bool bLoading);
	/**
	 * A timed use of an object (stage P2.10, the action DelayedInteraction of playeractions_interact: the options Trigger,
	 * PickupBuildable, RevivePlayer): the weapons go to their sheaths, Delayed_Interact plays (the kneel), and the character
	 * does not move until the use ends.
	 */
	void SetTimedUse(bool bOn);
	/** @return True while the input of the player does nothing: the death, a loading screen, the respawn before its mark, a
	 *  timed use. */
	bool IsInputLocked() const { return LifeState.locked(); }
	bool IsDead() const { return LifeState.state() == swc::combat::Life::State::dead; }

	/** The count of the attack fragments that started, and of the weapon switches of the driver (a log line and a check). */
	uint32 GetVisitCount() const { return Visits; }
	/** The learned nodes of the character (the ProgressionPoint state; stage P3.5): the rows of the strikes. */
	void SetLearnedNodes(uint32 Revision, const std::vector<std::uint32_t>& Nodes);
	/** @return The attack graph of a weapon slot in the driver (24, 25), or null. */
	const swc::combat::Graph* GetDriverGraph(uint32 Slot) const
	{
		return Slot == 24 || Slot == 25 ? static_cast<const swc::combat::Graph*>(DriverGraphs[Slot - 24]) : nullptr;
	}
	uint32 GetSwitchCount() const { return SeenSwitches; }

	/** The melee contact query of the creatures: the contacts of one shape with the hit volumes (the item fields). */
	using FContactQuery = TFunction<void(const FSwcMeleeShape&, std::vector<swc::net::MeleeContactItem>&)>;
	void SetContactQuery(FContactQuery Query) { ContactQuery = MoveTemp(Query); }
	/** @return The contacts of this frame for list 3 of the next input entry. */
	std::vector<swc::net::MeleeContactItem> TakeContacts();
	/** The counts of the shapes that went to the query, of the strikes with a shape, and of the contacts. */
	uint32 GetShapeQueries() const { return ShapeQueries; }
	uint32 GetStrikeCount() const { return StrikeCount; }
	uint32 GetSkippedStrikes() const { return SkippedStrikes; }
	uint32 GetContactCount() const { return ContactCount; }

protected:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

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
	/** The strikes of a graph fragment whose window has a clock of (From, To]: their shapes to the query. */
	void QueryStrikes(const FSwcWeaponActions& Weapon, int32 GraphFragment, uint32 VisitNumber, float From, float To);
	/** The dodge of the driver on the FullBody channel, its root motion and its facing. @return False when no dodge plays. */
	bool PlayDodge(const swc::combat::Driver::Snapshot& Snapshot, float Since, float DeltaTime);
	/** The strikes of this frame at its final pose (the delegate of the bone transforms of the mesh). */
	void OnBonesFinal();
	/** Draws the shapes of the strikes whose window has the clock (-SwcDrawStrikes). */
	void DrawStrikes(const FSwcWeaponActions& Weapon, int32 GraphFragment, float Clock) const;
	/** @return False when the frame of the shape is not there (no weapon in the hand). */
	bool ShapeOf(const FSwcStrikeShape& Clip, FSwcMeleeShape& Out) const;
	/** Starts a fragment of the life on the FullBody channel: Kind of the map Life or Use of the unarmed set. */
	void StartLifeFragment(const TMap<FString, int32> FSwcWeaponActions::* Map, const TCHAR* Kind);
	/** The death and the timed use: the attack and the aim end, the weapons go to their sheaths at once. */
	void SheatheForAction();
	/** The death or the respawn plays: its fragment, no movement. @return False when the character is alive and free. */
	bool UpdateLife(float DeltaTime);

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
	/** The keys of the bar, the presses and the shots of the driver that the log saw, the blocks of the movement by a creature. */
	bool bAbilityDown[3] = {false, false, false};
	uint32 SeenAbilityPresses = 0;
	uint32 SeenShots = 0;
	uint32 SeenBlocks = 0;
	uint32 CollidedVisit = 0;
	uint32 NodesRevision = MAX_uint32;
	TSet<uint32> LearnedNodes;
	/** The strikes with no row for the learned nodes that the query skipped. */
	uint32 SkippedStrikes = 0;
	/** The melee contacts: the query, the contacts of this frame, the targets of each strike of the visit, the last
	 *  center of each shape (the cast direction), and the counts. */
	struct FStrikeWork
	{
		const FSwcWeaponActions* Weapon = nullptr;
		int32 GraphFragment = 0;
		uint32 Visit = 0;
		float From = 0.0f;
		float To = 0.0f;
		float Clock = 0.0f;
		bool bQuery = false;
	};
	FStrikeWork Work;
	/** The dodge: its number, the number of a dodge that ended (its fragment or a move key after CanTransition), its fragment. */
	uint32 DodgeNumber = 0;
	uint32 DodgeReleased = 0;
	int32 DodgeFragment = INDEX_NONE;
	const FSwcWeaponActions* DodgeWeapon = nullptr;
	bool bDodgePlayed = false;
	/** The largest speed that the movement keeps at the end of the action (cm/s; 0: no limit). */
	float CarryLimit = 0.0f;
	bool bAimHeld = false;
	bool bAiming = false;
	bool bLockAllowed = true;
	bool bAttackBeforeTransition = false;
	swc::combat::Life LifeState;
	int32 LifeFragment = INDEX_NONE;
	uint32 LifeSerial = 0;
	FDelegateHandle BonesHandle;
	TWeakObjectPtr<USkeletalMeshComponent> BonesMesh;
	FContactQuery ContactQuery;
	std::vector<swc::net::MeleeContactItem> Outgoing;
	swc::combat::ContactFilter Contacted;
	TMap<int32, FVector> LastCenters;
	TSet<int32> StrikesSeen;
	float StrikeClock = -1.0f;
	uint32 ShapeQueries = 0;
	uint32 StrikeCount = 0;
	uint32 ContactCount = 0;
	bool bDrawStrikes = false;
	bool bContactProbe = false;
	bool bQueryNoted = false;
};
