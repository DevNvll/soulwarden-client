#pragma once

// The life of the player on the client (stage P2.10, docs/ACTIONS.md "The death and the respawn"): the actions Death and Respawn
// of playeractions_death.actionlist and the action DelayedInteraction of playeractions_interact.actionlist as one state, with the
// lock of the input. Plain C++ (no engine type): tools/test-actions.sh tests it.

namespace swc::combat {

class Life {
public:
    enum class State {
        /** The character lives; the input is free unless a timed use plays or a loading screen shows. */
        alive,
        /** The dead mark of the vitals state: the death fragment plays and holds its last pose. */
        dead,
        /** The character lives again, and a loading screen shows (the respawn, the cells of the place): the death pose holds. */
        waiting,
        /** The fragment Respawn plays: the input is free at its mark CanTransition. */
        respawning
    };
    /** A change for the view: a fragment starts (death, respawn), the pose of the death holds, the body is free again. */
    enum class Event { none, death, waiting, respawn, free };

    /** The dead mark of the vitals state and the loading screen of this frame. */
    Event set(bool dead, bool loading);
    /** A timed use starts or ends. @return False when nothing changes (a use needs a character that lives). */
    bool set_timed_use(bool on);
    /** The time of the frame (s); the respawn is free at `canTransition` of its fragment. The clock does not move while waiting. */
    Event advance(float seconds, float canTransition);

    [[nodiscard]] State state() const { return m_state; }
    [[nodiscard]] bool loading() const { return m_loading; }
    [[nodiscard]] bool timed_use() const { return m_timedUse; }
    /** @return True while the input of the player does nothing. */
    [[nodiscard]] bool locked() const { return m_state != State::alive || m_loading || m_timedUse; }
    /** @return True while a fragment of the life (or of the timed use) owns the full body. */
    [[nodiscard]] bool owns_body() const { return m_state != State::alive || m_timedUse; }
    /** The clock of the fragment that plays (s). */
    [[nodiscard]] float clock() const { return m_clock; }

private:
    State m_state{State::alive};
    bool m_loading{false};
    bool m_timedUse{false};
    float m_clock{0.0F};
};

} // namespace swc::combat
