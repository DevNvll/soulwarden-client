#pragma once

#include <array>

// The bend of the upper body to the aim during an attack (the Mannequin clip AttackHeightRetargeting and the AttackHeightRetargetComponent
// of the player slice; usertest-1024 item 4, research usertest-hitvolume.md section 4: the retail update 0x6865830, the direction
// 0x6781C60). Plain C++ with no engine header: tools/test-actions.sh tests it. Engine space (x east, y north, z up), radians inside.
namespace swc::retarget {

using Vec3 = std::array<double, 3>;

/** The settings of a clip: the defaults of the component, with the overrides of the clip (degrees, s). */
struct Settings {
    double blendIn{0.35};
    double blendOut{0.35};
    double minAngle{-60.0};
    double maxAngle{90.0};
    double upperOffset{0.0};
    double lowerOffset{15.0};
    double deadZone{5.0};
    bool lockDir{false};
};

/** The result of a frame: the angle of the whole bend (radians, negative: down) and the unit axis of the turn in engine space (the
 *  horizontal right of the aim); each of the N joints turns by angle / N about the axis. */
struct Bend {
    double angle{0.0};
    Vec3 axis{1.0, 0.0, 0.0};
};

class Retarget {
public:
    /**
     * One frame (the order of 0x6865830):
     * - the horizontal part h of the aim direction (normalized; +x when the aim is vertical), and the angle between h and the aim,
     *   signed by the height of the aim, times max(0, cos) of the facing of the character to h;
     * - a change of the side of the dead zone (|angle| against it), and the start and the end of the clip [I], restart the blend
     *   from the current weight;
     * - the blend: in (toward 1) over blendIn while the clip is active and |angle| > dead zone, else out (toward 0) over blendOut, a
     *   smoothstep (3 - 2u) u^2 from the weight of the restart;
     * - beyond the dead zone the upper offset (angle > dead zone) or the lower offset (angle < -dead zone) is added [I: the stored
     *   lower offset 15 is added as it is, so it reduces a downward angle];
     * - the angle times the weight, clamped to [min, max].
     * With lockDir the aim of the first active frame stays for the clip.
     */
    Bend update(double dt, bool active, const Settings& settings, const Vec3& aim, const Vec3& facing);
    [[nodiscard]] double weight() const { return m_weight; }

private:
    double m_weight{0.0};
    double m_start{0.0};
    double m_elapsed{0.0};
    double m_previous{0.0};
    bool m_wasActive{false};
    Vec3 m_lockedAim{0.0, 1.0, 0.0};
};

} // namespace swc::retarget
