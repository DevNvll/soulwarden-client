#include "Character/SwcAttackRetarget.h"

#include <algorithm>
#include <cmath>

namespace swc::retarget {

namespace {
constexpr double kDegrees = 3.14159265358979323846 / 180.0;
}

Bend Retarget::update(double dt, bool active, const Settings& settings, const Vec3& aimIn, const Vec3& facing)
{
    // The aim: with lockDir the one of the first active frame. The start and the end of the clip restart the blend from the current
    // weight [I: the update 0x6865830 restarts only at a change of the side of the dead zone; the enter and the exit of the clip must
    // restart it, else the bend would stay after the clip].
    if (active && !m_wasActive) {
        m_lockedAim = aimIn;
    }
    if (active != m_wasActive) {
        m_elapsed = 0.0;
        m_start = m_weight;
    }
    m_wasActive = active;
    const Vec3 aim = active && settings.lockDir ? m_lockedAim : aimIn;
    const double length = std::sqrt(aim[0] * aim[0] + aim[1] * aim[1] + aim[2] * aim[2]);
    const Vec3 d = length > 1e-9 ? Vec3{aim[0] / length, aim[1] / length, aim[2] / length} : Vec3{0.0, 1.0, 0.0};
    const double flat = std::sqrt(d[0] * d[0] + d[1] * d[1]);
    const Vec3 h = flat > 1e-6 ? Vec3{d[0] / flat, d[1] / flat, 0.0} : Vec3{1.0, 0.0, 0.0};
    // The angle between h and the aim, times the facing term, signed by the height of the aim.
    const double facingLength = std::sqrt(facing[0] * facing[0] + facing[1] * facing[1]);
    const double front = facingLength > 1e-9 ? std::max(0.0, (facing[0] * h[0] + facing[1] * h[1]) / facingLength) : 0.0;
    const double between = std::acos(std::clamp(h[0] * d[0] + h[1] * d[1], -1.0, 1.0));
    const double raw = (d[2] < 0.0 ? -1.0 : 1.0) * between * front;
    const double dead = settings.deadZone * kDegrees;
    // A change of the side of the dead zone restarts the blend.
    if ((std::fabs(raw) > dead) != (std::fabs(m_previous) > dead)) {
        m_elapsed = 0.0;
        m_start = m_weight;
    }
    const bool in = active && std::fabs(raw) > dead;
    const double time = in ? settings.blendIn : settings.blendOut;
    if (m_elapsed < time && time > 0.0) {
        m_elapsed = std::min(m_elapsed + dt, time);
        const double u = m_elapsed / time;
        m_weight = (3.0 - 2.0 * u) * u * u * ((in ? 1.0 : 0.0) - m_start) + m_start;
    }
    double angle = raw;
    if (raw > dead) {
        angle += settings.upperOffset * kDegrees;
    } else if (raw < -dead) {
        angle += settings.lowerOffset * kDegrees;
    }
    m_previous = raw;
    Bend out;
    out.angle = std::clamp(angle * m_weight, settings.minAngle * kDegrees, settings.maxAngle * kDegrees);
    // The horizontal right of the aim: a positive turn about it lifts the forward direction (engine space is right-handed).
    out.axis = {h[1], -h[0], 0.0};
    return out;
}

} // namespace swc::retarget
