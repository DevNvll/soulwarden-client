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

/**
 * The candidate shape of an Interact bound (AST-198; research prompt-range.md): retail admits a bound as a prompt candidate when the
 * player point is in this shape (the marker path: the cached player transform point; the trigger path: a point callback), not by its
 * validation range. The shape is centred on the place of the bound; the basis is the 3 x 3 part of the transform of the bound entity
 * in its slice (rows; its rotation and scale). Type none: the data has no shape (the range rule of the use is the area).
 */
struct Shape {
    enum class Type : std::uint8_t { none, sphere, box, capsule, cylinder };
    Type type{Type::none};
    float radius{0.0F};
    /** The height of a capsule or a cylinder along its local z (m). */
    float height{0.0F};
    /** The full sizes of a box (m; Dimensions). */
    float dimensions[3]{0.0F, 0.0F, 0.0F};
    float basis[9]{1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 1.0F};
};

/** One Interact bound of a template: its component, its entity in the slice, its place in the frame of the slice, its range. */
struct Bound {
    std::uint32_t slot{0};
    std::uint64_t component{0};
    std::uint64_t entity{0};
    float position[3]{0.0F, 0.0F, 0.0F};
    /** The squared 3D range (m^2; m_interactRangeSq; equal passes): the check of the use, not the area of the prompt. */
    float rangeSq{0.0F};
    Shape shape;
    std::vector<Option> options;
};

/** The template of an object of the server: its slice, its root entity, and its bounds. */
struct Template {
    std::string name;
    swc::net::ActorId asset{};
    std::uint32_t sub{0};
    std::uint64_t root{0};
    std::vector<Bound> bounds;
    /** A crafting station (stage MVP-J): the Assembly key of the use (m_craftingStationEntry: "Blacksmith5"; empty for another object),
     *  the fee flag (m_chargeCraftingFee), and the name key of the station ("@BlacksmithT5_MasterName"). */
    std::string station;
    bool stationFee{false};
    std::string interactName;
};

/** @return The instance id of an entity of the slice of an object with the entity group G (protocol notes 4.2: K = root XOR G). */
std::uint64_t instance_of(std::uint64_t root, std::uint64_t group, std::uint64_t sliceEntity);
/** @return The facet target of a component of an entity of the slice: the instance id + the component id (a 64-bit sum). */
std::uint64_t facet_target(std::uint64_t root, std::uint64_t group, std::uint64_t sliceEntity, std::uint64_t component);

/** An object of the server: its interest number, its actor, its slice, and its place and rotation (PositionInTheWorld). */
struct Object {
    std::uint32_t interest{0};
    swc::net::ActorId actor{};
    swc::net::ActorId sliceAsset{};
    std::uint32_t sliceSub{0};
    swc::net::WorldPosition place;
    swc::net::Quaternion rotation;
};

/**
 * The range rule of the server (backend world/interactions.cpp, Interactions::check; docs/INTERACTIONS.md "The range"): the squared
 * 3D distance from the position of the character to the place of the bound, in float, is not more than m_interactRangeSq (equal
 * passes, no margin). The position of the character is the position field of the input report that has the request: the server
 * takes the position of a report before its request (RepClient::next_reported).
 */
float distance_sq(const swc::net::WorldPosition& character, const swc::net::WorldPosition& place);
bool in_range(const swc::net::WorldPosition& character, const swc::net::WorldPosition& place, float rangeSq);

/**
 * @return The place of a point of a slice in the world: the place of the object plus the point turned by the rotation of the
 * object (backend gamedata::transform_point, the bounds of a server child).
 */
swc::net::WorldPosition transform_point(const swc::net::WorldPosition& place, const swc::net::Quaternion& rotation, const float local[3]);

/**
 * @return True when a point is in the candidate shape of a bound (AST-198): the point goes into the frame of the shape (the rotation of
 * the object, then the basis of the bound). A sphere is strict (retail IsPointInside 0xCC2BE0: distance^2 < radius^2), a box is
 * inclusive (0xCC21F0: half of Dimensions on each axis); a capsule or a cylinder stands on its local z. With no shape: in_range.
 */
bool contains(const Shape& shape, const swc::net::WorldPosition& center, const swc::net::Quaternion& rotation, const swc::net::WorldPosition& point,
    float rangeSq);

/**
 * The score of the focus of retail (0x490FED0; research prompt-range.md "Retail focus"): with the camera position C, its unit forward
 * vector F, and the place of the bound B: t = dot(B - C, F); a bound with t <= 0 (at or behind the camera plane) has no score; else the
 * score is the squared distance from B to the forward line, |C + t F - B|^2. The smallest score wins; an equal score keeps the first.
 * @return False with no score.
 */
bool line_score(const float camera[3], const float forward[3], const float place[3], float& score);

/** A usable option of a bound of an object near the player. */
struct Candidate {
    std::uint32_t interest{0};
    const Template* object{nullptr};
    const Option* option{nullptr};
    /** The request of the block: the reference of the bound, the action, the option entity, the option id, the hold time. */
    swc::net::InteractionRequest request;
    /** The place of the bound (engine m), its squared distance to the character, and its squared range (m^2). */
    swc::net::WorldPosition place;
    float distanceSq{0.0F};
    float rangeSq{0.0F};
    /** The focus score of the camera line (line_score). */
    float score{0.0F};
};

/** @return The request of an option of a bound of an object (the ids of the object from its actor). */
swc::net::InteractionRequest request_of(const Template& object, const Bound& bound, const Option& option, const swc::net::ActorId& actor);

/**
 * @return The first option of the bound with the smallest camera line score (line_score) whose shape has the point of the character
 * (contains), or no value. The place of a bound is its place in the frame of the slice, turned and moved by the rotation and the place
 * of its object (transform_point). The camera is in engine m.
 */
std::optional<Candidate> focus(const std::vector<Template>& templates, const std::vector<Object>& objects, const swc::net::WorldPosition& character,
    const float camera[3], const float forward[3]);

/**
 * The use of one option by the player (the client side of protocol notes 4.35 and 4.36):
 *   press      the key goes down at a focus: the hold of the prompt (hold > 0), else the request at once;
 *   the hold   the key stays down for the hold time: the request (the block goes into each input entry); a hold whose
 *              character goes out of the range of the bound ends with no request (moved: the server refuses a request
 *              out of the range, and it measures at the request, not at the press);
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

    /**
     * @param keepUntilEnd True for a use whose end comes from its source (the Take All of a loot bag: the client keeps its request
     * after the execution and the reply 1 until the claim of the bag is Looted, then it sends 2473; protocol notes 4.42, live L6):
     * the reply 1 and a release do not end it; finish() does.
     */
    void press(double now, const Candidate& candidate, bool keepUntilEnd = false);
    /** The source of a use that keeps its request ends it (the Looted claim of a bag, the stop of its record): the cancel 2473. */
    void finish();
    void release(double now);
    void tick(double now);
    /** The reply 3743 to the current request (1 accepted, 2 refused) at its arrival. */
    void reply(std::uint8_t result, double now);
    /** Message 3746: the server ended the use. */
    void server_cancel();
    /** The player cannot use an object now (the death, a loading screen): a use that is in progress ends with the cancel. */
    void abort();
    /**
     * The position field of the next input entry (before tick): a hold out of the range of its bound (in_range) ends with no
     * request. @return True when the hold ended.
     */
    bool moved(const swc::net::WorldPosition& reported);

    [[nodiscard]] Phase phase() const { return m_phase; }
    [[nodiscard]] bool timed() const { return m_option.delay > 0.0F; }
    /** @return The part of the hold or of the delay that passed (0 to 1). */
    [[nodiscard]] float progress(double now) const;
    [[nodiscard]] const Option& option() const { return m_option; }
    [[nodiscard]] std::uint32_t interest() const { return m_interest; }
    /** The place of the bound of the use and its squared range (m^2). */
    [[nodiscard]] const swc::net::WorldPosition& place() const { return m_place; }
    [[nodiscard]] float rangeSq() const { return m_rangeSq; }
    /** The counts: the requests, the executions, the cancels of the client, the ends by the server, the holds out of range. */
    [[nodiscard]] std::uint32_t requests() const { return m_requests; }
    [[nodiscard]] std::uint32_t executions() const { return m_executions; }
    [[nodiscard]] std::uint32_t cancels() const { return m_cancels; }
    [[nodiscard]] std::uint32_t serverEnds() const { return m_serverEnds; }
    [[nodiscard]] std::uint32_t rangeEnds() const { return m_rangeEnds; }
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
    swc::net::WorldPosition m_place;
    float m_rangeSq{0.0F};
    double m_start{0.0};
    double m_executedAt{0.0};
    double m_acceptedAt{0.0};
    bool m_down{false};
    bool m_accepted{false};
    bool m_keepUntilEnd{false};
    Effects m_effects;
    std::uint32_t m_requests{0};
    std::uint32_t m_executions{0};
    std::uint32_t m_cancels{0};
    std::uint32_t m_serverEnds{0};
    std::uint32_t m_rangeEnds{0};
};

} // namespace swc::interact
