#pragma once

#include "CoreMinimal.h"

class AActor;
class UWorld;

/**
 * A temporary test floor for the movement until the world level (stage 1.3) gives the ground: a flat floor of 100 x 100 m,
 * and to the right two ramps (30 and 75 degrees: walkable to 70 degrees) and two steps (0.5 m and 0.8 m: the step-up limit
 * is 0.65 m), and a wall behind the spawn for the camera. docs/MOVEMENT.md has the rules.
 */
namespace SwcTestFloor
{
/** @return True when the level has a surface below the feet (50 m at most). */
bool HasGround(UWorld* World, const FVector& Feet);

/** Puts the floor, the ramps, and the steps around the feet; Forward is the facing of the character. */
void Spawn(UWorld* World, const FVector& Feet, const FVector& Forward);

/** The actors of the test floor (the line-of-sight checks ignore them: they are not world geometry). */
void Actors(UWorld* World, TArray<AActor*>& Out);
}
