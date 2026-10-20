#include "Water/SwcWaterData.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace swc::water {

namespace {

constexpr std::uint32_t kMeshVersion = 1;
constexpr std::uint32_t kTreeVersion = 1;
/** The height of a split node (region.waterqt). */
constexpr float kSplit = 65535.0F;

/** A little-endian reader of the water files. */
class Reader {
public:
    explicit Reader(std::span<const std::uint8_t> data) : m_data(data) {}

    bool bytes(void* out, std::size_t count)
    {
        if (m_offset + count > m_data.size()) {
            m_ok = false;
            return false;
        }
        std::memcpy(out, m_data.data() + m_offset, count);
        m_offset += count;
        return true;
    }
    template <typename T> T value()
    {
        T out{};
        bytes(&out, sizeof(T));   // the machines of the project are little-endian
        return out;
    }
    [[nodiscard]] bool ok() const { return m_ok; }
    [[nodiscard]] std::size_t left() const { return m_data.size() - m_offset; }

private:
    std::span<const std::uint8_t> m_data;
    std::size_t m_offset{0};
    bool m_ok{true};
};

} // namespace

bool read_bodies(std::span<const std::uint8_t> data, std::vector<Body>& out, std::string& error)
{
    Reader reader(data);
    char magic[4]{};
    reader.bytes(magic, 4);
    const auto version = reader.value<std::uint32_t>();
    const auto count = reader.value<std::uint32_t>();
    if (!reader.ok() || std::memcmp(magic, "SWCW", 4) != 0 || version != kMeshVersion) {
        error = "not a water mesh file of version 1";
        return false;
    }
    out.clear();
    out.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        Body body;
        body.kind = static_cast<Kind>(reader.value<std::uint8_t>());
        body.shape = reader.value<std::uint8_t>();
        body.material = reader.value<std::uint16_t>();
        for (float& value : body.fog) {
            value = reader.value<float>();
        }
        body.density = reader.value<float>();
        body.scale = reader.value<float>();
        body.stream = reader.value<float>();
        for (float& value : body.low) {
            value = reader.value<float>();
        }
        for (float& value : body.high) {
            value = reader.value<float>();
        }
        const auto length = reader.value<std::uint16_t>();
        body.name.resize(length);
        reader.bytes(body.name.data(), length);
        const auto vertices = reader.value<std::uint32_t>();
        const auto indices = reader.value<std::uint32_t>();
        if (!reader.ok() || indices % 3 != 0 || static_cast<std::size_t>(vertices) * 20 + static_cast<std::size_t>(indices) * 4 > reader.left()) {
            error = "body " + std::to_string(index) + " is cut";
            return false;
        }
        body.vertices.resize(static_cast<std::size_t>(vertices) * 5);
        reader.bytes(body.vertices.data(), body.vertices.size() * sizeof(float));
        body.indices.resize(indices);
        reader.bytes(body.indices.data(), body.indices.size() * sizeof(std::uint32_t));
        for (const std::uint32_t vertex : body.indices) {
            if (vertex >= vertices) {
                error = "body " + std::to_string(index) + " has an index out of its vertices";
                return false;
            }
        }
        out.push_back(std::move(body));
    }
    if (!reader.ok()) {
        error = "the file is cut";
        return false;
    }
    return true;
}

bool Tree::read(std::span<const std::uint8_t> data, std::string& error)
{
    Reader reader(data);
    char magic[4]{};
    reader.bytes(magic, 4);
    const auto version = reader.value<std::uint32_t>();
    const auto size = reader.value<std::uint32_t>();
    const auto count = reader.value<std::uint32_t>();
    if (!reader.ok() || std::memcmp(magic, "SWQT", 4) != 0 || version != kTreeVersion || size != static_cast<std::uint32_t>(kRegionSize)
        || static_cast<std::size_t>(count) * 12 > reader.left()) {
        error = "not a water quadtree file of version 1 for a region of 2048 m";
        return false;
    }
    m_height.resize(count);
    m_floor.resize(count);
    m_flags.resize(count);
    m_firstChild.assign(count, 0);
    for (std::uint32_t index = 0; index < count; ++index) {
        m_height[index] = reader.value<float>();
        m_floor[index] = reader.value<float>();
        m_flags[index] = reader.value<std::uint32_t>();
    }
    // Level order: the children of the k-th split node are the nodes 1 + 4k to 4 + 4k.
    std::uint32_t splits = 0;
    for (std::uint32_t index = 0; index < count; ++index) {
        if (m_height[index] == kSplit) {
            const std::uint32_t first = 1 + 4 * splits++;
            if (first + 4 > count) {
                error = "a split node has no children";
                return false;
            }
            m_firstChild[index] = first;
        }
    }
    return reader.ok();
}

std::optional<Sample> Tree::at(double localX, double localY) const
{
    if (m_height.empty() || localX < 0.0 || localY < 0.0 || localX >= kRegionSize || localY >= kRegionSize) {
        return std::nullopt;
    }
    std::uint32_t node = 0;
    double baseX = 0.0;
    double baseY = 0.0;
    double edge = kRegionSize;
    while (m_height[node] == kSplit) {
        edge /= 2.0;
        const bool east = localX >= baseX + edge;
        const bool north = localY >= baseY + edge;
        baseX += east ? edge : 0.0;
        baseY += north ? edge : 0.0;
        // The children: (x, y), (x + h, y), (x, y + h), (x + h, y + h).
        node = m_firstChild[node] + (east ? 1U : 0U) + (north ? 2U : 0U);
    }
    if (m_height[node] == 0.0F && m_floor[node] == 0.0F) {
        return std::nullopt;
    }
    return Sample{m_height[node], m_floor[node], m_flags[node]};
}

DepthClass depth_class(const std::optional<Sample>& water, float feetZ, float* depth)
{
    if (depth != nullptr) {
        *depth = 0.0F;
    }
    if (!water || feetZ < water->floor) {
        return DepthClass::none;
    }
    const float value = water->surface - feetZ > 0.0F ? water->surface - feetZ : 0.0F;
    if (depth != nullptr) {
        *depth = value;
    }
    return value >= kDeepDepth ? DepthClass::deep : value >= kShallowDepth ? DepthClass::shallow : value >= kVeryShallowDepth ? DepthClass::veryShallow
        : DepthClass::none;
}

std::optional<Sample> with_ocean(const std::optional<Sample>& leaf, float oceanLevel)
{
    return leaf ? leaf : std::optional<Sample>(Sample{oceanLevel, 0.0F, kFlagSalt});
}

std::optional<std::pair<int, int>> region_of(const std::string& name)
{
    int x = 0;
    int y = 0;
    if (std::sscanf(name.c_str(), "r_%d_%d", &x, &y) != 2) {
        return std::nullopt;
    }
    return std::pair<int, int>{x, y};
}

float Curve::at(float age) const
{
    if (keys.empty()) {
        return 1.0F;
    }
    if (age <= keys.front().t) {
        return keys.front().v;
    }
    for (std::size_t i = 0; i + 1 < keys.size(); ++i) {
        const CurveKey& a = keys[i];
        const CurveKey& b = keys[i + 1];
        if (age <= b.t) {
            const float span = b.t - a.t;
            if (span <= 0.0F) {
                return b.v;
            }
            const float u = (age - a.t) / span;
            const float u2 = u * u;
            const float u3 = u2 * u;
            return (2.0F * u3 - 3.0F * u2 + 1.0F) * a.v + (u3 - 2.0F * u2 + u) * span * a.out + (-2.0F * u3 + 3.0F * u2) * b.v
                + (u3 - u2) * span * b.in;
        }
    }
    return keys.back().v;
}

float Curve::value(float age, float r) const
{
    return base * (1.0F + random * (2.0F * r - 1.0F)) * at(age);
}

float sheet_age(double seconds, float delay, float life, std::int64_t* cycle)
{
    if (life <= 0.0F || seconds < delay) {
        return -1.0F;
    }
    const double t = (seconds - delay) / life;
    const double whole = std::floor(t);
    if (cycle != nullptr) {
        *cycle = static_cast<std::int64_t>(whole);
    }
    return static_cast<float>(t - whole);
}

float sheet_random(std::uint32_t emitter, std::uint32_t sheet, std::int64_t cycle, std::uint32_t channel)
{
    std::uint64_t h = 1469598103934665603ULL;
    for (const std::uint64_t part : {static_cast<std::uint64_t>(emitter), static_cast<std::uint64_t>(sheet), static_cast<std::uint64_t>(cycle),
             static_cast<std::uint64_t>(channel)}) {
        h = (h ^ part) * 1099511628211ULL;
        h ^= h >> 29;
    }
    return static_cast<float>((h >> 40) & 0xFFFFFFULL) / static_cast<float>(0x1000000);
}

} // namespace swc::water
