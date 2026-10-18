#include "Creatures/SwcEffects.h"

#include <algorithm>
#include <cmath>

namespace swc::creatures {
namespace {

std::uint64_t add_seconds(std::uint64_t timeUs, double seconds) {
    const double value = static_cast<double>(timeUs) + seconds * 1.0e6;
    return value <= 0.0 ? 0 : static_cast<std::uint64_t>(std::llround(value));
}

bool near(float a, float b) {
    return std::fabs(a - b) < 0.001F;
}

} // namespace

swc::net::WorldPosition projectile_place(const Projectile& shot, const ProjectileFlight& flight, std::uint64_t timeUs, bool* outHeld) {
    if (outHeld != nullptr) {
        *outHeld = false;
    }
    if (shot.collided && (timeUs >= shot.contactUs || !shot.hasLaunch)) {
        if (outHeld != nullptr) {
            *outHeld = true;
        }
        return shot.contact;
    }
    if (!shot.hasLaunch || timeUs <= shot.launchUs) {
        return shot.anchor;
    }
    const float t = static_cast<float>(static_cast<double>(timeUs - shot.launchUs) / 1.0e6);
    return {shot.anchor.x + shot.velocity[0] * t + 0.5F * flight.accel[0] * t * t,
            shot.anchor.y + shot.velocity[1] * t + 0.5F * flight.accel[1] * t * t,
            shot.anchor.z + shot.velocity[2] * t + 0.5F * flight.accel[2] * t * t};
}

std::array<float, 3> projectile_velocity(const Projectile& shot, const ProjectileFlight& flight, std::uint64_t timeUs) {
    bool held = false;
    (void)projectile_place(shot, flight, timeUs, &held);
    if (held || !shot.hasLaunch) {
        return {};
    }
    const float t = timeUs > shot.launchUs ? static_cast<float>(static_cast<double>(timeUs - shot.launchUs) / 1.0e6) : 0.0F;
    return {shot.velocity[0] + flight.accel[0] * t, shot.velocity[1] + flight.accel[1] * t, shot.velocity[2] + flight.accel[2] * t};
}

std::uint64_t spell_start_us(const SpellParts& parts, const std::vector<SpellEventData>& events) {
    if (parts.hasSpell && parts.spawnUs != 0) {
        return parts.spawnUs;
    }
    // A state that is not completed gives its due time: the start is that time less the delay of its event.
    for (const SpellEventData& event : events) {
        const auto state = parts.events.find(event.slot);
        if (state != parts.events.end() && !state->second.completed) {
            return add_seconds(state->second.appliedUs, static_cast<double>(state->second.remaining) - event.delay);
        }
    }
    return parts.firstSeenUs;
}

std::uint64_t event_due_us(const SpellParts& parts, const SpellEventData& event, std::uint64_t startUs) {
    const std::uint64_t planned = add_seconds(startUs, event.delay);
    const auto state = parts.events.find(event.slot);
    if (state == parts.events.end()) {
        return planned;
    }
    if (state->second.completed) {
        return std::min(state->second.appliedUs, planned);
    }
    const std::uint64_t byState = add_seconds(state->second.appliedUs, state->second.remaining);
    // A held activation: for an activation that passed, the server sends the time to the end of the spell and 1 s (the client must
    // not play the bolt again; backend spell_state.cpp, kHoldAfterEnd). With the spawn time of the Spell state the event is in the
    // past.
    if (parts.hasSpell && parts.spawnUs != 0 && byState > add_seconds(planned, kHeldEventSlack)) {
        return planned;
    }
    return byState;
}

AreaTimes area_times(const SpellParts& parts, const SpellVolumeData& volume, const std::vector<SpellEventData>& events) {
    AreaTimes times;
    times.startUs = spell_start_us(parts, events);
    times.dangerUs = volume.activeAfter < 0.0F ? 0 : add_seconds(times.startUs, volume.activeAfter);
    times.endUs = volume.inactiveAfter < 0.0F ? 0 : add_seconds(times.startUs, volume.inactiveAfter);
    // The events of the slice with the times of the volume: their states move the times (a session that sees the spell late).
    for (const SpellEventData& event : events) {
        if (event.type == 2 && volume.activeAfter >= 0.0F && near(event.delay, volume.activeAfter)) {
            times.dangerUs = event_due_us(parts, event, times.startUs);
        } else if (event.type == 3 && volume.inactiveAfter >= 0.0F && near(event.delay, volume.inactiveAfter)) {
            times.endUs = event_due_us(parts, event, times.startUs);
        }
    }
    if (times.endUs != 0 && times.dangerUs != 0) {
        times.endUs = std::max(times.endUs, add_seconds(times.dangerUs, kShortestDanger));
    }
    return times;
}

AreaPhase area_phase(const AreaTimes& times, std::uint64_t timeUs) {
    if (times.endUs != 0 && timeUs >= times.endUs) {
        return AreaPhase::ended;
    }
    if (times.dangerUs != 0 && timeUs >= times.dangerUs) {
        return AreaPhase::danger;
    }
    // A volume that is never active (activeAfter -1): a warning until the stop.
    return AreaPhase::warning;
}

} // namespace swc::creatures
