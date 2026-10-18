#pragma once

#include "CoreMinimal.h"

/**
 * A convex shape in Unreal world space (cm) for the melee contacts: a core (a point, a segment, or an oriented box) and a radius
 * around it. A sphere is a point with a radius, a capsule is a segment with a radius, a box has the radius 0.
 */
struct SOULWARDENCLIENT_API FSwcHitShape
{
	enum class ECore : uint8
	{
		Segment,
		Box
	};
	ECore Core = ECore::Segment;
	/** The segment (a point: Start == End). */
	FVector Start = FVector::ZeroVector;
	FVector End = FVector::ZeroVector;
	/** The box: its center, its rotation, its half size. */
	FVector Center = FVector::ZeroVector;
	FQuat Rotation = FQuat::Identity;
	FVector HalfSize = FVector::ZeroVector;
	float Radius = 0.0f;

	static FSwcHitShape Sphere(const FVector& Center, float Radius);
	static FSwcHitShape Capsule(const FVector& Start, const FVector& End, float Radius);
	static FSwcHitShape Box(const FVector& Center, const FQuat& Rotation, const FVector& HalfSize);

	/** @return The center of the shape. */
	FVector Middle() const { return Core == ECore::Box ? Center : (Start + End) * 0.5; }
};

/**
 * The contact of two shapes.
 * @param OutPoint The point of the contact on the surface of `Volume` (toward `Attack`; in the core of `Volume` when the cores
 *        meet).
 * @param OutNormal The normal of the surface of `Volume` at the point (a unit vector toward `Attack`; `Fallback` when the cores
 *        meet).
 * @return True when the shapes touch or overlap.
 */
SOULWARDENCLIENT_API bool SwcShapeContact(const FSwcHitShape& Volume, const FSwcHitShape& Attack, const FVector& Fallback, FVector& OutPoint,
	FVector& OutNormal);
