#pragma once

#include "CoreMinimal.h"

class FSwcActions;
class UMeshComponent;
class UNiagaraComponent;
class USkeletalMeshComponent;
class UWorld;
struct FSwcActionFragment;
struct FSwcEffectClip;
struct FSwcWeaponEffectEntry;

/**
 * AST-230: the place of a helper of a weapon file (a CA_BONE attachment vfx_*) on the mesh of the weapon: its socket (the bone, or the
 * component for the root of a static weapon mesh) and its transform in the frame of the socket (the space map of stage 1.1).
 * @return False when the file has no such helper.
 */
SOULWARDENCLIENT_API bool SwcWeaponHelperPlace(const FSwcActions& Data, const FString& WeaponFile, FName Helper, const UMeshComponent* Weapon,
                                               FName& OutSocket, FTransform& OutRelative);

/** The parts of the character that the effects of a fragment attach to (AST-230). */
struct FSwcEffectOwner
{
	UWorld* World = nullptr;
	USkeletalMeshComponent* Body = nullptr;
	/** The weapon of the main hand (null: none), its tag text ("1H_Melee,Sword"), its .cdf file (the helpers vfx_*), its element. */
	UMeshComponent* Weapon = nullptr;
	FString WeaponTags;
	FString WeaponFile;
	FString Element = TEXT("Default");
	/** The learned nodes of the weapon trees (the conditions of the clips: IfAbilityActive); null: none known. */
	const TSet<uint32>* LearnedNodes = nullptr;
};

/**
 * The fragment effect player (AST-230, docs/EFFECTS.md; the study research/skill-effects.md "Runtime order for the client"): the visual
 * clips of the fragment of each action channel on its clock. A clip enters when the clock crosses its start (one start for each
 * fragment serial; a late frame enters the clips that it crossed, and a clip that also ends in that frame exits at once) and exits when
 * the clock crosses its end, or when its fragment ends (a new serial, a new fragment, or the end of the channel). The exit follows the
 * flags of the clip: KillOnExit removes the particles; else KeepEmitterActive false stops the emission and the particles finish; else
 * the emitter stays active until it ends (a weapon group entry: killOnStop, else the emission stops).
 *
 * A ParticleEffect clip attaches to its joint of the body (EmitterFollows: it follows; else it starts at the place of the joint and stays);
 * a clip of the weapon scope attaches to the weapon (a joint of the weapon, or a helper of the weapon file by AttachmentName); a body clip
 * with an AttachmentName and no joint of the body takes the helper of the weapon in the main hand [I].
 * A WeaponEffects group (OptionOnEnter Enable) starts the particle entries of the group of the weapon effect script of the weapon in
 * the main hand (WeaponEffectBase:StartParticle): a joint of the owner (playOnOwner), a joint of the weapon, or a helper of the weapon file
 * (attachmentName). The condition of a clip (a named condition of the action lists) is tested at its enter with the tags of the weapon and
 * of the fragment and the learned nodes (IfAbilityActive); a false or unknown condition skips the clip (logged once). Not done:
 * SpawnSlice, RunFxScript, and CAGE-CastSpell (logged; the spells come as spell entities of the backend), an attachment of the body
 * (AttachmentName), the element of the weapon (Default).
 */
class SOULWARDENCLIENT_API FSwcFragmentEffects
{
public:
	/** The fragment of a channel at a clock (s), with the weapon record of the fragment (or null). Fragment null: the channel ends. */
	void Update(const FSwcActions& Data, const FSwcEffectOwner& Owner, int32 Channel, const FSwcActionFragment* Fragment,
	            const FSwcActionFragment* WeaponRecord, uint32 Serial, float Clock);
	/** Exits the live clips of a channel. */
	void StopChannel(UWorld* World, int32 Channel);
	/** Exits all live clips (the end of the character). */
	void StopAll(UWorld* World);

private:
	enum class EExit : uint8
	{
		Kill,
		StopEmission,
		Keep,
	};
	struct FLive
	{
		int32 Channel = 0;
		const FSwcEffectClip* Clip = nullptr;
		/** The entry of a weapon group (INDEX_NONE: a ParticleEffect clip). */
		int32 Entry = INDEX_NONE;
		TWeakObjectPtr<UNiagaraComponent> Component;
		EExit Exit = EExit::StopEmission;
	};
	struct FChannel
	{
		const FSwcActionFragment* Fragment = nullptr;
		const FSwcActionFragment* Weapon = nullptr;
		uint32 Serial = 0;
		float Clock = 0.0f;
	};

	/** bWeaponRecord: a clip of the weapon scope (the attachment databases): it runs on the weapon (its joints are weapon bones, its
	 *  attachments helpers of the weapon file). */
	void Enter(const FSwcActions& Data, const FSwcEffectOwner& Owner, int32 Channel, const FSwcEffectClip& Clip, bool bWeaponRecord);
	void StartEntry(const FSwcActions& Data, const FSwcEffectOwner& Owner, int32 Channel, const FSwcEffectClip& Clip, int32 Index,
	                const FSwcWeaponEffectEntry& Entry);
	void ExitClip(UWorld* World, int32 Channel, const FSwcEffectClip& Clip);
	void ExitLive(UWorld* World, FLive& Item);
	void Note(const FString& Text);

	FChannel Channels[2];
	TArray<FLive> Live;
	TSet<FString> Logged;
};
