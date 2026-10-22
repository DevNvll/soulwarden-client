#pragma once

#include "CoreMinimal.h"

class UNiagaraComponent;
class USkeletalMeshComponent;
class UWorld;

/**
 * The status effects of one body (AST-230 plan step 10, docs/EFFECTS.md "The statuses"; SWC_CONTENT/particles/status-fx.json,
 * tools/convert-particles.sh skills). The status rows of the active statuses (their index in the StatusEffects state) name an On and an
 * Off FX script; a script is found in the FX lists of the body (the player list; the lists of a creature slice, the status list first).
 * At the start of a status the On script starts its particle nodes at the joints of the body (a joint alias of the skeleton of the body:
 * javelindata_jointalias) and holds them; at its end the held effects stop by their flags (KillOnExit removes the particles,
 * KeepEmitterActive lets the emitter run, else the emission stops) and the Off script plays once. A potency of zero skips a script with
 * IgnoreFxScriptWhenPotencyIsZero. Not done: ShouldRefreshFxScript (a refresh of a status does not restart its script), the material
 * overrides (POTION_Heal; logged), the attachment helpers of a node.
 */
class SOULWARDENCLIENT_API FSwcStatusEffects
{
public:
	/** The body: the player (true) or a creature slice (its path). */
	void Setup(bool bPlayer, const FString& SlicePath);
	/** The active statuses: the index of each status row and its potency (false: zero). */
	void Update(UWorld* World, USkeletalMeshComponent* Body, const TArray<TPair<uint32, bool>>& Active);
	/** Stops the held effects (the end of the body). */
	void StopAll(UWorld* World);

private:
	struct FHeld
	{
		TWeakObjectPtr<UNiagaraComponent> Component;
		bool bKill = false;
		bool bKeep = false;
	};
	/** Starts the particle nodes of a script. bHold: the effects of an On script (stopped at the end of the status); else they play once. */
	void Run(UWorld* World, USkeletalMeshComponent* Body, const FString& Script, TArray<FHeld>* Held);
	void Note(const FString& Text);

	bool bReady = false;
	TArray<FString> Lists;
	TMap<FString, FString> Aliases;
	TMap<uint32, TArray<FHeld>> Live;
	TSet<FString> Logged;
};
