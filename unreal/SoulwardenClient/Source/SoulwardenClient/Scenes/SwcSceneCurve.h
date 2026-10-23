#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

/**
 * The keyframe curves of a scene timeline (stage Q5, AST-240; SWC_CONTENT/quests/scenes/NAME.json, swc_convert.scenes): each key has a
 * time (s), a value, and the handles IncomingHandleX/Y and OutgoingHandleX/Y relative to the key (X in seconds, Y in the value unit).
 * [I] until research/scene-curves.md: a segment is the cubic Bezier (t0, v0), (t0 + outX0, v0 + outY0), (t1 + inX1, v1 + inY1), (t1, v1),
 * solved for the time; IncomingType is kept, not used; before the first key and after the last key the curve holds the end value. Only
 * the development preview (swc.ScenePreview) uses this rule. No engine type: tests/scenes.
 */
namespace swc::scene
{
struct Key
{
	double time{0.0};
	double value{0.0};
	double inX{0.0};
	double inY{0.0};
	double outX{0.0};
	double outY{0.0};
	int inType{0};
};

class Curve
{
public:
	std::vector<Key> keys;

	bool empty() const { return keys.empty(); }

	double evaluate(double t) const
	{
		if (keys.empty())
		{
			return 0.0;
		}
		if (t <= keys.front().time)
		{
			return keys.front().value;
		}
		if (t >= keys.back().time)
		{
			return keys.back().value;
		}
		const auto next = std::upper_bound(keys.begin(), keys.end(), t, [](double time, const Key& key) { return time < key.time; });
		const Key& a = *(next - 1);
		const Key& b = *next;
		const double span = b.time - a.time;
		if (span <= 0.0)
		{
			return b.value;
		}
		// The handles in time stay inside the segment (a Bezier in time must be monotonic).
		const double x1 = a.time + std::clamp(a.outX, 0.0, span);
		const double x2 = b.time + std::clamp(b.inX, -span, 0.0);
		const double y1 = a.value + a.outY;
		const double y2 = b.value + b.inY;
		// Solve x(s) = t by bisection (x(s) is monotonic for handles inside the segment).
		double lo = 0.0;
		double hi = 1.0;
		for (int step = 0; step < 48; ++step)
		{
			const double s = 0.5 * (lo + hi);
			if (bezier(a.time, x1, x2, b.time, s) < t)
			{
				lo = s;
			}
			else
			{
				hi = s;
			}
		}
		return bezier(a.value, y1, y2, b.value, 0.5 * (lo + hi));
	}

	double end() const { return keys.empty() ? 0.0 : keys.back().time; }

private:
	static double bezier(double p0, double p1, double p2, double p3, double s)
	{
		const double u = 1.0 - s;
		return u * u * u * p0 + 3.0 * u * u * s * p1 + 3.0 * u * s * s * p2 + s * s * s * p3;
	}
};
} // namespace swc::scene
