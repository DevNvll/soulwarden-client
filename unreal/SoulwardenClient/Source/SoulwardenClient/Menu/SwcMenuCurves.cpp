#include "Menu/SwcMenuCurves.h"

#include <algorithm>
#include <cmath>

namespace swc::menu {
namespace {

constexpr float Third = 1.0f / 3.0f;
/** The time error of the solve and its step count (0x7F40038, 0xF2DCF0). */
constexpr float TimeError = 1e-5f;
constexpr int SolveSteps = 10;

void Set(float* Out, float X, float Y)
{
	Out[0] = X;
	Out[1] = Y;
}

/** The time-reach limit: a tangent whose time is longer than the adjacent span is scaled by span / time. */
void Limit(float* Tangent, float Span)
{
	if (Span > 0.0f && Tangent[0] > Span)
	{
		const float Scale = Span / Tangent[0];
		Tangent[0] *= Scale;
		Tangent[1] *= Scale;
	}
}

float Bezier(float P0, float P1, float P2, float P3, float U)
{
	const float V = 1.0f - U;
	return V * V * V * P0 + 3.0f * U * V * V * P1 + 3.0f * U * U * V * P2 + U * U * U * P3;
}

float BezierSlope(float P0, float P1, float P2, float P3, float U)
{
	const float V = 1.0f - U;
	return 3.0f * V * V * (P1 - P0) + 6.0f * U * V * (P2 - P1) + 3.0f * U * U * (P3 - P2);
}

} // namespace

void UpdateTangents(std::vector<FCurveKey>& Keys)
{
	const size_t Count = Keys.size();
	if (Count < 2)
	{
		return;
	}
	// The serialized vectors (a custom tangent keeps them).
	std::vector<FCurveKey> Saved = Keys;
	for (size_t I = 0; I < Count; ++I)
	{
		FCurveKey& Key = Keys[I];
		const float* P = Key.Value;
		// The auto tangents.
		if (I == 0)
		{
			const float* Next = Keys[1].Value;
			Set(Key.Dd, (Next[0] - P[0]) * Third, (Next[1] - P[1]) * Third);
			Set(Key.Ds, Key.Dd[0], Key.Dd[1]);
		}
		else if (I == Count - 1)
		{
			const float* Prev = Keys[I - 1].Value;
			Set(Key.Ds, (P[0] - Prev[0]) * Third, (P[1] - Prev[1]) * Third);
			Set(Key.Dd, Key.Ds[0], Key.Ds[1]);
		}
		else
		{
			const float* Prev = Keys[I - 1].Value;
			const float* Next = Keys[I + 1].Value;
			const float Span = Keys[I + 1].Time - Keys[I - 1].Time;
			if (Span > 0.0f)
			{
				const float Dx = (Next[0] - Prev[0]) * Third;
				const float Dy = (Next[1] - Prev[1]) * Third;
				const float A = (Key.Time - Keys[I - 1].Time) / Span;
				Set(Key.Ds, Dx * A, Dy * A);
				Set(Key.Dd, Dx * (1.0f - A), Dy * (1.0f - A));
			}
			else
			{
				Set(Key.Ds, 0.0f, 0.0f);
				Set(Key.Dd, 0.0f, 0.0f);
			}
		}
		// The incoming tangent by its mode.
		const float SpanIn = I > 0 ? Key.Time - Keys[I - 1].Time : 0.0f;
		switch (Key.In())
		{
		case ETangent::Custom:
			Set(Key.Ds, Saved[I].Ds[0], Saved[I].Ds[1]);
			break;
		case ETangent::Linear:
			if (I > 0)
			{
				Set(Key.Ds, (P[0] - Keys[I - 1].Value[0]) * Third, (P[1] - Keys[I - 1].Value[1]) * Third);
			}
			break;
		case ETangent::Zero:
			Set(Key.Ds, SpanIn * Third, 0.0f);
			break;
		default:
			break;
		}
		// The outgoing tangent by its mode.
		const float SpanOut = I + 1 < Count ? Keys[I + 1].Time - Key.Time : 0.0f;
		switch (Key.Out())
		{
		case ETangent::Custom:
			Set(Key.Dd, Saved[I].Dd[0], Saved[I].Dd[1]);
			break;
		case ETangent::Linear:
			if (I + 1 < Count)
			{
				Set(Key.Dd, (Keys[I + 1].Value[0] - P[0]) * Third, (Keys[I + 1].Value[1] - P[1]) * Third);
			}
			break;
		case ETangent::Zero:
			Set(Key.Dd, SpanOut * Third, 0.0f);
			break;
		default:
			break;
		}
		Limit(Key.Ds, SpanIn);
		Limit(Key.Dd, SpanOut);
	}
}

float Evaluate(const FCurve& Curve, float Time)
{
	const std::vector<FCurveKey>& Keys = Curve.Keys;
	if (Keys.empty())
	{
		return Curve.Default;
	}
	if (Time <= Keys.front().Time || Keys.size() == 1)
	{
		return Keys.front().Value[1];
	}
	if (Time >= Keys.back().Time)
	{
		return Keys.back().Value[1];
	}
	size_t I = 0;
	while (I + 1 < Keys.size() && Keys[I + 1].Time <= Time)
	{
		++I;
	}
	const FCurveKey& A = Keys[I];
	const FCurveKey& B = Keys[I + 1];
	if (A.Out() == ETangent::Step || B.In() == ETangent::Step)
	{
		return A.Value[1];
	}
	const float X0 = A.Value[0];
	const float X1 = A.Value[0] + A.Dd[0];
	const float X2 = B.Value[0] - B.Ds[0];
	const float X3 = B.Value[0];
	const float Span = X3 - X0;
	float U = Span > 0.0f ? std::clamp((Time - X0) / Span, 0.0f, 1.0f) : 0.0f;
	for (int Step = 0; Step < SolveSteps; ++Step)
	{
		const float Error = Bezier(X0, X1, X2, X3, U) - Time;
		if (std::fabs(Error) < TimeError)
		{
			break;
		}
		const float Slope = BezierSlope(X0, X1, X2, X3, U);
		if (Slope == 0.0f)
		{
			break;
		}
		U = std::clamp(U - Error / Slope, 0.0f, 1.0f);
	}
	return Bezier(A.Value[1], A.Value[1] + A.Dd[1], B.Value[1] - B.Ds[1], B.Value[1], U);
}

} // namespace swc::menu
