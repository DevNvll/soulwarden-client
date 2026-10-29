#include "Objects/SwcObjectRules.h"

#include <algorithm>
#include <cstring>

namespace swc::objects {
namespace {

constexpr std::uint32_t kFormat = 1;
constexpr std::size_t kHeaderSize = 24;
constexpr std::size_t kNodeSize = 20;

int hex_digit(char value) {
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    if (value >= 'A' && value <= 'F') {
        return value - 'A' + 10;
    }
    return -1;
}

template <typename T>
T read_at(std::span<const std::uint8_t> bytes, std::size_t offset) {
    T value{};
    std::memcpy(&value, bytes.data() + offset, sizeof(T));   // the file is little-endian, as the machines of the client
    return value;
}

} // namespace

std::optional<std::array<std::uint8_t, 16>> parse_uuid(std::string_view text) {
    std::array<std::uint8_t, 16> out{};
    std::size_t count = 0;
    int high = -1;
    for (const char value : text) {
        if (value == '-') {
            continue;
        }
        const int digit = hex_digit(value);
        if (digit < 0 || count >= out.size()) {
            return std::nullopt;
        }
        if (high < 0) {
            high = digit;
        } else {
            out[count++] = static_cast<std::uint8_t>((high << 4) | digit);
            high = -1;
        }
    }
    if (count != out.size() || high >= 0) {
        return std::nullopt;
    }
    return out;
}

swc::net::ActorId placement_actor(const std::array<std::uint8_t, 16>& uuid) {
    swc::net::ActorId actor{};
    for (std::size_t index = 0; index < 8; ++index) {
        actor[7 - index] = static_cast<std::uint8_t>(uuid[index] ^ uuid[index + 8]);
    }
    actor[0] = static_cast<std::uint8_t>((actor[0] & 0xF8) | 3);
    return actor;
}

swc::net::ActorId actor_of_group(std::uint64_t group) {
    swc::net::ActorId actor{};
    for (std::size_t index = 0; index < 8; ++index) {
        actor[index] = static_cast<std::uint8_t>(group >> (8 * index));
    }
    return actor;
}

std::uint64_t natural_group(std::int32_t regionX, std::int32_t regionY, std::uint32_t counter) {
    return (static_cast<std::uint64_t>(counter & 0x7FFFFU) << 13) | (static_cast<std::uint64_t>(regionY & 0x1F) << 8)
        | (static_cast<std::uint64_t>(regionX & 0x1F) << 3) | 1U;
}

BoundIds bound_ids(std::uint64_t root, std::uint64_t group, std::uint64_t boundEntity, std::uint64_t component, std::uint64_t option) {
    const std::uint64_t key = root ^ group;
    return {(boundEntity ^ key) + component, option ^ key};
}

std::uint32_t tool_slot(std::string_view gatheringType) {
    if (gatheringType == "Chopping") {
        return 33;
    }
    if (gatheringType == "Cutting") {
        return 34;
    }
    if (gatheringType == "Dressing") {
        return 35;
    }
    if (gatheringType == "Mining") {
        return 36;
    }
    return 0;
}

ToolCheck check_tool(std::string_view gatheringType, std::string_view restriction, bool slotItemHasType) {
    ToolCheck out;
    out.slot = tool_slot(gatheringType);
    out.hasTool = out.slot != 0 && slotItemHasType;
    out.missing = out.slot != 0 && !out.hasTool && restriction == "ToolRequired";
    return out;
}

bool client_hides_option(std::string_view optionName) {
    return optionName == "OpenMissionBoardOption";
}

std::string gather_use_kind(std::string_view action, std::string_view toolTags, bool hasTool) {
    constexpr std::string_view kOwn[] = {"Chopping", "ChoppingVertical", "Mining", "Cutting", "Dressing"};
    for (const std::string_view own : kOwn) {
        if (hasTool && action == own) {
            return "gather|" + std::string(action) + "|" + std::string(toolTags);
        }
    }
    return action == "Handcrafting" ? "gather|Handcrafting|" : "gather|None|";
}

ChestState chest_state(std::optional<std::uint32_t> claim) {
    if (claim == kClaimLooted) {
        return ChestState::looted;
    }
    if (claim == kClaimLocked || claim == kClaimNotOnQuest) {
        return ChestState::locked;
    }
    return ChestState::ready;
}

ChestChange chest_change(std::optional<std::uint32_t> before, std::optional<std::uint32_t> now) {
    const bool wasLooted = before == kClaimLooted;
    const bool isLooted = now == kClaimLooted;
    if (isLooted && !wasLooted) {
        return ChestChange::depleted;
    }
    if (wasLooted && !isLooted) {
        return ChestChange::reset;
    }
    return ChestChange::none;
}

bool own_mesh(double largestSizeMeters, double originPlaneMeters, double originHeightMeters, bool boxHoldsObject) {
    if (largestSizeMeters > kSightOwnSizeMeters) {
        return false;
    }
    const bool near = originPlaneMeters <= kSightOwnOriginMeters && originHeightMeters <= 2.0 * kSightOwnOriginMeters;
    return near || boxHoldsObject;
}

std::optional<double> box_exit(const std::array<double, 3>& min, const std::array<double, 3>& max, const std::array<double, 3>& from,
    const std::array<double, 3>& step) {
    std::optional<double> exit;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        if (step[axis] > 1.0e-9 || step[axis] < -1.0e-9) {
            const double at = ((step[axis] > 0.0 ? max[axis] : min[axis]) - from[axis]) / step[axis];
            exit = exit ? std::min(*exit, at) : at;
        }
    }
    return exit;
}

bool NodeFile::read(std::span<const std::uint8_t> bytes) {
    m_offsets.clear();
    m_nodes.clear();
    if (bytes.size() < kHeaderSize || std::memcmp(bytes.data(), "SWON", 4) != 0 || read_at<std::uint32_t>(bytes, 4) != kFormat) {
        return false;
    }
    m_regionX = read_at<std::int32_t>(bytes, 8);
    m_regionY = read_at<std::int32_t>(bytes, 12);
    const auto count = read_at<std::uint32_t>(bytes, 16);
    m_side = read_at<std::uint32_t>(bytes, 20);
    if (m_side == 0 || m_side > 1024) {
        return false;
    }
    const std::size_t cells = static_cast<std::size_t>(m_side) * m_side + 1;
    const std::size_t nodesAt = kHeaderSize + cells * 4;
    if (bytes.size() != nodesAt + static_cast<std::size_t>(count) * kNodeSize) {
        return false;
    }
    m_offsets.resize(cells);
    for (std::size_t index = 0; index < cells; ++index) {
        m_offsets[index] = read_at<std::uint32_t>(bytes, kHeaderSize + index * 4);
        if (m_offsets[index] > count || (index > 0 && m_offsets[index] < m_offsets[index - 1])) {
            m_offsets.clear();
            return false;
        }
    }
    if (m_offsets.back() != count) {
        m_offsets.clear();
        return false;
    }
    m_nodes.resize(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        const std::size_t at = nodesAt + static_cast<std::size_t>(index) * kNodeSize;
        NodeRecord& node = m_nodes[index];
        node.record = read_at<std::uint32_t>(bytes, at);
        node.type = read_at<std::uint16_t>(bytes, at + 4);
        node.x = read_at<float>(bytes, at + 8);
        node.y = read_at<float>(bytes, at + 12);
        node.z = read_at<float>(bytes, at + 16);
    }
    return true;
}

std::span<const NodeRecord> NodeFile::cell(std::int32_t cx, std::int32_t cy) const {
    if (m_offsets.empty() || cx < 0 || cy < 0 || cx >= static_cast<std::int32_t>(m_side) || cy >= static_cast<std::int32_t>(m_side)) {
        return {};
    }
    const std::size_t index = static_cast<std::size_t>(cy) * m_side + static_cast<std::size_t>(cx);
    return std::span<const NodeRecord>(m_nodes).subspan(m_offsets[index], m_offsets[index + 1] - m_offsets[index]);
}

} // namespace swc::objects
