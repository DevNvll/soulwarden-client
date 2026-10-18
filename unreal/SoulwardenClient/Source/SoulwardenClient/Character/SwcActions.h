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

struct FSwcActionFragment
{
	FString Key;
	FString Name;
	FString Tags;
	float BlendOut = 0.2f;
	TArray<TArray<FSwcActionItem>> Layers;
	std::vector<swc::combat::Mark> Marks;
	/** The end of the animation (s): the last item of layer 0 (an empty item: its start). Below 0: a loop. */
	float Length = -1.0f;

	/** @return The time of the first mark with this name (no case), or Default. */
	float MarkTime(const TCHAR* MarkName, float Default) const;
};

struct FSwcActionClip : FSwcClip
{
	bool bAdditive = false;
	/** The animated bones (the controllers of the clip on the player skeleton). */
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
};

/** The damage clips of one damage key of a fragment: one hit of each target (protocol notes 4.32). */
struct FSwcStrike
{
	FString Key;
	/** The window on the clock of the fragment (s). */
	float Start = 0.0f;
	float End = 0.0f;
	TArray<FSwcStrikeShape> Shapes;
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
	/** The attack graph (empty: no basic attack), and the fragment of each graph fragment (or INDEX_NONE). */
	swc::combat::Graph Graph;
	TArray<int32> GraphFragments;
	/** The strikes of each graph fragment. */
	TArray<TArray<FSwcStrike>> Strikes;

	bool HasGraph() const { return !Graph.fragments.empty() && Graph.start >= 0; }
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

	/** @return A joint of a weapon skeleton in the model of the weapon (Unreal cm, the space map of stage 1.1), or null. */
	const FTransform* WeaponJoint(const FString& Model, FName Joint) const;

	TArray<FSwcActionFragment> Fragments;
	TArray<FSwcActionClip> Clips;
	TArray<FSwcBlendSpace> BlendSpaces;
	TArray<TSharedPtr<FSwcWeaponActions>> Weapons;

private:
	bool Evaluate(const FSwcCondition& Condition, const FSwcConditionFacts& Facts, int32 Depth) const;

	TMap<FString, FSwcActivity> Activities;
	TMap<FString, FSwcCondition> Conditions;
	TMap<FString, int32> ClipIndex;
	TMap<FString, TMap<FName, FTransform>> WeaponJoints;
	swc::combat::StaminaData StaminaData;
	TMap<FString, float> ItemLoads;
	float LoadMax = 500.0f;
	float RatioMedium = 26.0f;
	float RatioHeavy = 46.0f;
	float RatioOverburdened = 100.0f;
};
