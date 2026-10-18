#pragma once

#include "CoreMinimal.h"
#include "Net/Codec/SwcRepMessages.h"

/**
 * The map from the engine space of the game data and of the wire (meters; X east, Y north, Z up; right-handed) to
 * Unreal (centimeters; X east, Y south, Z up; left-handed). Stage 1.1 proved it in Unreal with asymmetric markers and
 * the placements of one area (docs/ARCHITECTURE.md, "Spaces"; converter/swc_convert/space.py):
 * Unreal = C x engine with C = diag(100, -100, 100). The world is not mirrored. The imported meshes use the same map:
 * a mesh that faces engine +Y (north, the forward of a character of the game) faces Unreal -Y in its asset.
 */
inline FVector SwcEngineToUnreal(const swc::net::WorldPosition& Position)
{
	return FVector(Position.x * 100.0, -Position.y * 100.0, Position.z * 100.0);
}

/** The inverse map: an Unreal position (cm) to the engine space of the wire (m). */
inline swc::net::WorldPosition SwcUnrealToEngine(const FVector& Position)
{
	return swc::net::WorldPosition{static_cast<float>(Position.X / 100.0), static_cast<float>(-Position.Y / 100.0),
		static_cast<float>(Position.Z / 100.0)};
}

/** A rotation of the engine space as an Unreal rotation: the quaternion (x, y, z, w) becomes (-x, y, -z, w). An engine
 *  yaw of a degrees (from east toward north) is an Unreal yaw of -a degrees. */
inline FQuat SwcEngineToUnreal(const swc::net::Quaternion& Rotation)
{
	return FQuat(-Rotation.x, Rotation.y, -Rotation.z, Rotation.w);
}
