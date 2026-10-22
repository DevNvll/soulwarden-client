#include "Interact/SwcInteraction.h"

#include <algorithm>
#include <cmath>

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

float distance_sq(const swc::net::WorldPosition& character, const swc::net::WorldPosition& place) {
    // The expression of the backend (world/interactions.cpp distance_sq): the same float operations in the same order.
    const float dx = character.x - place.x;
    const float dy = character.y - place.y;
    const float dz = character.z - place.z;
    return dx * dx + dy * dy + dz * dz;
}

bool in_range(const swc::net::WorldPosition& character, const swc::net::WorldPosition& place, float rangeSq) {
    return !(distance_sq(character, place) > rangeSq);
}

swc::net::WorldPosition transform_point(const swc::net::WorldPosition& place, const swc::net::Quaternion& rotation, const float local[3]) {
    // v' = v + 2 w (u x v) + 2 u x (u x v), with u = (x, y, z): the expression of the backend (gamedata::transform_point).
    const float ux = rotation.x;
    const float uy = rotation.y;
    const float uz = rotation.z;
    const float w = rotation.w;
    const float cx = uy * local[2] - uz * local[1];
    const float cy = uz * local[0] - ux * local[2];
    const float cz = ux * local[1] - uy * local[0];
    const float dx = uy * cz - uz * cy;
    const float dy = uz * cx - ux * cz;
    const float dz = ux * cy - uy * cx;
    return swc::net::WorldPosition{place.x + local[0] + 2.0F * (w * cx + dx), place.y + local[1] + 2.0F * (w * cy + dy),
                                   place.z + local[2] + 2.0F * (w * cz + dz)};
}

bool contains(const Shape& shape, const swc::net::WorldPosition& center, const swc::net::Quaternion& rotation, const swc::net::WorldPosition& point,
    float rangeSq) {
    if (shape.type == Shape::Type::none) {
        return in_range(point, center, rangeSq);
    }
    // Into the frame of the object: the inverse rotation (the conjugate) of the offset.
    const float offset[3]{point.x - center.x, point.y - center.y, point.z - center.z};
    const swc::net::Quaternion inverse{-rotation.x, -rotation.y, -rotation.z, rotation.w};
    const swc::net::WorldPosition turned = transform_point(swc::net::WorldPosition{0.0F, 0.0F, 0.0F}, inverse, offset);
    // Into the frame of the bound: the inverse of its basis (rows: world = basis * local).
    const float* m = shape.basis;
    const float det = m[0] * (m[4] * m[8] - m[5] * m[7]) - m[1] * (m[3] * m[8] - m[5] * m[6]) + m[2] * (m[3] * m[7] - m[4] * m[6]);
    if (std::abs(det) < 1e-9F) {
        return false;
    }
    const float inv[9]{(m[4] * m[8] - m[5] * m[7]) / det, (m[2] * m[7] - m[1] * m[8]) / det, (m[1] * m[5] - m[2] * m[4]) / det,
        (m[5] * m[6] - m[3] * m[8]) / det, (m[0] * m[8] - m[2] * m[6]) / det, (m[2] * m[3] - m[0] * m[5]) / det,
        (m[3] * m[7] - m[4] * m[6]) / det, (m[1] * m[6] - m[0] * m[7]) / det, (m[0] * m[4] - m[1] * m[3]) / det};
    const float v[3]{turned.x, turned.y, turned.z};
    const float x = inv[0] * v[0] + inv[1] * v[1] + inv[2] * v[2];
    const float y = inv[3] * v[0] + inv[4] * v[1] + inv[5] * v[2];
    const float z = inv[6] * v[0] + inv[7] * v[1] + inv[8] * v[2];
    switch (shape.type) {
    case Shape::Type::sphere:
        return x * x + y * y + z * z < shape.radius * shape.radius;
    case Shape::Type::box:
        return std::abs(x) <= shape.dimensions[0] * 0.5F && std::abs(y) <= shape.dimensions[1] * 0.5F && std::abs(z) <= shape.dimensions[2] * 0.5F;
    case Shape::Type::capsule: {
        // The height is the full length; the segment of the centres is the height less the two caps.
        const float half = std::max(shape.height * 0.5F - shape.radius, 0.0F);
        const float dz = std::max(std::abs(z) - half, 0.0F);
        return x * x + y * y + dz * dz <= shape.radius * shape.radius;
    }
    case Shape::Type::cylinder:
        return std::abs(z) <= shape.height * 0.5F && x * x + y * y <= shape.radius * shape.radius;
    default:
        return false;
    }
}

bool trigger_overlaps(const Shape& shape, const swc::net::WorldPosition& center, const swc::net::Quaternion& rotation, const swc::net::WorldPosition& point,
    float rangeSq, float radius) {
    if (shape.type == Shape::Type::none) {
        return in_range(point, center, rangeSq);
    }
    const float offset[3]{point.x - center.x, point.y - center.y, point.z - center.z};
    const swc::net::Quaternion inverse{-rotation.x, -rotation.y, -rotation.z, rotation.w};
    const swc::net::WorldPosition turned = transform_point(swc::net::WorldPosition{0.0F, 0.0F, 0.0F}, inverse, offset);
    const float* m = shape.basis;
    const float det = m[0] * (m[4] * m[8] - m[5] * m[7]) - m[1] * (m[3] * m[8] - m[5] * m[6]) + m[2] * (m[3] * m[7] - m[4] * m[6]);
    if (std::abs(det) < 1e-9F) {
        return false;
    }
    const float inv[9]{(m[4] * m[8] - m[5] * m[7]) / det, (m[2] * m[7] - m[1] * m[8]) / det, (m[1] * m[5] - m[2] * m[4]) / det,
        (m[5] * m[6] - m[3] * m[8]) / det, (m[0] * m[8] - m[2] * m[6]) / det, (m[2] * m[3] - m[0] * m[5]) / det,
        (m[3] * m[7] - m[4] * m[6]) / det, (m[1] * m[6] - m[0] * m[7]) / det, (m[0] * m[4] - m[1] * m[3]) / det};
    const float v[3]{turned.x, turned.y, turned.z};
    const float p[3]{inv[0] * v[0] + inv[1] * v[1] + inv[2] * v[2], inv[3] * v[0] + inv[4] * v[1] + inv[5] * v[2],
        inv[6] * v[0] + inv[7] * v[1] + inv[8] * v[2]};
    // The closest point of the shape in its local frame.
    float q[3]{p[0], p[1], p[2]};
    switch (shape.type) {
    case Shape::Type::sphere: {
        const float length = std::sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
        if (length > shape.radius && length > 0.0F) {
            for (float& each : q) {
                each *= shape.radius / length;
            }
        }
        break;
    }
    case Shape::Type::box:
        for (int axis = 0; axis < 3; ++axis) {
            q[axis] = std::clamp(p[axis], -shape.dimensions[axis] * 0.5F, shape.dimensions[axis] * 0.5F);
        }
        break;
    case Shape::Type::capsule: {
        const float half = std::max(shape.height * 0.5F - shape.radius, 0.0F);
        const float axisZ = std::clamp(p[2], -half, half);
        const float d[3]{p[0], p[1], p[2] - axisZ};
        const float length = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        const float scale = length > shape.radius && length > 0.0F ? shape.radius / length : 1.0F;
        q[0] = d[0] * scale;
        q[1] = d[1] * scale;
        q[2] = axisZ + d[2] * scale;
        break;
    }
    case Shape::Type::cylinder: {
        const float length = std::sqrt(p[0] * p[0] + p[1] * p[1]);
        const float scale = length > shape.radius && length > 0.0F ? shape.radius / length : 1.0F;
        q[0] = p[0] * scale;
        q[1] = p[1] * scale;
        q[2] = std::clamp(p[2], -shape.height * 0.5F, shape.height * 0.5F);
        break;
    }
    default:
        return false;
    }
    // Back to meters of the object frame (world = basis * local): the distance of the point to the closest point.
    const float d[3]{p[0] - q[0], p[1] - q[1], p[2] - q[2]};
    const float w[3]{m[0] * d[0] + m[1] * d[1] + m[2] * d[2], m[3] * d[0] + m[4] * d[1] + m[5] * d[2], m[6] * d[0] + m[7] * d[1] + m[8] * d[2]};
    return w[0] * w[0] + w[1] * w[1] + w[2] * w[2] <= radius * radius;
}

bool line_score(const float camera[3], const float forward[3], const float place[3], float& score) {
    const float d[3]{place[0] - camera[0], place[1] - camera[1], place[2] - camera[2]};
    const float t = d[0] * forward[0] + d[1] * forward[1] + d[2] * forward[2];
    if (t <= 0.0F) {
        return false;
    }
    const float e[3]{camera[0] + t * forward[0] - place[0], camera[1] + t * forward[1] - place[1], camera[2] + t * forward[2] - place[2]};
    score = e[0] * e[0] + e[1] * e[1] + e[2] * e[2];
    return true;
}

std::optional<Candidate> focus(const std::vector<Template>& templates, const std::vector<Object>& objects, const swc::net::WorldPosition& character,
    const float camera[3], const float forward[3]) {
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
            const swc::net::WorldPosition place = transform_point(object.place, object.rotation, bound.position);
            const float point[3]{place.x, place.y, place.z};
            float score = 0.0F;
            if (!contains(bound.shape, place, object.rotation, character, bound.rangeSq) || !line_score(camera, forward, point, score)
                || (best && score >= best->score)) {
                continue;
            }
            Candidate candidate;
            candidate.interest = object.interest;
            candidate.object = &*found;
            candidate.option = &bound.options.front();
            candidate.request = request_of(*found, bound, bound.options.front(), object.actor);
            candidate.place = place;
            candidate.distanceSq = distance_sq(character, place);
            candidate.rangeSq = bound.rangeSq;
            candidate.score = score;
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
    m_place = candidate.place;
    m_rangeSq = candidate.rangeSq;
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

bool Use::moved(const swc::net::WorldPosition& reported) {
    if (m_phase != Phase::holding || in_range(reported, m_place, m_rangeSq)) {
        return false;
    }
    // No request went out: no cancel.
    m_phase = Phase::idle;
    ++m_rangeEnds;
    return true;
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
