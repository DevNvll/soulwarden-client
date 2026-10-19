#pragma once

#include <cstdint>

// The Talk of the client (stage P4.8, docs/PERSONS.md "The Talk"; research ~/Games/soulwarden-client/research/p48-talk/; the Soulwarden
// docs/systems/travel-and-conversations.md and protocol notes 4.20). Plain C++ with no engine header: tools/test-net.sh tests it.
//
//   open       the request of OpenConversation goes out (action 63, committed): the client opens the conversation itself in the same
//              frame (the camera of the line, the lock of the input of the character);
//   the ends   the player (E at the last line, Escape): 6705, then 2473 (2 ms later in the retail client);
//              a refusal (3743 result 2): the conversation closes at once: 6705, no 2473;
//              the server (3746; the session client sent 2473 at once): 6705;
//   the lock   the input of the character stays blocked after the 6705 until the answer 6709 (the camera is free again); with no
//              answer the lock never ends (user test 1 of the Soulwarden project).
//   the camera the preset of the line (NPC_CameraPresets: origin and look-at point in the frame of the person) with its FOV, a cut.

namespace swc::interact {

struct ScenePoint {
    double x{0.0};
    double y{0.0};
    double z{0.0};
};

/**
 * A point of a camera preset in the world (engine m): the frame of the person is its side axis (cos yaw, sin yaw, 0), its forward
 * axis (-sin yaw, cos yaw, 0), and up; `local` is (side, forward, up). Check: the camera of the commander of Oxboro is 0.00 m from
 * the live camera of the retail client in the horizontal plane (research p48-talk/camera-check.txt).
 */
ScenePoint scene_point(const ScenePoint& place, double yawDegrees, const ScenePoint& local);

class Conversation {
public:
    enum class Phase { closed, open, closing };

    /** The effects for the session: send 6705 (the end of the cutscene), end the use of the interaction (2473), in this order. */
    struct Effects {
        bool endCutscene{false};
        bool cancelInteraction{false};
    };

    /** The request of OpenConversation went out. */
    void open();
    /** The player ends the conversation: 6705, then 2473. */
    void player_close();
    /** The reply 3743 result 2: 6705, no 2473. */
    void refused();
    /** The server ends the conversation (3746): 6705 (the session client sent 2473). */
    void server_ended();
    /** The count of the answers 6709 of the session (each frame): an answer after the 6705 ends the lock. */
    void update(std::uint32_t lockEnds);

    [[nodiscard]] Phase phase() const { return m_phase; }
    /** The input of the character is blocked (from the open to the answer 6709). */
    [[nodiscard]] bool locked() const { return m_phase != Phase::closed; }
    /** The scene of the line shows (its camera, the hidden player): from the open to the end. */
    [[nodiscard]] bool scene() const { return m_phase == Phase::open; }
    Effects take();

private:
    void close(bool cancel);

    Phase m_phase{Phase::closed};
    std::uint32_t m_lockEnds{0};
    std::uint32_t m_lockEndsAtClose{0};
    Effects m_effects;
};

} // namespace swc::interact
