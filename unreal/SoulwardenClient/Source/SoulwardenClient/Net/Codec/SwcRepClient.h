#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
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
        /** Keep the records and the stops of the other entities, and the damage numbers, for take_other_updates() (the entity
         *  table of the creatures). */
        bool keepOtherRecords{false};
        /**
         * A step of the input of the player (stage P2.7, docs/ACTIONS.md): called for each input entry with the player motion,
         * with its input number (the 16 bits of the wire), the steady clock in nanoseconds, and its action bits. It adds the bits
         * of the attack input and follows the attack graph at the same step as the server. Called on the thread of the session.
         */
        std::function<void(std::uint16_t input, std::int64_t steadyNs, std::vector<unsigned>& actionBits)> inputStep;
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
        /** The paperdoll (protocol notes 4.11): paperdoll slot -> item index on the wire (0: the slot is empty). */
        std::map<std::uint32_t, std::uint32_t> equipment;
        /** One more at each change of the appearance or of the equipment (the view builds the body again). */
        std::uint32_t lookRevision{0};
        /** The paperdoll, group 2 (protocol notes 4.11): bit N (byte N / 8, bit N % 8 of the 7 bytes) is paperdoll slot N.
         *  sheatheMap (field 0): the item of the slot is in its sheath. activeMap (field 2): the active weapon slots. */
        std::uint64_t sheatheMap{0};
        std::uint64_t activeMap{0};

        /** The stamina state (slot 42, type 4297, fields 0 and 1, f32). The retail client predicts its stamina itself; this
         *  is the copy of the server. No state yet: the vitals values below. */
        bool hasStamina{false};
        float stamina{0.0F};
        float staminaMax{0.0F};
        /** The mana state (slot 47, type 1652, fields 0 and 1, f32). The mana meter of the retail HUD reads this component.
         *  `manaMax` is the base maximum: the current value can be larger (the client maximum has the MaxMana parts of the
         *  attributes, protocol notes 4.5). */
        bool hasMana{false};
        float mana{0.0F};
        float manaMax{0.0F};
        /** The vitals state: StaminaAmount and StaminaMax (group 0, fields 1 and 6), ManaAmount and ManaMax (group 1,
         *  fields 0 and 3), and bit 0 of vitalsData (group 0, field 2): the character is dead. */
        float vitalsStamina{0.0F};
        float vitalsStaminaMax{0.0F};
        float vitalsMana{0.0F};
        float vitalsManaMax{0.0F};
        bool dead{false};

        /** The ability state (slot 46, type 185, field 0 persistentAbilityData): one bar for each ability table, in the
         *  sequence of the field; each place has an ability id (the CRC-32 of the lower-case ability id text) and its
         *  button number. */
        struct AbilityBar {
            std::uint32_t table{0};
            std::vector<std::uint32_t> abilities;
            std::vector<std::uint32_t> buttons;
        };
        std::vector<AbilityBar> abilityBars;
        /** The cooldown state (slot 45, type 2932): fields 0 to 2 are the maps of the places 0 to 2 of the bars. Times in
         *  microseconds of the server clock (the clock of server_time_ns). An indefinite cooldown has the end
         *  0xFFFFFFFFFFFFFFFF and the minimum end 0. */
        struct Cooldown {
            std::uint32_t ability{0};
            std::uint64_t endUs{0};
            std::uint64_t minimumEndUs{0};
        };
        std::array<std::vector<Cooldown>, 3> cooldowns;
        /** Field 3 (ccdmap): the cooldowns of the consumables: the CRC-32 of the lower-case item id and the end. */
        std::vector<std::pair<std::uint32_t, std::uint64_t>> itemCooldowns;
    };

    /** An entity of a different interest number: its metadata and its position. */
    struct OtherEntity {
        ActorId actor{};
        ActorId sliceAsset{};
        std::uint32_t sliceSubId{0};
        bool hasPosition{false};
        WorldPosition position;
    };

    /** A record of an interest number that is not 1, a stop of the control block, or a message for the view of the other
     *  entities (the damage number, kDamageDealt: `messageType` is not 0), in the order of the messages. */
    struct OtherUpdate {
        bool stop{false};
        std::uint32_t interest{0};
        soulwarden::backend::tests::Record record;
        std::uint32_t messageType{0};
        Bytes message;
    };

    struct Counts {
        std::uint64_t messages{0};
        std::uint64_t bundles{0};
        std::uint64_t records{0};
        std::uint64_t movementStates{0};
        std::uint64_t incompleteBundles{0};
        std::uint64_t droppedBundles{0};
        std::uint64_t inputReports{0};
        /** The melee contacts in list 3 of the input entries (each one in one entry). */
        std::uint64_t meleeContacts{0};
    };

    /** The player of the game thread for the input report (protocol notes 4.7). */
    struct Motion {
        /** The position of the character (the feet, after the physics step of the client): the position field (flag 0x8000). */
        WorldPosition character;
        /** The position of the camera (mask bit 7). */
        WorldPosition camera;
        /** The look of the camera in radians (mask bit 6): yaw to the left from north (+y), a positive pitch is down. */
        float yaw{0.0F};
        float pitch{0.0F};
        /** The forward key is down: the action bits of W. */
        bool forward{false};
        /** The distance to the ground below the feet in meters while the character is in the air; negative on the ground. */
        float airHeight{-1.0F};
        /** The action bits of the last key press (a weapon key): one entry has them, at each new serial. */
        std::uint32_t pressSerial{0};
        std::vector<unsigned> pressBits;
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
    /**
     * The player of the game thread: from now on each input entry has its camera, its look, its keys, and the position of
     * the character. With no motion (the default, or std::nullopt) an entry has the camera of a character at rest at
     * the position of the server and no position field (stage 1.5).
     */
    void set_motion(const std::optional<Motion>& motion) { m_motion = motion; }
    /** Melee contacts of the player (stage P2.8): they go into list 3 of the next input entry with the player motion. */
    void queue_contacts(const std::vector<MeleeContactItem>& contacts) { m_pendingContacts.insert(m_pendingContacts.end(), contacts.begin(), contacts.end()); }

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
    /** The server clock at `now` in nanoseconds (the clock of the registration reply, of 335, and of the cooldowns), or 0
     *  before the registration reply. */
    [[nodiscard]] std::uint64_t server_time_ns(Clock::time_point now) const;
    /** The offset of the server clock: server nanoseconds = the steady clock of the client in nanoseconds + this value. */
    [[nodiscard]] std::int64_t server_clock_offset_ns() const { return m_serverOffsetNs; }
    [[nodiscard]] bool has_server_clock() const { return m_hasServerClock; }
    std::vector<Event> take_events();
    /** @return The records and the stops of the other entities since the last call (Options::keepOtherRecords). */
    std::vector<OtherUpdate> take_other_updates();
    /** The most updates that wait for take_other_updates(): the oldest half goes away above it (one log line). */
    static constexpr std::size_t kOtherUpdateLimit = 20000;

    /** javelin.alc-server-timeout-for-input of the retail client (protocol notes 4.6). */
    static constexpr auto kMovementStateTimeout = std::chrono::milliseconds(2000);
    /** connection_ingame_timeout: no game message for this time ends the connection (protocol notes 4.1). */
    static constexpr auto kGameMessageTimeout = std::chrono::seconds(30);
    /** The input report rate: 30 each second (protocol notes 4.7). */
    static constexpr auto kInputInterval = std::chrono::nanoseconds(1'000'000'000 / 30);
    /** The clock correction of 335 (protocol notes 4.43, live L8: +0.5 s went to the slow correction in steps of 2 ms, +5 s
     *  moved the clock at once): a difference above this limit moves the clock at once. The limit is an inference. */
    static constexpr std::int64_t kClockJumpNs = 1'000'000'000;
    /** The step of the slow correction for each tick of the session (10 ms). */
    static constexpr std::int64_t kClockStepNs = 2'000'000;

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
    /** @return The position field of the next entry: the reported position one step nearer to the character. */
    WorldPosition reported_position(const WorldPosition& character);

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
    std::vector<OtherUpdate> m_otherUpdates;
    bool m_otherOverflow{false};
    Clock::time_point m_lastGameMessage{};
    Clock::time_point m_lastMovementState{};
    bool m_movementStale{false};
    Clock::time_point m_nextInput{};
    std::uint16_t m_inputNumber{0};
    std::vector<InputEntry> m_recentInputs;
    std::optional<Motion> m_motion;
    std::vector<MeleeContactItem> m_pendingContacts;
    /** The position of the last position field (it follows the character at a limited speed: reported_position). */
    bool m_hasReported{false};
    WorldPosition m_reported;
    bool m_sentMotion{false};
    std::uint32_t m_pressSerial{0};
    bool m_hasServerClock{false};
    std::int64_t m_serverOffsetNs{0};
    /** The part of the last correction of 335 that the slow correction did not apply yet. */
    std::int64_t m_clockCorrectionNs{0};
    std::uint32_t m_clockLogs{0};
};

} // namespace swc::net
