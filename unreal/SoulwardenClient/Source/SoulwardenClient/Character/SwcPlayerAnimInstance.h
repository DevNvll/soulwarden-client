#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "SwcPlayerAnimInstance.generated.h"

class FSwcActions;
class FSwcFragmentEffects;
class FSwcLocomotion;
class UAnimSequence;
struct FSwcActionFragment;
struct FSwcBlendSpace;
struct FSwcWeaponActions;

/** One clip of the pose of a frame: the clip, its time, and its weight. */
struct FSwcPoseSample
{
	TObjectPtr<UAnimSequence> Sequence = nullptr;
	float Time = 0.0f;
	float Weight = 0.0f;
	bool bLoop = false;
};

/**
 * One layer over the base pose: the samples of one item of a fragment layer (their weights sum to 1), its weight, and the
 * bones of its clips. An override layer moves its bones to its pose; an additive layer adds its rotations.
 */
struct FSwcPoseOverlay
{
	TArray<FSwcPoseSample> Samples;
	float Weight = 0.0f;
	bool bAdditive = false;
	/** The bones of the clips: indexes of the reference skeleton of the mesh. */
	TArray<int32> Bones;
};

/** One visit of the weapon scope (AST-202): a weapon record (a fragment of the action data), its time, and its weight. */
struct FSwcWeaponVisit
{
	int32 Fragment = INDEX_NONE;
	float Time = 0.0f;
	float Weight = 0.0f;
};

/**
 * The worker part of the anim instance: it blends the samples of the frame (no animation graph asset). With no sample and with
 * overlays (a weapon: USwcWeaponAnimInstance) the overlays go over the bind pose.
 */
struct FSwcPlayerAnimProxy : public FAnimInstanceProxy
{
	FSwcPlayerAnimProxy() = default;
	explicit FSwcPlayerAnimProxy(UAnimInstance* Instance) : FAnimInstanceProxy(Instance) {}

	virtual bool Evaluate(FPoseContext& Output) override;

	TArray<FSwcPoseSample> Samples;
	TArray<FSwcPoseOverlay> Overlays;
};

/**
 * The animation of the player (docs/MOVEMENT.md, docs/ACTIONS.md): the Mannequin fragments of the movement state with
 * their blend-in times, the blend spaces with the weight grid of the data, and the motion parameters of the action list
 * with their smoothing (MoveSpeed and TravelAngle 0.1 s, TurnSpeed 0.75 s; research R2.3.3). With a drawn weapon the
 * fragments of its stance replace the unarmed ones: layer 0 is the base, the other layers (and the fragments of a shield)
 * are overlays. Two action channels go over them: the attack (scope FullBody) and the draw or the sheathe (UpperBody).
 * The clips play in place: the movement data and the root tracks of the data move the character.
 */
UCLASS(Transient)
class SOULWARDENCLIENT_API USwcPlayerAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	/** Gives the locomotion data of the gender of the body. */
	void SetLocomotion(const TSharedPtr<FSwcLocomotion>& Data);
	/** Gives the action data and the gender of the body (the clip assets of the gender). */
	void SetActions(const TSharedPtr<FSwcActions>& Data, bool bFemale);
	/** The stance of the weapon in the hand (null: unarmed), and the fragments of its shield. */
	void SetStance(const FSwcWeaponActions* Weapon, bool bOffhand);
	/** The variant of the stance fragments ("+Aim", "+ChargedAim"; empty: none), when the stance has it (stage P3.G). */
	void SetStanceVariant(const TCHAR* Variant) { StanceVariant = Variant; }
	/**
	 * The fragment of an action channel (0: the attack; 1: the draw or the sheathe) at a time of its clock. A new serial
	 * starts the fragment again; Fragment INDEX_NONE ends the channel (the last fragment blends out).
	 */
	void SetAction(int32 Channel, int32 Fragment, uint32 Serial, float Time);

	/** The samples of the last update. */
	const TArray<FSwcPoseSample>& GetSamples() const { return Samples; }
	/**
	 * The weapon scope of the last update (AST-202, docs/GEAR.md "The weapon clips"): for each fragment of the stance and of the
	 * action channels whose weapon has a weapon record for it, the record with the time and the weight of the fragment, in the
	 * order of the overlays (the stance, then the draw or the sheathe, then the attack). USwcWeaponAnimInstance plays them.
	 */
	const TArray<FSwcWeaponVisit>& GetWeaponVisits() const { return WeaponVisits; }
	int32 GetOverlayCount() const { return Overlays.Num(); }

protected:
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;
	virtual void NativeUninitializeAnimation() override;
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override;

private:
	/** A fragment of the base: a locomotion fragment (by name) or a stance fragment of the actions (by index). */
	struct FLayer
	{
		FString Fragment;
		int32 Action = INDEX_NONE;
		int32 Offhand = INDEX_NONE;
		float Weight = 0.0f;
		float BlendIn = 0.0f;
		float Elapsed = 0.0f;
		/** The normalized time of a blend space (0 to 1 for one cycle). */
		float Phase = 0.0f;
	};
	/** A fragment of an action channel. */
	struct FActionLayer
	{
		int32 Fragment = INDEX_NONE;
		uint32 Serial = 0;
		float Time = 0.0f;
		/** 1 while it plays; from 1 to 0 over the blend-out time of the fragment at its end. */
		float Weight = 1.0f;
		bool bEnding = false;
	};

	/** AST-230: the visual clips of an action fragment on its clock (Effects/SwcFragmentEffects), with the weapon in the main hand. */
	void UpdateEffects(int32 Channel, int32 Fragment, uint32 Serial, float Time);
	void AddSamples(const FLayer& Layer, TMap<UAnimSequence*, FSwcPoseSample>& Out) const;
	/** @return The motion parameter values of a blend space. */
	TArray<float> ParamValues(const FSwcBlendSpace& Space) const;
	/** The samples of a blend space (examples of the action data) at a phase, with a weight. */
	void SpaceSamples(const FSwcBlendSpace& Space, float Phase, bool bLoop, float Weight, TArray<FSwcPoseSample>& Out) const;
	/** The phase step of a blend space of the action data for a time step (one cycle: the weighted example length). */
	float PhaseStep(const FSwcBlendSpace& Space, float DeltaSeconds, float Speed) const;
	/** Adds the overlays of the layers of an action fragment at a time: First is the first layer (0 or 1). */
	void AddOverlays(const FSwcActionFragment& Fragment, int32 First, float Time, float Phase, float Weight);
	UAnimSequence* ActionSequence(int32 Clip) const;
	const TArray<int32>& ClipBones(int32 Clip) const;

	TSharedPtr<FSwcLocomotion> Locomotion;
	TSharedPtr<FSwcActions> Actions;
	const FSwcWeaponActions* Stance = nullptr;
	bool bOffhandStance = false;
	const TCHAR* StanceVariant = TEXT("");
	UPROPERTY(Transient)
	TArray<TObjectPtr<UAnimSequence>> Sequences;
	/** The clip assets of the action data (loaded at the first use), and the mesh bones of each clip. */
	TArray<FString> ActionPaths;
	UPROPERTY(Transient)
	mutable TArray<TObjectPtr<UAnimSequence>> ActionSequences;
	mutable TArray<bool> ActionLoaded;
	mutable TMap<int32, TArray<int32>> BoneCache;
	TArray<FLayer> Layers;
	TArray<FActionLayer> Channels[2];
	TSharedPtr<FSwcFragmentEffects> FragmentEffects;
	TArray<FSwcPoseSample> Samples;
	TArray<FSwcPoseOverlay> Overlays;
	TArray<FSwcWeaponVisit> WeaponVisits;
	/** The motion parameters (SmoothCD state: value and velocity). */
	float MoveSpeed = 0.0f;
	float MoveSpeedRate = 0.0f;
	float TravelAngle = 0.0f;
	float TravelAngleRate = 0.0f;
	float TurnSpeed = 0.0f;
	float TurnSpeedRate = 0.0f;
};
