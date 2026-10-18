#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "Net/Codec/SwcCarrier.h"
#include "server/hub/wire.h"
#include "support/state_decoder.h"

// The messages of the game server transport (Soulwarden protocol notes 3.3, 3.4, 3.6, 4.1, 4.2, 4.7). Plain C++ with
// no engine header. The primitive encodings are the vendored wire codec of the backend (Vendor/soulwarden).
//
//   client -> server frame := crc32:u32 size:u32 payload        payload := route:16 bytes envelope
//   server -> client frame := size envelope                     (no checksum, no route)
//   envelope := flags:u8 [8 bytes if bit 0] [8 bytes if bit 1] [extension if bit 2] present:u8 type:size body
//
// `type` is an index into typeindex.json of the game copy. The client checks the type id of each index that it uses
// (SwcTypeIndex) and refuses the connection when the game copy has a different index.

namespace swc::net {

using Reader = soulwarden::backend::server::hub::Reader;
using Writer = soulwarden::backend::server::hub::Writer;

/** A message type: its type id (typeindex.json) and its index, the constant of the backend (rep_session.h). */
struct MessageType {
    const char* name;
    const char* typeId;
    std::uint32_t index;
};

inline constexpr MessageType kRegistrationRequestV3{"REPClient::RegistrationRequestV3Msg", "0B826B3389F549E0B8CBFE4433427778", 19};
inline constexpr MessageType kRegistrationResponse{"REPClient::RegistrationResponseMsg", "104145A7FF9544F1946821FB41C8AC2B", 3};
inline constexpr MessageType kReplicatedStateBundle{"ReplicatedStateBundle", "8A40AEC2AE074F929BF378FC0CC94FDF", 8};
inline constexpr MessageType kKickClient{"KickClientTrait::KickClientMsg", "4A0A26F6F24E4239A7717BA1F135C6FD", 271};
inline constexpr MessageType kClientInput{"ActionListComponentServerFacet_ReceiveInputFromClient", "01346CCD785E448DA4294B281972B1D7", 1589};
inline constexpr MessageType kReceivePlayerSpawnPoint{"ClientViewListenerTrait::ReceivePlayerSpawnPointMsg", "21207525B4484193AACA83D4F847929F", 1617};
inline constexpr MessageType kSelfIdentification{"PlayerManagerSelfIdentificationMsg", "169443E0A5084653B43749B7A195F69C", 1628};
inline constexpr MessageType kPlayerManagerRejected{"ClientMessagesTrait::PlayerManagerRejectedMsg", "DCB01CB8D04A40EAA3AF96604FE322FB", 1630};
inline constexpr MessageType kLevelInfoChanged{"LevelInfoChangedMsg", "45918D0302E742A48A301B205F8C37FE", 1635};
// State types of the world entry (the vendored state reader has their indexes).
inline constexpr MessageType kStateMetadata{"MB::GdeMetadataReplicatedState", "203DC8C70C60454BA46F566114314B84", 10};
inline constexpr MessageType kStateMovement{"MB::ALCReplicatedState", "01B0664B3AB644A687E38C69D40E0365", 11};
inline constexpr MessageType kStateVitals{"MB::VitalsComponentReplicatedState", "0E721C702CDB4E85BAE4D545FDC6D25B", 15};
inline constexpr MessageType kStatePlayer{"MB::PlayerComponentReplicatedState", "BDDDA784A6E7416BA041449920D90FB6", 3935};
inline constexpr MessageType kStateAppearance{"MB::PlayerAppearanceComponentReplicatedState", "6C597946250643858FB4882FB6A98D5D", 1195};
inline constexpr MessageType kStatePaperdoll{"MB::PaperdollComponentReplicatedState", "B258A60EFC2140CF8B8657B7F6083D32", 3183};
inline constexpr MessageType kStateStamina{"MB::StaminaComponentReplicatedState", "4A6A34E3FE184746972DD48DCE3DB1E3", 4297};
inline constexpr MessageType kStateMana{"MB::ManaComponentReplicatedState", "3729094D0FBA4DCFB1917E9E53AA9B85", 1652};
inline constexpr MessageType kStateAbility{"MB::AbilityComponentReplicatedState", "35877C3E4EF843DDABE7ABF35104F1B5", 185};
inline constexpr MessageType kStateCooldowns{"MB::CooldownTimersComponentReplicatedState", "6D45EB2095FE442096C53F9367A3FC5C", 2932};
inline constexpr MessageType kTimeSynch{"REPClient::TimeSynchMsg", "038CD847065342439A26936E3BD7F312", 335};
/** The damage number of the attacker (protocol notes 4.15; backend server/rep/combat_messages.h): the creature view reads it. */
inline constexpr MessageType kDamageDealt{"DamageReceiverComponentClientFacet_OnDamageDealt", "07D9175B3AFE4F43A49443005BB1BB4F", 2071};

/** Each type that the client uses: the client checks them against typeindex.json. */
inline constexpr std::array<MessageType, 21> kUsedTypes{kRegistrationRequestV3, kRegistrationResponse, kReplicatedStateBundle,
    kKickClient, kClientInput, kReceivePlayerSpawnPoint, kSelfIdentification, kPlayerManagerRejected, kLevelInfoChanged, kTimeSynch,
    kDamageDealt, kStateMetadata, kStateMovement, kStateVitals, kStatePlayer, kStateAppearance, kStatePaperdoll, kStateStamina,
    kStateMana, kStateAbility, kStateCooldowns};

/** The slots of the player slice (protocol notes 4.4). */
inline constexpr std::uint32_t kSlotMetadata = 0;
inline constexpr std::uint32_t kSlotPlayer = 9;
inline constexpr std::uint32_t kSlotAppearance = 12;
inline constexpr std::uint32_t kSlotMovement = 16;
inline constexpr std::uint32_t kSlotVitals = 41;
inline constexpr std::uint32_t kSlotPaperdoll = 62;
inline constexpr std::uint32_t kSlotStamina = 42;
inline constexpr std::uint32_t kSlotCooldowns = 45;
inline constexpr std::uint32_t kSlotAbility = 46;
inline constexpr std::uint32_t kSlotMana = 47;

/** The component id of the action list component (slot 16) of the entity RootPlayer of the player slice (game data
 *  slice-ids.json; research ids.md, table of the targets). The facet target of the input report is G + this id. */
inline constexpr std::uint64_t kActionListComponent = 0x3C591A4EA3C2AD75ULL;

using ActorId = std::array<std::uint8_t, 16>;

/** @return G, the entity group id: the first 8 bytes of the actor id as one little-endian number (protocol notes 4.2). */
std::uint64_t group_of(const ActorId& actor);
/** @return The hexadecimal text of bytes, in their sequence. */
std::string hex_of(std::span<const std::uint8_t> bytes);

/** One message of the server. */
struct ServerMessage {
    std::uint32_t type{};
    Bytes body;
};

/**
 * Reads the frames of one carrier message of the server (size envelope, one or more times).
 * @return False with a problem text when a frame does not have the form.
 */
bool read_server_frames(std::span<const std::uint8_t> data, std::vector<ServerMessage>& out, std::string& problem);

/** @return A client frame: crc32 size payload, payload := route envelope (flags 0, present 1, type, body). */
Bytes client_frame(const ActorId& route, std::uint32_t type, std::span<const std::uint8_t> body);

/** The login token of the login queue reply (protocol notes 2.1 step 5, 3.4). */
struct LoginToken {
    std::string signature;
    std::string repAddress;
    std::string worldId;
    std::string characterId;
    std::string personaId;
    std::uint32_t steamAppId{1063730};
    std::string steamUserId;
    std::string channelId;
    std::string ticketId;
    std::uint32_t issueTime{0};
    std::uint32_t generateTime{0};
    std::uint32_t accountAge{0};
    std::uint32_t tokenVersion{10};
};

/** @return The local ticket of the Soulwarden game hook as lower-case hexadecimal text (protocol notes 2.1). */
std::string local_ticket_hex(const std::string& steamId, const std::string& personaName);

/**
 * @return The body of RegistrationRequestV3Msg (protocol notes 3.4): u32, the version map, a text, the login token, the
 *         client data (the Steam id of the client and "steam|" with the ticket), two identities, a flag.
 */
Bytes registration_body(const LoginToken& token, const std::string& steamId, const std::string& ticketHex);

/** RegistrationResponseMsg: result:u32 (signed; 0 accepted) serverTime:u64 text text flag x4. */
struct RegistrationResponse {
    std::int32_t result{};
    std::uint64_t serverTime{};
    std::string serverVersion;
    bool antiCheatRequired{};
};
std::optional<RegistrationResponse> read_registration_response(std::span<const std::uint8_t> body);
/** @return The text key of a registration result (protocol notes 3.6), or an empty text. */
const char* registration_result_text(std::int32_t result);

/** An actor reference: hub:u32 actor:16 bytes type:16 bytes. */
struct ActorReference {
    std::uint32_t hub{};
    ActorId actor{};
    ActorId type{};
};

/** PlayerManagerSelfIdentificationMsg: identity, three actor references, the level block, identity (protocol notes 4.1). */
struct SelfIdentification {
    ActorId sessionActor{};
    ActorReference playerManager;
    ActorReference clientProxy;
    ActorReference third;
    std::uint32_t instance{};
};
std::optional<SelfIdentification> read_self_identification(std::span<const std::uint8_t> body);

/** LevelInfoChangedMsg: level terrain u32 x4 u64 list flag instance:u8 reload:flag flag version:u64. */
struct LevelInfo {
    std::string level;
    std::string terrain;
    std::uint8_t instance{};
    bool reload{};
    std::uint64_t version{};
};
std::optional<LevelInfo> read_level_info(std::span<const std::uint8_t> body);

/** KickClientMsg (reason:u32 u64 text) and PlayerManagerRejectedMsg (reason:u32 text). */
std::optional<std::uint32_t> read_reason(std::span<const std::uint8_t> body);

/** A world position: x:f32 y:f32 height:u16, height in meters = -100 + value / 65535 * 1100 (protocol notes 4.3). */
struct WorldPosition {
    float x{};
    float y{};
    float z{};
};
WorldPosition read_world_position(std::span<const std::uint8_t> value);

/** A compressed unit quaternion (protocol notes 4.3). */
struct Quaternion {
    float x{};
    float y{};
    float z{};
    float w{1.0F};
};
Quaternion read_quaternion(std::span<const std::uint8_t> value);

/** A state bundle with the place of the first fragment that the state reader does not know. */
struct CheckedBundle {
    soulwarden::backend::tests::Bundle bundle;
    /** True when the reader read the payload to its end. */
    bool complete{false};
    bool headerOk{false};
    /** The first fragment that the reader does not know (when not complete). */
    std::uint32_t failedInterest{};
    std::uint32_t failedSlot{};
    std::uint32_t failedType{};
};
/**
 * Reads the body of a ReplicatedStateBundle (protocol notes 4.1, 4.2) with the vendored state reader of the backend
 * tests. A fragment has no length: the reader stops at the first fragment of a state type that it does not know, and
 * the records before it stay.
 */
CheckedBundle read_state_bundle(std::span<const std::uint8_t> body);

/** The input entry of a client (protocol notes 4.7): the fields that this client writes. */
/**
 * One melee contact of an input entry (list 3, 96 bytes; protocol notes 4.15; the reader of the backend
 * server/rep/client_input.cpp read_contact): the receiver of the hit and the geometry of the contact.
 *
 *   0 provider:u32 (0xFFFFFFFF: none, each live contact of a sword)
 *   4 group := actor id:16 bytes G:u64 (the entity group of the receiver)
 *   28 receiver := actor id:16 bytes G:u64 facet target:u64 (its hit volume: the id rule of 4.2)
 *   60 point:v3  72 normal:v3  84 direction:v3 (engine space: the point in meters, two unit vectors)
 */
struct MeleeContactItem {
    std::uint32_t provider{0xFFFFFFFFU};
    ActorId groupActor{};
    std::uint64_t group{0};
    ActorId actor{};
    std::uint64_t entity{0};
    std::uint64_t facet{0};
    WorldPosition point;
    WorldPosition normal;
    WorldPosition direction;
};
inline constexpr std::size_t kMeleeContactSize = 96;
/** Writes the 96 bytes of one contact. */
void write_melee_contact(Writer& out, const MeleeContactItem& contact);

struct InputEntry {
    std::uint16_t sequence{1};
    std::uint64_t flags{0};
    int permissions{-1};
    int groundDistance{-1};
    unsigned actionWords{0};
    std::vector<unsigned> actionBits;
    float stepTime{0.0F};
    bool hasLook{false};
    float yaw{0.0F};
    float pitch{0.0F};
    bool hasCamera{true};
    WorldPosition camera;
    /** The position of the character (needs the flag 0x8000). */
    bool hasCharacter{false};
    WorldPosition character;
    /** The byte of entry +0x470 after the position (the live client sends it). */
    bool value470{false};
    /** List 3: the melee contacts of this step (an entry has only its own items). */
    std::vector<MeleeContactItem> contacts;
};

/** Flags of an entry of a client in the world that stands on the ground (live captures, backend tests). */
inline constexpr std::uint64_t kInputFlagsWorld = 0x20004F0ULL;
/** The flag of the position field of an entry. */
inline constexpr std::uint64_t kInputFlagPosition = 0x8000ULL;

/**
 * @return The body of an input report (type 1589) with these entries, the newest one first:
 *         target:u64 entity:u64 header count:u8 entry*. Each entry is a difference from the entry before it.
 */
Bytes input_report_body(const std::vector<InputEntry>& entries, std::uint64_t target, std::uint64_t entity);

} // namespace swc::net
