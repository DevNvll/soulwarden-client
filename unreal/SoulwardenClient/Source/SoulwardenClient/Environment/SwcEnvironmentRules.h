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

/** @return The hour of the sun path with the night delay of the source (e_TimeOfDay_NightDelay, default 0.7 h; research
 *  e5-menu-probe.md): strictly inside the daylight interval (dawnStart, duskEnd) and for a positive delay, effectiveHour = a + d +
 *  q x (b - a - 2 d) with q = (hour - a) / (b - a) and d = min(delay, (b - a) / 4); else the hour. Only the sun path takes it: the
 *  curves of the time of day keep the hour. */
double sun_path_hour(double hour, double dawnStart, double duskEnd, double nightDelay);

/** @return The step of the sun light in degrees (AST-273 P1, docs/ENVIRONMENT.md "The sun step"): the light turns only when the sun
 *  path is this angle from it, so that the virtual shadow map cache holds between the turns. A turn of dTheta moves the tip of the
 *  shadow of an object of height h by h x dTheta / sin(e)^2 at the sun elevation e: the step keeps that move at most shiftPerMeter
 *  (meters for each meter of height), dTheta = shiftPerMeter x sin(e)^2, and at most maxDegrees. 0 for maxDegrees <= 0 or a sun at
 *  or below the horizon (a turn in each frame). */
double sun_step_degrees(double towardZ, double shiftPerMeter, double maxDegrees);

/** @return The edges of a mission (the Lighting element of a level mission: DawnTime, DawnDuration, DuskTime, DuskDuration in
 *  minutes; the dusk with the 12-hour offset of the engine): each window centered on its time (the retail loader). */
Edges mission_edges(double dawnMinutes, double dawnDurationMinutes, double duskMinutes, double duskDurationMinutes);

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
    /** The distance of the blend inside the edge (m; 0: no distance blend). */
    double blendDistance{0.0};
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

/** @return The depth of a point in the shape of a volume (m in the frame of its entity; positive inside, negative outside): the
 *  distance to the nearest face of a box, to the side or the caps of a cylinder, to the surface of a sphere or a capsule, to the
 *  nearest edge or cap of a polygon prism. */
double inside_depth(const Volume& volume, const Vec3& point);

/** @return The target weight of a volume at a point (stage E3): with a blend distance, the depth / the blend distance (0 to 1); else
 *  1 inside and 0 outside. */
double volume_weight(const Volume& volume, const Vec3& point);

/** @return The weight one step later: it goes to the target at the rate 1 / the blend time of the volume (at once with no blend
 *  time). */
double approach_weight(double weight, double target, double blendTime, double deltaSeconds);

/**
 * @return The height falloff of the Unreal height fog (the property: log2 units per cm x 1000) for two fog layers of the data: the
 * density at the bottom height and the density at the top height (the density falls by the ratio between the two heights). 0 when
 * the layers do not fall.
 */
double fog_height_falloff(double bottomDensity, double topDensity, double bottomHeight, double topHeight);

/** The rotation of the celestial frame (stage E3): a 3 x 3 matrix in rows. */
using Rotation = std::array<std::array<double, 3>, 3>;

/**
 * @return The rotation from the engine space to the celestial frame of the stars at an hour: the inverse of the sun path rotation of
 * sun_toward, turned half about the sun (the celestial pole is the axis of the sun path, the north pole +z above the horizon; the sun
 * is at the right ascension 0 h and the declination 0). The
 * celestial frame: x = cos(dec) sin(ra), y = cos(dec) cos(ra), z = sin(dec). An inference of this client (the public engine rule of
 * the star rotation is not known): the stars turn with the sun.
 */
Rotation celestial_from_engine(double hour, double sunRotationDegrees, double longitudeDegrees);

/** @return The unit vector of a star of the catalog (right ascension in hours, declination in degrees) in the celestial frame. */
Vec3 star_vector(double rightAscensionHours, double declinationDegrees);

/** @return A rotation times a vector. */
Vec3 rotate(const Rotation& rotation, const Vec3& v);

/**
 * @return The EV of the eye adaptation of the source (stage E5; the final scene shader of the game copy, HDRFinalScenePS) for the
 * illuminance E (lux) of the scene: log2(E x 100 / 330) - (clamp(log10(E + 1), 0.1, 5.2) - 3) / 2 x the auto compensation, clamped
 * to the EV bounds. The exposure of the source is 1 / (1.2 x 2^EV).
 */
double source_ev(double illuminance, double evMin, double evMax, double autoCompensation);

/**
 * @return The bias (EV) of the Unreal exposure curve at its EV100 that gives the exposure of the source: Unreal sets the exposure
 * 0.18 x 2^bias / M with the mean metered value M = 0.18 x 2^EV100, and the illuminance of the source is E = pi x M / meterScale
 * (the meter that ignores the materials: M = L / albedo x meterScale).
 */
double source_exposure_bias(double ev100, double evMin, double evMax, double autoCompensation, double meterScale);

/**
 * The sky model of the source (stage E5 step 2b; the public engine: SkyLightNishita.cpp, SkyLightManager.cpp, and the sky shader
 * of the game copy, skyhdr@skyhdrps): the single scattering of Nishita in an atmosphere of 100 km over an earth of 6368 km, the
 * Rayleigh density exp(-h / 7.994 km) and the Mie density exp(-h / 1.2 km).
 */
struct SkyModel {
    /** Toward the sun (engine axes, z up, unit). */
    Vec3 sunToward;
    /** "Sky light: Sun intensity" times "Sky light: Sun intensity multiplier". */
    std::array<double, 3> sunIntensity{};
    /** The data values ("Sky light: Rayleigh scattering", "Mie scattering"); the model uses 1e-4 times them. */
    double rayleigh = 1.0;
    double mie = 4.8;
    /** "Sky light: Sun anisotropy factor" (clamped to +-0.9995). */
    double g = -0.99;
    /** "Sky light: Wavelength (R, G, B)" (nm). */
    std::array<double, 3> waveLengths{693.0, 596.0, 450.0};
};

/** The two parts of the sky color in a direction (the units of the source: 10,000 cd/m2), each times its in-scattering constant
 *  and with no phase: the sky color is mie x mie_phase + rayleigh x rayleigh_phase. */
struct SkyParts {
    std::array<double, 3> mie{};
    std::array<double, 3> rayleigh{};
};

/** @return The two parts of the sky color for a view direction (engine, unit; z up) from the ground: the integral over 32 height
 *  shells along the view ray (the shell heights of the source: 100 km x exp(10 (x - 1)) x x), the light of the sun through the
 *  optical depth toward the sun (none where the earth is in the way). Below the horizon: the horizon direction. */
SkyParts source_sky(const SkyModel& model, const Vec3& view);

/** @return The Mie phase of the source (the phase constants of GetPhaseFunctionConsts: miePart (1 + c^2) / (1 + g^2 + 2 g c)^1.5)
 *  and the Rayleigh phase 0.75 (1 + c^2) for the cosine c between the view and the direction toward the sun. */
std::array<double, 2> source_sky_phases(double g, double cosine);

/** @return The point of the unit ball for a point of the cube -1 to 1 (shadelib MapCubeToSphere of the source). */
Vec3 map_cube_to_sphere(const Vec3& v);

/** @return The fade of a local probe of the source at a point in its box space (each axis -1 to 1 inside; 0 outside): the tiled
 *  shading of the source (tiledshading.cfib ApplyProbe, shadelib GetAttenuation and smoothstep_opt): saturate(1 - |s|^2) with s
 *  the point on the unit ball, then smoothstep(saturate(that / falloffMax)), times the uniform attenuation of the probe. */
double probe_fade(const Vec3& boxSpace, double falloffMax, double uniform);

/** @return The weights of the probes at a point, in the order of the light list: each probe takes its fade of the part that the
 *  probes before it left (weight = (1 - sum) x fade), and the list stops at a sum of 1 (ApplyProbe). The rest (1 - the sum) is
 *  the last value. */
std::vector<double> probe_weights(const std::vector<double>& fades);

/** The fall of a Snow volume (AST-257; research/snow.md section 1, the native 0xF1E6E0): the weight at the rendering camera, 1 at the
 *  center and 0 at the radius: max(1 - |C - P|^2 / R^2, 0) with the 3D distance and R = max(Radius, 0.001). No inner radius. */
double snow_count_weight(const Vec3& camera, const Vec3& center, double radius);

/** @return The flake count of each cluster: trunc(count x weight) (the integer steps of the source). */
int snow_flake_count(int count, double weight);

/** The cluster lifetime of the legacy flakes (research/snow.md section 2, 0x5FF17C0): 4 / max(gravity scale, 1) plus a random part
 *  of -2.5 to 2.5 s (random: 0 to 1); at least 0.1 s (a rule of this client: no negative life). */
double snow_cluster_life(double gravityScale, double random);

/** @return The downward speed of a cluster (m/s; 0x5FF18A7): gravity scale x 9.8 x the cluster multiplier (no acceleration). */
double snow_fall_speed(double gravityScale, double multiplier);

/** The shadow paths of a directional light (the screening test of the hour fault, AST-200 stage 2c, research/water-hour-rule.md section 6):
 *  the fields of ULightComponentBase, ULightComponent, and UDirectionalLightComponent that make a sun shadow (the shadow maps and the
 *  virtual shadow maps, the static shadows, the translucent and volumetric shadows, the distance-field shadows, the contact shadows, and
 *  the cloud shadows). */
struct ShadowPaths {
    bool castShadows{true};
    bool castStaticShadows{true};
    bool castDynamicShadows{true};
    bool castTranslucentShadows{true};
    bool castVolumetricShadow{true};
    bool rayTracedDistanceFieldShadows{false};
    bool castCloudShadows{false};
    double contactShadowLength{0.0};
    bool operator==(const ShadowPaths&) const = default;
};

/** @return The paths with every shadow off (each flag false, the contact shadow length 0). */
ShadowPaths shadow_paths_off();

/** @return The names of the fields that differ between two path sets (the log of a change). */
std::vector<std::string> shadow_path_changes(const ShadowPaths& from, const ShadowPaths& to);

/** The reversible switch of the shadow paths: the first "off" saves the paths of the light, each "off" gives all paths off, and the first
 *  "on" after that gives the saved paths back exactly (once; then the light is its own again). With no "off", "on" changes nothing. */
class ShadowSwitch {
public:
    /** @return The paths that the light must have now; `current` are the paths that it has. */
    ShadowPaths update(bool off, const ShadowPaths& current);
    bool holds() const { return m_saved; }

private:
    bool m_saved{false};
    ShadowPaths m_paths;
};

} // namespace swc::environment
