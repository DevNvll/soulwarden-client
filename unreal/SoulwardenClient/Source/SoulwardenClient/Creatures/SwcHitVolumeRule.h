#pragma once

#include <array>
#include <cmath>

// The retail rule of a creature hit volume (usertest-1024 item 4; research usertest-hitvolume.md sections 1, 2, 5: the centered shape
// factory 0x169DC80 and the bounds function 0x16B7440 of the retail client). Plain C++ with no engine header: tools/test-creatures.sh
// tests it with the rabbit and rat values of the study.
namespace swc::creatures {

/** The size of a hit capsule in the world: the half of its center segment and its radius (m). The authored height is the center
 *  segment length (the rounded ends are not in it): half = height / 2 x scale, radius = radius x scale. The outer length is
 *  height + 2 x radius. */
struct HitCapsuleSize {
    double half{0.0};
    double radius{0.0};
};

inline HitCapsuleSize hit_capsule_size(double height, double radius, double scale)
{
    return {height * 0.5 * scale, radius * scale};
}

/** The unit axis of a hit volume in the frame of its bone: the authored triple normalized by its length (no Euler angles: the rabbit
 *  torso "0 90 1" is a direction), local +Z for a zero triple. */
inline std::array<double, 3> hit_axis(const std::array<double, 3>& raw)
{
    const double length = std::sqrt(raw[0] * raw[0] + raw[1] * raw[1] + raw[2] * raw[2]);
    if (length <= 1e-12) {
        return {0.0, 0.0, 1.0};
    }
    return {raw[0] / length, raw[1] / length, raw[2] / length};
}

/** The axis-aligned bounds of a capsule (its two ends and its radius): the broad phase of the melee query must contain them. */
inline void capsule_bounds(const std::array<double, 3>& end0, const std::array<double, 3>& end1, double radius, std::array<double, 3>& low,
                           std::array<double, 3>& high)
{
    for (int axis = 0; axis < 3; ++axis) {
        low[axis] = std::fmin(end0[axis], end1[axis]) - radius;
        high[axis] = std::fmax(end0[axis], end1[axis]) + radius;
    }
}

} // namespace swc::creatures
