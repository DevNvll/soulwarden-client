#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <vector>

#include "Net/Codec/SwcRepMessages.h"

// The table of the other entities of a session (the creatures; protocol notes 4.2, 4.14, 4.17): one entry for each interest
// number that is not 1. A record makes the entry or changes it; a stop of the control block removes it. Plain C++ with no
// engine header: tools/test-creatures.sh runs it with states that the encoders of the backend make.
//
// What the table keeps for an entity, from its states (the backend encoders apps/backend/src/server/rep/states):
//   metadata (slot 0, type 10)     the asset id of the slice and its sub id, the actor id
//   movement (type 11, the slot of the ActionList component of the slice)
//                                  worldPosAbs (27), rotation (11), timeAnchor (40, server microseconds), dataBits (41),
//                                  and the scope fields: scopeData (32), scopeInfoBlob (33), scopeTimeBlobEx (29),
//                                  globalFragTags (34), the time of scope 0 (28, 38, 49, 50: ticks of 1/30 s)
//   vitals (type 15)               HealthAmount (0), vitalsData (2, bit 0: dead), healthChangeFlags (3), HealthMax (4),
//                                  vitalsId (8), vitalsCategoryId (9), vitalsLevel (10), invulnerability (11)
//   marker (type 5399)             enabled (0)
// The rules of the retail client for the scope fields (protocol notes 4.14): the client reads them only when the state has
// scopeTimeBlobEx; a state with no scope fields keeps the last record; a mask with no bit of a scope ends the action of that
// scope (the client then plays its own idle); a new start count starts the fragment again.

namespace swc::creatures {

using Clock = swc::net::Clock;

/** One scope record of an action of the server (the reader of the client 0x2A75F70). */
struct ScopeRecord {
    std::uint16_t startCount{0};
    /** The fragment index and the action index (the wire has each one plus 1); kNone: no value. */
    std::uint32_t fragment{0};
    std::uint32_t action{0};
    /** The playback scale (a half float on the wire; 1.0 from the backend) and the option (0). */
    float speed{1.0F};
    std::uint8_t option{0};
    /** The time of the action at the time anchor of its state, in seconds (scope 0: ticks / 30; a scope of 2 or more: the
     *  half float of scopeTimeBlobEx). */
    float time{0.0F};
};

/** One movement state of an entity: the time anchor of the server and the place. */
struct MovementSample {
    std::uint64_t timeUs{0};
    swc::net::WorldPosition position;
    swc::net::Quaternion rotation;
};

struct Entity {
    std::uint32_t interest{0};
    /** One more for each new entity of the table: a stop and a new record of the same interest number make a new serial. */
    std::uint64_t serial{0};
    bool hasMetadata{false};
    swc::net::ActorId actor{};
    swc::net::ActorId sliceAsset{};
    std::uint32_t sliceSubId{0};

    /** The movement states with a time anchor, oldest first (at most kSamples). */
    std::deque<MovementSample> samples;
    std::uint32_t movementSlot{0};
    std::uint8_t dataBits{0};
    std::uint8_t teleport{0};

    /** The scope fields: true after the first state with them. */
    bool hasScopes{false};
    /** The mask of the scopes with a record (bit 0: FullBody). */
    std::uint32_t scopeMask{0};
    /** The records of the scopes of the mask (scope index -> record). */
    std::map<std::uint32_t, ScopeRecord> scopes;
    /** The time anchor of the state of the scope fields (server microseconds). */
    std::uint64_t scopeTimeUs{0};
    std::array<std::uint8_t, 12> tags{};
    /** One more at each state with the scope fields. */
    std::uint32_t scopeRevision{0};

    bool hasVitals{false};
    float health{0.0F};
    float healthMax{0.0F};
    bool dead{false};
    bool immune{false};
    std::uint32_t vitalsId{0};
    std::uint32_t categoryId{0};
    std::uint32_t level{0};
    /** One more at each health change that the name plate shows (bit 0 of healthChangeFlags; protocol notes 4.14). */
    std::uint32_t healthRevision{0};

    bool hasMarker{false};
    bool markerEnabled{false};

    /** The counts of the states of this entity. */
    std::uint32_t movementStates{0};
    std::uint32_t vitalsStates{0};

    [[nodiscard]] bool hasPosition() const { return !samples.empty(); }
    [[nodiscard]] const ScopeRecord* scope(std::uint32_t index) const {
        const auto found = scopes.find(index);
        return found != scopes.end() && (scopeMask & (1U << index)) != 0 ? &found->second : nullptr;
    }
    /** @return The text "GUID:SUB" of the slice asset (the key of the creature data: upper-case GUID with dashes). */
    [[nodiscard]] std::string slice_key() const;
};

/** A change of the table for the log and the view. */
struct Change {
    enum class Kind { added, removed, action, health, death };
    Kind kind{Kind::added};
    std::uint32_t interest{0};
    std::uint64_t serial{0};
};

class EntityTable {
public:
    /** The fragment and action values of the wire are the index plus 1: 0 is no value. */
    static constexpr std::uint32_t kNone = 0xFFFFFFFFU;
    /** The number of movement states that an entity keeps (the interpolation needs the last two or three). */
    static constexpr std::size_t kSamples = 16;
    /** Ticks of the time of scope 0 (movement_state.cpp kActionTicksPerSecond). */
    static constexpr float kTicksPerSecond = 30.0F;

    /** Reads one record of an interest number that is not 1. */
    void apply(const soulwarden::backend::tests::Record& record);
    /** Removes the entity of a stop of the control block. */
    void stop(std::uint32_t interest);
    void clear();

    [[nodiscard]] const std::map<std::uint32_t, Entity>& entities() const { return m_entities; }
    [[nodiscard]] const Entity* find(std::uint32_t interest) const;
    std::vector<Change> take_changes();

    /** Reads the scope fields of a movement state into an entity. @return False: the state has no scope fields. */
    static bool read_scopes(const soulwarden::backend::tests::Fragment& fragment, Entity& entity);

private:
    void apply_movement(const soulwarden::backend::tests::Fragment& fragment, Entity& entity);
    void apply_vitals(const soulwarden::backend::tests::Fragment& fragment, Entity& entity);

    std::map<std::uint32_t, Entity> m_entities;
    std::uint64_t m_serial{0};
    std::vector<Change> m_changes;
};

/** @return The float of a 16-bit IEEE half float (the playback scale and the times of scopeTimeBlobEx). */
float half_to_float(std::uint16_t half);

} // namespace swc::creatures
