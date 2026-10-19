#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The movement rules of the player from the game data (stage P3.3; SWC_CONTENT/player/movement.json, made by
// tools/convert-player.sh movement; docs/MOVEMENT.md; research p33-movement.md): the motion lists of the movement actions with
// their named conditions (the first Conditional whose condition holds gives the speed, the acceleration, and the deceleration),
// the facing lists, the posture (stand, crouch, prone: the keys C and Z), the limits of the load (the bag weight, the equip load)
// and of the statuses (the movement columns of the status map of the server, the speed table), and autorun. Plain C++ (no
// engine type): tools/test-movement.sh tests it with the data and with the position check of the backend.

namespace swc::move {

/** A condition of the data: its type (And, Or, Not, IfMannequinTag, ...), its attributes, its children. */
struct Condition {
	std::string type;
	std::map<std::string, std::string> attrs;
	std::vector<Condition> children;
};

/** One entry of a motion list (m/s and m/s2 on each axis: x side, y forward, z up). */
struct Motion {
	std::string name;
	float speed[3]{0.0F, 0.0F, 0.0F};
	float accel[3]{0.0F, 0.0F, 0.0F};
	float decel[3]{0.0F, 0.0F, 0.0F};
	/** BackwardsMultiplier: the speed of a backward input (the sprint: 0.55). */
	float backwards{1.0F};
};

struct MotionList {
	Motion fallback;
	std::vector<Motion> conditionals;
};

struct FacingList {
	float fallback{0.0F};
	std::vector<std::pair<std::string, float>> conditionals;
};

/** The movement columns of a status row (by its index on the wire). */
struct StatusRow {
	std::string id;
	bool noNav{false};
	bool noRun{false};
	bool noSprint{false};
	bool noDodge{false};
	bool stunned{false};
	bool silenced{false};
	float moveSpeedMod{0.0F};
	float sprintSpeedMod{0.0F};
	std::string categories;
	[[nodiscard]] bool has_category(std::string_view category) const;
};

struct Data {
	/** Nav_Start, Nav, Nav_Stop, Sprint_Start, Sprint. */
	std::map<std::string, MotionList> motions;
	std::map<std::string, FacingList> facings;
	std::map<std::string, Condition> conditions;
	std::map<std::uint16_t, StatusRow> statuses;
	[[nodiscard]] bool loaded() const { return !motions.empty(); }
};

/** The equip load classes of the action lists (ActionConditionEquipLoadCategory). */
enum class EquipLoad : std::uint8_t { fast, normal, slow, overburdened };
[[nodiscard]] const char* equip_load_text(EquipLoad load);

/** The facts for the conditions of the data. */
struct Facts {
	/** The Mannequin tags that are set, in the case of the data (Stand, Crouch, Prone, Unarmed, 1H_Melee, Blocking, Aim, AutoRun,
	 *  Sprint_TravelMode, ...). */
	std::set<std::string> tags;
	/** The inputs that are down (walk, block_active_virtual, ...) and the named marks of the fragment that plays (ForceWalk, ...). */
	std::set<std::string> inputs;
	std::set<std::string> markers;
	/** IfEncumbered: the bag over the movement limit (see Limits::encumbered_movement). */
	bool encumbered{false};
	/** IfCanRun, IfCanSprint, IfCanNav, IfCanDodge: the client value (the server bits and the movement columns of the statuses);
	 *  with UseServer=True the server bit alone. */
	bool canRun{true};
	bool canSprint{true};
	bool canNav{true};
	bool canDodge{true};
	bool serverCanRun{true};
	bool serverCanSprint{true};
	EquipLoad equipLoad{EquipLoad::fast};
	bool cameraLock{false};
	/** IfExternalCondition Exhausted: the stamina is empty and not full again (stage P2.5). */
	bool exhausted{false};
	/** MoveSpeed: the horizontal speed (m/s). */
	float moveSpeed{0.0F};
	/** IfWaterDepth: the depth class of the feet (stage WF5: 0 None, 1 VeryShallow, 2 Shallow, 3 Deep; the enum WaterDepth of the
	 *  retail client, research wf5-swim/swim-rules.json). */
	std::uint8_t waterDepth{0};
	/** IfPlayerSetting: the settings that are on (the default keyboard: LockedStrafeMovement, TargetLockStrafe,
	 *  SprintStrafeMovement, AttackFacing, Swimming). */
	std::set<std::string> settings{"LockedStrafeMovement", "TargetLockStrafe", "SprintStrafeMovement", "AttackFacing", "Swimming"};
};

/** @return True when a named condition of the data holds; a name with no condition is a Mannequin tag state ("Crouch",
 *  "Wade+Crouch": each tag of the list is set). An unknown condition type is false. */
[[nodiscard]] bool holds(const Data& data, std::string_view name, const Facts& facts);
[[nodiscard]] bool holds(const Data& data, const Condition& condition, const Facts& facts);

/** @return The entry of the motion list of an action for the facts: the first Conditional whose condition holds, else the
 *  Default. Null for an action that the data does not have. */
[[nodiscard]] const Motion* motion_of(const Data& data, std::string_view action, const Facts& facts);
/** The speed and the rates of the native Nav (m/s, m/s2), and the name of its branch (for the log). */
struct NavMotion {
	float speed{0.0F};
	float accel{0.0F};
	float decel{0.0F};
	const char* branch{""};
};

/**
 * The native Nav of the retail client (the compiled action, run function 0x1423FC970, build 1.400.6031.6004151; research nav-speed-2
 * RESULT.md): the retail client runs this code for Nav, not the motion list of playeractions_nav.actionlist (the XML of the pak is
 * older). The first branch that holds gives the speed S and the rates; then the walk key; the result goes to CameraRelativeMotion
 * with Speed (S, S, S), MinSpeed 0.5 S, and BackwardsMultiplier 1.0 (the backward speed is the forward speed).
 *   start       S = 4.0 with the flag "can run" (the constant ctx+0x318 of the action context 0x141924130), else 3.25; 35, 35
 *   load        the bag over 1.5 x its limit: 0.625; over its limit: 1.65; 10, 10
 *   wade        the depth class Shallow or deeper and the setting Swimming, not blocking: 2.75; 10, 10
 *   dry         crouch 1.25 (10, 10); prone 0.625 (1, 5); the flame thrower 2.75 (12, 35); aim or shoot 1.65 (12, 35); a combat
 *               stance S (25, 25); a charged aim or a hip fire walk 1.65 (25, 25); the block 2.0 (25, 25); not yet 1.65 m/s:
 *               S (12, 35); running with "can run": S (35, 35); else 1.65 (25, 25)
 *   walk key    S = max(0.5 S, min(S, 1.65))
 * The named conditions of the data select the branches of the weapons and the block (FireStaff_FlameThrower_MoveSpeed,
 * Aiming_Or_Shooting, Combat_Nav_Stand*, Bow_ChargedAim, Combat_Nav_MagicGauntlet_ChargedAim, Stand_Walk_Hipfire,
 * Block_Nav_Speed) [inference of the research: the native checks are functions with no names].
 */
[[nodiscard]] NavMotion native_nav(const Data& data, const Facts& facts, bool overloaded, bool encumbered, bool walk);

/** A ground velocity in the yaw frame of the camera (m/s): x side (right), y forward. */
struct Planar {
	float x{0.0F};
	float y{0.0F};
};

/**
 * The target velocity of CameraRelativeMotion (the client code 0x2AA3830; research nav-speed): `direction` is the move input in the
 * yaw frame of the camera (x right, y forward; any length, zero for no input) and `speed` the speed of the entry on each axis with
 * the multipliers. The target is |dir x speed| x dir, with dir the unit input: its direction is the input direction, also when the
 * axes have different speeds. A backward target (y < 0) is multiplied as a whole by 1 - |dir.y| x (1 - backwards).
 * No input: a zero target (the client leaves Nav when the input ends).
 */
[[nodiscard]] Planar camera_relative_target(Planar direction, Planar speed, float backwards);
/**
 * One step of the velocity to the target (the same code): on each axis the rate is the deceleration when |current| > |target| on
 * that axis, else the acceleration; the step goes along the difference (target - current) with the length |rate x unit difference|
 * x seconds, and stops at the target. The multiplier of the speed table scales the acceleration and the deceleration (with the speed).
 */
[[nodiscard]] Planar camera_relative_step(Planar current, Planar target, Planar accel, Planar decel, float seconds);
/** @return The TurnRate of the facing list of an action for the facts (0: no facing data). */
[[nodiscard]] float facing_of(const Data& data, std::string_view action, const Facts& facts);

/** The posture of the character (the tag group Posture) and its desired tag (DesiredPosture). */
enum class Posture : std::uint8_t { stand, crouch, prone };
[[nodiscard]] const char* posture_text(Posture posture);
[[nodiscard]] const char* posture_tag(Posture posture);

/**
 * The keys of the posture (playeractions_postureinput): C (ToggleCrouch) sets Crouch_Desired, or Stand_Desired when crouch is
 * desired; Z (ToggleProne) sets Prone_Desired, or Stand_Desired when prone is desired. Thus C from prone goes to crouch, Z from
 * crouch to prone. The posture follows the desired tag when the gates of the transitions hold (the caller: room above, no
 * attack, no block, on the ground).
 */
class PostureState {
public:
	void toggle_crouch();
	void toggle_prone();
	/** A stand-up of an action (a jump, an attack, a fall, a hit reaction, a weapon draw from prone: crouch): the desired tag and
	 *  the posture at once. */
	void force(Posture posture);
	[[nodiscard]] Posture posture() const { return m_posture; }
	[[nodiscard]] Posture desired() const { return m_desired; }
	/** @return The posture to go to (the desired one), or none when the posture is the desired one. */
	[[nodiscard]] std::optional<Posture> pending() const;
	/** The transition is done: the posture is the desired one. */
	void arrive(Posture posture) { m_posture = posture; }

private:
	Posture m_posture{Posture::stand};
	Posture m_desired{Posture::stand};
};

/** The posture transitions of the data (playeractions_idleposetransition, playeractions_sprintposetransition; research
 *  p33-movement.md Q1): the fragment (in locomotion.json), its mark CanTransition, and the forced motion of a sprint transition. */
struct Transition {
	std::string fragment;
	float canTransition{0.0F};
	/** A sprint transition: the forced forward speed (m/s), its deceleration before and after CanTransition (m/s2). */
	bool sprint{false};
	float forcedSpeed{0.0F};
	float decel{0.0F};
	float decelAfter{0.0F};
	/** A transition with no fragment: the tag changes at once (a crouch to stand while moving). */
	[[nodiscard]] bool instant() const { return fragment.empty(); }
};
/** @return The transition from a posture to a posture: at idle, in a sprint, or while moving in Nav (the tag at once). The mark
 *  CanTransition is a default: the client takes the mark of the fragment in locomotion.json when it has one. */
[[nodiscard]] Transition transition_of(Posture from, Posture to, bool sprinting, bool moving);

/** The capsule half heights of the postures (cm): the standing capsule (radius 0.41 m, segment 0.8 m: research R2.6), and the
 *  resize of the data (TraversalComponent m_characterControllerResizeParams: the half segment of crouch 0.17 m, crawl 0.01 m). */
[[nodiscard]] float capsule_half_height(Posture posture);

/**
 * The limits of the load and of the statuses (docs/MOVEMENT.md "The limits").
 *   the bag weight (tenths; the Container of slot 61 with the item weights of the catalog: the rule of the backend
 *   Inventory::encumbrance) against EncumbranceBaseMax (2000 tenths: 200.0):
 *     above the limit: no dodge, no ability (the backend refuses them: DodgeVerdict and CastVerdict encumbered, stage U3);
 *     above 1.5 x the limit: the movement condition IfEncumbered (walk, no sprint, no dodge, no jump), and no attack from rest
 *     (the backend: AttackBits::attackAllowed). The factor of the movement condition (scaleMaxEncumbrance of its client code)
 *     is not in the data: 1.5 is an inference from the live checks of the backend (a sprint at 200.2, a walk at 300.2).
 *   the equip load class (stage P2.5): Overburdened gives no sprint, no dodge, no jump (the conditions of the data).
 *   the statuses of the status map of the owner (slot 44) with the movement columns of their rows: NoNav, NoRun, NoSprint,
 *   NoDodge, Stunned (a rule of this project: the backend never clears the permission bits of the heartbeat, thus the client
 *   reads the columns of the statuses that the server sends), and the speed table (slot 43).
 */
struct Limits {
	std::int32_t bagWeight{0};
	std::int32_t bagLimit{2000};
	EquipLoad equipLoad{EquipLoad::fast};
	/** The server bits of the heartbeat (protocol notes 4.6). */
	bool serverSprint{true};
	bool serverRun{true};
	bool serverNav{true};
	bool serverDodge{true};
	/** The statuses of the owner (their rows; an index with no row has no movement column). */
	std::vector<const StatusRow*> statuses;
	/** The speed table of the server: key 0x35 (SprintSpeedMod); an absent key is 1.0. */
	float sprintMultiplier{1.0F};
	/** The stamina is empty and not full again (the action Exhausted, stage P2.5): IfExternalCondition Exhausted. */
	bool exhausted{false};

	[[nodiscard]] bool encumbered() const { return bagWeight > bagLimit; }
	[[nodiscard]] bool encumbered_movement() const { return static_cast<double>(bagWeight) > 1.5 * static_cast<double>(bagLimit); }
	[[nodiscard]] bool status(bool StatusRow::*column) const;
	[[nodiscard]] bool slowed() const;
	[[nodiscard]] bool stunned() const { return status(&StatusRow::stunned); }
	[[nodiscard]] bool can_nav() const { return serverNav && !status(&StatusRow::noNav) && !stunned(); }
	[[nodiscard]] bool can_run() const { return serverRun && !status(&StatusRow::noRun); }
	[[nodiscard]] bool can_sprint() const;
	[[nodiscard]] bool can_dodge() const;
	[[nodiscard]] bool can_jump() const;
	[[nodiscard]] bool can_attack_from_rest() const { return !encumbered_movement() && !stunned(); }
	[[nodiscard]] bool can_use_abilities() const { return !encumbered() && !stunned() && !status(&StatusRow::silenced); }
	/** @return The multiplier of the speed, the acceleration, and the deceleration of Nav and Sprint (the speed table key 0x35, in the limits of
	 *  StatMultiplierData: 0 to 1.5). The live walks of the backend (stage 3.3b-S2a) found that the key 0x35 changes the run and
	 *  the sprint of the client, and the key 0x34 changes nothing: the client follows that (the data has MoveSpeedMod for Nav). */
	[[nodiscard]] float speed_multiplier() const;
	/** @return The multiplier of the root motion of a dodge (ApplyMoveSpeedMultiplier SprintSpeedMod, ClampMax 1.0: a slow
	 *  shortens a dodge, a haste does not lengthen it) and of a jump (Jump_Slowed: 0.85 with a status of the category Slow). */
	[[nodiscard]] float dodge_multiplier() const;
	[[nodiscard]] float jump_multiplier() const { return slowed() ? 0.85F : 1.0F; }
	/** The facts of the conditions for these limits. */
	void apply(Facts& facts) const;
};

/**
 * Autorun (the key `=`, the action AutoRun of playeractions_global): a toggled virtual W (the input ForwardNav is moveforward or
 * the tag AutoRun). It ends at `=` again, the back key, a new press of W, an aim, an attack, and a use of an object (the
 * deactivation of the data); A and D, a dodge, a jump, and the camera do not end it.
 */
class AutoRun {
public:
	/** The key `=`. @return The new state. */
	bool toggle();
	/** The ends of the data (the caller gives the events of this frame). */
	void cancel() { m_on = false; }
	[[nodiscard]] bool on() const { return m_on; }
	/** @return The move input with autorun: the forward axis 1 while it is on (the side axis of A and D stays). */
	[[nodiscard]] std::pair<float, float> input(float side, float forward) const;

private:
	bool m_on{false};
};

} // namespace swc::move
