// The camera curves of the menu scene (stage F4 of docs/FRONTEND.md): the Bezier spline tracks of the sequences of the
// NightHaven scene (CCompoundSplineTrack -> TAnimSplineTrack<Vec2> -> BezierSplineVec2). The rules are the ones of the native
// study (research frontend-native.md, FE7 evidence: the tangent update 0x7947F0 and 0x794B50, the evaluation 0x7A1F70, the
// time solve 0xF2D8D0):
//   * A key has a time, flags (incoming tangent mode bits 0..2, outgoing bits 3..5, unified bit 6), a value (time, value), and
//     the incoming and outgoing tangent vectors ds and dd (time, value).
//   * Before the evaluation, each tangent gets its mode: 0 auto (an interior key splits (P[i+1] - P[i-1]) / 3 by the two
//     relative time spans; an end key takes the adjacent difference / 3), 1 custom (the serialized vector), 2 zero (one third
//     of the adjacent time span and a flat value), 3 step, 4 linear ((P[i] - P[i-1]) / 3 and (P[i+1] - P[i]) / 3). A tangent
//     whose time reaches past the adjacent time span is scaled by span / time.
//   * A segment is the cubic Bezier curve P0 = P[i], P1 = P[i] + dd[i], P2 = P[i+1] - ds[i+1], P3 = P[i+1] in time-value space:
//     the parameter u solves B.x(u) = t (error below 1e-5, at most ten steps, u in [0, 1]), the value is B.y(u). A step key
//     holds its value over the segment.
// Plain C++: tools/test-menu.sh tests it with the keys of the scene.

#pragma once

#include <vector>

namespace swc::menu {

/** The tangent modes of a key (the flag bits). */
enum class ETangent : int
{
	Auto = 0,
	Custom = 1,
	Zero = 2,
	Step = 3,
	Linear = 4,
};

struct FCurveKey
{
	float Time = 0.0f;
	int Flags = 0;
	/** The point (time, value) and the tangents (time, value). */
	float Value[2] = {0.0f, 0.0f};
	float Ds[2] = {0.0f, 0.0f};
	float Dd[2] = {0.0f, 0.0f};

	ETangent In() const { return static_cast<ETangent>(Flags & 0x07); }
	ETangent Out() const { return static_cast<ETangent>((Flags >> 3) & 0x07); }
};

/** One scalar track: its keys (after UpdateTangents) and its value with no key. */
struct FCurve
{
	std::vector<FCurveKey> Keys;
	float Default = 0.0f;
};

/** Sets each tangent by the mode of its key (the native tangent update). */
void UpdateTangents(std::vector<FCurveKey>& Keys);

/** @return The value of a track at a time (clamped to its first and last key), or its default with no key. */
float Evaluate(const FCurve& Curve, float Time);

} // namespace swc::menu
