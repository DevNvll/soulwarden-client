#pragma once

#include "CoreMinimal.h"
#include "Character/SwcLocomotion.h"

/**
 * The converted creature content (tools/convert-creatures.sh, tools/import-creatures.sh; docs/CREATURES.md), read from
 * SWC_CONTENT/creatures:
 *   slices.json      the asset id of a slice ("GUID:SUB") -> its body (.cdf), skeleton, animation set, weapons, material
 *                    variants, and marker values, with the object paths in /SwcGameContent (tools/import.sh)
 *   names.json       the name of each category row and vitals row (the name plate) and the creature type
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
	TArray<FSwcFragmentItem> Items;
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
	/** The bone of the socket of the body (CA_BONE) and the transform of the socket on it, in engine space (m; the rotation
	 *  as w, x, y, z). */
	FName Bone;
	FVector EnginePosition = FVector::ZeroVector;
	FQuat EngineRotation = FQuat::Identity;
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

	/** The name plate: the name of the category row, else the name of the vitals row (protocol notes 4.14). */
	FString Name(uint32 CategoryId, uint32 VitalsId) const;
	/** @return True for a creature type with no level mark (Critter: the plate of the rabbit has no level mark). */
	bool HidesLevel(uint32 CategoryId, uint32 VitalsId) const;
	/** @return The scale of a creature: CharacterScale of the base row of its vitals row (1 when it is not known). */
	float Scale(uint32 VitalsId) const;

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
	TSet<FString> Wanted;
};
