#pragma once

#include "CoreMinimal.h"

/**
 * One damage shape of a strike of an attack in Unreal world space (stage P2.8, docs/ACTIONS.md): the shape of a CAGE-Damage
 * clip of the attack fragment at the shown pose of the attacker, for the melee contact query of the creatures. The player
 * track makes it at each input step in the window of the strike; the creature track tests it against the hit volumes.
 */
struct FSwcMeleeShape
{
	enum class EKind : uint8
	{
		Sphere,
		Capsule,
		Box,
	};
	EKind Kind = EKind::Capsule;
	/** The center (cm) and the rotation of the shape: the axis of a capsule is its local Z. */
	FVector Center = FVector::ZeroVector;
	FQuat Rotation = FQuat::Identity;
	/** Sphere and capsule: the radius (cm). Capsule: half the length of its segment, without the caps (cm). */
	float Radius = 0.0f;
	float HalfLength = 0.0f;
	/** Box: the half extents on its local axes (cm). */
	FVector HalfExtents = FVector::ZeroVector;
	/** The feet of the attacker (cm), and the unit direction of the move of the shape since the step before (the cast
	 *  direction; at the first step of a window the facing of the attacker). */
	FVector Attacker = FVector::ZeroVector;
	FVector Direction = FVector::ForwardVector;
	/** DisableLOSCheck of the clip: no line of sight test. */
	bool bNoSight = false;
	/** The visit of the fragment, the index of the strike in the fragment, and its damage key (one contact for each target
	 *  for each strike). */
	uint32 Visit = 0;
	int32 Strike = 0;
	FString Key;
	/** The damage row of the shape (AST-230: the hit effect; empty: a strike with no rows). */
	FString DamageRow;
};
