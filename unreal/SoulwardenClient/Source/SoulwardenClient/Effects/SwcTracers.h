#pragma once

#include "CoreMinimal.h"

class UNiagaraComponent;
class USceneComponent;
class UWorld;

/**
 * The projectile trails (AST-230 plan step 8, docs/EFFECTS.md "The trails"; SWC_CONTENT/particles/tracers.json, tools/convert-particles.sh
 * skills): the trail of an attack of the ranged attack database by the weapon effect of the weapon (m_tracerEffectsByEffect). A weapon
 * effect with no value takes _Default [I: the retail Penetrating Shot of a bow with the weapon effect Default shows the trail of _Default;
 * the converter leaves the empty values out]. The trail starts with the projectile (the visibility delays of the data are 0) and stays on
 * it; at a hit or at the end of the flight it stays at its place, its emission stops, and its particles finish.
 */
namespace SwcTracers
{
/** @return The trail effect of an attack ("Bow/PenetratingShot") for a weapon effect ("Default"), or empty. */
SOULWARDENCLIENT_API FString Key(const FString& Attack, const FString& Element);
/** @return The attack ("AI_Bow/Basic_Arcane") of a shot of a creature: its slice and the CRC-32 of its AmmoID, or empty. */
SOULWARDENCLIENT_API FString CreatureAttack(const FString& SlicePath, uint32 AmmoKey);
/** Starts the trail of an attack on a projectile component whose local +Z is the flight (the trail has the local +Y of the game along
 *  the flight: the forward of an entity of the game). @return The component, or null (no trail, or no system). */
SOULWARDENCLIENT_API UNiagaraComponent* Start(UWorld* World, const FString& Attack, const FString& Element, USceneComponent* Projectile);
/** As Start, for a projectile in the frame of the game (a mesh of the game in the space map of stage 1.1, for example the arrow of the
 *  ammunition: its flight is its local Unreal -Y, the local +Y of the game): no turn (usertest-1024). */
SOULWARDENCLIENT_API UNiagaraComponent* StartInGameFrame(UWorld* World, const FString& Attack, const FString& Element, USceneComponent* Projectile);
/** Ends a trail: it leaves the projectile at its world place, its emission stops, and its particles finish. */
SOULWARDENCLIENT_API void Stop(UWorld* World, UNiagaraComponent* Trail);
} // namespace SwcTracers
