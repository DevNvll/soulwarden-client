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
