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
 * @return True when the world has no World Partition; or when the cells at Location are loaded and a line from 2 m
 * above Location down 50 m hits the ground; or when TimeoutSeconds passed since the first call for this place. Until
 * then a streaming source stands at Location (call again each frame).
 */
SOULWARDENCLIENT_API bool IsReadyAt(UWorld* World, const FVector& Location, double TimeoutSeconds = 30.0);

/** Removes the streaming source of IsReadyAt (the character has its own). */
SOULWARDENCLIENT_API void Release();
}
