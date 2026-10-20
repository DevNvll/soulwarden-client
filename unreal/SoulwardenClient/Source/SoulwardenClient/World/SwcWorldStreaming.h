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
 * @return True when the world has no World Partition; or when the cells within 150 m of Location are loaded, a line
 * from 2 m above Location down 50 m hits a ground near Location (at most 1.5 m below it), the content systems are ready
 * near Location (the loading gate, SwcLoadingGate: the ground cover, the surface layer, the persons, the creatures, the
 * objects), and the engine work near the picture is done (at most 20 s more); or 3 s after those cells when the line
 * hits nothing (no ground there) and the systems are ready; or when TimeoutSeconds passed since the first call for this
 * place. A ground far below Location (for example the Landscape below the floor of a building) means that the cells of
 * the floor are not there yet: the wait goes on. Until then a streaming source (a sphere of 150 m, the highest priority)
 * stands at Location, and the place of the loading gate is Location (call again each frame). The light: a camera at the
 * place (behind the feet along Forward, as the camera of the character) is the view of the player during the wait (a travel:
 * from 1 s after its start), with a fast eye adaptation; the wait also needs 12 frames at the place with an exposure within
 * 0.05 EV (at most 10 s after the content). Release gives the view back to the character.
 */
SOULWARDENCLIENT_API bool IsReadyAt(UWorld* World, const FVector& Location, double TimeoutSeconds = 90.0, const FVector& Forward = FVector::ZeroVector);

/** Removes the streaming source of IsReadyAt (the character has its own). */
SOULWARDENCLIENT_API void Release();
}
