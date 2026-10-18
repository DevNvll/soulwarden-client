#include "Creatures/SwcHitShapes.h"

namespace
{
/** The steps of the golden section search on a segment: the interval becomes 0.618^40 of the segment (1e-8). */
constexpr int32 SearchSteps = 40;

FVector ClosestOnBox(const FSwcHitShape& Box, const FVector& Point)
{
	const FVector Local = Box.Rotation.UnrotateVector(Point - Box.Center);
	const FVector Clamped(FMath::Clamp(Local.X, -Box.HalfSize.X, Box.HalfSize.X), FMath::Clamp(Local.Y, -Box.HalfSize.Y, Box.HalfSize.Y),
		FMath::Clamp(Local.Z, -Box.HalfSize.Z, Box.HalfSize.Z));
	return Box.Center + Box.Rotation.RotateVector(Clamped);
}

/** The closest points of a segment and a box. The distance from a point of the segment to a convex box is convex along the
 *  segment: a golden section search finds its minimum. */
void SegmentToBox(const FSwcHitShape& Segment, const FSwcHitShape& Box, FVector& OutOnSegment, FVector& OutOnBox)
{
	const auto At = [&Segment](double T) { return FMath::Lerp(Segment.Start, Segment.End, T); };
	const auto Distance = [&](double T) { return FVector::DistSquared(At(T), ClosestOnBox(Box, At(T))); };
	constexpr double Ratio = 0.6180339887498949;
	double Low = 0.0;
	double High = 1.0;
	double A = High - Ratio * (High - Low);
	double B = Low + Ratio * (High - Low);
	double DistanceA = Distance(A);
	double DistanceB = Distance(B);
	for (int32 Step = 0; Step < SearchSteps; ++Step)
	{
		if (DistanceA <= DistanceB)
		{
			High = B;
			B = A;
			DistanceB = DistanceA;
			A = High - Ratio * (High - Low);
			DistanceA = Distance(A);
		}
		else
		{
			Low = A;
			A = B;
			DistanceA = DistanceB;
			B = Low + Ratio * (High - Low);
			DistanceB = Distance(B);
		}
	}
	const double T = (Low + High) * 0.5;
	OutOnSegment = At(T);
	OutOnBox = ClosestOnBox(Box, OutOnSegment);
}

/** @return True when two boxes overlap (the separating axis test: the 3 + 3 face axes and the 9 edge axes). */
bool BoxesOverlap(const FSwcHitShape& A, const FSwcHitShape& B)
{
	const FVector AxesA[3] = {A.Rotation.GetAxisX(), A.Rotation.GetAxisY(), A.Rotation.GetAxisZ()};
	const FVector AxesB[3] = {B.Rotation.GetAxisX(), B.Rotation.GetAxisY(), B.Rotation.GetAxisZ()};
	const FVector Between = B.Center - A.Center;
	const auto Separates = [&](const FVector& Axis)
	{
		if (Axis.SizeSquared() < 1e-12)
		{
			return false;
		}
		const auto Extent = [&Axis](const FVector (&Axes)[3], const FVector& Half)
		{ return Half.X * FMath::Abs(Axes[0] | Axis) + Half.Y * FMath::Abs(Axes[1] | Axis) + Half.Z * FMath::Abs(Axes[2] | Axis); };
		return FMath::Abs(Between | Axis) > Extent(AxesA, A.HalfSize) + Extent(AxesB, B.HalfSize);
	};
	for (int32 I = 0; I < 3; ++I)
	{
		if (Separates(AxesA[I]) || Separates(AxesB[I]))
		{
			return false;
		}
		for (int32 J = 0; J < 3; ++J)
		{
			if (Separates(AxesA[I] ^ AxesB[J]))
			{
				return false;
			}
		}
	}
	return true;
}
} // namespace

FSwcHitShape FSwcHitShape::Sphere(const FVector& InCenter, float InRadius)
{
	return Capsule(InCenter, InCenter, InRadius);
}

FSwcHitShape FSwcHitShape::Capsule(const FVector& InStart, const FVector& InEnd, float InRadius)
{
	FSwcHitShape Shape;
	Shape.Core = ECore::Segment;
	Shape.Start = InStart;
	Shape.End = InEnd;
	Shape.Radius = InRadius;
	return Shape;
}

FSwcHitShape FSwcHitShape::Box(const FVector& InCenter, const FQuat& InRotation, const FVector& InHalfSize)
{
	FSwcHitShape Shape;
	Shape.Core = ECore::Box;
	Shape.Center = InCenter;
	Shape.Rotation = InRotation;
	Shape.HalfSize = InHalfSize;
	return Shape;
}

bool SwcShapeContact(const FSwcHitShape& Volume, const FSwcHitShape& Attack, const FVector& Fallback, FVector& OutPoint, FVector& OutNormal)
{
	using ECore = FSwcHitShape::ECore;
	FVector OnVolume;
	FVector OnAttack;
	if (Volume.Core == ECore::Segment && Attack.Core == ECore::Segment)
	{
		FMath::SegmentDistToSegmentSafe(Volume.Start, Volume.End, Attack.Start, Attack.End, OnVolume, OnAttack);
	}
	else if (Volume.Core == ECore::Segment)
	{
		SegmentToBox(Volume, Attack, OnVolume, OnAttack);
	}
	else if (Attack.Core == ECore::Segment)
	{
		SegmentToBox(Attack, Volume, OnAttack, OnVolume);
	}
	else
	{
		if (!BoxesOverlap(Volume, Attack))
		{
			return false;
		}
		OnVolume = ClosestOnBox(Volume, Attack.Center);
		OnAttack = OnVolume;
	}
	const FVector Between = OnAttack - OnVolume;
	const double Distance = Between.Size();
	if (Distance > Volume.Radius + Attack.Radius)
	{
		return false;
	}
	// 0.1 mm: the cores meet.
	if (Distance > 0.01)
	{
		OutNormal = Between / Distance;
		OutPoint = OnVolume + OutNormal * Volume.Radius;
	}
	else
	{
		OutNormal = Fallback.GetSafeNormal(UE_SMALL_NUMBER, FVector::XAxisVector);
		OutPoint = OnVolume;
	}
	return true;
}
