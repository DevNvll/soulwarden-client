#include "Interact/SwcInteraction.h"

#include <algorithm>

namespace swc::interact {

std::uint64_t instance_of(std::uint64_t root, std::uint64_t group, std::uint64_t sliceEntity) {
    return sliceEntity ^ (root ^ group);
}

std::uint64_t facet_target(std::uint64_t root, std::uint64_t group, std::uint64_t sliceEntity, std::uint64_t component) {
    return instance_of(root, group, sliceEntity) + component;
}

swc::net::InteractionRequest request_of(const Template& object, const Bound& bound, const Option& option, const swc::net::ActorId& actor) {
    swc::net::InteractionRequest request;
    request.reference.actor = actor;
    request.reference.group = swc::net::group_of(actor);
    request.reference.target = facet_target(object.root, request.reference.group, bound.entity, bound.component);
    request.action = option.action;
    // The base action of a committed option is its own action (protocol notes 4.35: ReadLore 47/47, OpenConversation 63/63).
    request.baseAction = option.committed ? option.action : 0;
    request.optionEntity = instance_of(object.root, request.reference.group, option.entity);
    request.committed = option.committed;
    // The live client sends the Delay Time as the hold time (protocol notes 4.36: 5.000 for the keg).
    request.holdTime = option.delay;
    request.optionId = option.id;
    return request;
}

std::optional<Candidate> focus(const std::vector<Template>& templates, const std::vector<Object>& objects, const swc::net::WorldPosition& character) {
    std::optional<Candidate> best;
    for (const Object& object : objects) {
        const auto found = std::find_if(templates.begin(), templates.end(),
            [&object](const Template& item) { return item.asset == object.sliceAsset && item.sub == object.sliceSub; });
        if (found == templates.end()) {
            continue;
        }
        for (const Bound& bound : found->bounds) {
            if (bound.options.empty()) {
                continue;
            }
            const swc::net::WorldPosition place{object.place.x + bound.position[0], object.place.y + bound.position[1], object.place.z + bound.position[2]};
            const float dx = place.x - character.x;
            const float dy = place.y - character.y;
            const float dz = place.z - character.z;
            const float distanceSq = dx * dx + dy * dy + dz * dz;
            if (distanceSq > bound.rangeSq || (best && distanceSq >= best->distanceSq)) {
                continue;
            }
            Candidate candidate;
            candidate.interest = object.interest;
            candidate.object = &*found;
            candidate.option = &bound.options.front();
            candidate.request = request_of(*found, bound, bound.options.front(), object.actor);
            candidate.place = place;
            candidate.distanceSq = distanceSq;
            best = candidate;
        }
    }
    return best;
}

void Use::press(double now, const Candidate& candidate, bool keepUntilEnd) {
    if (m_phase != Phase::idle || candidate.option == nullptr) {
        return;
    }
    m_keepUntilEnd = keepUntilEnd;
    m_option = *candidate.option;
    m_interest = candidate.interest;
    m_request = candidate.request;
    m_start = now;
    m_down = true;
    m_accepted = false;
    if (m_option.hold > 0.0F) {
        m_phase = Phase::holding;
    } else {
        request(now);
    }
}

void Use::request(double now) {
    m_phase = Phase::requested;
    m_start = now;
    ++m_requests;
    m_effects.requestChanged = true;
    m_effects.request = m_request;
    m_effects.cancel = false;
    if (m_option.delay <= 0.0F) {
        // No Delay Time: the client executes the option at the request (protocol notes 4.35: before any reply).
        m_phase = Phase::executed;
        m_executedAt = now;
        ++m_executions;
    }
}

void Use::clear_when_due(double now) {
    if (m_phase == Phase::executed && m_accepted && !m_keepUntilEnd && now >= m_executedAt + kClearDelay
        && now >= m_acceptedAt + m_option.delay + kClearDelay) {
        end(false);
    }
}

void Use::tick(double now) {
    if (m_phase == Phase::holding && m_down && now - m_start >= m_option.hold) {
        request(now);
    }
    if (m_phase == Phase::requested && now - m_start >= m_option.delay) {
        // The end of the Delay Time: the object execution (0x4910750, 4.99 s to 5.00 s after the press).
        m_phase = Phase::executed;
        m_executedAt = now;
        ++m_executions;
    }
    // With the reply 1 the client clears its request after the execution (no 2473).
    clear_when_due(now);
}

void Use::release(double now) {
    m_down = false;
    if (m_phase == Phase::holding) {
        // A release before the hold time: no request.
        m_phase = Phase::idle;
        return;
    }
    tick(now);
    if (m_phase == Phase::requested || (m_phase == Phase::executed && !m_accepted && !m_keepUntilEnd)) {
        // A release before the end of the Delay Time, or after the execution with no reply: the cancel 2473.
        end(true);
    }
    // After the execution with the reply 1 the request ends at its time (clear_when_due), also when the key is up.
}

void Use::reply(std::uint8_t result, double now) {
    if (m_phase != Phase::requested && m_phase != Phase::executed) {
        return;
    }
    if (result == 1) {
        m_accepted = true;
        m_acceptedAt = now;
        clear_when_due(now);
        return;
    }
    // A refusal: the records of the client clear (the session client cleared the block already).
    m_phase = Phase::idle;
}

void Use::server_cancel() {
    if (m_phase == Phase::idle) {
        return;
    }
    // The session client answered with 2473 and cleared the block (0x490EDA0, 0x4916150).
    ++m_serverEnds;
    m_phase = Phase::idle;
}

void Use::finish() {
    if (m_phase == Phase::holding) {
        m_phase = Phase::idle;
    } else if (m_phase != Phase::idle) {
        end(true);
    }
}

void Use::abort() {
    if (m_phase == Phase::holding) {
        m_phase = Phase::idle;
    } else if (m_phase != Phase::idle) {
        end(true);
    }
}

void Use::end(bool cancel) {
    m_phase = Phase::idle;
    if (cancel) {
        ++m_cancels;
        m_effects.cancel = true;
        m_effects.requestChanged = false;
        m_effects.request.reset();
    } else {
        m_effects.requestChanged = true;
        m_effects.request.reset();
    }
}

float Use::progress(double now) const {
    const float time = static_cast<float>(now - m_start);
    if (m_phase == Phase::holding && m_option.hold > 0.0F) {
        return std::clamp(time / m_option.hold, 0.0F, 1.0F);
    }
    if (m_phase == Phase::requested && m_option.delay > 0.0F) {
        return std::clamp(time / m_option.delay, 0.0F, 1.0F);
    }
    return m_phase == Phase::executed ? 1.0F : 0.0F;
}

Use::Effects Use::take() {
    Effects out = m_effects;
    m_effects = {};
    return out;
}

} // namespace swc::interact
