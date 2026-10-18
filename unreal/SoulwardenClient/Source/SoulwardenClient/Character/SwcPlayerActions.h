#pragma once

#include "CoreMinimal.h"
#include "Combat/SwcMeleeShape.h"
#include "Components/ActorComponent.h"
#include "Combat/SwcAttackGraph.h"
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
 *   goes into list 3 of the next input entry.
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

	/** The count of the attack fragments that started, and of the weapon switches of the driver (a log line and a check). */
	uint32 GetVisitCount() const { return Visits; }
	uint32 GetSwitchCount() const { return SeenSwitches; }

	/** The melee contact query of the creatures: the contacts of one shape with the hit volumes (the item fields). */
	using FContactQuery = TFunction<void(const FSwcMeleeShape&, std::vector<swc::net::MeleeContactItem>&)>;
	void SetContactQuery(FContactQuery Query) { ContactQuery = MoveTemp(Query); }
	/** @return The contacts of this frame for list 3 of the next input entry. */
	std::vector<swc::net::MeleeContactItem> TakeContacts();
	/** The counts of the shapes that went to the query, of the strikes with a shape, and of the contacts. */
	uint32 GetShapeQueries() const { return ShapeQueries; }
	uint32 GetStrikeCount() const { return StrikeCount; }
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
	FDelegateHandle BonesHandle;
	TWeakObjectPtr<USkeletalMeshComponent> BonesMesh;
	FContactQuery ContactQuery;
	std::vector<swc::net::MeleeContactItem> Outgoing;
	TSet<FString> Contacted;
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
