// The retail rule of the creature hit volumes (usertest-1024 item 4; Creatures/SwcHitVolumeRule.h): the capsule size, the axis, and the
// bounds, with the rabbit and rat values of the study research/usertest-hitvolume.md (section 2, the table; the retail centered shape
// factory 0x169DC80 and the bounds function 0x16B7440).
#include <cmath>
#include <cstdio>
#include <string>

#include "Creatures/SwcHitVolumeRule.h"

namespace {
int g_checks = 0;
int g_failures = 0;

void check(bool condition, const std::string& text)
{
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::printf("FAIL  %s\n", text.c_str());
    } else {
        std::printf("  ok  %s\n", text.c_str());
    }
}

bool near(double a, double b, double tolerance = 1e-6)
{
    return std::fabs(a - b) <= tolerance;
}

struct Row {
    const char* name;
    double radius;
    double height;
    double half;
    double outer;
};
} // namespace

int main()
{
    using namespace swc::creatures;
    std::printf("the hit volumes of the creatures (usertest-1024)\n");
    // The table of the study: the retail radius, half-segment, and outer length before the creature scale.
    const Row rows[] = {
        {"rabbit bind_spine2 (torso)", 0.50, 0.25, 0.125, 1.25},
        {"rabbit bind_head_01", 0.15, 0.10, 0.050, 0.40},
        {"rat bind_pelvis", 0.20, 0.50, 0.250, 0.90},
        {"rat front lower arm", 0.10, 0.25, 0.125, 0.45},
        {"rat back lower leg", 0.10, 0.25, 0.125, 0.45},
        {"rat bind_tail_c", 0.10, 0.50, 0.250, 0.70},
    };
    for (const Row& row : rows) {
        const HitCapsuleSize size = hit_capsule_size(row.height, row.radius, 1.0);
        check(near(size.half, row.half) && near(size.radius, row.radius) && near(2.0 * size.half + 2.0 * size.radius, row.outer),
              std::string(row.name) + ": half-segment " + std::to_string(size.half) + ", outer length " + std::to_string(2.0 * (size.half + size.radius)));
    }
    // The rabbit of the check has the creature scale 1.1: the torso half-segment 0.1375 m, the radius 0.55 m, the outer length 1.375 m.
    const HitCapsuleSize scaled = hit_capsule_size(0.25, 0.50, 1.1);
    check(near(scaled.half, 0.1375) && near(scaled.radius, 0.55) && near(2.0 * (scaled.half + scaled.radius), 1.375),
          "the rabbit torso at scale 1.1: half 0.1375 m, radius 0.55 m, outer 1.375 m");
    // The old client rule max(0, height / 2 - radius) made the torso a sphere: the retail rule does not.
    check(hit_capsule_size(0.25, 0.50, 1.0).half > 0.0 && std::fmax(0.0, 0.25 * 0.5 - 0.50) == 0.0,
          "the rabbit torso is a capsule (the old rule gave a sphere)");
    // The axis: normalized, no Euler angles; +Z for a zero triple.
    const auto torso = hit_axis({0.0, 90.0, 1.0});
    check(near(torso[0], 0.0) && near(torso[1], 0.99993828, 1e-8) && near(torso[2], 0.01111043, 1e-8), "the rabbit torso axis 0 90 1 is a direction");
    const auto zero = hit_axis({0.0, 0.0, 0.0});
    check(zero[0] == 0.0 && zero[1] == 0.0 && zero[2] == 1.0, "a zero axis is local +Z (the rat pelvis and tail)");
    const auto leg = hit_axis({1.0, 0.5, 0.0});
    check(near(leg[0], 0.89442719, 1e-8) && near(leg[1], 0.44721360, 1e-8) && near(leg[2], 0.0), "the rat left back leg axis 1 0.5 0");
    // The torso ends in the bone frame: +/- (0, 0.12499228, 0.00138880) m.
    check(near(torso[1] * 0.125, 0.12499228, 1e-8) && near(torso[2] * 0.125, 0.00138880, 1e-8), "the rabbit torso end offsets");
    // The bounds of a capsule: its two ends and its radius (the broad phase must contain the volume).
    std::array<double, 3> low{};
    std::array<double, 3> high{};
    capsule_bounds({0.0, -0.125, 0.3}, {0.0, 0.125, 0.3}, 0.5, low, high);
    check(near(low[0], -0.5) && near(low[1], -0.625) && near(low[2], -0.2) && near(high[0], 0.5) && near(high[1], 0.625) && near(high[2], 0.8),
          "the bounds of the rabbit torso lying along y at 0.3 m: -0.5 -0.625 -0.2 to 0.5 0.625 0.8");
    std::printf("%d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
