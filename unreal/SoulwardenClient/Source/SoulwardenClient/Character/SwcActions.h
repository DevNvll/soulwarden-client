#pragma once

#include "CoreMinimal.h"
#include "Character/SwcLocomotion.h"
#include "Combat/SwcAttackGraph.h"

/**
 * The actions of the player with a weapon (stage P2.7; SWC_CONTENT/player/actions.json made by tools/convert-player.sh
 * actions, docs/ACTIONS.md): for each weapon tag set its stance fragments, its off-hand fragments, its draw and sheathe
 * fragments, and the attack graph of the backend; the layers and the marks of each fragment; the clips with their bones
 * and root tracks; the motion and facing activities of the attack actions and their named conditions.
 */

/** One item of a layer of a fragment: a clip, a blend space, or nothing (the end of the animation of the layer). */
struct FSwcActionItem
{
	/** The time of the item in the fragment (s): the exit times of the items of the layer up to this one. */
	float At = 0.0f;
	/** The time in the clip at the start of the item (s), its blend-in time, speed, and weight. */
	float Start = 0.0f;
	float BlendIn = 0.0f;
	float Speed = 1.0f;
	float Weight = 1.0f;
	bool bLoop = false;
	int32 Clip = INDEX_NONE;
	int32 BlendSpace = INDEX_NONE;

	bool IsEmpty() const { return Clip == INDEX_NONE && BlendSpace == INDEX_NONE; }
	/** @return The time in the clip at a time of the fragment (a loop wraps; a clip of this duration stops at its end). */
	float ClipTime(float Time, float Duration) const;
};

/** A Homing procedural clip of a fragment (stage P3.G, research p3g-collision): the move of an attack to its target; the root motion
 *  stops while it runs. Times in s from the start of the fragment (End below 0: the end of the fragment); distances in m. */
struct FSwcHoming
{
	float Start = 0.0f;
	float End = -1.0f;
	bool bMoveToTarget = false;
	float MoveToDistance = 1.0f;
	float MoveToDuration = 0.2f;
	float MaxMoveDistance = 0.0f;
	float Radius = 0.0f;
	float MaxAngle = 0.0f;
	float Height = 0.0f;
	float TurnRate = 0.0f;
	bool bAllowUpwards = false;
};

/**
 * A visual clip of a fragment (AST-230, actions.json "effects"; docs/EFFECTS.md): a ParticleEffect or a WeaponEffects group on the clock
 * of the fragment (the other types are kept for the log). The position (m) and the rotation (radians, x first, then y, then z) are in the
 * frame of the joint of the game.
 */
struct FSwcEffectClip
{
	enum class EKind : uint8
	{
		Particle,
		WeaponGroup,
		/** CAGE-Attachment: the ammunition on an attachment of the weapon (the nocked arrow; usertest-1024). */
		Attachment,
		Other,
	};
	EKind Kind = EKind::Other;
	FString Type;
	int32 Layer = 0;
	int32 Clip = 0;
	float Start = 0.0f;
	/** Below 0: the end of the fragment. */
	float End = -1.0f;
	/** The effect key (Particle) or the group name (WeaponGroup). */
	FString Name;
	FName Joint;
	FName Attachment;
	FVector Position = FVector::ZeroVector;
	FVector Rotation = FVector::ZeroVector;
	float Scale = 1.0f;
	float AlphaScale = 1.0f;
	float TimeScale = 1.0f;
	FLinearColor Tint = FLinearColor::White;
	/** EmitterFollows, KillOnExit, KeepEmitterActive. */
	bool bFollow = true;
	bool bKill = false;
	bool bKeep = false;
	FString Condition;
	/** OptionOnEnter and OptionOnExit of a weapon group (Enable, Disable). */
	FString Enter;
	FString Exit;
};

/** A particle entry of a weapon effect group (actions.json weaponEffects: the weapon effect scripts; docs/EFFECTS.md). */
struct FSwcWeaponEffectEntry
{
	FString Particle;
	/** jointName: a joint of the owner (bOnOwner) or of the weapon; attachmentName: a helper of the weapon file. */
	FName Joint;
	FName Attachment;
	/** positionOffset (m), rotationDeg (degrees), scale: given only with all three (WeaponEffectBase:StartParticle). */
	bool bOffset = false;
	FVector Position = FVector::ZeroVector;
	FVector RotationDeg = FVector::ZeroVector;
	float Scale = 1.0f;
	bool bFollow = true;
	bool bKill = false;
	bool bOnOwner = false;
};

/** A helper of a weapon file (a CA_BONE attachment, vfx_*): its bone and its transform in the frame of the bone (game m, w x y z). */
struct FSwcWeaponHelper
{
	FName Bone;
	FVector Position = FVector::ZeroVector;
	FQuat Rotation = FQuat::Identity;
};

struct FSwcActionFragment
{
	FString Key;
	FString Name;
	FString Tags;
	float BlendOut = 0.2f;
	TArray<TArray<FSwcActionItem>> Layers;
	std::vector<swc::combat::Mark> Marks;
	TArray<FSwcHoming> Homing;
	/** The visual clips (AST-230). */
	TArray<FSwcEffectClip> Effects;
	/** The end of the animation (s): the last item of layer 0 (an empty item: its start). Below 0: a loop. */
	float Length = -1.0f;

	/** @return The time of the first mark with this name (no case), or Default. */
	float MarkTime(const TCHAR* MarkName, float Default) const;
};

struct FSwcActionClip : FSwcClip
{
	bool bAdditive = false;
	/** A weapon clip (AST-202: a clip of the prop folder; it plays on the skeleton of the weapon, not of the player). */
	bool bProp = false;
	/** The animated bones (the controllers of the clip on the player skeleton, or on the weapon skeletons for a weapon clip). */
	TArray<FName> Bones;
};

/** One shape of a CAGE-Damage clip (the backend file weapon-actions.json with the rotation of the animation database). */
struct FSwcStrikeShape
{
	enum class EKind : uint8
	{
		Sphere,
		Capsule,
		Box,
	};
	EKind Kind = EKind::Capsule;
	/** Meters: the radius and the half height of the segment of a capsule; the full dimensions of a box (an inference: the
	 *  box of the greatsword is 2.0 m on the blade of 1.76 m). */
	float Radius = 0.0f;
	float HalfHeight = 0.0f;
	FVector Box = FVector::ZeroVector;
	/** The offset (engine meters) and the rotation (degrees about x, then y, then z) in the frame of the joint. */
	FVector Offset = FVector::ZeroVector;
	FVector Rotation = FVector::ZeroVector;
	/** A bone of the player skeleton, or a joint of the weapon skeleton (empty: the frame of the weapon). */
	FName Joint;
	/** UseOffhandWeapon, UseEndAsCenter, DisableLOSCheck. */
	bool bOffhand = false;
	bool bEndAsCenter = false;
	bool bNoSight = false;
	/** The index of the shape in the shapes of the strike of the data (the shape of a row). */
	int32 DataIndex = 0;
};

/** The damage clips of one damage key of a fragment: one hit of each target (protocol notes 4.32). */
struct FSwcStrike
{
	FString Key;
	/** The window on the clock of the fragment (s). */
	float Start = 0.0f;
	float End = 0.0f;
	TArray<FSwcStrikeShape> Shapes;
	/** The rows of the strike (stage P3.5): the learned nodes that a row needs and forbids (progression ids), and its shape. A
	 *  strike with rows and no row for the nodes of the character is no strike of it (the backend: Combat row_for). */
	struct FRow
	{
		std::vector<uint32> Needs;
		std::vector<uint32> Forbids;
		int32 Shape = 0;
		/** The damage row (DamageTable.DamageID; AST-230: the hit effect). */
		FString Damage;
	};
	TArray<FRow> Rows;
};

struct FSwcWeaponActions
{
	/** The tags in the case of the tables ("1H_Melee", "Sword"), and the key of the clip pack ("1h_melee_sword"). */
	TArray<FString> Tags;
	FString ClipKey;
	/** The fragments of the stance and of its shield: the movement fragment name ("Idle", "Land+Soft") -> fragment. */
	TMap<FString, int32> Stance;
	TMap<FString, int32> Offhand;
	/** The dodge fragments: the fragment of a load class and a direction ("Dodge_Roll_Fast+BackNav"; no tag: forward). */
	TMap<FString, int32> Dodge;
	/** The death and the respawn of the unarmed set (stage P2.10, playeractions_death): "death" (r_Death, Unarmed), "respawn". */
	TMap<FString, int32> Life;
	/** The use of an object (stage P2.10, playeractions_interact): "delayed" (Delayed_Interact, the timed use: the kneel). */
	TMap<FString, int32> Use;
	int32 Draw = INDEX_NONE;
	int32 Sheathe = INDEX_NONE;
	/** The block (stage P3.G, playeractions_block): the raise (Block_UpperBody_Raise) and the hold (Block_UpperBody) of the upper body. */
	int32 BlockRaise = INDEX_NONE;
	int32 BlockLoop = INDEX_NONE;
	/** The attack graph (empty: no basic attack), and the fragment of each graph fragment (or INDEX_NONE). */
	swc::combat::Graph Graph;
	TArray<int32> GraphFragments;
	/** The strikes of each graph fragment. */
	TArray<TArray<FSwcStrike>> Strikes;

	/**
	 * The weapon scopes (AST-202, docs/GEAR.md "The weapon clips"): a fragment of the main hand that owns the scope Weapon or
	 * WeaponTransition for its tags -> the record of the same fragment in the attachment database of the weapon (its clip plays on
	 * the weapon, on the clock of the fragment).
	 */
	TMap<int32, int32> WeaponRecords;

	bool HasGraph() const { return !Graph.fragments.empty() && Graph.start >= 0; }
	/** @return The weapon record of a fragment, or INDEX_NONE. */
	int32 WeaponRecord(int32 Fragment) const { return WeaponRecords.FindRef(Fragment, INDEX_NONE); }
	/** The ranged program of a bow, a fire staff, or a life staff (stage P3.6, Combat/SwcRanged; null: none). */
	TSharedPtr<swc::ranged::Program> Ranged;
};

/** A named condition of the action list (And, Or, Not, IfMannequinMarker, IfMannequinTag, IfInput, ...). */
struct FSwcCondition
{
	FString Type;
	TMap<FString, FString> Attrs;
	TArray<FSwcCondition> Children;
};

/** The facts of the character for the conditions. */
struct FSwcConditionFacts
{
	const std::vector<swc::combat::Mark>* Marks = nullptr;
	float Time = 0.0f;
	/** The Mannequin tags in lower case (the weapon tags, or "unarmed"). */
	TSet<FString> Tags;
	bool bCameraLock = false;
	bool bAttackPress = false;
	bool bAttackHold = false;
	/** The angle between the facing and the horizontal velocity (degrees); below 0: no velocity. */
	float FacingVelocityAngle = -1.0f;
	/** The learned nodes of the weapon trees (IfAbilityActive: the progression id of the AbilityName); null: not known (false). AST-230:
	 *  only the conditions of the effect clips set it. */
	const TSet<uint32>* LearnedNodes = nullptr;
};

/** A parameter set of AnimationDrivenMotion (a conditional merged onto the default). */
struct FSwcMotionRule
{
	FString Condition;
	float MotionMultiplier = 1.0f;
	/** The deceleration of the inherited velocity on each axis (x right, y forward, z up; m/s2). */
	FVector Decel = FVector::ZeroVector;
	bool bInheritVelocity = false;
	float InheritVelocityMaxSpeed = 0.0f;
};

/** A parameter set of CameraRelativeFacing or InputRelativeFacing. */
struct FSwcFacingRule
{
	FString Condition;
	/** 0: no turn. Exponential: the share of the remaining angle in each step of 1/30 s; else degrees in each second. */
	float TurnRate = 0.0f;
	bool bExponential = false;
};

struct FSwcActivity
{
	FSwcMotionRule MotionDefault;
	TArray<FSwcMotionRule> Motion;
	FSwcFacingRule CameraDefault;
	TArray<FSwcFacingRule> CameraFacing;
	FSwcFacingRule InputDefault;
	TArray<FSwcFacingRule> InputFacing;
};

class FSwcActions
{
public:
	/** Reads actions.json. @return False with a reason when the file is not there. */
	bool Load(FString& OutReport);
	/**
	 * The object paths of the clips for one gender (imported.json: the clip packs "clips|<gender>|<key>", then the clips of
	 * the base). @return The count of the clips with an asset.
	 */
	int32 ClipAssets(bool bFemale, TArray<FString>& OutPaths) const;

	/** @return The actions of a weapon tag text ("1H_Melee,Sword"; no case), or null. */
	const FSwcWeaponActions* Weapon(const FString& TagText) const;
	/** @return The weapon actions whose graph this is, or null. */
	const FSwcWeaponActions* WeaponOfGraph(const swc::combat::Graph* Graph) const;

	/** @return True when the named condition holds (an unknown name or type: false). */
	bool Holds(const FString& Name, const FSwcConditionFacts& Facts) const;
	bool HasCondition(const FString& Name) const { return Conditions.Contains(Name); }
	/** The motion and the facing of an attack fragment (its action of playeractions_attack.actionlist). */
	const FSwcActivity* Activity(const FString& FragmentName) const { return Activities.Find(FragmentName); }
	/** @return The motion rule of an action at its facts: the first conditional that holds, else the default. */
	FSwcMotionRule MotionRule(const FSwcActivity& Activity, const FSwcConditionFacts& Facts) const;
	/** @return The facing rule (camera or input) of an action at its facts, or null when no rule turns. */
	const FSwcFacingRule* FacingRule(const TArray<FSwcFacingRule>& Rules, const FSwcFacingRule& Default, const FSwcConditionFacts& Facts) const;

	/**
	 * The root motion of a fragment from one time to another (m, x right, y forward, z up): the root track of the clip of
	 * the first layer with a root track. @return False when no layer has one.
	 */
	bool RootDelta(const FSwcActionFragment& Fragment, float From, float To, FVector& OutDelta) const;

	/**
	 * The equip load of armor items (the slots 0 to 4: the whole tenths of each load, gear.h of the backend) and its class by
	 * the encumbrance row of the player (light below 26 %, medium below 46 %, heavy to 100 % of the maximum, else overburdened).
	 */
	swc::combat::LoadClass LoadClassOf(const TArray<FString>& ArmorItems, float& OutLoad) const;
	/** The stamina and dodge data of the backend copy (stamina-data.json in actions.json). */
	const swc::combat::StaminaData& GetStaminaData() const { return StaminaData; }
	/** The bag limit in tenths of a weight unit (EncumbranceBaseMax of the row Player of the encumbrance table; stage P3.3). */
	int32 GetBagLimit() const { return FMath::RoundToInt(EncumbranceMax); }

	/** The shared data of the ranged programs (stage P3.6: the shot rules with their flight, the spells, the mana costs). */
	const swc::ranged::Shared& GetRangedShared() const { return RangedData; }
	/** @return The clip of a clip name (lower case), or INDEX_NONE. */
	int32 ClipOf(const FString& Name) const { return ClipIndex.FindRef(Name, INDEX_NONE); }
	/** @return The time from the start of a fragment to the end of the blend-in of its first item with a weight (all layers). */
	static float FullIn(const FSwcActionFragment& Fragment);
	/** @return The fragment of a record key of actions.json (a variant of a ranged fragment), or INDEX_NONE. */
	int32 FragmentOfRecord(const FString& Record) const { return RecordIndex.FindRef(Record, INDEX_NONE); }

	/** @return A joint of a weapon skeleton in the model of the weapon (Unreal cm, the space map of stage 1.1), or null. */
	const FTransform* WeaponJoint(const FString& Model, FName Joint) const;

	/**
	 * AST-230: the entries of a weapon effect group for a weapon tag text ("1H_Melee,Sword"; no case) and an element (Default, Fire,
	 * Arcane, Void, Lightning, Ice, Nature): the script of the row of its weapon effect id. @return Null when the group is not there.
	 */
	const TArray<FSwcWeaponEffectEntry>* WeaponEffectGroup(const FString& TagText, const FString& Element, const FString& Group) const;
	/** @return The helpers of a weapon file (its .cdf path; no case), or null. */
	const TMap<FName, FSwcWeaponHelper>* WeaponHelpers(const FString& CdfPath) const { return Helpers.Find(CdfPath.ToLower()); }

	TArray<FSwcActionFragment> Fragments;
	/** Stage MVP-E: the MannequinTag of each consumable (the item id in lower case; javelindata_itemdefinitions_consumables): the
	 *  variant of the fragment Use ("use|POSTURE|TAG" of the use map of the unarmed set). */
	TMap<FString, FString> ConsumableTags;
	TArray<FSwcActionClip> Clips;
	TArray<FSwcBlendSpace> BlendSpaces;
	TArray<TSharedPtr<FSwcWeaponActions>> Weapons;

private:
	bool Evaluate(const FSwcCondition& Condition, const FSwcConditionFacts& Facts, int32 Depth) const;

	TMap<FString, FSwcActivity> Activities;
	TMap<FString, FSwcCondition> Conditions;
	TMap<FString, int32> ClipIndex;
	TMap<FString, TMap<FName, FTransform>> WeaponJoints;
	/** AST-230: script -> group -> entries; weapon effect id -> element -> script; lower-case tag text -> weapon effect id; the helpers. */
	TMap<FString, TMap<FString, TArray<FSwcWeaponEffectEntry>>> EffectScripts;
	TMap<FString, TMap<FString, FString>> EffectTable;
	TMap<FString, FString> EffectIds;
	TMap<FString, TMap<FName, FSwcWeaponHelper>> Helpers;
	swc::combat::StaminaData StaminaData;
	swc::ranged::Shared RangedData;
	TMap<FString, int32> RecordIndex;
	TMap<FString, float> ItemLoads;
	float LoadMax = 500.0f;
	float EncumbranceMax = 2000.0f;
	float RatioMedium = 26.0f;
	float RatioHeavy = 46.0f;
	float RatioOverburdened = 100.0f;
};

/** The object path of the mesh of the ammunition of a weapon (usertest-1024): the choice of swc::ranged::Shared::choose_ammo (an equipped
 *  ammunition item of the type of the fallback, else the fallback) and its look. Empty: no look. */
SOULWARDENCLIENT_API FString SwcAmmoMesh(const FSwcActions& Data, const std::string& Fallback, const std::vector<std::string>& Equipped);
