#pragma once

#include "CoreMinimal.h"
#include "Net/Codec/SwcRepMessages.h"

/**
 * The map from the engine space of the game data and of the wire (meters; X east, Y north, Z up; right-handed) to
 * Unreal (centimeters, Z up, left-handed). PROVISIONAL: stage 1.1 (the world track) proves the map of the placements
 * with an asymmetric mesh at a known place; this function then takes its rule. Until then: Unreal Y = -engine Y.
 */
inline FVector SwcEngineToUnreal(const swc::net::WorldPosition& Position)
{
	return FVector(Position.x * 100.0, -Position.y * 100.0, Position.z * 100.0);
}
