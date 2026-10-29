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
#include "Net/Codec/SwcWorldTime.h"

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

/**
 * A shot item that a step of the player asks for (stage P3.6): the attack, the row, the charge, and the ammunition (CRC-32 values),
 * and the origin: `forward` m along the look from the reported position of the character, `up` m above its feet.
 */
struct ShotRequest {
    std::uint32_t attack{0};
    std::uint32_t row{0};
    float charge{0.0F};
    std::uint32_t ammo{0};
    float forward{0.0F};
    float up{1.5F};
};

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
         * Stage P3.5: it adds the attack of each shot of a cast of an ability at this step (`shots`: the progression id of its
         * RangedAttackName); the shot item goes into list 2 of the entry with the origin 1.5 m above the character and the
         * direction of the look (protocol notes 4.27, the shot of Gravity Well). Stage P3.6: each shot has its row, its charge, its
         * ammunition, and its origin rule (ShotRequest); the direction goes to the aim point of the camera when the motion has one.
         */
        std::function<void(std::uint16_t input, std::int64_t steadyNs, std::vector<unsigned>& actionBits, std::vector<ShotRequest>& shots)> inputStep;
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
        /** Fields 2, 3, and 5 of the stamina state: the winded time and the delay of the regeneration that were left when the
         *  server wrote it (s), and the multiplier of the regeneration; the steady time of its arrival (ns) and a count. */
        float staminaWinded{0.0F};
        float staminaRegenDelay{0.0F};
        float staminaRegenMultiplier{1.0F};
        std::int64_t staminaAtNs{0};
        std::uint32_t staminaRevision{0};
        /** The mana state (slot 47, type 1652, fields 0 and 1, f32). The mana meter of the retail HUD reads this component.
         *  `manaMax` is the base maximum: the current value can be larger (the client maximum has the MaxMana parts of the
         *  attributes, protocol notes 4.5). */
        bool hasMana{false};
        float mana{0.0F};
        float manaMax{0.0F};
        /** Fields 2 and 3 of the mana state (stage P3.6): the delay of the regeneration that was left (s) and the rate (mana/s), the
         *  steady time of its arrival (ns), and a count. */
        float manaDelay{0.0F};
        float manaRate{0.0F};
        std::int64_t manaAtNs{0};
        std::uint32_t manaRevision{0};
        /** Message 4289 (the prediction sync): the four values of the server (the rate is the factor 1 + the ManaRate parts), the steady
         *  time of its arrival, and a count. The retail client copies them into its predicted mana. */
        struct ManaSync {
            float value{0.0F};
            float maximum{0.0F};
            float delay{0.0F};
            float factor{1.0F};
            std::int64_t atNs{0};
        };
        ManaSync manaSync;
        std::uint32_t manaSyncRevision{0};
        /**
         * The heal numbers (stage P3.6; protocol notes 4.31): each owner vitals state with bit 1 of healthChangeFlags (group 0, field
         * 3) gives a green "+N", N = the new health less the health that the client had; and each message 4298 (a heal of a different
         * character by the owner) with the Stats id of the healed character. The newest at the back (at most 16), and a count.
         */
        struct Heal {
            float amount{0.0F};
            /** 0: the own character; else the Stats id of the healed character (4298). */
            std::uint64_t healed{0};
            std::int64_t atNs{0};
        };
        std::vector<Heal> heals;
        std::uint32_t healRevision{0};
        /** The damage that the own character took (stage WF7; message 3601, read_damage_taken): the newest at the back (at most
         *  16) with the steady time of its arrival (ns), and a count. The UI shows its red numbers. */
        struct Damage {
            DamageTaken message;
            std::int64_t atNs{0};
        };
        std::vector<Damage> damages;
        std::uint32_t damageRevision{0};
        /** The vitals state: StaminaAmount and StaminaMax (group 0, fields 1 and 6), ManaAmount and ManaMax (group 1,
         *  fields 0 and 3), and bit 0 of vitalsData (group 0, field 2): the character is dead. */
        float vitalsStamina{0.0F};
        float vitalsStaminaMax{0.0F};
        float vitalsMana{0.0F};
        float vitalsManaMax{0.0F};
        bool dead{false};
        /** The life and the travel of the character (stage P2.10; protocol notes 4.16, 4.18, 4.20):
         *  - the loading screen of the client: from the respawn request (1938) or message 3951 to message 3954;
         *  - the death notices (3950) with the time to the respawn screen and the steady time of the last one (ns);
         *  - the travels: one more at each movement state of the server whose teleport number (the low 4 bits of field 31) is not
         *    the number of the client (the client then takes the place of the state: the world entry, a travel, a respawn);
         *  - the counts of the messages 3954, 3746 (the server ends the interaction), and 6709 (the end of a lock of the input). */
        bool loadingScreen{false};
        std::uint32_t deathNotices{0};
        float respawnScreenTime{0.0F};
        std::int64_t deathNoticeNs{0};
        std::uint32_t travelRevision{0};
        std::uint32_t teleportNotices{0};
        std::uint32_t interactionCancels{0};
        std::uint32_t lockEnds{0};
        /** The grants of a cutscene (6707) and the CRC of the last one (AST-239). */
        std::uint32_t cutsceneGrants{0};
        std::uint32_t lastCutsceneGrant{0};
        /** Stage Q5 (AST-240): the cancels of a pending cutscene by the server (6708), the whispers of the server (5070: the count and
         *  the CRC-32 of the lower-case WhisperId of the last kWhisperHistory ones, oldest first), and the cinematic reports (6606, 6542). */
        std::uint32_t cutsceneCancels{0};
        std::uint32_t whisperStarts{0};
        std::vector<std::uint32_t> whispers;
        std::uint32_t cinematicStarts{0};
        std::uint32_t cinematicEnds{0};
        /** The interaction request of the client (the block of the input entries) and the replies 3743 to it. */
        std::optional<InteractionRequest> interaction;
        std::uint32_t interactionResults{0};
        std::uint8_t lastInteractionResult{0};
        std::uint8_t lastInteractionAction{0};
        /** The crafting (stage MVP-J; protocol notes 4.50): the last progress 2993 of a batch (the recipe id, the completed jobs, the
         *  jobs of the batch; the stop form: no recipe, 0 of 0) and the count of the progress messages. */
        std::string craftRecipe;
        std::uint32_t craftCompleted{0};
        std::uint32_t craftTotal{0};
        std::uint32_t craftProgresses{0};
        /** The answers 2858 to the tree resets of the client (stage MVP-G): their count, the last success and table index. */
        std::uint32_t respecAnswers{0};
        bool lastRespecSuccess{false};
        std::uint32_t lastRespecTable{0};
        /** The loot of the owner (stage P2.10; protocol notes 4.10, 4.41, 4.42), each with a count of its changes:
         *  - the bag of the character (the Container of slot 61): one element for each slot (an empty slot: quantity 0);
         *  - the LootTracker (slot 33): the claims of the chests and of the bags (Rolled shows a bag, Looted hides it);
         *  - the selected Container (slot 76): the items of the open bag (the panel);
         *  - the GlobalStorage map (slot 77): the items of each bag near the character (the list under its prompt). */
        std::vector<soulwarden::backend::tests::WireItem> bag;
        std::uint32_t bagRevision{0};
        std::vector<LootClaim> lootClaims;
        std::uint32_t lootRevision{0};
        std::vector<soulwarden::backend::tests::WireItem> selectedContainer;
        std::uint32_t selectedRevision{0};
        std::vector<StorageKey> globalStorage;
        std::uint32_t storageRevision{0};
        /** The Achievement state (slot 66): the byte vector of the achievements (a read lore page sets its bit; stage P4.1 part 2). */
        std::vector<std::uint8_t> achievements;
        std::uint32_t achievementRevision{0};
        /** The Gathering state (slot 36): the gather or the open that runs (stage P4.1 part 2). One more revision at each change. */
        GatheringState gathering;
        std::uint32_t gatheringRevision{0};
        /** The world time (protocol notes 4.43, Net/Codec/SwcWorldTime.h): the record of the last 7036 of the own Time facet (no
         *  value before it), and the phase of the last 7036 or 4247. One more revision at each record. */
        std::optional<TimeRecord> timeRecord;
        std::uint32_t timeRevision{0};
        std::uint32_t dayPhase{kPhaseDay};

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
        /**
         * The learned nodes of the weapon trees (the ProgressionPoint state, slot 67, type 3681; stage P3.5): the node id (the
         * CRC-32 of the lower-case node id text) of each point entry with a level above 0. A state has each pool and each node
         * (the backend never cuts it), thus each state replaces the list. `nodesRevision` counts the states.
         */
        std::vector<std::uint32_t> learnedNodes;
        std::uint32_t nodesRevision{0};
        /**
         * The speed table of the owner (StatMultiplierTable, slot 43, type 1525; stage P3.3, protocol notes 4.24): the multiplier of
         * each key of a full table (0x34 MoveSpeedMod, 0x35 SprintSpeedMod, 0x0B StaminaRate, 0x4F CooldownReduction); an absent key
         * is 1.0, and an empty table is all 1.0. `speedRevision` counts the tables.
         */
        std::map<std::uint8_t, float> speedTable;
        std::uint32_t speedRevision{0};
        /**
         * The status map of the owner (StatusEffects, slot 44, type 4236, group 1 field 0, the full map; stage P3.3, protocol notes
         * 4.24): each status with its index in the status rows, its end on the synced wall clock (Unix ns), its potency, and its
         * stacks. The server sends the map again at each change and at the end of a status. `statusRevision` counts the maps.
         */
        struct Status {
            std::uint16_t index{0};
            std::uint64_t wallEndNs{0};
            float potency{1.0F};
            std::uint8_t stacks{0};
        };
        std::vector<Status> statuses;
        std::uint64_t statusStamp{0};
        std::uint32_t statusRevision{0};

        /** The bag and the equipment screens (stage P2.4; protocol notes 4.10, 4.11):
         *  - the full items of the paperdoll (owner view): paperdoll slot -> item (instance, durability, gear score, perks); a slot
         *    that loses its item goes away; one more at each change;
         *  - the coin (slot 60, Currency state, field 0): hundredths of a coin;
         *  - the refusals of a request of the equipment (message 3626): a count. */
        std::map<std::uint32_t, soulwarden::backend::tests::WireItem> paperdollItems;
        std::uint32_t paperdollRevision{0};
        bool hasCoin{false};
        std::uint64_t coin{0};
        std::uint32_t equipFails{0};
        /** The Progression state (slot 70): the level of the wire (the level of the screen minus 1) and the experience. */
        bool hasProgression{false};
        std::uint32_t level{0};
        std::uint32_t experience{0};

        /**
         * The attributes (stage MVP-A; the Attribute state, slot 27, type 129; protocol notes 4.12, backend attribute_state.cpp). The
         * index is the CharacterAttributeType of the client: 0 Constitution, 1 Focus, 2 Strength, 3 Dexterity, 4 Intelligence.
         * `base` (field 0) has the minimum 5 and the spent points, no bonus; `bonus` (field 1, the map attributeBonuses) is the part
         * of the equipment, the food, and the perks (an absent type is 0); `spent` and `unspent` come from field 3. Each fragment has
         * the four fields (the facet of the client copies them all, apply 0x2788BE0): each state replaces the values.
         */
        struct Attributes {
            std::array<std::uint32_t, 5> base{};
            std::array<std::uint32_t, 5> bonus{};
            std::array<std::uint32_t, 5> spent{};
            std::uint32_t unspent{0};
        };
        bool hasAttributes{false};
        Attributes attributes;
        std::uint32_t attributeRevision{0};
        /**
         * The rank and the experience of each weapon mastery and trade skill (the CategoricalProgression state, slot 65, type 911;
         * protocol notes 4.13, backend skill_states.cpp): the progression id (the CRC-32 of the lower-case id text) to the rank and
         * the points. The facet of the client (apply 0x3CB7170) makes its map again from each fragment: an id that is not in the
         * three lists has the rank 0. A state with a smaller stamp changes nothing.
         */
        struct Rank {
            std::uint16_t rank{0};
            std::uint64_t points{0};
        };
        std::map<std::uint32_t, Rank> ranks;
        std::uint64_t rankStamp{0};
        std::uint32_t rankRevision{0};
        /** The point pools of the weapon trees (the ProgressionPoint state, slot 67, fields 0 and 1): the pool id (the CRC-32 of the
         *  mastery table) to its free points. The client removes a pool that is not in the lists: each state replaces the map. */
        std::map<std::uint32_t, std::uint16_t> pointPools;
        /**
         * The hit counts of the tracked abilities (the ability state, slot 46, fields 1 and 2 hitDataNumHits and hitDataAbilityIds;
         * backend skill_states.cpp, stage 3.3b-M2): the ability id to its hits. The action lists test them (IfAbilityNumHits). Two
         * parallel lists in the full form: a state with the two fields replaces the map.
         */
        std::map<std::uint32_t, std::uint8_t> abilityHits;
        std::uint32_t hitRevision{0};
        /** The heart rune charge (the ChargeComponent state, slot 48, type 5257; protocol notes 4.23): field 0, the whole percent 0 to
         *  100. The HUD fills the icon of the rune from it (apply 0x683AB80: the maximum times the percent times 0.01). */
        bool hasCharge{false};
        std::uint8_t chargePercent{0};
        std::uint32_t chargeRevision{0};
        /**
         * The craft state (the Crafting state, slot 63, type 205; protocol notes 4.51, backend crafting_state.cpp): field 0, the
         * cooldown window of each recipe (the CRC-32 of the lower-case recipe id, the crafts in the window, the end on the synced wall
         * clock in Unix ns); field 1, the craft gear score bonus of each trade skill (the TradeskillType code, Weaponsmithing 0). Two
         * maps in the full form: each state replaces both.
         */
        struct CraftCooldown {
            std::uint32_t recipe{0};
            std::uint8_t used{0};
            std::uint64_t wallEndNs{0};
        };
        std::vector<CraftCooldown> craftCooldowns;
        std::map<std::uint8_t, std::uint16_t> craftGearBonus;
        std::uint32_t craftRevision{0};
        /** The entitlements of the account (the Entitlement state, slot 19, type 3133; backend ownership_states.cpp): field 0, the byte
         *  vector (bit index & 7 of byte index >> 3 is the entitlement with the EntitlementIndex `index`), field 2, the mark
         *  "received". */
        std::vector<std::uint8_t> entitlements;
        bool entitlementsReceived{false};
        std::uint32_t entitlementRevision{0};
        /**
         * The quests (AST-236; the Objectives state, slot 22, type 3857; protocol notes 4.53, backend objectives_state.cpp): group 1, field 0
         * the task starts (instance, index -> the synced wall clock in Unix ns), field 1 the tracked instances (8 at most), field 3 the
         * active objectives (56-byte elements), field 4 the task elements (18 bytes) in the index order of each tree. Each field in the full
         * form replaces its list; a field that a state does not have keeps its list. The instances are full u64 values.
         */
        struct QuestActive {
            std::uint32_t objective{0};
            std::uint64_t instance{0};
            std::uint32_t mission{0};
            std::uint64_t acceptTime{0};
            std::uint16_t originTerritory{0};
            bool available{false};
            bool communityGoal{false};
            bool poiObjective{false};
            bool dynamicPoiObjective{false};
            std::uint64_t parent{0};
            bool resetObjective{false};
        };
        struct QuestTask {
            std::uint64_t instance{0};
            std::uint8_t index{0};
            std::uint32_t task{0};
            std::uint32_t progress{0};
            std::uint8_t state{0};
        };
        struct QuestTaskStart {
            std::uint64_t instance{0};
            std::uint8_t index{0};
            std::uint64_t startNs{0};
        };
        bool hasObjectives{false};
        std::vector<QuestTaskStart> questStarts;
        std::vector<std::uint64_t> questTracked;
        std::vector<std::uint64_t> questCompleted;
        std::vector<QuestActive> questActive;
        std::vector<QuestTask> questTasks;
        std::uint64_t questSequence{0};
        std::uint32_t questRevision{0};
        /** The encounter roots of the character (QG5; group 1 field 5: a full vector of the entity group ids G of the roots; protocol
         *  notes 4.62 "The link of a nearby event"). A state with no field 5 keeps the list; an empty field 5 clears it. */
        std::vector<std::uint64_t> encounterLinks;
        /** The pin readiness (the SeasonsRewardsTrackedStat state, slot 73, type 5606): group 0 field 0 `initialized`. */
        bool pinReady{false};
        std::uint32_t pinReadyRevision{0};
    };

    /** An entity of a different interest number: its metadata and its position. */
    struct OtherEntity {
        ActorId actor{};
        ActorId sliceAsset{};
        std::uint32_t sliceSubId{0};
        bool hasPosition{false};
        WorldPosition position;
        /** The place of an object of the server (a fixture, a loot bag; stage P2.10): the PositionInTheWorld state (type 13),
         *  field 0 x:f32 y:f32 height:u16 over -100 m to 2000 m (backend server/rep/states/object_states.cpp). */
        bool hasPlace{false};
        WorldPosition place;
        /** The rotation of the object (PositionInTheWorld, field 1): the server turns the bounds of its slice with it. */
        Quaternion placeRotation;
        /** An encounter root (QG5; protocol notes 4.62): the manager status (6786), the last stage entry of each stage slot (2133: the
         *  entry of the largest signed sequence), and the root Objectives (3857 at the objectives slot of the slice: field 0 the task
         *  starts, 3 the instances, 4 the task elements; the reader of the own Objectives).
         *  No std::map here: a TArray of the snapshot (FSwcRepConnection::FSnapshot::Objects) moves its elements with memcpy, and the
         *  tree of a libc++ map points into the object (a crash in the copy of the snapshot after the array grew; ut567, 2026-10-29). */
        bool hasEncounterStatus{false};
        std::uint32_t encounterStatus{0};
        struct StageEntry {
            std::uint32_t slot{0};
            std::uint32_t sequence{0};
            std::uint32_t status{0};
        };
        std::vector<StageEntry> encounterStages;
        const StageEntry* encounter_stage(std::uint32_t slot) const {
            for (const StageEntry& entry : encounterStages) {
                if (entry.slot == slot) {
                    return &entry;
                }
            }
            return nullptr;
        }
        bool hasRootObjectives{false};
        std::vector<OwnEntity::QuestTaskStart> rootStarts;
        std::vector<OwnEntity::QuestActive> rootActive;
        std::vector<OwnEntity::QuestTask> rootTasks;
        std::uint32_t encounterRevision{0};
        /** The VariationData of a start object (14): the row index of the variation table of its slice. */
        bool hasVariation{false};
        std::uint16_t variation{0};
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
        /** The shots in list 2 of the input entries (stage P3.5). */
        std::uint64_t shots{0};
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
        /** The distance to the ground below the feet in meters while the character is in the air or swims; negative on the ground. */
        float airHeight{-1.0F};
        /** The depth class of the feet (0 None to 3 Deep; stage WF5): the field of mask bit 3 when it is above None. */
        std::uint8_t depthClass{0};
        /** The action bits of the last key press (a weapon key): one entry has them, at each new serial. */
        std::uint32_t pressSerial{0};
        std::vector<unsigned> pressBits;
        /** The point of the crosshair (the camera ray to the first hit or to the range of the weapon; stage P3.6): the shot items go to it. */
        bool hasAimPoint{false};
        WorldPosition aimPoint;
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
    /**
     * The respawn request of the death screen (stage P2.10; protocol notes 4.18): message 1938 with the row "Random Location"
     * (random 1, no id). The client shows its loading screen before it sends (loadingScreen until message 3954).
     */
    void request_respawn(bool random = true, const std::string& id = {});
    /**
     * The interaction request of the player (protocol notes 4.35, 4.36): its block goes into each input entry while it is current.
     * A new request replaces the request before it. std::nullopt ends it with no message (the use completed, or a refusal).
     */
    void set_interaction(const std::optional<InteractionRequest>& request);
    /** The client ends its committed interaction: the block goes away and message 2473 names its reference. */
    void cancel_interaction();
    /** The end of a cutscene of the client (a conversation; stage P4.8): message 6705. The server answers 6709 (lockEnds). */
    void end_cutscene();
    /** The craft request of the crafting screen (stage MVP-J; protocol notes 4.50): message 2997. The server answers with the item
     *  records and the progress 2993, or with the stop form (a refusal). */
    void request_craft(const CraftRequest& request);
    /** The stop of a batch by the client (the cancel of the crafting screen): message 2999. The completed jobs stay. */
    void cancel_craft();
    /** The quests (AST-236; protocol notes 4.53, 4.54): the auto-pin flags (2709; the backend sends the Objectives state and the pin
     *  readiness only after the first one), the pinned list (1458), the open of a UI screen (2617), abandon (3836), reset (5600). */
    void enable_objectives(const std::vector<bool>& flags);
    void update_tracked_objectives(const std::vector<std::uint64_t>& instances);
    void open_ui_screen(std::uint32_t screen);
    void abandon_objective(std::uint64_t instance);
    void reset_objective(std::uint64_t instance);
    /** The place cues of a point of interest (AST-241; research poi-cues.md): 2297 at an entry (achievement: the CRC-32 of the full
     *  achievement name, 0 for none) and 6416 at an exit (no achievement), on the TerritoryDetector facet. */
    void territory_report(const std::array<std::uint8_t, 16>& actor, std::uint64_t group, std::uint64_t local, std::uint16_t territory,
                          std::optional<std::uint32_t> achievement);
    /** The topics of a person conversation (AST-248): the selected topic (3158) and the finished topics after the close 2473 (5397). */
    void conversation_topic(std::uint32_t topic);
    void finished_topics(const std::vector<std::uint32_t>& topics);
    /** The offer and the hand-in at a person (AST-239): 3772 and 794 with the CRC-32 of the lower-case ObjectiveID. */
    void accept_objective(std::uint32_t objective);
    void complete_objective(std::uint32_t objective);
    /** The cutscene of a quest task (AST-239; only the check aid of the development runs uses them before stage Q5): the request 6703
     *  (the server answers 6707: cutsceneGrants) and the start 6706. The end is end_cutscene (6705). */
    void request_cutscene(std::uint32_t cutscene);
    void cutscene_started(std::uint32_t cutscene);
    /** The start (6606) and the end (6542; also at a skip) of a cinematic video (stage Q5): the CRC-32 of the lower-case cinematic id. */
    void cinematic_started(std::uint32_t cinematic);
    void cinematic_ended(std::uint32_t cinematic);
    /** The whispers that OwnEntity::whispers keeps (the host reads the new ones by the count whisperStarts). */
    static constexpr std::size_t kWhisperHistory = 32;
    /** Copies an Objectives state (slot 22) into an own entity: false for a fragment that does not have its form (no change). */
    static bool read_objectives(const soulwarden::backend::tests::Fragment& fragment, OwnEntity& own);
    /** The states of an entity of a different interest number (apply_other; public for the tests of the encounter states). */
    static void apply_other_fragments(const soulwarden::backend::tests::Record& record, OtherEntity& entity);
    /** True after the first 2709 of this session. */
    [[nodiscard]] bool objectives_enabled() const { return m_objectivesEnabled; }
    /** The edits of the weapon trees and the bars (stage MVP-G; protocol notes 4.13): a point of a node (1112, count 1), the ability
     *  of a place of a bar (2701; the slot 0 to 2), the end of a point change (2857), a tree reset (186; the answer 2858). */
    void assign_point(std::uint32_t pool, std::uint32_t node);
    void change_bar(std::uint16_t tableIndex, std::uint32_t slot, std::uint32_t ability);
    void apply_points();
    void respec_tree(std::uint16_t tableIndex);
    /** The take of all the items of the open bag (2939), and of one item instance with a quantity (2942): the server moves them into
     *  the bag of the character; the answer is the states of the slots 61, 76, 77, and 33 (protocol notes 4.42). */
    void take_all_items();
    void take_item(const std::array<std::uint8_t, 16>& instance, std::uint32_t quantity);
    /** The requests of the bag and of the equipment (stage P2.4; protocol notes 4.10, 4.11, 4.23): equip the stack of a bag slot,
     *  unequip a paperdoll slot (bag slot -1: the first free slot), swap two paperdoll slots, use the item of a bag slot, drop bag
     *  items or the item of a paperdoll slot. The answer is the new states of the slots 61 and 62, or message 3626 (equipFails). */
    void equip_item(std::int32_t bagSlot, std::uint32_t paperdollSlot, std::uint32_t quantity, const ItemInstanceId& instance);
    void unequip_item(std::uint32_t paperdollSlot, std::int32_t bagSlot, std::uint32_t quantity);
    void swap_items(std::uint32_t slotTo, std::uint32_t slotFrom, const ItemInstanceId& instanceTo, const ItemInstanceId& instanceFrom);
    void use_item(std::uint32_t bagSlot);
    void drop_items(const std::vector<DropItemEntry>& entries);
    void drop_paperdoll_item(std::uint32_t paperdollSlot, std::uint32_t quantity);
    /** The open of a loot container item of a bag slot (1733, stage Q7; protocol notes 4.57): the quantity 1 for Open. The answer is the
     *  bag state with the cause OpenRewardChest (26). */
    void open_container_item(std::uint32_t bagSlot, const ItemInstanceId& instance, std::uint32_t quantity);
    /** The tests: the body of a ReplicatedStateBundle message (8) as if it came in now (tests/net/item_tests.cpp gives the states of
     *  the encoders of the backend this way). */
    void apply_bundle_for_tests(std::span<const std::uint8_t> body, Clock::time_point now) { on_bundle(body, now); }
    /** Applies one message of the server (its type and body) as from a carrier frame (stage P3.6: 4289, 4298). For the tests. */
    void apply_message_for_tests(std::uint32_t type, std::span<const std::uint8_t> body, Clock::time_point now) {
        on_message(ServerMessage{type, Bytes(body.begin(), body.end())}, now);
    }
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
    /** @return The position field of the last input entry, or none (the last entry had no position field). */
    [[nodiscard]] std::optional<WorldPosition> reported() const {
        return m_hasReported ? std::optional<WorldPosition>(m_reported) : std::nullopt;
    }
    /**
     * @return The position field of the next input entry for a position of the character: the last field one step nearer to the
     * character (at the limits of the steps), or the character with no last field or after a travel. The server takes the
     * position of a report before the interaction request of the same report: the range of a request is from this position.
     */
    [[nodiscard]] static WorldPosition next_reported(const std::optional<WorldPosition>& last, const WorldPosition& character);
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
    /** The record 7036 and the phase 4247 of the own Time facet (protocol notes 4.43). */
    void on_world_time(const ServerMessage& message);
    void apply_own(const soulwarden::backend::tests::Record& record, Clock::time_point now);
    /** @return The values of an attribute state (slot 27), or none for a fragment that does not have its form. */
    static std::optional<OwnEntity::Attributes> read_attributes(const soulwarden::backend::tests::Fragment& fragment);
    /** Copies a crafting state (slot 63) into the own entity: false for a fragment that does not have its form (no change). */
    bool apply_crafting(const soulwarden::backend::tests::Fragment& fragment);
    bool m_objectivesEnabled{false};
    void apply_other(const soulwarden::backend::tests::Record& record);
    void send_registration();
    void send_input(Clock::time_point now);
    /** Sends a component call on the reliable channel (the route: the own actor). */
    void send_call(const MessageType& type, const Bytes& body);
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
    /** The teleport number of the client: the low 4 bits of field 31 of the last movement state that it took (0 at the start). */
    std::uint8_t m_clientTeleport{0};
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
    /** The request 4248 went out (one time for each world entry of the session). */
    bool m_timeRequested{false};
};

} // namespace swc::net
