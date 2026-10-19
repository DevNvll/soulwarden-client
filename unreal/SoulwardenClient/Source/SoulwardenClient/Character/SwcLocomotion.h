#pragma once

#include "CoreMinimal.h"
#include "Character/SwcMoveRules.h"

class FJsonObject;

/**
 * The player locomotion of the Mannequin data (research feel-values.md R2.7, R3; SWC_CONTENT/player/locomotion.json made
 * by tools/convert-player.sh locomotion): the default unarmed fragments of player.adb, their blend spaces with the
 * precomputed weight grid of the .bspace files (VGrid: the exact blend of the retail client), and their clips.
 */
struct FSwcBlendSpace
{
	struct FParam
	{
		FString Name;
		float Min = 0.0f;
		float Max = 0.0f;
		int32 Cells = 1;
	};
	TArray<FParam> Params;
	/** For each example: its clip (index into FSwcLocomotion::Clips) and its playback scale. */
	TArray<int32> ExampleClips;
	TArray<float> ExampleScales;
	/** For each cell (parameter 0 changes fastest): 4 example indexes and 4 weights. */
	TArray<int32> GridIndexes;
	TArray<float> GridWeights;

	/** @return The index of a parameter, or INDEX_NONE. */
	int32 ParamIndex(const TCHAR* Name) const;

	/**
	 * The weights of the examples for a parameter vector: the weights of the cells around the values, each with its
	 * multilinear share (the grid of the retail client; between the cells the interpolation is an inference).
	 * @param Values One value for each parameter, in the sequence of Params.
	 */
	void Evaluate(const TArray<float>& Values, TMap<int32, float>& OutExampleWeights) const;
};

struct FSwcFragment
{
	/** One of the two: a clip or a blend space. */
	int32 Clip = INDEX_NONE;
	int32 BlendSpace = INDEX_NONE;
	/** The blend-in time of the fragment (the Blend of its first layer) and its blend-out time. */
	float BlendIn = 0.2f;
	float BlendOut = 0.2f;
	bool bLoop = false;
	float Speed = 1.0f;
	/** The named markers of the fragment (CAGE-Named: Enable_Gravity, CanTransition, ...): name -> time in seconds. */
	TMap<FString, float> Markers;

	/** @return The time of a marker, or Default when the fragment has none. */
	float Marker(const TCHAR* Name, float Default) const
	{
		const float* Time = Markers.Find(Name);
		return Time != nullptr ? *Time : Default;
	}
};

struct FSwcClip
{
	FString Name;
	float Duration = 0.0f;
	float MoveSpeed = 0.0f;
	/** The object path of the imported clip on the base skeleton of the gender. */
	FString AssetPath;
	/** The root motion of a jump clip: the keys of the root bone Xform (time in s; x right, y forward, z up in m). */
	TArray<FVector4f> Root;

	/** @return The root position at a time (linear between the keys; the last key after the end), in m. */
	FVector RootAt(float Time) const;
};

class FSwcLocomotion
{
public:
	/**
	 * Reads locomotion.json and the clip assets of the base of one gender (imported.json).
	 * @return False with a reason when a file or a clip is missing.
	 */
	bool Load(bool bFemale, FString& OutReport);

	const FSwcFragment* Fragment(const FString& Name) const { return Fragments.Find(Name); }
	/** @return The end of a fragment that plays once (s): its clip, or the longest example of its blend space (AST-199: Nav_Stop). */
	float Length(const FSwcFragment& Fragment) const;

	TArray<FSwcClip> Clips;
	/** The movement rules of the data (stage P3.3; SWC_CONTENT/player/movement.json, tools/convert-player.sh movement): empty
	 *  when the file is not there (the movement then uses its own values of the research). */
	swc::move::Data Rules;
	/** Reads movement.json into the rules (no document: empty rules). */
	static void ReadRules(const TSharedPtr<FJsonObject>& Document, swc::move::Data& Out);
	TArray<FSwcBlendSpace> BlendSpaces;
	TMap<FString, FSwcFragment> Fragments;
};
