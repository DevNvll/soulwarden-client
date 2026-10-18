#include "Combat/SwcLife.h"

namespace swc::combat {

Life::Event Life::set(bool dead, bool loading) {
    m_loading = loading;
    if (dead && (m_state == State::alive || m_state == State::respawning)) {
        // The action Death (IfDead): it ends a timed use (DisableInteractor) and plays r_Death.
        m_state = State::dead;
        m_timedUse = false;
        m_clock = 0.0F;
        return Event::death;
    }
    if (!dead && m_state == State::dead) {
        if (loading) {
            m_state = State::waiting;
            return Event::waiting;
        }
        m_state = State::respawning;
        m_clock = 0.0F;
        return Event::respawn;
    }
    if (!loading && m_state == State::waiting) {
        // The action Respawn (the tag Respawn of the death) when the loading screen ends: the character at its new place.
        m_state = State::respawning;
        m_clock = 0.0F;
        return Event::respawn;
    }
    return Event::none;
}

bool Life::set_timed_use(bool on) {
    if (on == m_timedUse || (on && m_state != State::alive)) {
        return false;
    }
    m_timedUse = on;
    m_clock = 0.0F;
    return true;
}

Life::Event Life::advance(float seconds, float canTransition) {
    if (m_state != State::waiting) {
        m_clock += seconds;
    }
    if (m_state == State::respawning && m_clock >= canTransition) {
        m_state = State::alive;
        return Event::free;
    }
    return Event::none;
}

} // namespace swc::combat
