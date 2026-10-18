#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "Net/Codec/SwcRepMessages.h"

// The interactions of the player with the objects of the server (stage P2.10, docs/INTERACTIONS.md; Soulwarden protocol notes
// 4.35, 4.36, 4.42; backend world/interactions.cpp): the ids of an object, the focus, and the use of one option (the hold of the
// prompt, the request, the timed use, the execution, the cancels). Plain C++ with no engine header: tools/test-net.sh tests it.

namespace swc::interact {

/** One option of an Interact bound (interaction-data.json of the backend copy). */
struct Option {
    std::string name;
    /** The CRC-32 of the lower-case option name (the option id of the block). */
    std::uint32_t id{0};
    std::uint8_t action{0};
    bool committed{true};
    /** The Delay Time of the option (s): a timed use (the powder keg: 5 s). */
    float delay{0.0F};
    /** The hold time of the prompt (s): the key is down this long before the request (the loot bag: 0.5 s). */
    float hold{0.0F};
    /** The option entity in the frame of the slice (XOR K gives the option entity of the block). */
    std::uint64_t entity{0};
};

/** One Interact bound of a template: its component, its entity in the slice, its place in the frame of the slice, its range. */
struct Bound {
    std::uint32_t slot{0};
    std::uint64_t component{0};
    std::uint64_t entity{0};
    float position[3]{0.0F, 0.0F, 0.0F};
    /** The squared 3D range (m^2; m_interactRangeSq; equal passes). */
    float rangeSq{0.0F};
    std::vector<Option> options;
};

/** The template of an object of the server: its slice, its root entity, and its bounds. */
struct Template {
    std::string name;
    swc::net::ActorId asset{};
    std::uint32_t sub{0};
    std::uint64_t root{0};
    std::vector<Bound> bounds;
};

/** @return The instance id of an entity of the slice of an object with the entity group G (protocol notes 4.2: K = root XOR G). */
std::uint64_t instance_of(std::uint64_t root, std::uint64_t group, std::uint64_t sliceEntity);
/** @return The facet target of a component of an entity of the slice: the instance id + the component id (a 64-bit sum). */
std::uint64_t facet_target(std::uint64_t root, std::uint64_t group, std::uint64_t sliceEntity, std::uint64_t component);

/** An object of the server: its interest number, its actor, its slice, and its place (PositionInTheWorld). */
struct Object {
    std::uint32_t interest{0};
    swc::net::ActorId actor{};
    swc::net::ActorId sliceAsset{};
    std::uint32_t sliceSub{0};
    swc::net::WorldPosition place;
};

/** A usable option of a bound of an object near the player. */
struct Candidate {
    std::uint32_t interest{0};
    const Template* object{nullptr};
    const Option* option{nullptr};
    /** The request of the block: the reference of the bound, the action, the option entity, the option id, the hold time. */
    swc::net::InteractionRequest request;
    /** The place of the bound (engine m) and its squared distance to the character. */
    swc::net::WorldPosition place;
    float distanceSq{0.0F};
};

/** @return The request of an option of a bound of an object (the ids of the object from its actor). */
swc::net::InteractionRequest request_of(const Template& object, const Bound& bound, const Option& option, const swc::net::ActorId& actor);

/**
 * @return The option of the nearest bound in its range (the 3D squared distance to the character, equal passes; the backend check
 * Interactions::check) of the first option of each bound, or no value. The place of a bound is the place of its object plus the
 * place of the bound in the frame of the slice (the rotation of the object is not used: the bounds of the client templates are
 * at the origin of their slice).
 */
std::optional<Candidate> focus(const std::vector<Template>& templates, const std::vector<Object>& objects, const swc::net::WorldPosition& character);

/**
 * The use of one option by the player (the client side of protocol notes 4.35 and 4.36):
 *   press      the key goes down at a focus: the hold of the prompt (hold > 0), else the request at once;
 *   the hold   the key stays down for the hold time: the request (the block goes into each input entry);
 *   the delay  a timed option (delay > 0): the progress for the Delay Time while the key is down; the execution at its end;
 *   release    before the request: nothing; before the execution: the cancel (2473); after the execution with no reply: the
 *              cancel (the client keeps its request until the release); after the execution with the reply 1: nothing;
 *   reply 1    after the execution the request ends with no message: the client clears its request kClearDelay after the
 *              execution (the retail client: the block goes away 5.03 s after the press, protocol notes 4.36), and not before
 *              the Delay Time after the arrival of the reply (the timer of the server starts at its accept; a block that goes
 *              away before that time is a release for the server: no completion);
 *   reply 2    the request ends (the records of the client clear; the session client cleared the block);
 *   3746       the server ends the use (the session client sent 2473 and cleared the block).
 * The effects for the session (the request, the cancel) go out through take().
 */
class Use {
public:
    enum class Phase { idle, holding, requested, executed };

    struct Effects {
        /** The request of the block changed: `request` is the new request, or none (the end with no message). */
        bool requestChanged{false};
        std::optional<swc::net::InteractionRequest> request;
        /** The client ends its committed interaction: message 2473 (the session client clears the block). */
        bool cancel{false};
    };

    /** The time from the execution to the end of the request (s): the measure of the retail client. */
    static constexpr double kClearDelay = 0.03;

    void press(double now, const Candidate& candidate);
    void release(double now);
    void tick(double now);
    /** The reply 3743 to the current request (1 accepted, 2 refused) at its arrival. */
    void reply(std::uint8_t result, double now);
    /** Message 3746: the server ended the use. */
    void server_cancel();
    /** The player cannot use an object now (the death, a loading screen): a use that is in progress ends with the cancel. */
    void abort();

    [[nodiscard]] Phase phase() const { return m_phase; }
    [[nodiscard]] bool timed() const { return m_option.delay > 0.0F; }
    /** @return The part of the hold or of the delay that passed (0 to 1). */
    [[nodiscard]] float progress(double now) const;
    [[nodiscard]] const Option& option() const { return m_option; }
    [[nodiscard]] std::uint32_t interest() const { return m_interest; }
    /** The counts: the requests, the executions, the cancels of the client, the ends by the server. */
    [[nodiscard]] std::uint32_t requests() const { return m_requests; }
    [[nodiscard]] std::uint32_t executions() const { return m_executions; }
    [[nodiscard]] std::uint32_t cancels() const { return m_cancels; }
    [[nodiscard]] std::uint32_t serverEnds() const { return m_serverEnds; }
    Effects take();

private:
    void request(double now);
    void end(bool cancel);
    /** The execution with the reply 1: the request ends when its time came (kClearDelay). */
    void clear_when_due(double now);

    Phase m_phase{Phase::idle};
    Option m_option;
    std::uint32_t m_interest{0};
    swc::net::InteractionRequest m_request;
    double m_start{0.0};
    double m_executedAt{0.0};
    double m_acceptedAt{0.0};
    bool m_down{false};
    bool m_accepted{false};
    Effects m_effects;
    std::uint32_t m_requests{0};
    std::uint32_t m_executions{0};
    std::uint32_t m_cancels{0};
    std::uint32_t m_serverEnds{0};
};

} // namespace swc::interact
