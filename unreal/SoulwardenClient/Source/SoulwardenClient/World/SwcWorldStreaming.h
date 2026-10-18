#pragma once

#include "CoreMinimal.h"

class UWorld;

/**
 * The world level of stage 1.3 is a World Partition level: the cells near a place load when a streaming source is near
 * it. A character must not stand at a place before its ground is there (docs/WORLD.md).
 */
namespace SwcWorldStreaming
{
/**
 * Location is the place of the character from the server (its Z is about 0.5 m above the feet).
 * @return True when the world has no World Partition; or when the cells within 80 m of Location are loaded and a line
 * from 2 m above Location down 50 m hits a ground near Location (at most 1.5 m below it); or 3 s after those cells when
 * the line hits nothing (no ground there); or when TimeoutSeconds passed since the first call for this place. A ground
 * far below Location (for example the Landscape below the floor of a building) means that the cells of the floor are
 * not there yet: the wait goes on. Until then a streaming source (a sphere of 80 m, the highest priority) stands at
 * Location (call again each frame).
 */
SOULWARDENCLIENT_API bool IsReadyAt(UWorld* World, const FVector& Location, double TimeoutSeconds = 90.0);

/** Removes the streaming source of IsReadyAt (the character has its own). */
SOULWARDENCLIENT_API void Release();
}
