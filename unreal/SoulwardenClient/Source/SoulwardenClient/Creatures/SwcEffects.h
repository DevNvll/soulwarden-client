#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "Creatures/SwcEntityTable.h"

// The rules of the shots and the danger areas of the creatures (stage P2.9; docs/CREATURES.md). Plain C++ with no engine header:
// tools/test-creatures.sh tests them with the states that the encoders of the backend make.
//
// A shot (protocol notes 4.37): the client flies the projectile by itself from the anchor of its launch state with its velocity
// and the acceleration of its slice (m_accel; not the gravity of the profile); a collision state holds it at the contact; the
// stop of its record removes it.
//
// A danger area (protocol notes 4.25, 4.39): the first detection volume of the spell slice is the area that hurts. Before the
// volume is active the area is a warning (the tell, the omen); while it is active it is the danger; after the volume ends (or at
// the stop of the record) it is gone. The times come from the delayed events of the slice: the time from the start of the
// spell, or the state of the event (its time that is left; a completed event is in the past).

namespace swc::creatures {

/** The flight values of a projectile slice (effects.json; game data m_accel, m_flyingLifetime, m_stuckLifetime). */
struct ProjectileFlight {
    std::array<float, 3> accel{};
    float flyingLifetime{0.0F};
    float stuckLifetime{0.0F};
};

/**
 * @return The place of a projectile at a server time (us): the anchor + velocity t + accel t^2 / 2 from the launch time (no
 *         place before it: the anchor), and the contact place from the contact time. `outHeld`: true at the contact.
 */
swc::net::WorldPosition projectile_place(const Projectile& shot, const ProjectileFlight& flight, std::uint64_t timeUs,
                                         bool* outHeld = nullptr);
/** @return The velocity of a projectile at a server time (m/s; 0 at the contact). */
std::array<float, 3> projectile_velocity(const Projectile& shot, const ProjectileFlight& flight, std::uint64_t timeUs);

/** One delayed event of a spell slice (effects.json): its slot, its time from the start (s), its type (0 trigger, 2 activate,
 *  3 deactivate), and its target entity. */
struct SpellEventData {
    std::uint32_t slot{0};
    float delay{0.0F};
    std::uint32_t type{0};
    std::string target;
};

/** The first detection volume of a spell slice (effects.json): its entity, its shape ("sphere", "cylinder", "none": the shape
 *  of the spell row), its sizes (m), and its active time from the start (s; -1: never). */
struct SpellVolumeData {
    std::string name;
    std::string shape;
    float radius{0.0F};
    float shapeRadius{0.0F};
    float height{0.0F};
    float offset{0.0F};
    float activeAfter{0.0F};
    float inactiveAfter{-1.0F};
};

enum class AreaPhase { warning, danger, ended };

/** The times of a danger area on the server clock (us): its start, the start of the danger, and its end (0: no end before the
 *  stop of the record). */
struct AreaTimes {
    std::uint64_t startUs{0};
    std::uint64_t dangerUs{0};
    std::uint64_t endUs{0};
};

/** The shortest danger time that the client shows (s): a strike of 0.05 s (the shaman: 0.9 s to 0.95 s) shows as a flash of this
 *  time. A value of this client. */
inline constexpr float kShortestDanger = 0.3F;
/** A state of a delayed event whose due time is later than its planned time by more than this (s) is a held activation of the
 *  server (an event in the past). */
inline constexpr float kHeldEventSlack = 0.5F;

/**
 * @return The start of a spell entity on the server clock (us): the spawn time of its Spell state; else the due time of the state of
 *         one of its delayed events less the delay of that event (a status area has no Spell state); else its first state.
 */
std::uint64_t spell_start_us(const SpellParts& parts, const std::vector<SpellEventData>& events);
/** @return The due time of a delayed event on the server clock (us): from its state (the time that is left when the client
 *  applied it; a completed event: that time; a held activation: the start + its delay), else the start + its delay. */
std::uint64_t event_due_us(const SpellParts& parts, const SpellEventData& event, std::uint64_t startUs);
/** @return The times of the danger area of a spell entity: the activation and the deactivation of the entity of its volume
 *  (the events of the slice with the times of the volume), else the times of the volume. */
AreaTimes area_times(const SpellParts& parts, const SpellVolumeData& volume, const std::vector<SpellEventData>& events);
/** @return The phase of a danger area at a server time. */
AreaPhase area_phase(const AreaTimes& times, std::uint64_t timeUs);

} // namespace swc::creatures
