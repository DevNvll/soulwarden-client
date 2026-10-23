#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

/**
 * The scalar key curves of a scene timeline (stage Q5, AST-240; research/scene-curves.md sections 2 and 4, the client pseudocode):
 * Curve, CurveAxis, Keyframe of the quest timelines (not the menu spline of SwcMenuCurves). The RIGHT key of a pair selects the
 * mode of the interval: 0 Linear (and each value with no name), 1 Step (hold the left value), 2 the legacy Hermite branch (raw t in
 * its basis), 3 Bezier (the handles are signed offsets in time and value from the key; P1 = A + out, P2 = B + in; the time is solved
 * by subdivision, error 1e-6 or 1000 steps). Before the first and after the last key a curve holds the end value, or extends it with
 * the tangent of modes 2 and 3 (a Bezier tangent is 0 while |handleX| < 1000000: the retail literal). Float arithmetic as retail.
 * No engine type: tests/scenes.
 */
namespace swc::scene
{
struct Key
{
	float time{0.0f};
	float value{0.0f};
	float inX{0.0f};
	float inY{0.0f};
	float outX{0.0f};
	float outY{0.0f};
	int mode{0};
};

class Curve
{
public:
	std::vector<Key> keys;

	bool empty() const { return keys.empty(); }

	/** @return The value at t (seconds of the clip); an empty curve gives the fallback (NO_SAMPLE of the study). */
	float evaluate(float t, float fallback = 0.0f) const
	{
		const std::size_t n = keys.size();
		if (n == 0)
		{
			return fallback;
		}
		if (n == 1 || t == keys[0].time)
		{
			return keys[0].value;
		}
		if (t == keys[n - 1].time)
		{
			return keys[n - 1].value;
		}
		if (keys[0].time == keys[n - 1].time)
		{
			return keys[0].value;
		}
		if (t < keys[0].time)
		{
			const Key& a = keys[0];
			if (a.mode != 2 && a.mode != 3)
			{
				return a.value;
			}
			return a.value + tangentOut(0) / (keys[1].time - a.time) * (t - a.time);
		}
		if (t > keys[n - 1].time)
		{
			const Key& b = keys[n - 1];
			if (b.mode != 2 && b.mode != 3)
			{
				return b.value;
			}
			return b.value + tangentIn(n - 1) / (b.time - keys[n - 2].time) * (t - b.time);
		}
		const auto it = std::lower_bound(keys.begin(), keys.end(), t, [](const Key& key, float time) { return key.time < time; });
		const std::size_t j = static_cast<std::size_t>(it - keys.begin());
		if (keys[j].time == t)
		{
			return keys[j].value;
		}
		return samplePair(j - 1, t);
	}

	float end() const { return keys.empty() ? 0.0f : keys.back().time; }

private:
	static float cubic(const float p[4], float u)
	{
		const float v = 1.0f - u;
		return v * v * v * p[0] + 3.0f * u * v * v * p[1] + 3.0f * u * u * v * p[2] + u * u * u * p[3];
	}

	static float solveTime(float x0, float x1, float x2, float x3, float t)
	{
		if (t - x0 < 1e-6f)
		{
			return 0.0f;
		}
		if (x3 - t < 1e-6f)
		{
			return 1.0f;
		}
		float lo = 0.0f;
		float hi = 1.0f;
		for (int count = 0; count < 1000; ++count)
		{
			const float a = (x0 + x1) / 2.0f;
			const float b = (x1 + x2) / 2.0f;
			const float c = (x2 + x3) / 2.0f;
			const float d = (a + b) / 2.0f;
			const float e = (b + c) / 2.0f;
			const float midX = (d + e) / 2.0f;
			const float midU = (lo + hi) / 2.0f;
			if (std::fabs(midX - t) < 1e-6f)
			{
				return std::clamp(midU, 0.0f, 1.0f);
			}
			if (midX < t)
			{
				x0 = midX;
				x1 = e;
				x2 = c;
				lo = midU;
			}
			else
			{
				x1 = a;
				x2 = d;
				x3 = midX;
				hi = midU;
			}
		}
		return std::clamp((lo + hi) / 2.0f, 0.0f, 1.0f);
	}

	float tangentOut(std::size_t i) const
	{
		if (i + 1 >= keys.size())
		{
			return 0.0f;
		}
		const Key& a = keys[i];
		const Key& b = keys[i + 1];
		if (a.mode == 1 || a.mode == 2)
		{
			return 0.0f;
		}
		if (a.mode == 3)
		{
			return std::fabs(a.outX) < 1000000.0f ? 0.0f : (b.time - a.time) / a.outX * a.outY;
		}
		if (i == 0)
		{
			return b.value - a.value;
		}
		const Key& p = keys[i - 1];
		return (b.time - a.time) / (b.time - p.time) * (b.value - p.value);
	}

	float tangentIn(std::size_t i) const
	{
		if (i == 0 || i >= keys.size())
		{
			return 0.0f;
		}
		const Key& a = keys[i - 1];
		const Key& b = keys[i];
		if (b.mode == 1 || b.mode == 2)
		{
			return 0.0f;
		}
		if (b.mode == 3)
		{
			return std::fabs(b.inX) < 1000000.0f ? 0.0f : (b.time - a.time) / b.inX * b.inY;
		}
		if (i == keys.size() - 1)
		{
			return b.value - a.value;
		}
		const Key& next = keys[i + 1];
		return (b.time - a.time) / (next.time - a.time) * (next.value - a.value);
	}

	float samplePair(std::size_t i, float t) const
	{
		const Key& a = keys[i];
		const Key& b = keys[i + 1];
		const float u = (t - a.time) / (b.time - a.time);
		if (b.mode == 1)
		{
			return a.value;
		}
		if (b.mode == 3)
		{
			const float y[4] = {a.value, a.value + a.outY, b.value + b.inY, b.value};
			return cubic(y, solveTime(a.time, a.time + a.outX, b.time + b.inX, b.time, t));
		}
		if (b.mode == 2)
		{
			const float m0 = tangentOut(i);
			const float m1 = tangentIn(i + 1);
			const float s = t;   // the recovered legacy branch uses the raw time (research/scene-curves.md 2)
			return (2 * s * s * s - 3 * s * s + 1) * a.value + (-2 * s * s * s + 3 * s * s) * b.value + (s * s * s - 2 * s * s + s) * m0
				+ (s * s * s - s * s) * m1;
		}
		return a.value + (b.value - a.value) * u;
	}
};
} // namespace swc::scene
