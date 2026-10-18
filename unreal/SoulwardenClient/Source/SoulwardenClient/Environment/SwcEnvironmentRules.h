#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

// The rules of the runtime environment (stage E1, docs/ENVIRONMENT.md; research world-environment.md): the time curves of the presets
// (SWC_CONTENT/environment/presets.json of tools/convert-environment.sh), the sun path and the moon of the main-world mission, the
// branch of the dominant light at the visual dawn and dusk, and the time-of-day volumes. Plain C++ with no engine header:
// tools/test-environment.sh tests it. The engine space: meters, X east, Y north, Z up.

namespace swc::environment {

struct Vec3 {
    double x{0.0};
    double y{0.0};
    double z{0.0};
};

/** One key of a spline: the time (0 to 1), the value (1 or 3 channels), and the tangent flags (bits 0 to 2 incoming, 3 to 5 outgoing:
 *  2 and 3 zero or step, 4 linear; the higher bits are editor flags). */
struct Key {
    double time{0.0};
    std::array<double, 3> value{};
    std::uint32_t flags{0};
};

/** A variable of a preset: its keys, or its saved value when it has no key. */
struct Curve {
    int channels{1};
    std::vector<Key> keys;
    std::array<double, 3> saved{};
};

/**
 * @return The value of a curve at the time t (0 to 1): the Bezier rule of the public Lumberyard splines with the tangent flags of each
 * key (the evaluator of converter/swc_convert/environment.py; an exact key time gives the key value). A compatible evaluator, not a
 * proof of the retail one (research, open point 4).
 */
std::array<double, 3> evaluate(const Curve& curve, double t);

/** @return The time of a curve for an hour: float32(hour) x float32(1/24) (the retail evaluator; research, normalization evidence). */
double curve_time(double hour);

/**
 * @return The direction toward the sun (engine space, unit) at an hour (the public engine with the sun linked to the time of day:
 * the angle (hour + 12) / 24 x 2 pi about Z, then the longitude about X and the sun rotation about Y of the mission; research
 * world-environment.md, "Sun, moon, sky, and clouds"). The optional retail clamp of the sun path is not modeled (open point 2).
 */
Vec3 sun_toward(double hour, double sunRotationDegrees, double longitudeDegrees);

/** @return The direction toward the moon (engine space, unit): the latitude and the longitude of the Moon element of the mission
 *  (the public engine: a fixed direction; 240, 35 give about 0.41, 0.71, 0.57). */
Vec3 moon_toward(double latitudeDegrees, double longitudeDegrees);

/** The visual dawn and dusk of the client (hours; client code 0x644ECC0 with the record: 6.0 to 6.3667 and 19.0 to 19.3667). */
struct Edges {
    double dawnStart{6.0};
    double dawnEnd{6.0 + 22.0 / 60.0};
    double duskStart{19.0};
    double duskEnd{19.0 + 22.0 / 60.0};
};
/** @return The edges of a record: the start at dawnTime and duskTime, the length duration x ((dawnTime - duskTime + 86400) / dayLength,
 *  an integer division) / 3600 hours. */
Edges edges_of(std::uint32_t dawnTime, std::uint32_t dawnDuration, std::uint32_t duskTime, std::uint32_t duskDuration, std::uint32_t dayLength);

/**
 * The multipliers of the two directional lights at an hour (the public engine, CTimeOfDay::UpdateEnvLighting): by night the dominant
 * light is the moon (1); in the first half of the dawn the moon fades out, in the second half the sun fades in; by day the sun (1);
 * in the first half of the dusk the sun fades out, in the second half the moon fades in. The value of "Sun intensity" times the
 * multiplier is the illuminance of the light (lux).
 */
struct LightMultipliers {
    double sun{0.0};
    double moon{0.0};
};
LightMultipliers light_multipliers(double hour, const Edges& edges);

/**
 * The color of a light with the luminance of the source rule (ConvertIlluminanceToLightColor: the color keeps its hue and the
 * illuminance is its luminance): a color with the largest channel 1, and the factor of the intensity (the illuminance x factor gives
 * the luminance of the color the illuminance; Rec. 709 luminance of the linear color).
 */
struct LightColor {
    std::array<double, 3> color{1.0, 1.0, 1.0};
    double intensityFactor{1.0};
};
LightColor light_color(const std::array<double, 3>& linear);

/** A time-of-day volume of volumes.json (stage E0): its shape in the frame of its entity, its preset, priority, and hour override. */
struct Volume {
    enum class Shape { none, box, cylinder, sphere, capsule, polygonPrism };
    Shape shape{Shape::none};
    /** The world matrix (rows, engine m) and its inverse. */
    std::array<double, 16> matrix{};
    std::array<double, 16> inverse{};
    double radius{0.0};
    double height{0.0};
    std::array<double, 3> dimensions{};
    std::vector<std::array<double, 2>> vertices;
    std::string preset;
    std::string sky;
    int priority{0};
    double blendTime{0.0};
    bool hasHourOverride{false};
    double hourOverride{0.0};
};
/** @return The inverse of a 4 x 4 matrix (rows), or the identity for a singular one. */
std::array<double, 16> inverse_of(const std::array<double, 16>& m);
/** @return True when a point (engine m) is in the shape of a volume (a cylinder and a box are centered on the origin of the entity, a
 *  polygon prism goes up from it). */
bool contains(const Volume& volume, const Vec3& point);
/** @return The index of the volume that holds a point with the highest priority (the first one of a tie), or -1. */
int select_volume(const std::vector<Volume>& volumes, const Vec3& point);

} // namespace swc::environment
