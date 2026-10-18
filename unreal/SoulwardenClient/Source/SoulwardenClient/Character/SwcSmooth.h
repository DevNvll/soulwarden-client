#pragma once

#include "CoreMinimal.h"

/**
 * The critically damped smoothing of the retail client (SmoothCD, C 0x104A460; research feel-values.md R1.4.2): omega = 2 / T,
 * x = omega dt, k = 1 / (1 + x + 0.48 x^2 + 0.235 x^3). A step reaches 50 % at 0.84 T, 90 % at 1.95 T. A time of 0 or less
 * gives the target at once.
 */
template <typename T>
void SwcSmoothCD(T& Value, T& Rate, const T& Target, float Time, float DeltaSeconds)
{
	if (Time <= 0.0f)
	{
		Value = Target;
		Rate = T(0);
		return;
	}
	const float Omega = 2.0f / Time;
	const float X = Omega * DeltaSeconds;
	const float K = 1.0f / (1.0f + X + 0.48f * X * X + 0.235f * X * X * X);
	const T Change = Value - Target;
	const T Temp = (Rate + Change * Omega) * DeltaSeconds;
	Rate = (Rate - Temp * Omega) * K;
	Value = Target + (Change + Temp) * K;
}

/** @return The value of a curve of (x, y) pairs at x: linear between the keys, the end values outside them. */
inline float SwcCurveAt(const TArray<float>& Pairs, float X)
{
	const int32 Count = Pairs.Num() / 2;
	if (Count == 0)
	{
		return 0.0f;
	}
	if (X <= Pairs[0])
	{
		return Pairs[1];
	}
	for (int32 Index = 1; Index < Count; ++Index)
	{
		const float X1 = Pairs[Index * 2];
		if (X <= X1)
		{
			const float X0 = Pairs[Index * 2 - 2];
			const float Alpha = X1 > X0 ? (X - X0) / (X1 - X0) : 1.0f;
			return FMath::Lerp(Pairs[Index * 2 - 1], Pairs[Index * 2 + 1], Alpha);
		}
	}
	return Pairs[Count * 2 - 1];
}
