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
/** The weapon trees of the owner (stage P3.5: the learned nodes for the conditions of the abilities; protocol notes 4.13). */
inline constexpr MessageType kStatePoints{"MB::ProgressionPointReplicatedState", "F7B56641F8C341A983B213AC4F9843F9", 3681};
inline constexpr MessageType kTimeSynch{"REPClient::TimeSynchMsg", "038CD847065342439A26936E3BD7F312", 335};
/** The world time (protocol notes 4.43; Net/Codec/SwcWorldTime.h): the request of the client, the record, and the phase. */
inline constexpr MessageType kTimeRequest{"TimeComponentServerFacet_SyncToClient", "5AFEA4119BF64DBEAF48B8CB7EE0F6A3", 4248};
inline constexpr MessageType kTimeAndPhase{"TimeComponentClientFacet_SyncTimeAndPhase", "32096077F75C4BF7A444EC738F237632", 7036};
inline constexpr MessageType kDayPhase{"TimeComponentClientFacet_SyncDayPhase", "DEEBDC36DEA0485284F6A63ABEA5E8BA", 4247};
/** The damage number of the attacker (protocol notes 4.15; backend server/rep/combat_messages.h): the creature view reads it. */
inline constexpr MessageType kDamageDealt{"DamageReceiverComponentClientFacet_OnDamageDealt", "07D9175B3AFE4F43A49443005BB1BB4F", 2071};

// The life, the travel, and the interactions of the own character (stage P2.10; protocol notes 4.16, 4.18, 4.20, 4.35, 4.36;
// backend server/rep/health_messages.h, travel_messages.h, lock_messages.h). Component calls: target:u64 entity:u64 body.
/** The damage mark of the owner (4.18). */
inline constexpr MessageType kDamageTaken{"VitalsComponentClientFacet_OnDamage", "290F24ABB20B4A75A961FF765E67A03D", 3601};
/** The death: the time to the respawn screen (f32, s). */
inline constexpr MessageType kPlayerDeath{"PlayerComponentClientFacet_OnPlayerDeath", "C9FB82A32A3D480A91BFDC72C0F3A277", 3950};
/** A far travel: the loading screen (kind:u8). */
inline constexpr MessageType kTeleportWithLoadingScreen{"PlayerComponentClientFacet_OnTeleportWithLoadingScreen", "8D371AC0B5AD49BC83818DA7943772DB", 3951};
/** The end of a travel or a respawn: the loading screen ends. */
inline constexpr MessageType kPlayerTeleport{"PlayerComponentClientFacet_OnPlayerTeleport", "B34A7676EFC1472B86E6130F6AC470C6", 3954};
/** The respawn request of the death screen (client to server): random:u8 id:text. */
inline constexpr MessageType kRequestRespawn{"PlayerComponentServerFacet_RequestRespawn", "12A0B32C81384305B122DCF5C1B1F71E", 1938};
/** The end of a lock of the input (a cutscene, a conversation). */
inline constexpr MessageType kCutsceneEndFinalized{"PlayerCutsceneComponentClientFacet_OnServerCutsceneEndRequestFinalized", "D19070A4AEA24E0D9585EC073F006828", 6709};
/** The cancel of the committed interaction by the client (client to server): its reference (32 bytes). */
inline constexpr MessageType kCancelCommittedInteraction{"InteractorComponentServerFacet_RequestCancelCommittedInteractionFromClient", "44D7E916377940A48EC63FAEA459EE13", 2473};
/** The server ends the interaction of the client. */
inline constexpr MessageType kCancelCurrentInteraction{"InteractorComponentClientFacet_CancelCurrentInteraction", "91F8DF17FB504EA3AEBF219B977F194E", 3746};
/** The reply to an interaction request: the reference, the action, the result (1 accepted, 2 refused). */
inline constexpr MessageType kInteractionResult{"InteractorComponentClientFacet_ReceiveInteractionResult", "188632B415E649A484A4DD7FEA17B5CF", 3743};

// The loot of the player (stage P2.10; protocol notes 4.10, 4.41, 4.42; backend server/rep/item_requests.h, the states of
// server/rep/states/chest_states.cpp and loot_bag_states.cpp).
/** The take of all the items of the open bag (client to server, 48 bytes): the reference of the own GlobalStorage Container. */
inline constexpr MessageType kTakeAllItems{"PlayerTradeComponentServerFacet_TakeAllItems", "1C4DF3BE03D147DCA7DC08FC4FFCB8CF", 2939};
/** The take of one item instance (client to server, 68 bytes): the reference, the instance, and the quantity. */
inline constexpr MessageType kTakeItem{"PlayerTradeComponentServerFacet_TakeItem", "46E59A060D7246569E06E3667726AE51", 2942};
inline constexpr MessageType kStateContainer{"MB::ContainerComponentReplicatedState", "EF1A20F2F6CF439FA1AC63460F803134", 1755};
inline constexpr MessageType kStateLootTracker{"MB::LootTrackerComponentReplicatedState", "756DEAE5A1F04863BE20B44D871C46A1", 982};
inline constexpr MessageType kStateGlobalStorage{"MB::GlobalStorageComponentReplicatedState", "37F7555D8476410A9753850945075374", 2938};
/** The achievements of the owner (stage P4.1 part 2; backend server/rep/states/ownership_states.cpp): field 0 is the byte vector of
 *  the achievements (the bit of a read lore page). */
inline constexpr MessageType kStateAchievement{"MB::AchievementComponentReplicatedState", "44528310AAA54E1BB8C168135B81CD06", 3210};
/** The gather or the open of the owner (backend server/rep/states/chest_states.cpp; docs/systems/chests.md, gathering.md). */
inline constexpr MessageType kStateGathering{"MB::GatheringComponentReplicatedState", "0091234C7DDA45FC99EED44859C02A7F", 81};

// The bag and the equipment of the player (stage P2.4; protocol notes 4.10, 4.11, 4.23; backend server/rep/item_requests.h and
// server/rep/states/currency_state.cpp). Component calls (client to server): target:u64 entity:u64 body. The server has no reply
// for a request that it did: the new states of the slots 61 and 62 are the answer.
/** Equip (45 bytes): bagSlot:i32 paperdollSlot:u32 quantity:u32 instance:16 flag:u8 (the full stack; the flag of the captures: 0). */
inline constexpr MessageType kEquipItem{"PaperdollComponentServerFacet_ServerEquipItem", "073F09E718EC4818AA7217639CB0FDB2", 3895};
/** Unequip (29 bytes): paperdollSlot:u32 bagSlot:i32 (-1: the first free slot) quantity:u32 flag:u8 (the flag of the capture: 1). */
inline constexpr MessageType kUnequipItem{"PaperdollComponentServerFacet_ServerUnequipItem", "8282246D76644676A8CE05E04A9DAACD", 3896};
/** Swap of two paperdoll slots (57 bytes): the slot of the drop, the slot of the drag, their instances (zero: no item), count:u8. */
inline constexpr MessageType kSwapItems{"PaperdollComponentServerFacet_ServerSwapItems", "52D8A095A6D74C999FF46E4C880E2687", 3900};
/** The refusal of a request of the equipment (server to client, 20 bytes): the target and the entity of the request, reason:u32. */
inline constexpr MessageType kEquipItemFail{"PaperdollComponentClientFacet_OnEquipItemFail", "5C40A053F92B446FAAFA6F9C819DF06E", 3626};
/** The use of an item of the bag (20 bytes): bagSlot:u32 (the entry "Use" of the action menu of the bag). */
inline constexpr MessageType kUseItem{"ContainerComponentServerFacet_ServerUseItem", "B359CD4BA0D24B25A72402BAB73BFB5F", 2917};
/** The drop of items of the bag: count:size, then for each entry slot:u32 instance:16 u32 (FFFFFFFF) quantity:u32. */
inline constexpr MessageType kDropInventoryItems{"ItemDropComponentServerFacet_DropInventoryItems", "EBE3D337F65246F89A7C432AC4CA44B8", 1838};
/** The drop of an item of the paperdoll (24 bytes): paperdollSlot:u32 quantity:u32. */
inline constexpr MessageType kDropPaperdollItem{"ItemDropComponentServerFacet_DropPaperdollItem", "43F9A0B65B1D4D97A4A361F51E0610C8", 826};
/** The coin of the character (slot 60): group 0, field 0, u64 in hundredths of a coin. */
inline constexpr MessageType kStateCurrency{"MB::CurrencyComponentReplicatedState", "C3F8F5F55D8E45A0890C36B9B0183C8E", 3786};
/** The level and the experience of the character (slot 70; protocol notes 4.12): group 0 field 0 level (the level of the screen
 *  minus 1), group 1 field 0 experiencePoints (u32 each). The bag window needs the level (Hud.LocalPlayer.Progression.Level). */
inline constexpr MessageType kStateProgression{"MB::ProgressionComponentReplicatedState", "3529CE4BE0EF40C5AF115165DA637303", 899};

/** The movement of the owner (stage P3.3; protocol notes 4.24): the speed table (slot 43) and the status map (slot 44). */
inline constexpr MessageType kStateSpeedTable{"MB::StatMultiplierTableReplicatedState", "DDD468968DAD4EE5836E341A72D44403", 1525};
inline constexpr MessageType kStateStatuses{"MB::StatusEffectsComponentReplicatedState", "E36B9CC4082E40F0BA4F5C5BE9CD3C16", 4236};

/** The mana and the heals of the owner (stage P3.6; protocol notes 4.5, 4.31): the prediction sync of the server (4289: target,
 *  entity, current, maximum, delay, rate factor) and the heal of a different character by the owner (4298: target, entity, the Stats
 *  id of the healed character, amount). */
inline constexpr MessageType kManaForceSync{"ManaComponentClientFacet_ClientForceSyncServerState", "07C85A72F95645FB98529E92A516DB7A", 4289};
inline constexpr MessageType kHealedPlayer{"VitalsComponentClientFacet_OnHealedPlayer", "6F1EB843106A427AA09E61BC50DD82F2", 4298};

/** Each type that the client uses: the client checks them against typeindex.json. */
inline constexpr std::array<MessageType, 54> kUsedTypes{kRegistrationRequestV3, kRegistrationResponse, kReplicatedStateBundle,
    kKickClient, kClientInput, kReceivePlayerSpawnPoint, kSelfIdentification, kPlayerManagerRejected, kLevelInfoChanged, kTimeSynch,
    kDamageDealt, kStateMetadata, kStateMovement, kStateVitals, kStatePlayer, kStateAppearance, kStatePaperdoll, kStateStamina,
    kStateMana, kStateAbility, kStateCooldowns, kDamageTaken, kPlayerDeath, kTeleportWithLoadingScreen, kPlayerTeleport,
    kRequestRespawn, kCutsceneEndFinalized, kCancelCommittedInteraction, kCancelCurrentInteraction, kInteractionResult, kTakeAllItems,
    kTakeItem, kStateContainer, kStateLootTracker, kStateGlobalStorage, kStateAchievement, kStatePoints, kStateGathering, kEquipItem,
    kUnequipItem, kSwapItems, kEquipItemFail, kUseItem, kDropInventoryItems, kDropPaperdollItem, kStateCurrency, kStateSpeedTable,
    kStateStatuses, kTimeRequest, kTimeAndPhase, kDayPhase, kStateProgression, kManaForceSync, kHealedPlayer};

/** The slots of the player slice (protocol notes 4.4). */
inline constexpr std::uint32_t kSlotMetadata = 0;
inline constexpr std::uint32_t kSlotPlayer = 9;
inline constexpr std::uint32_t kSlotAppearance = 12;
inline constexpr std::uint32_t kSlotMovement = 16;
inline constexpr std::uint32_t kSlotVitals = 41;
inline constexpr std::uint32_t kSlotPaperdoll = 62;
inline constexpr std::uint32_t kSlotStamina = 42;
inline constexpr std::uint32_t kSlotCooldowns = 45;
/** The speed table (StatMultiplierTable) and the status map (StatusEffects) of the owner (stage P3.3). */
inline constexpr std::uint32_t kSlotSpeedTable = 43;
inline constexpr std::uint32_t kSlotStatuses = 44;
inline constexpr std::uint32_t kSlotAbility = 46;
inline constexpr std::uint32_t kSlotMana = 47;
/** The ProgressionPoint state of the owner (the pools and the nodes of the weapon trees). */
inline constexpr std::uint32_t kSlotPoints = 67;
/** The loot of the owner (protocol notes 4.10, 4.41, 4.42): the bag (the Container of slot 61), the LootTracker (33), the selected
 *  Container (76: the open bag), and the GlobalStorage map (77). */
inline constexpr std::uint32_t kSlotBag = 61;
inline constexpr std::uint32_t kSlotLootTracker = 33;
inline constexpr std::uint32_t kSlotSelectedContainer = 76;
inline constexpr std::uint32_t kSlotGlobalStorage = 77;
/** The Achievement state of the player slice (backend state_codecs.h kAchievementSlot). */
inline constexpr std::uint32_t kSlotAchievement = 66;
/** The Gathering state of the player slice (owner only). */
inline constexpr std::uint32_t kSlotGathering = 36;
/** The coin of the owner (the Currency of the entity Inventory). */
inline constexpr std::uint32_t kSlotCurrency = 60;
/** The level and the experience of the character (the Progression component). */
inline constexpr std::uint32_t kSlotProgression = 70;

/** The component id of the action list component (slot 16) of the entity RootPlayer of the player slice (game data
 *  slice-ids.json; research ids.md, table of the targets). The facet target of the input report is G + this id. */
inline constexpr std::uint64_t kActionListComponent = 0x3C591A4EA3C2AD75ULL;
/** The components of the root entity of the player slice whose facets have the messages of the life, the travel, and the
 *  interactions (protocol notes 4.16, 4.20, 4.35): the target is G + the component id (a 64-bit sum). */
inline constexpr std::uint64_t kPlayerComponent = 0xDF0CE8C98D27619DULL;
inline constexpr std::uint64_t kPlayerCutsceneComponent = 0xD49931510E1DD23FULL;
inline constexpr std::uint64_t kInteractorComponent = 0x6C5718DBFE6DF612ULL;
/** The PlayerTrade component of the root entity (the target of the take calls: G + this id) and the Container component of the
 *  entity GlobalStorage of the player slice (slice-ids.json: RootPlayer/f4e8fe37-..., GlobalStorage/91b8850b-...; the root of the
 *  slice DAEE4E2555650AB7). */
inline constexpr std::uint64_t kPlayerTradeComponent = 0x5E6EA4CE9484F842ULL;
inline constexpr std::uint64_t kPlayerSliceRoot = 0xDAEE4E2555650AB7ULL;
inline constexpr std::uint64_t kGlobalStorageEntity = 0x00006914A15737B2ULL;
inline constexpr std::uint64_t kGlobalStorageContainer = 0x476865E50327A1C7ULL;
/** The entity Inventory of the player slice and its components Container (the bag, slot 61) and Paperdoll (slot 62), and the
 *  ItemDrop component of the root entity (slice-ids.json: Inventory/91b8850b-..., Inventory/ba93b9ff-..., RootPlayer/5b3ec9f6-...;
 *  the targets of the rule of 4.2 agree with the paperdoll target of the captures and with the table of research inventory.md 9). */
inline constexpr std::uint64_t kInventoryEntity = 0xE97EBAF556467A3AULL;
inline constexpr std::uint64_t kInventoryContainer = 0x76258CC02DF25991ULL;
inline constexpr std::uint64_t kPaperdollComponent = 0xA4682AF2564E7814ULL;
inline constexpr std::uint64_t kItemDropComponent = 0xE39A2483DC525915ULL;

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
/**
 * @return The position that the server reads from the position field of an input entry with this position of the character
 * (backend server/rep/client_input.cpp): x and y as f32, the height cut off to a step of 1100 / 65535 m (as the retail client
 * writes it) and read back. The range of an interaction is from this position (docs/INTERACTIONS.md "The range").
 */
WorldPosition position_field(const WorldPosition& character);
/** The place of an object (PositionInTheWorld, field 0): x:f32 y:f32 height:u16, height = -100 + value / 65535 * 2100. */
WorldPosition read_object_place(std::span<const std::uint8_t> value);
/** The type of the PositionInTheWorld state (the vendored state reader has it). */
inline constexpr std::uint32_t kStatePositionInTheWorld = 13;

/** A compressed unit quaternion (protocol notes 4.3). */
struct Quaternion {
    float x{};
    float y{};
    float z{};
    float w{1.0F};
};
Quaternion read_quaternion(std::span<const std::uint8_t> value);
/**
 * The rotation of an object (PositionInTheWorld, field 1; backend server/rep/states/object_states.cpp): header:u8, then u16 for
 * each of x, y, z with no header bit. The bits 0x01, 0x02, 0x04 give 0, the bits 0x08, 0x10, 0x20 give 1, a u16 value v gives
 * v / 32767.5 - 1, w is the square root of the rest, and the bit 0x40 makes w negative.
 */
Quaternion read_place_rotation(std::span<const std::uint8_t> value);

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

/**
 * One shot of an input entry (list 2, 192 bytes; protocol notes 4.27; the reader of the backend server/rep/client_input.cpp and
 * the writer of its made-up client, tests/support/client_frames.h): the shot of a cast of an ability (stage P3.5: the throw of
 * Gravity Well at its mark, with no receiver).
 *
 *   0 origin:v3  12 direction:v3 (a unit vector)  24 the offset of the aim:v3 (0)  36 point:v3 (a hit near the muzzle)
 *   92 receiver := actor id:16 bytes G:u64 facet target:u64 (no receiver: the actor id 0 and G 0xFFFFFFFF)
 *   172 charge:f32  176 ammunition:u32  180 attack:u32 (the progression id of the RangedAttackName)  184 row:u32
 *   190 the category of the receiver:u8 (255: none; the live client)
 * The other bytes are 0 (the identifier, the owner reference, the token: the backend does not read them).
 */
struct ShotItem {
    WorldPosition origin;
    WorldPosition direction;
    WorldPosition point;
    ActorId receiverActor{};
    std::uint64_t receiver{0xFFFFFFFFULL};
    std::uint64_t facet{0};
    float charge{0.0F};
    std::uint32_t ammo{0};
    std::uint32_t attack{0};
    std::uint32_t row{0};
};
inline constexpr std::size_t kShotSize = 192;
/** Writes the 192 bytes of one shot. */
void write_shot(Writer& out, const ShotItem& shot);

/**
 * The reference of an interaction (protocol notes 4.35): the actor of the object, its entity group G, and the facet target of
 * its Interact bound (the id rule of 4.2: K = root XOR G, target = (entity XOR K) + component).
 */
struct InteractionReference {
    ActorId actor{};
    std::uint64_t group{0};
    std::uint64_t target{0};
    bool operator==(const InteractionReference&) const = default;
};

/** The fields of an interaction request that the backend reads (the block of mask bit 8 of an input entry). */
struct InteractionRequest {
    InteractionReference reference;
    std::uint8_t action{0};
    std::uint8_t baseAction{0};
    /** The option entity: the instance of the option (entity XOR K). */
    std::uint64_t optionEntity{0};
    bool committed{true};
    /** The hold time of a timed option (its Delay Time; the live client sends it, the backend does not read it). */
    float holdTime{0.0F};
    /** The CRC-32 of the lower-case option name. */
    std::uint32_t optionId{0};
};

/**
 * @return True when the client takes the place of a movement state of the server (protocol notes 4.16, 0x486D89B): the low 4
 * bits of field 31 (teleportAndMigrationId) are not the teleport number of the client. The start number of a client is 0; the
 * world entry has 1; each travel the next number (number % 15 + 1: 1 to 15, never 0); the high 4 bits (the migration) stay out.
 */
inline bool takes_travel(std::uint8_t clientNumber, std::uint8_t field) {
    return (field & 0x0F) != clientNumber;
}

/** @return The interaction block from its present byte (131 bytes with empty texts and no vectors; backend
 *  server/rep/client_input.cpp read_interaction). */
Bytes interaction_block(const InteractionRequest& request);

/** @return The body of RequestRespawn (1938): target (G + Player) entity (G) random:u8 id:text; the row "Random Location" is
 *  random 1 and an empty id (18 bytes, the live client). */
Bytes respawn_request_body(std::uint64_t group, bool random = true, const std::string& id = {});
/** @return The body of RequestCancelCommittedInteractionFromClient (2473, 48 bytes): target (G + Interactor) entity (G) and the
 *  reference of the interaction. */
Bytes interaction_cancel_body(std::uint64_t group, const InteractionReference& reference);

/** The head of a component call: target:u64 entity:u64. */
struct ComponentCall {
    std::uint64_t target{0};
    std::uint64_t entity{0};
};
std::optional<ComponentCall> read_component_call(std::span<const std::uint8_t> body);
/** OnPlayerDeath (3950): the call and the time to the respawn screen (s). */
struct PlayerDeath {
    ComponentCall call;
    float time{0.0F};
};
std::optional<PlayerDeath> read_player_death(std::span<const std::uint8_t> body);
/**
 * VitalsComponentClientFacet_OnDamage (3601; protocol notes 4.18; backend server/rep/health_messages.cpp): the damage that the own
 * character took. The call (the vitals facet of the character, its G), the entity that did the damage, the flags (two bytes, the
 * low byte first; bit 1: the damage has a place), the damage as a part of the maximum health, the place of the source (x y z f32),
 * and the entries (count:size, then type:u8 amount:f32 part:f32: the number of the damage type, the damage, the part before the
 * defense). The message changes no value: the health is in the vitals state.
 */
struct DamageTaken {
    struct Entry {
        std::uint8_t type{0};
        float amount{0.0F};
        float part{1.0F};
    };
    ComponentCall call;
    std::uint64_t source{0};
    std::uint16_t flags{0};
    float part{0.0F};
    std::array<float, 3> from{};
    std::vector<Entry> entries;

    static constexpr std::uint16_t kHasPlace = 1U << 1;
};
std::optional<DamageTaken> read_damage_taken(std::span<const std::uint8_t> body);
/** ReceiveInteractionResult (3743): the call, the reference, the action of the request, the result (1 accepted, 2 refused). */
struct InteractionResult {
    ComponentCall call;
    InteractionReference reference;
    std::uint8_t action{0};
    std::uint8_t result{0};
};
std::optional<InteractionResult> read_interaction_result(std::span<const std::uint8_t> body);

/** @return The facet target of the Container of the entity GlobalStorage of the own character (the rule of 4.2: the instance
 *  id (entity XOR root XOR G) + the component id). */
std::uint64_t global_storage_target(std::uint64_t group);
/** @return The body of TakeAllItems (2939, 48 bytes): target (G + PlayerTrade) entity (G) and the reference of the own
 *  GlobalStorage Container (the own actor, G, its facet target), not the bag (live check L5 of the backend stage 3.5b.2). */
Bytes take_all_items_body(const ActorId& actor);
/** @return The body of TakeItem (2942, 68 bytes): the same, then the item instance (16 bytes) and the quantity (1 to 10,000). */
Bytes take_item_body(const ActorId& actor, const std::array<std::uint8_t, 16>& instance, std::uint32_t quantity);

/** The requests of the bag and of the equipment (stage P2.4). An item instance id: 16 bytes (zero: no item). */
using ItemInstanceId = std::array<std::uint8_t, 16>;
/** @return The facet target of a component of the entity Inventory of the own character (the rule of 4.2). */
std::uint64_t inventory_target(std::uint64_t group, std::uint64_t component);
/** @return The body of ServerEquipItem (3895, 45 bytes): the stack of a bag slot into a paperdoll slot (the full stack). */
Bytes equip_item_body(std::uint64_t group, std::int32_t bagSlot, std::uint32_t paperdollSlot, std::uint32_t quantity, const ItemInstanceId& instance);
/** @return The body of ServerUnequipItem (3896, 29 bytes): the item of a paperdoll slot into a bag slot (-1: the first free slot). */
Bytes unequip_item_body(std::uint64_t group, std::uint32_t paperdollSlot, std::int32_t bagSlot, std::uint32_t quantity);
/** @return The body of ServerSwapItems (3900, 57 bytes): the slot of the drop, the slot of the drag, the instance of each slot. */
Bytes swap_items_body(std::uint64_t group, std::uint32_t slotTo, std::uint32_t slotFrom, const ItemInstanceId& instanceTo,
                      const ItemInstanceId& instanceFrom);
/** @return The body of ServerUseItem (2917, 20 bytes): the use of the item of a bag slot (the target: the Container of the bag). */
Bytes use_item_body(std::uint64_t group, std::uint32_t bagSlot);
/** One entry of a drop of bag items. */
struct DropItemEntry {
    std::uint32_t slot{0};
    ItemInstanceId instance{};
    std::uint32_t quantity{0};
};
/** @return The body of DropInventoryItems (1838; 17 + 28 bytes for each entry; the target: the ItemDrop component of the root). */
Bytes drop_inventory_items_body(std::uint64_t group, const std::vector<DropItemEntry>& entries);
/** @return The body of DropPaperdollItem (826, 24 bytes). */
Bytes drop_paperdoll_item_body(std::uint64_t group, std::uint32_t paperdollSlot, std::uint32_t quantity);
/** @return The coin of field 0 of a Currency state (u64, hundredths of a coin), or no value. */
std::optional<std::uint64_t> read_currency(std::span<const std::uint8_t> field);

/** One claim of the LootTracker map (58 bytes; protocol notes 4.41): a chest or a bag of the character and its state. */
struct LootClaim {
    ActorId actor{};
    /** InstancedLootState: 0 Initial, 1 Rolled, 2 Looted, 3 RegeneratedForMission, 4 NotOnRequiredQuest, 5 Locked, 6 Unlocked. */
    std::uint32_t state{0};
    /** InstancedLootType: 1 AILoot (the bag of a kill), 5 Discarded. */
    std::uint32_t kind{0};
    std::uint64_t resetTime{0};
    std::uint64_t lootedTime{0};
    bool privileged{false};
    WorldPosition place;
    std::uint32_t tag{0};
    std::uint8_t selector{0};
};
inline constexpr std::uint32_t kLootRolled = 1;
inline constexpr std::uint32_t kLootLooted = 2;
/** @return The claims of field 0 of a LootTracker state (the full form of the map), or no value. */
std::optional<std::vector<LootClaim>> read_loot_tracker(std::span<const std::uint8_t> field);

/** One key of the GlobalStorage map: the actor of a storage (a loot bag) and its items. */
struct StorageKey {
    ActorId actor{};
    std::vector<soulwarden::backend::tests::WireItem> items;
};
/** @return The keys of field 0 of a GlobalStorage state (the full form of the map), or no value. */
std::optional<std::vector<StorageKey>> read_global_storage(std::span<const std::uint8_t> field);
/** @return The bytes of field 0 of an Achievement state (the full form of the byte vector), or no value. */
std::optional<std::vector<std::uint8_t>> read_bit_vector(std::span<const std::uint8_t> field);
/** @return True when the bit `index` of a bit vector is set: byte index >> 3, mask 1 << (index & 7) (backend write_bits). */
bool has_bit(const std::vector<std::uint8_t>& bytes, std::uint32_t index);

/** The Gathering state of the owner (type 81, slot 36; group 1, big-endian numbers; backend chest_states.cpp): the progress ring of
 *  the prompt of a gather or an open runs remaining / speed seconds; Done (the completion) and Failure (a cancel of the server) end
 *  the use of the client (it sends 2473: live check L3 of the backend stage 3.5b.1). */
struct GatheringState {
    std::uint8_t status{0};
    float remaining{0.0F};
    float speed{1.0F};
    std::uint8_t gatherers{0};
    std::uint32_t reference{0};
    bool active{false};
    /** m_gatherableGDEID: the G of the chest or of the node (0x00000000FFFFFFFF: none). */
    std::uint64_t target{0x00000000FFFFFFFFULL};
    std::uint32_t syncReference{0};
};
inline constexpr std::uint8_t kGatherNone = 0;
inline constexpr std::uint8_t kGatherSuccess = 1;
inline constexpr std::uint8_t kGatherFailure = 2;
inline constexpr std::uint8_t kGatherDone = 3;
/**
 * Applies a Gathering state with the rule of the client (apply 0x4905130): fields 0 to 3 only when field 4 (the reference) changes,
 * fields 6 and 7 only when field 8 (the sync reference) changes. @return True when the state changed.
 */
bool apply_gathering(const soulwarden::backend::tests::Fragment& fragment, GatheringState& state);

/** The elements of a Container (protocol notes 4.10): one element for each slot; the full vector replaces them, a journal
 *  sets elements (at the length of the vector: a new element at the end). @return False when the value has no form. */
bool apply_container(std::span<const std::uint8_t> field, std::vector<soulwarden::backend::tests::WireItem>& elements);

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
    /** List 2: the shots of this step (an entry has only its own items). */
    std::vector<ShotItem> shots;
    /** List 3: the melee contacts of this step (an entry has only its own items). */
    std::vector<MeleeContactItem> contacts;
    /** The interaction block of mask bit 8 (from its present byte): the request that is current at this step. An entry with
     *  no request after a newer entry with one has bit 8 with the present byte 0 (else the reader gives it the newer block). */
    std::optional<Bytes> interaction;
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
