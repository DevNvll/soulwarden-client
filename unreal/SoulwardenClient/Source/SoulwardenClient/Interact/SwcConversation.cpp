#include "Interact/SwcConversation.h"

#include <cmath>
#include <numbers>

namespace swc::interact {

ScenePoint scene_point(const ScenePoint& place, double yawDegrees, const ScenePoint& local)
{
    const double yaw = yawDegrees * std::numbers::pi / 180.0;
    const double c = std::cos(yaw);
    const double s = std::sin(yaw);
    return {place.x + local.x * c - local.y * s, place.y + local.x * s + local.y * c, place.z + local.z};
}

bool presence_visible(bool hasSwap, const std::vector<int>& rule, const std::vector<std::uint8_t>& achievements, bool showOnLocked)
{
    if (!hasSwap) {
        return true;
    }
    std::vector<bool> stack;
    bool valid = true;
    for (const int code : rule) {
        if (code == kPresenceNot) {
            if (stack.empty()) {
                valid = false;
                break;
            }
            stack.back() = !stack.back();
        } else if (code == kPresenceAnd || code == kPresenceOr) {
            if (stack.size() < 2) {
                valid = false;
                break;
            }
            const bool b = stack.back();
            stack.pop_back();
            stack.back() = code == kPresenceAnd ? (stack.back() && b) : (stack.back() || b);
        } else {
            const std::size_t index = code >= 0 ? static_cast<std::size_t>(code) : 0;
            stack.push_back(code >= 0 && index / 8 < achievements.size() && ((achievements[index / 8] >> (index % 8)) & 1) != 0);
        }
    }
    const bool value = valid && stack.size() == 1 && stack.back();
    return value != showOnLocked;
}

void Conversation::open()
{
    m_phase = Phase::open;
    m_effects = {};
}

void Conversation::close(bool cancel)
{
    if (m_phase != Phase::open) {
        return;
    }
    m_phase = Phase::closing;
    m_lockEndsAtClose = m_lockEnds;
    m_effects.endCutscene = true;
    m_effects.cancelInteraction = cancel;
}

void Conversation::player_close()
{
    close(true);
}

void Conversation::refused()
{
    close(false);
}

void Conversation::server_ended()
{
    close(false);
}

void Conversation::update(std::uint32_t lockEnds)
{
    if (m_phase == Phase::closing && lockEnds != m_lockEndsAtClose) {
        m_phase = Phase::closed;
    }
    m_lockEnds = lockEnds;
}

Conversation::Effects Conversation::take()
{
    const Effects out = m_effects;
    m_effects = {};
    return out;
}

} // namespace swc::interact
