#pragma once

#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <vector>

#include "Net/Codec/SwcCarrier.h"
#include "Net/Codec/SwcRepMessages.h"

// The session of the client with the game server: the carrier handshake, the registration, the world entry, the input
// report, and the timers of the client contract (Soulwarden protocol notes 1, 3.4 to 3.6, 4.1 to 4.7). Plain C++ with
// no engine header and no clock of its own: the caller gives the time, the clear datagrams of the server, and sends
// the datagrams of tick() (the DTLS layer is outside). tools/test-net.sh runs it with the bytes of the backend.
//
// The phases have the names of the client states of the retail client:
//   WaitingForREPConnection          the carrier handshake
//   WaitingForActorGameConnection    the registration request is out
//   WaitingForSpawnPoint             the registration is accepted, the self identification arrived
//   WaitingForPlayerSpawn            the level information arrived and the level load started (first state bundle)
//   InGame                           the record of interest 1 has the character id of the login: the local player
//   Ended                            the session ended (a cause)

namespace swc::net {

class RepClient {
public:
    struct Options {
        LoginToken token;
        /** The Steam id of the client (GetSteamID) and the ticket (GetAuthTicketForWebApi, hexadecimal text). */
        std::string steamId;
        std::string ticketHex;
        /** The input report (type 1589) at 30 Hz from InGame. */
        bool sendInput{true};
    };

    enum class Phase { idle, waitingForRepConnection, waitingForActorGameConnection, waitingForSpawnPoint, waitingForPlayerSpawn, inGame, ended };

    struct Event {
        enum class Level { info, warning, error };
        Level level{Level::info};
        std::string text;
    };

    /** The own entity (interest 1) from its fragments. */
    struct OwnEntity {
        bool hasMetadata{false};
        ActorId actor{};
        std::uint64_t group{0};
        ActorId sliceAsset{};
        std::uint32_t sliceSubId{0};
        std::string characterId;
        std::string name;
        bool connected{false};
        bool hasPosition{false};
        WorldPosition position;
        Quaternion rotation;
        Quaternion lookDirection;
        std::uint8_t teleportId{0};
        std::uint64_t timeAnchor{0};
        std::uint8_t dataBits{0};
        std::uint8_t moreDataBits{0};
        /** The id of the movement state of the server: idAbs (high 8 bits) and idRel (low 8 bits). */
        std::uint16_t stateId{0};
        float health{0.0F};
        float healthMax{0.0F};
        bool hasAppearance{false};
        std::array<std::uint8_t, 12> appearance{};
    };

    /** An entity of a different interest number: its metadata and its position. */
    struct OtherEntity {
        ActorId actor{};
        ActorId sliceAsset{};
        std::uint32_t sliceSubId{0};
        bool hasPosition{false};
        WorldPosition position;
    };

    struct Counts {
        std::uint64_t messages{0};
        std::uint64_t bundles{0};
        std::uint64_t records{0};
        std::uint64_t movementStates{0};
        std::uint64_t incompleteBundles{0};
        std::uint64_t droppedBundles{0};
        std::uint64_t inputReports{0};
    };

    explicit RepClient(Options options);

    /** Starts the carrier handshake. */
    void start(Clock::time_point now);
    /** Reads one clear datagram of the server. */
    void receive(std::span<const std::uint8_t> datagram, Clock::time_point now);
    /** @return The clear datagrams to send now. */
    std::vector<Bytes> tick(Clock::time_point now);
    /** Leaves the world: the carrier disconnect goes out at the next tick, as at "Exit to Main Menu". */
    void leave(const std::string& cause);

    [[nodiscard]] Phase phase() const { return m_phase; }
    [[nodiscard]] static const char* phase_name(Phase phase);
    [[nodiscard]] const std::string& end_cause() const { return m_endCause; }
    [[nodiscard]] const OwnEntity& own() const { return m_own; }
    [[nodiscard]] const std::map<std::uint32_t, OtherEntity>& others() const { return m_others; }
    [[nodiscard]] const std::optional<SelfIdentification>& self_identification() const { return m_self; }
    [[nodiscard]] const std::optional<LevelInfo>& level_info() const { return m_level; }
    [[nodiscard]] const Counts& counts() const { return m_counts; }
    [[nodiscard]] const Carrier& carrier() const { return m_carrier; }
    /** True while the last movement state of the own entity is older than 2 s (the input then has no actions). */
    [[nodiscard]] bool movement_stale() const { return m_movementStale; }
    std::vector<Event> take_events();

    /** javelin.alc-server-timeout-for-input of the retail client (protocol notes 4.6). */
    static constexpr auto kMovementStateTimeout = std::chrono::milliseconds(2000);
    /** connection_ingame_timeout: no game message for this time ends the connection (protocol notes 4.1). */
    static constexpr auto kGameMessageTimeout = std::chrono::seconds(30);
    /** The input report rate: 30 each second (protocol notes 4.7). */
    static constexpr auto kInputInterval = std::chrono::nanoseconds(1'000'000'000 / 30);

private:
    void set_phase(Phase phase);
    void end(const std::string& cause, bool disconnect);
    void log(Event::Level level, std::string text);
    void on_message(const ServerMessage& message, Clock::time_point now);
    void on_bundle(std::span<const std::uint8_t> body, Clock::time_point now);
    void apply_own(const soulwarden::backend::tests::Record& record, Clock::time_point now);
    void apply_other(const soulwarden::backend::tests::Record& record);
    void send_registration();
    void send_input(Clock::time_point now);

    Options m_options;
    Carrier m_carrier;
    Phase m_phase{Phase::idle};
    std::string m_endCause;
    std::vector<Event> m_events;
    Counts m_counts;
    std::set<std::uint32_t> m_seenTypes;
    std::set<std::uint32_t> m_unknownStateTypes;
    std::optional<SelfIdentification> m_self;
    std::optional<LevelInfo> m_level;
    bool m_levelLoad{false};
    std::uint64_t m_bundleSequence{0};
    bool m_bundleGap{false};
    OwnEntity m_own;
    std::map<std::uint32_t, OtherEntity> m_others;
    Clock::time_point m_lastGameMessage{};
    Clock::time_point m_lastMovementState{};
    bool m_movementStale{false};
    Clock::time_point m_nextInput{};
    std::uint16_t m_inputNumber{0};
    std::vector<InputEntry> m_recentInputs;
};

} // namespace swc::net
