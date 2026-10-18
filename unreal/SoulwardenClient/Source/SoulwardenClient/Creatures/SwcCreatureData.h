#pragma once

#include "CoreMinimal.h"
#include "Character/SwcLocomotion.h"
#include "Creatures/SwcEffects.h"

/**
 * The converted creature content (tools/convert-creatures.sh, tools/import-creatures.sh; docs/CREATURES.md), read from
 * SWC_CONTENT/creatures:
 *   slices.json      the asset id of a slice ("GUID:SUB") -> its body (.cdf), skeleton, animation set, weapons, material
 *                    variants, and marker values, with the object paths in /SwcGameContent (tools/import.sh)
 *   names.json       the name of each category row and vitals row (the name plate) and the creature type
 *   effects.json     the projectile slices, the spell slices (their delayed events and their danger volume), the spell rows,
 *                    the loot containers
 *   animations/KEY.json   the fragments of one animation set: for each fragment id the variant of the tags of its slice
 */

/** One item of the layer of a fragment variant: a clip, a blend space, a combination, or the end of the layer. */
struct FSwcFragmentItem
{
	enum class EKind : uint8
	{
		Clip,
		BlendSpace,
		Comb,
		End,
		Missing
	};
	EKind Kind = EKind::Missing;
	/** The index into the clips, the blend spaces, or the combinations of the animation set. */
	int32 Index = INDEX_NONE;
	/** The start of the item in the fragment (s): the sum of the ExitTime values before it, or the end of the clip before it. */
	float Start = 0.0f;
	/** The start time in the clip (StartTime, s), the blend-in time (Duration, s), the speed, the weight, the loop flag. */
	float ClipStart = 0.0f;
	float BlendIn = 0.0f;
	float Speed = 1.0f;
	float Weight = 1.0f;
	bool bLoop = false;
};

/** One fragment of an animation set: the variant of the tags of the slice (research feel-values.md R3.2.5, R3.3.3). */
struct FSwcCreatureFragment
{
	FString Name;
	/** The items of the first layer (the base pose), and of each later layer of the variant (they change only the bones of
	 *  their clips: a hand pose, an arm blend space, or a copy). */
	TArray<FSwcFragmentItem> Items;
	TArray<TArray<FSwcFragmentItem>> ExtraLayers;
	/** The length of the fragment (s): its end blend or the end of its last clip; -1: it loops (or no length is known). */
	float Length = -1.0f;
	bool bLoop = false;
	float BlendOut = 0.2f;
	/** The scopes of the FragmentDef (FullBody, Additive, ...). */
	TArray<FString> Scopes;
};

/** A combined blend space (.comb): its parameter selects one of its blend spaces (wolf: DesiredFacing). */
struct FSwcComb
{
	FString Param;
	TArray<int32> Spaces;
	float Min = 0.0f;
	float Max = 0.0f;
};

/** The fragments, the clips, and the blend spaces of one animation set (one controller, database, tag state, skeleton). */
struct FSwcCreatureAnimations
{
	FString Key;
	TMap<int32, FSwcCreatureFragment> Fragments;
	/** The names of the fragments in the order of the fragment file (the wire id of a fragment is its position). */
	TArray<FString> FragmentNames;
	TArray<FSwcClip> Clips;
	/** For each clip: the bones that it animates (not the root), and its additive mark (its keys are differences). */
	TArray<TArray<FName>> ClipBones;
	TArray<bool> ClipAdditive;
	TArray<FSwcBlendSpace> BlendSpaces;
	TArray<FSwcComb> Combs;
	/** The fragment of the idle of the client (fragment Idle: the client plays it when the server has no action). */
	int32 IdleFragment = INDEX_NONE;
	/** The locomotion fragment of the client when a creature moves with no action (fragment Move). */
	int32 MoveFragment = INDEX_NONE;
	/** The clips that have an imported asset. */
	int32 ImportedClips = 0;

	const FSwcCreatureFragment* Fragment(int32 Id) const { return Fragments.Find(Id); }
	FString FragmentName(int32 Id) const { return FragmentNames.IsValidIndex(Id) ? FragmentNames[Id] : FString::Printf(TEXT("#%d"), Id); }
};

/** A default weapon of a slice on a socket of its body (research creature-visuals.md section 4). */
struct FSwcCreatureWeapon
{
	int32 Slot = 0;
	FString Item;
	/** The object path of the static mesh of the weapon (empty: not imported). */
	FString MeshPath;
	/** An animated weapon (the weapon scope, Mannequin: the bow string): the skeletal mesh of its skeleton file, the skeletal
	 *  mesh of its body (the same one, or a body that follows its pose), and its animation set. Empty: a static weapon. */
	FString SkeletonMeshPath;
	FString BodyMeshPath;
	FString AnimationKey;
	/** The bone of the socket of the body (CA_BONE) and the transform of the socket on it, in engine space (m; the rotation
	 *  as w, x, y, z). */
	FName Bone;
	FVector EnginePosition = FVector::ZeroVector;
	FQuat EngineRotation = FQuat::Identity;
};

/**
 * A hit volume of a slice: the HitVolume component of a slice entity (Head, Leg_F_L, ...; protocol notes 4.15). The weapon of
 * a character touches it; the contact names the entity and the component (FSwcCreatureSubsystem::FindMeleeContacts).
 */
struct FSwcHitVolume
{
	enum class EShape : uint8
	{
		Capsule,
		Sphere,
		Box
	};
	FString Name;
	/** The bone of the volume (m_targetBoneName) and the center on it (m_center), in engine space (m). */
	FName Bone;
	FVector EngineCenter = FVector::ZeroVector;
	EShape Shape = EShape::Capsule;
	/** A capsule: the radius, the height with the two ends, and the axis (in the space of the bone); a sphere: the radius;
	 *  a box: the half sizes (m). */
	float Radius = 0.0f;
	float Height = 0.0f;
	FVector EngineAxis = FVector::UpVector;
	FVector EngineHalfSize = FVector::ZeroVector;
	bool bHeadshot = false;
	bool bLegshot = false;
	float DamageMult = 1.0f;
	/** The component id, and the local id of its entity (the entity id XOR the root id of the slice): the facet target of the
	 *  volume for an entity group id G is (Local XOR G) + Component (swc::creatures::facet_target). */
	uint64 Component = 0;
	uint64 Local = 0;
};

/** The model of one slice. */
struct FSwcCreatureSlice
{
	FString Path;
	FString Body;
	/** The object path of the skeletal mesh of the body, and of the skeleton mesh (the body of the skeleton file, with the
	 *  clips): a different body follows its pose (Leader Pose, by bone names). */
	FString MeshPath;
	FString SkeletonMeshPath;
	/** The material variants: the category id of the vitals state -> the skeletal mesh of the body with the material of
	 *  the category row (MtlOverride, FemaleMtlOverride). */
	TMap<uint32, FString> VariantMeshes;
	TArray<FSwcCreatureWeapon> Weapons;
	TArray<FSwcHitVolume> HitVolumes;
	/** The lock point of the camera (CameraLockTargetComponent of the slice): m_Offset (engine space, m, from the feet in the frame
	 *  of the creature), the joint of the lock icon and its vertical offset (m), and the pitch limits of the observer (degrees). */
	bool bHasLock = false;
	FVector LockOffset = FVector::ZeroVector;
	FName LockJoint;
	float LockJointOffset = 0.0f;
	bool bLimitPitch = false;
	float PitchMin = 0.0f;
	float PitchMax = 0.0f;
	/** The body capsule (the QueryShapeCapsule of the CharacterComponent of the slice; backend gamedata CreatureCapsule): the
	 *  radius, the length of the segment, its axis and its offset in the frame of the creature (m; x right, y front, z up). */
	bool bHasCapsule = false;
	float CapsuleRadius = 0.0f;
	float CapsuleHeight = 0.0f;
	FVector CapsuleAxis = FVector::UpVector;
	FVector CapsuleOffset = FVector::ZeroVector;
	/** The names of the action table of the slice (the action index of a scope record is a position in it). */
	TArray<FString> Actions;
	FString AnimationKey;
	/** The marker of the slice: the rule of the name plate (MarkerComponent client facet). */
	float NearRange = 6.0f;
	float NearAngle = 0.1309f;
	float FarAngle = 0.0174533f;
	float AngleShrinkDistance = 20.0f;
	float VerticalOffset = 0.0f;
	bool bNeverShowHealthBar = false;
};

/** The effect data (effects.json; stage P2.9): a projectile slice, a spell slice, a spell row. */
struct FSwcProjectileSlice
{
	FString Path;
	swc::creatures::ProjectileFlight Flight;
};

struct FSwcSpellSlice
{
	FString Path;
	std::vector<swc::creatures::SpellEventData> Events;
	bool bHasVolume = false;
	swc::creatures::SpellVolumeData Volume;
};

struct FSwcSpellRow
{
	FString Id;
	FString Shape;
	float Radius = 0.0f;
};

/** A loot container (effects.json, member lootContainers; protocol notes 4.42): the slice of the personal loot bag of a kill. */
struct FSwcLootContainer
{
	FString Name;
	FString Slice;
	/** The object path of its model (a static mesh of the shared content). */
	FString MeshPath;
	/** The id of the root entity of the slice (the XOR rule of protocol notes 4.2). */
	uint64 Root = 0;
	/** The lifetime (KillSelfDelay) and the end after the last item (FastKillSelfDelay), s: the server ends the bag (the stop). */
	float Lifetime = 0.0f;
	float EmptyLifetime = 0.0f;
	/** One Interact component of the slice: its slot (the Interact state, type 2930), its component id, its entity in the slice,
	 *  m_interactRangeSq (m^2), and its options (the index of the option in the options table of interaction-data.json, and the
	 *  entity of the option in the slice). */
	struct FBound
	{
		uint32 Slot = 0;
		uint64 Component = 0;
		uint64 Entity = 0;
		float RangeSq = 0.0f;
		TArray<TPair<uint32, uint64>> Options;
	};
	TArray<FBound> Bounds;
};

/** A status row that shows at a name plate (effects.json statusRows; the compact map of the StatusEffects state names it by its
 *  index). */
struct FSwcStatusRow
{
	FString Id;
	float Duration = 0.0f;
	float MoveSpeedMod = 0.0f;
	bool bStunned = false;
	bool bRoot = false;
	FString Categories;
};

class FSwcCreatureLibrary
{
public:
	/** Reads slices.json, imported.json, and names.json. @return False with a reason when a file is missing. */
	bool Load(FString& OutReport);
	bool IsLoaded() const { return bLoaded; }

	/** @return The model of a slice key ("GUID:SUB"), or null when the slice is not converted. */
	const FSwcCreatureSlice* FindSlice(const FString& Key) const { return Slices.Find(Key); }
	/** @return The animation set of a key (read at the first use), or null. */
	TSharedPtr<FSwcCreatureAnimations> Animations(const FString& Key);
	/** The animation sets read: HasAnimations, and AddAnimations of a set that ParseAnimations read on a worker thread (the read of a
	 *  set takes up to 0.15 s: not on the game thread at a spawn). */
	bool HasAnimations(const FString& Key) const { return Sets.Contains(Key); }
	void AddAnimations(const FString& Key, const TSharedPtr<FSwcCreatureAnimations>& Set) { Sets.Add(Key, Set); }
	/** Reads the animation set of a key (thread-safe: no state of the library). */
	static TSharedPtr<FSwcCreatureAnimations> ParseAnimations(const FString& Key);

	/** The name plate: the name of the category row, else the name of the vitals row (protocol notes 4.14). */
	FString Name(uint32 CategoryId, uint32 VitalsId) const;
	/** @return True for a creature type with no level mark (Critter: the plate of the rabbit has no level mark). */
	bool HidesLevel(uint32 CategoryId, uint32 VitalsId) const;
	/** @return The scale of a creature: CharacterScale of the base row of its vitals row (1 when it is not known). */
	float Scale(uint32 VitalsId) const;

	/** The effect data by the slice key ("GUID:SUB") or the key of the Spell state; null when it is not known. */
	const FSwcProjectileSlice* FindProjectile(const FString& Key) const { return Projectiles.Find(Key); }
	const FSwcSpellSlice* FindSpell(const FString& Key) const { return Spells.Find(Key); }
	const FSwcSpellRow* FindSpellRow(uint32 Key) const { return SpellRows.Find(Key); }
	const FSwcLootContainer* FindLootContainer(const FString& Key) const { return LootContainers.Find(Key); }
	const TMap<FString, FSwcLootContainer>& GetLootContainers() const { return LootContainers; }
	const FSwcStatusRow* FindStatusRow(uint32 Index) const { return StatusRows.Find(Index); }

	/** Adds a slice key to SWC_WORK/creatures/wanted.json (tools/convert-creatures.sh wanted). @return True when it is new. */
	bool AddWanted(const FString& Key);

	static FString ContentFolder();

private:
	bool bLoaded = false;
	TMap<FString, FSwcCreatureSlice> Slices;
	TMap<FString, TSharedPtr<FSwcCreatureAnimations>> Sets;
	TMap<uint32, TPair<FString, FString>> Categories;
	TMap<uint32, TPair<FString, FString>> VitalsRows;
	TMap<uint32, float> Scales;
	TMap<FString, FSwcProjectileSlice> Projectiles;
	TMap<FString, FSwcSpellSlice> Spells;
	TMap<uint32, FSwcSpellRow> SpellRows;
	TMap<FString, FSwcLootContainer> LootContainers;
	TMap<uint32, FSwcStatusRow> StatusRows;
	void LoadEffects(const FString& Folder);
	TSet<FString> Wanted;
};
