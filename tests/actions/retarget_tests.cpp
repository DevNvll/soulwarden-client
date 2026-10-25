// The bend of the upper body to the aim (usertest-1024 item 4; Character/SwcAttackRetarget.h): the rule of the retail update 0x6865830
// with the defaults of the AttackHeightRetargetComponent of the player slice (blend 0.35 s, limits -60 / +90, offsets 0 / 15, dead zone 5).
#include <cmath>
#include <cstdio>
#include <string>

#include "Character/SwcAttackRetarget.h"

namespace {
int g_checks = 0;
int g_failures = 0;

void check(bool condition, const std::string& text)
{
    ++g_checks;
    std::printf("%s  %s\n", condition ? "  ok" : "FAIL", text.c_str());
    g_failures += condition ? 0 : 1;
}

constexpr double kDegrees = 3.14159265358979323846 / 180.0;

/** An aim straight ahead (north, +y) at a pitch (degrees; negative: down). */
swc::retarget::Vec3 aim(double pitch)
{
    return {0.0, std::cos(pitch * kDegrees), std::sin(pitch * kDegrees)};
}

/** Runs the rule for a time at 60 frames each second; @return the last bend. */
swc::retarget::Bend run(swc::retarget::Retarget& rule, double seconds, bool active, const swc::retarget::Settings& settings,
                        const swc::retarget::Vec3& direction, const swc::retarget::Vec3& facing = {0.0, 1.0, 0.0})
{
    swc::retarget::Bend out;
    for (int frame = 0; frame < static_cast<int>(std::lround(seconds * 60.0)); ++frame) {
        out = rule.update(1.0 / 60.0, active, settings, direction, facing);
    }
    return out;
}
} // namespace

int main()
{
    using namespace swc::retarget;
    std::printf("the bend of the upper body to the aim (usertest-1024)\n");
    const Settings settings;
    {
        Retarget rule;
        const Bend level = run(rule, 1.0, true, settings, aim(0.0));
        check(std::fabs(level.angle) < 1e-9, "a level aim: no bend (inside the dead zone of 5 degrees)");
        const Bend small = run(rule, 1.0, true, settings, aim(-4.0));
        check(std::fabs(small.angle) < 1e-9, "an aim 4 degrees down: no bend (the dead zone)");
    }
    {
        Retarget rule;
        const Bend down = run(rule, 1.0, true, settings, aim(-30.0));
        check(std::fabs(down.angle / kDegrees - (-15.0)) < 1e-6, "an aim 30 degrees down: a bend of -15 degrees (the lower offset 15 [I]), " +
              std::to_string(down.angle / kDegrees));
        check(down.angle < 0.0 && down.angle >= -60.0 * kDegrees, "the bend is down and inside the -60 degree limit");
        // The axis: the horizontal right of an aim to the north is +x (east); a negative turn about +x takes the forward direction down.
        check(std::fabs(down.axis[0] - 1.0) < 1e-9 && std::fabs(down.axis[1]) < 1e-9 && std::fabs(down.axis[2]) < 1e-9, "the axis is the right of the aim");
        const Bend far = run(rule, 1.0, true, settings, aim(-89.0));
        check(std::fabs(far.angle / kDegrees - (-60.0)) < 1e-6, "an aim 89 degrees down: clamped at -60 degrees, " + std::to_string(far.angle / kDegrees));
        const Bend up = run(rule, 1.0, true, settings, aim(40.0));
        check(std::fabs(up.angle / kDegrees - 40.0) < 1e-6, "an aim 40 degrees up: a bend of +40 degrees (the upper offset 0)");
    }
    {
        // The blend: smoothstep over 0.35 s from the start of the bend; half way at 0.175 s; out over 0.35 s at the end of the clip.
        Retarget rule;
        run(rule, 0.175, true, settings, aim(-30.0));
        check(std::fabs(rule.weight() - 0.5) < 0.05, "the blend in: the weight about 0.5 at 0.175 s, " + std::to_string(rule.weight()));
        run(rule, 0.5, true, settings, aim(-30.0));
        check(std::fabs(rule.weight() - 1.0) < 1e-9, "the blend in: the weight 1 after 0.35 s");
        run(rule, 0.4, false, settings, aim(-30.0));
        check(std::fabs(rule.weight()) < 1e-9, "the blend out: the weight 0 0.35 s after the end of the clip");
    }
    {
        // The facing term: an aim to the side of the character gives no bend; an aim behind it gives none either.
        Retarget rule;
        const Bend side = run(rule, 1.0, true, settings, {std::cos(-30.0 * kDegrees), 0.0, std::sin(-30.0 * kDegrees)}, {0.0, 1.0, 0.0});
        check(std::fabs(side.angle) < 1e-6, "an aim 90 degrees to the side: no bend (the cosine of the facing is 0)");
    }
    {
        // LockTargetDirOnStart: the aim of the first active frame stays for the clip.
        Settings locked = settings;
        locked.lockDir = true;
        Retarget rule;
        rule.update(1.0 / 60.0, true, locked, aim(-30.0), {0.0, 1.0, 0.0});
        const Bend held = run(rule, 1.0, true, locked, aim(0.0));
        check(std::fabs(held.angle / kDegrees - (-15.0)) < 1e-6, "lockDir: the aim of the start (30 degrees down) stays, " + std::to_string(held.angle / kDegrees));
    }
    std::printf("%d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
