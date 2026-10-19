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

/** @return The region (x, y numbers) of a name r_+X_+Y, or none. */
std::optional<std::pair<int, int>> region_of(const std::string& name);

} // namespace swc::water
