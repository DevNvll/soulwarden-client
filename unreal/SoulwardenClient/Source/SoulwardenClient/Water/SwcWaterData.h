#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

// The water data of the client (stage WF5, docs/WATER.md): the readers of the files of the converter (converter/swc_convert/water.py:
// SWC_CONTENT/water/REGION.water, the surfaces of the water volumes and the rivers; REGION.waterq, the water quadtree of the game)
// and the query of the water at a point. Plain C++ with no engine header: tools/test-water.sh tests it.

namespace swc::water {

/** The size of a region (m) and the flags of a quadtree leaf (region.waterqt). */
inline constexpr int kRegionSize = 2048;
inline constexpr std::uint32_t kFlagGatherable = 1;
inline constexpr std::uint32_t kFlagFishable = 2;
inline constexpr std::uint32_t kFlagFresh = 4;
inline constexpr std::uint32_t kFlagSalt = 8;

enum class Kind : std::uint8_t { volume = 0, river = 1 };

/** One water surface: a water volume (the top of its shape) or a river (its ribbon). Engine m, Z up. */
struct Body {
    Kind kind{Kind::volume};
    /** 0 prism, 1 box, 2 cylinder, 3 Bezier river, 4 linear river. */
    std::uint8_t shape{0};
    /** The index of the material in water.json "materials". */
    std::uint16_t material{0};
    /** The water fog of the source: the color (linear), the density, and the color scale (a volume: fogColorMultiplier; a river:
     *  FogColorScale), and the stream speed of a river (WaterStreamSpeed: the physics flow, m/s). */
    float fog[3]{};
    float density{0.0F};
    float scale{1.0F};
    float stream{0.0F};
    float low[3]{};
    float high[3]{};
    std::string name;
    /** x, y, z (engine m), u, v for each vertex. */
    std::vector<float> vertices;
    /** Three indices for each triangle, counterclockwise from above in engine space. */
    std::vector<std::uint32_t> indices;

    [[nodiscard]] std::size_t vertex_count() const { return vertices.size() / 5; }
};

/** @return True when the bytes are a REGION.water file of version 1; the bodies go to `out`, a fault to `error`. */
bool read_bodies(std::span<const std::uint8_t> data, std::vector<Body>& out, std::string& error);

/** The water at a point of a region: the surface height and the floor height (m; the floor is not the terrain: 0 for the ocean,
 *  the surface minus WaterVolumeDepth for a river) and the flags. */
struct Sample {
    float surface{0.0F};
    float floor{0.0F};
    std::uint32_t flags{0};
};

/** The water quadtree of one region: the nodes in level order and, for each split node, the index of its first child. */
class Tree {
public:
    /** @return True when the bytes are a REGION.waterq file of version 1. */
    bool read(std::span<const std::uint8_t> data, std::string& error);
    /** @return The water at a point of the region (m from the south-west corner of the region), or none (no water there). */
    [[nodiscard]] std::optional<Sample> at(double localX, double localY) const;
    [[nodiscard]] std::size_t size() const { return m_height.size(); }

private:
    std::vector<float> m_height;
    std::vector<float> m_floor;
    std::vector<std::uint32_t> m_flags;
    std::vector<std::uint32_t> m_firstChild;
};

/**
 * The depth class of the movement (the retail enum WaterDepth, u8; research wf5-swim/swim-rules.json, client code 0x2C55000) and
 * its rule (client code 0x6A0DBF0): the leaf of the water quadtree at the integer cell of the feet x, y; no water, or the feet
 * below the floor of the leaf: None; depth = max(surface - feet z, 0); 1.8 m or more Deep, 0.9 m or more Shallow, 0.2 m or more
 * VeryShallow (the constants at 0x804D0E4, 0x7FC22FC, 0x7FBD008). No hysteresis: the movement state takes the class at each
 * update (0x4837360).
 */
enum class DepthClass : std::uint8_t { none = 0, veryShallow = 1, shallow = 2, deep = 3 };
inline constexpr float kVeryShallowDepth = 0.2F;
inline constexpr float kShallowDepth = 0.9F;
inline constexpr float kDeepDepth = 1.8F;

/** @return The depth class of the feet (z in engine m) at the water of their cell; `depth` gets max(surface - z, 0) (0: none). */
DepthClass depth_class(const std::optional<Sample>& water, float feetZ, float* depth = nullptr);

/**
 * @return The water of a point for the movement: the leaf of the quadtree, or, where the quadtree has no water, the ocean at its level
 * (floor 0, the flag salt water). The retail client changes the depth class at the ocean where region.waterqt has no water (the east
 * beach of r_+04_+01: ~/Games/soulwarden/work/evidence/unreal-references/set3-swim/swim-heights.json, place.water).
 */
std::optional<Sample> with_ocean(const std::optional<Sample>& leaf, float oceanLevel);

/**
 * A particle parameter of the source library by the particle age (the particle libraries, libs/particles: "8.5,Random=0.2,ParticleAge=(...)"; water.py
 * parse_curve): the base value, the random part, and the keys (t, v, the in slope s and the out slope d when they are written:
 * the Alpha of ShoreFoamWaveDecal rises 0 to 1 in 0.23 with d = 4.32 at its first key, its SizeY falls with s = -0.345 at its last).
 * The value is a cubic Hermite curve between two keys with the out slope of the first and the in slope of the second; a slope that
 * is not written is 0 [inference: the spline evaluator of the engine is not known; research water-shader.md U]. Before the first key
 * and after the last key the value is the value of that key; no key: 1.
 */
struct CurveKey {
    float t{0.0F};
    float v{0.0F};
    float in{0.0F};
    float out{0.0F};
};
struct Curve {
    float base{1.0F};
    float random{0.0F};
    std::vector<CurveKey> keys;
    /** @return The curve value at the age (0 to 1), not times the base. */
    [[nodiscard]] float at(float age) const;
    /** @return base x (1 + random x (2 r - 1)) x at(age); r in 0..1 is the random value of the particle. */
    [[nodiscard]] float value(float age, float r) const;
};

/**
 * The age of a continuous emitter with one particle (Count 1, Continuous): a particle starts at SpawnDelay and lives ParticleLifeTime
 * seconds, the next one starts when it dies. @return The age (0 to 1) at the time `seconds` and the number of the particle in
 * `cycle` (for its random values), or -1 before the delay.
 */
float sheet_age(double seconds, float delay, float life, std::int64_t* cycle = nullptr);

/** @return A random value in 0..1 for a particle (an integer hash of the emitter, the sheet, and the cycle). */
float sheet_random(std::uint32_t emitter, std::uint32_t sheet, std::int64_t cycle, std::uint32_t channel);

/**
 * The source tone of the Unreal water (docs/WATER.md "The source tone"): the scattering and the absorption coefficients of Single Layer Water
 * (per cm; SingleLayerWaterShading.ush: T = exp(-(scattering + absorption) x the depth in cm), the in-scatter light x scattering /
 * extinction x (1 - T); the light is the sun E x the phase, isotropic 1 / (4 pi), plus the ambient light) from the water fog of the
 * source. The source density is per m, the same for each channel: the extinction is density / 100 per cm.
 */
struct Tone {
    float absorption[3]{};
    float scattering[3]{};
};
/** The extinction per cm of one source density unit (per m). */
inline constexpr float kTonePerCm = 0.01F;
/** The largest albedo of a tone (a water with no absorption is not a source water). */
inline constexpr float kToneMaxAlbedo = 0.95F;
/** The mean |d.z| of the view rays into the ocean (0 at a grazing view, 1 straight down) of the ocean tone [inference: a mean view]. */
inline constexpr float kOceanMeanDown = 0.5F;

/**
 * A water volume or a river (watervolume.cfx: Cfog + T (Cbehind - Cfog), T = exp(-density x the path), Cfog = FogColor x FogColorScale x
 * the sun color E / pi): the albedo is albedo_scale x FogColor x FogColorScale (the sun of Single Layer Water is E / (4 pi): the scale 4
 * with no ambient light).
 */
Tone volume_tone(const float fog[3], float scale, float density, float albedo_scale);
/**
 * The ocean (WaterFogVolume/OceanInto: Cdeep x I + Cbehind x T, T = 2^(-k (L + the depth under the surface)), k = 1.4427 density,
 * I = (2^(j L) - 1) / j, j = k (d.z - 1), Cdeep = FogColor x fogColorMultiplier x the sun color): deep water goes to
 * Cdeep / (k (1 + |d.z|)), thus the albedo is albedo_scale x FogColor x multiplier / (1.4427 density (1 + mean_down)). The near color
 * (nearFogColor x the sun x T x sat(0.1 L)) has no term in Single Layer Water [inference: left out]. A density of 0 is clear water.
 */
Tone ocean_tone(const float fog[3], float multiplier, float density, float albedo_scale, float mean_down = kOceanMeanDown);
/**
 * A gain of the albedo of each channel with the extinction of each channel held (research/water-hue.md section 5, the test of the blue of
 * the ocean): sigma_t = absorption + scattering stays; the albedo scattering / sigma_t is multiplied by the gain of the channel, at most
 * kToneMaxAlbedo; then scattering = albedo x sigma_t and absorption = sigma_t - scattering. Thus the floor transmittance does not change,
 * only the color of the in-scatter. A gain of 1 gives the tone unchanged; a channel with no extinction stays clear.
 */
Tone albedo_gain(const Tone& tone, const float gain[3]);
/**
 * The parameters of the plugin water material (Water_Material, read from its graph): the absorption coefficient is
 * 1 / (Absorption.RGB x Absorption.A), the scattering coefficient (Scattering.RGB x Scattering.RGB + the foam) / 1000.
 * @return In `absorption` and `scattering` the RGBA of the two vector parameters for a tone.
 */
void tone_parameters(const Tone& tone, float absorption[4], float scattering[4]);

/**
 * A wave of the ocean waves of the client (the Gerstner waves of the Water plugin, FGerstnerWave): the direction in Unreal space (x, y,
 * a unit vector), the wave length and the amplitude in cm, and the steepness (the plugin keeps Q = Steepness / k, the horizontal
 * amplitude in cm).
 */
struct OceanWave {
    float direction[2]{1.0F, 0.0F};
    float wavelength{0.0F};
    float amplitude{0.0F};
    float steepness{0.0F};
};
/** The number of waves of ocean_waves. */
inline constexpr int kOceanWaveCount = 24;

/**
 * The ocean waves of the source as one fixed list of 24 Gerstner waves (docs/WATER.md "Stage 2b"; research/water-storm-waves.md,
 * section 5, "A numeric explicit-wave approximation" [I]). The source has one shared FFT field for the whole ocean [R]: WavesSize S
 * multiplies its displacement and WavesAmount M divides its wave lengths. The list has the 12 strongest signed bins (nx, ny) of the source
 * spectrum ([+-1, 0], [+-1, +-1], [+-2, 0], [+-2, +-1]), each with its two copies (the base copy 80 / (M r) m, the second copy 40 / (M r) m,
 * r = |(nx, ny)|), with the source power of the omitted bins added (x 1.204449094). The direction is (-nx / r, +ny / r) (the source
 * time phase and the mirror of y). The steepness is k x the amplitude x 2 beta (the base copy) or 3 beta (the second copy), with
 * beta = 1 + nx ny / r^2 (the source horizontal shear projected on the direction). @return No wave for S <= 0 or M <= 0 (the source
 * displacement is 0; a Gerstner wave of amplitude 0 has no defined Q).
 */
std::vector<OceanWave> ocean_waves(float size, float amount);

/**
 * The underwater view of the source path (docs/WATER.md "The underwater view"; the decision of the user, 2026-10-30, option 5). The
 * client turns on a post-process pass when the camera is near or under the surface of its water (the quadtree leaf of the camera, or
 * the ocean). The pass decides for each pixel with the camera of the frame (its first point at the near plane under the surface or not:
 * a pixel in the air keeps the scene color unchanged), thus the turn-on height only has to be above the near plane and the camera move of
 * one frame (the client reads the camera of the last frame): kUnderEnter. kUnderLeave is a hysteresis of the client. Under the floor of the
 * leaf (a cave under a river) the view is off. [the margins are client values, not retail values]
 */
inline constexpr float kUnderEnter = 0.5F;
inline constexpr float kUnderLeave = 1.0F;
/** @return True when the underwater pass must run for a camera at z (engine m); `wasOn` is the last answer. */
bool under_view(const std::optional<Sample>& water, float cameraZ, bool wasOn);

/** The light of the water along a ray in the water: the in-scatter weight (x the fog color x the sun) and the transmittance of the scene
 *  behind. */
struct Inside {
    float light{0.0F};
    float transmittance{1.0F};
};
/**
 * The ocean seen from inside: WaterFogVolume/OceanOutofPS of the source (research ground-cover/waterfogvolume.cfxb.txt 293-329, the cache
 * entry 960e41c0) for a ray that starts at the depth h0 (m under the surface) with the up part dz of its direction and the path L (m) in the
 * water (clipped at the plane of the water level or at the scene), k = 1.442695 x density (base 2): light = C x (2^(j L) - 1) / j with
 * j = k (dz - 1), transmittance = 2^(-k (L + max(level - the z of the scene point, 0))) = 2^(-k (L + max(h0 - L dz, 0))) [R]. C is the CPU
 * constant cUnderWaterInScatterConst (not resolved): here 2^(-k h0), the light at the camera depth as the into shader has it at the entry
 * [I]. With h0 = 0 and dz <= 0 these are the formulas of the view from above (OceanInto, the ocean branch of water_materials.py).
 */
Inside ocean_inside(float density, float h0, float dz, float path);
/** A water volume or a river seen from inside: WaterFogVolume/WaterVolumeOutofPS (the path clipped at the plane of the water level or
 *  at the scene): light = 1 - T, transmittance T = exp(-density x path) [R]. */
Inside volume_inside(float density, float path);
/** The index of refraction of the water for the view of the surface from below. */
inline constexpr float kWaterIndex = 1.333F;
/**
 * @return The part of the light from the air that comes through a flat surface to a ray in the water that goes up with the up part dz
 * (the cosine to the surface normal): 1 - F, F the Fresnel reflectance of unpolarized light (n = kWaterIndex); 0 outside the window of
 * the sky (dz below sqrt(1 - 1 / n^2), 0.661: the total reflection) and for a ray that does not go up.
 */
float underside_transmission(float dz);
/**
 * @return True when the body can be the water of a point: its bounds have x, y (engine m) and the surface height is in its height bounds
 * (with `tolerance` m). The second rule of the body of the underwater view (no triangle of a body covers the point): of these, the body
 * with the smallest area of its bounds.
 */
bool body_has(const Body& body, float x, float y, float surface, float tolerance = 0.5F);
/**
 * @return The height (engine m) of the surface of the body at x, y: the triangle that covers the point (the plane of its three vertices;
 * a point on an edge goes to the first triangle), or none. The first rule of the body of the underwater view: of the bodies that cover the
 * point, the one with its height nearest to the quadtree surface; the view uses that height (the visible sheet).
 */
std::optional<float> surface_at(const Body& body, float x, float y);

/** @return The region (x, y numbers) of a name r_+X_+Y, or none. */
std::optional<std::pair<int, int>> region_of(const std::string& name);

} // namespace swc::water
