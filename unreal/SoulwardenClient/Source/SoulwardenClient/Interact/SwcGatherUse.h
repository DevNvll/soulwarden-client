#pragma once

#include <cstdint>

#include "Net/Codec/SwcRepMessages.h"

// The end of a gather or of the open of a chest by the player (stage P4.1 part 2; backend docs/systems/chests.md, gathering.md,
// server/rep/states/chest_states.cpp; protocol notes 4.41, 4.44). Plain C++ with no engine header: tools/test-creatures.sh tests it.

namespace swc::interact {

/**
 * GatherOption (action 14) is a timed use of the server: its time is the work of the Gathering state of the owner (remaining / speed
 * seconds from the Success). The client keeps its request (the block in each input entry) while the work runs: a release before the
 * time ends the work with no item (chests.md, finding ST-R1). At the completion the server sends Done, and the client ends its use (it
 * sends 2473: live check L3 of the backend stage 3.5b.1). Failure (a cancel of the server: a walk out of the range, damage, a full bag)
 * ends it too. Limits: no Success kNoStart seconds after the press, or no end kSlack seconds after the time of the work, end the use.
 */
class GatherWatch {
public:
    enum class Step { waiting, running, done, failed, late };

    static constexpr double kNoStart = 5.0;
    static constexpr double kSlack = 2.0;

    /** The press of the key on a gathering node or a chest: the Gathering state now (only a later reference counts). */
    void start(double now, const swc::net::GatheringState& state);
    /** @return The step after a Gathering state or a tick. done, failed, and late stay. */
    Step update(double now, const swc::net::GatheringState& state);

    [[nodiscard]] Step step() const { return m_step; }
    /** @return The time of the end of the work (the clock of update), after the Success. */
    [[nodiscard]] double end_time() const { return m_end; }
    /** @return The part of the work that passed (0 to 1; the progress ring of the prompt). */
    [[nodiscard]] float progress(double now) const;

private:
    Step m_step{Step::waiting};
    std::uint32_t m_reference{0};
    double m_start{0.0};
    double m_begin{0.0};
    double m_end{0.0};
};

} // namespace swc::interact
