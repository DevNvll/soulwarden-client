#pragma once

#include "Environment/SwcEnvironmentRules.h"

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// The rules of the local lights (stage E2, docs/ENVIRONMENT.md): the point, spot, and area lights of the slices
// (SWC_CONTENT/environment/lights/REGION.json of tools/convert-environment.sh write). Plain C++ with no engine header:
// tools/test-environment.sh tests it. The engine space: meters, X east, Y north, Z up.

namespace swc::environment {

/** One light of lights/REGION.json. */
struct LocalLight {
    enum class Type { point, spot, rect };
    Type type{Type::point};
    /** The place, and the +X axis (the direction of a spot and of a rect) and the +Z axis of the entity (unit). */
    Vec3 p;
    Vec3 x{1.0, 0.0, 0.0};
    Vec3 z{0.0, 0.0, 1.0};
    /** COLOR (linear), DiffuseMultiplier, SpecMultiplier. */
    std::array<double, 3> color{1.0, 1.0, 1.0};
    double diffuse{1.0};
    double spec{1.0};
    /** The range (m): PointMaxDistance, ProjectorDistance, or AreaMaxDistance. */
    double range{0.0};
    /** PointAttenuationBulbSize or ProjectorAttenuationBulbSize (m). */
    double bulb{0.0};
    /** ProjectorFOV (degrees, the full angle). */
    double fov{90.0};
    /** AreaWidth and AreaHeight (m). */
    double width{0.0};
    double height{0.0};
    /** ViewDistanceCap (m) when ViewDistanceCapEnabled, else 0. */
    double cap{0.0};
    bool on{true};
    /** CastShadowsSpec is not EngineSpec::Never. */
    bool shadow{false};
    bool volumetric{false};
    /** The projector texture (a game path; empty for the default spot). */
    std::string cookie;
    /** The NPC slice that owns the light (empty for a static light), the matrix of the light in the frame of the slice root
     *  (rows), and the static place of the slice root. */
    std::string npc;
    std::array<double, 16> local{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    Vec3 root;
};

/**
 * The luminance of one unit of a source light color (cd/m2): RENDERER_LIGHT_UNIT_SCALE of the public engine (TimeOfDay.cpp,
 * ConvertIlluminanceToLightColor: the illuminance of the sun / 10,000 / pi is its render color; the diffuse shading of the
 * source has no 1 / pi, so a render color of 1 on a white surface has the radiance 10,000 cd/m2).
 */
constexpr double kSourceUnitLuminance = 10000.0;
/** The smallest bulb of a spot (m): a bulb of 0 has no light in the attenuation of the source. */
constexpr double kMinBulb = 0.01;
/** The smallest bulb of a point light (m; research/lights-emission.md "Point lights": r = max(PointAttenuationBulbSize, 0.001)). */
constexpr double kMinPointBulb = 0.001;
/**
 * The gain of the point falloff light function (M_SwcPointFalloff): a light function gives at most 1, and the source falloff is
 * up to about 2.87 times the Unreal falloff (at 0.8 of the range). The light has gain x its intensity, the function 1 / gain x the
 * correction.
 */
constexpr double kPointFalloffGain = 3.0;

/**
 * @return The luminous intensity of a light in Unreal (candela, with the source color as the light color), times a look scale.
 * A point (research/lights-emission.md "Point lights" [R]): the source light RGB is C x D x K with K = (1 + 1 / r)^2, and its
 * distance weight is A = f / (1 + max(d - r, 0) / r)^2 with f = sat(5 (1 - d / R))^2: thus E = I f / max(d, r)^2 with
 * I = pi x 10,000 x D x (1 + r)^2 (r = max(bulb, 0.001), d from the center). The near clamp and f come from point_falloff (a
 * light function); the Unreal light is a point (source radius 0) with this I. A spot: the source attenuation 1 / (1 + d / r)^2
 * from the bulb surface as an Unreal sphere light with the radius r and pi x 10,000 x D x r^2 (not changed: the study covers the
 * points). A rect: the same surface luminance (10,000 x D) over the area width x height.
 */
double light_candela(const LocalLight& light, double scale);

/** @return The source radius of the Unreal light (m): 0 for a point (the bulb is not a sphere radius) and a rect; the bulb of a spot. */
double source_radius(const LocalLight& light);

/** @return The bulb r of the point falloff (m): max(bulb, kMinPointBulb). */
double point_bulb(const LocalLight& light);

/**
 * @return The correction of the Unreal inverse-square falloff to the source falloff of a point light at the distance d (m) from
 * its center (research/lights-emission.md): ((d^2 + 0.0001) / max(d, r)^2) x f / UEfade with UEfade = sat(1 - (d / R)^4)^2, the
 * radial mask of Unreal (1 cm2 of distance bias). At the range end the ratio f / UEfade tends to 25 / 16; beyond it, 0.
 */
double point_falloff(double d, double bulb, double range);

/**
 * @return The draw distance of a light (m; a choice of this client for the cost, the Unreal way: the source view distance needs
 * the client code): 25 times the range, at least 50 m and at most 250 m, and not more than the cap of the source.
 */
double draw_distance(const LocalLight& light);

/** The draw distance rule. */
constexpr double kDrawRangeRatio = 25.0;
constexpr double kDrawMin = 50.0;
constexpr double kDrawMax = 250.0;

/** A grid of light indexes by cells of the horizontal place (the lights near a point). */
class LightGrid {
public:
    explicit LightGrid(double cellSize = 64.0) : cell_(cellSize) {}
    void add(int index, const Vec3& p);
    /** Adds to out the indexes of the cells that touch the square of the radius about a point (a superset of the lights in it). */
    void query(const Vec3& center, double radius, std::vector<int>& out) const;
    std::size_t size() const { return count_; }

private:
    static std::int64_t key(std::int64_t cx, std::int64_t cy) { return (cx << 32) ^ (cy & 0xFFFFFFFF); }
    double cell_;
    std::unordered_map<std::int64_t, std::vector<int>> cells_;
    std::size_t count_{0};
};

/** The largest distance (m) between the static place of an NPC slice and a creature of that slice that owns its lights. */
constexpr double kOwnerDistance = 30.0;

/** @return True when a creature with a slice path at a place owns the lights of an NPC slice (the same path, lower case, and the
 *  creature near the static place of the slice root). */
bool owns_lights(const std::string& npcSlice, const Vec3& root, const std::string& creatureSlice, const Vec3& creature);

/** @return The product of two 4 x 4 matrices (rows). */
std::array<double, 16> multiply(const std::array<double, 16>& a, const std::array<double, 16>& b);

} // namespace swc::environment
