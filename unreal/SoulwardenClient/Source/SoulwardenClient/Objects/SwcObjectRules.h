#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "Net/Codec/SwcRepMessages.h"

// The rules of the objects of the level data (stage P4.1 part 2, docs/OBJECTS.md; Soulwarden docs/systems/interactions.md,
// gathering.md, chests.md): the ids of an object by the backend rule, and the file of the natural nodes of a region
// (converter/swc_convert/objects.py). Plain C++ with no engine header: tools/test-creatures.sh tests it.

namespace swc::objects {

/** The node state of a gathering node (backend server/rep/states/gatherable_state.cpp, MB::GatherableControllerReplicatedState
 *  field 0; client enum 0x3378D60): Depleted hides the node and its marker, Healthy shows it again. */
inline constexpr std::uint8_t kNodeHealthy = 4;
inline constexpr std::uint8_t kNodeDepleted = 8;

/** The size of a region of the world and of a cell of the object grid (m). */
inline constexpr double kRegionMeters = 2048.0;
inline constexpr double kCellMeters = 64.0;

/** @return The 16 bytes of a UUID text (8-4-4-4-12 hexadecimal digits), or no value. */
std::optional<std::array<std::uint8_t, 16>> parse_uuid(std::string_view text);

/**
 * @return The actor id of a placement of a layer from its UUID (the client rule; research P4.1, backend test case IT10): XOR the
 * two halves of the 16 bytes, reverse the 8 bytes, set the low 3 bits of the first byte to 3, then 8 zero bytes. The entity
 * group G is the first 8 bytes (little-endian; swc::net::group_of).
 */
swc::net::ActorId placement_actor(const std::array<std::uint8_t, 16>& uuid);

/** @return The actor id of an offline id G: G little-endian and 8 zero bytes. */
swc::net::ActorId actor_of_group(std::uint64_t group);

/**
 * @return The offline id G of a record of a distribution file (backend GatheringData::natural_group; client 0x69D5090):
 * ((counter & 0x7FFFF) << 13) | ((regionY & 0x1F) << 8) | ((regionX & 0x1F) << 3) | 1, with the counter 1 for record 0.
 */
std::uint64_t natural_group(std::int32_t regionX, std::int32_t regionY, std::uint32_t counter);

/** The ids of an object from the bound of its template: the facet target ((entity XOR root XOR G) + component) and the option
 *  entity (option XOR root XOR G); protocol notes 4.2, backend gathering.md. */
struct BoundIds {
    std::uint64_t target{0};
    std::uint64_t optionEntity{0};
};
BoundIds bound_ids(std::uint64_t root, std::uint64_t group, std::uint64_t boundEntity, std::uint64_t component, std::uint64_t option);

/**
 * The tool of a gather (stage P5.2; Soulwarden protocol notes 4.44, live check L6; client 0x48FF240 FindFirstGatheringTool): the
 * paperdoll slot of the gathering type of the row (Chopping 33 the logging axe, Cutting 34 the sickle, Dressing 35 the skinning
 * knife, Mining 36 the pick; another type has no slot), and the item of that slot counts when its gathering types have the type. A
 * row with the restriction ToolRequired and no tool: the prompt has the refusal ("No equipped Axe") and the client sends no request.
 * ItemRequired with no item (BushSmall) and NoToolNecessary have no gate in the client.
 */
struct ToolCheck {
    std::uint32_t slot{0};
    bool hasTool{false};
    bool missing{false};
};
std::uint32_t tool_slot(std::string_view gatheringType);
ToolCheck check_tool(std::string_view gatheringType, std::string_view restriction, bool slotItemHasType);

/**
 * @return The use fragment of a gather ("gather|ACTION|TOOLTAGS": the Gather fragment of the action list with the tag of the
 * GatheringAction of the row and the tags of the tool in the hand; converter gather_actions.py): the action of the row with the tool;
 * with no tool the default fragment ("gather|None|"; Handcrafting has its own); an action with no fragment of its own (LootContainer,
 * the chests) plays the default one.
 */
std::string gather_use_kind(std::string_view action, std::string_view toolTags, bool hasTool);

/** One natural node of the file of a region: the record index (counter = record + 1), the type (an index into the types of the
 *  region: template, row, visual), and the place (engine m). */
struct NodeRecord {
    std::uint32_t record{0};
    std::uint16_t type{0};
    float x{0.0F};
    float y{0.0F};
    float z{0.0F};
};

/** The natural nodes of a region in cells of 64 m (nodes/REGION.bin; the format is in converter/swc_convert/objects.py). */
class NodeFile {
public:
    /** @return False when the bytes do not have the form of the file (format 1). */
    bool read(std::span<const std::uint8_t> bytes);

    [[nodiscard]] std::int32_t regionX() const { return m_regionX; }
    [[nodiscard]] std::int32_t regionY() const { return m_regionY; }
    [[nodiscard]] std::size_t size() const { return m_nodes.size(); }
    /** @return The nodes of a cell of the region (cx, cy: 0 to side - 1 from the corner of the region), or none. */
    [[nodiscard]] std::span<const NodeRecord> cell(std::int32_t cx, std::int32_t cy) const;

private:
    std::int32_t m_regionX{0};
    std::int32_t m_regionY{0};
    std::uint32_t m_side{0};
    std::vector<std::uint32_t> m_offsets;
    std::vector<NodeRecord> m_nodes;
};

/** @return The cell of the object grid of a place (engine m): floor(x / 64), floor(y / 64). */
inline std::int32_t grid_cell(double meters) {
    const double cell = meters / kCellMeters;
    const auto truncated = static_cast<std::int32_t>(cell);
    return truncated > cell ? truncated - 1 : truncated;
}

} // namespace swc::objects
