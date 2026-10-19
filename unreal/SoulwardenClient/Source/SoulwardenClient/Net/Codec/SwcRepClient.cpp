#include "Net/Codec/SwcRepClient.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

namespace swc::net {
namespace {

namespace decoder = soulwarden::backend::tests;

/** The route of the registration request: the backend reads no route for it. */
constexpr ActorId kNoRoute{};
/** The entries of one input report: the newest one and the 4 before it (a live client, protocol notes 4.7). */
constexpr std::size_t kEntriesPerReport = 5;
/** The action bits of an entry with no input after the loading screen (protocol notes 4.7, the captures). */
const std::vector<unsigned> kRestActionBits{475, 497, 506, 569};
/** The action bits of the forward key W while it is down (protocol notes 4.7, the captures; the backend reads bit 14). */
const std::vector<unsigned> kForwardActionBits{14, 43, 416, 542};
/** The action words of an entry in the world. */
constexpr unsigned kActionWords = 19;
/** The movement permissions and the ground distance of an entry of a client that stands (backend tests, report_entry). */
constexpr int kRestPermissions = 15;
constexpr int kRestGroundDistance = 180;
/** The camera of a character at rest, at the start of a session (protocol notes 4.7): 4.0 m behind in the direction of
 *  the camera (north, no pitch), 0.5 m to the right, 1.65 m above the feet. */
constexpr float kCameraRight = 0.5F;
constexpr float kCameraBehind = 4.0F;
constexpr float kCameraUp = 1.65F;
/** The ground value of an entry in a fall (protocol notes 4.7: "0 to 10; 10 in a fall"): a half float, 10 at most. */
constexpr float kMaxAirHeight = 10.0F;
/**
 * The steps of the position field. The game thread moves the character one time in each frame: at a frame rate below 30
 * frames in a second, two entries one step apart can have the move of a whole frame (a sprint of 5.5 m/s in a frame of
 * 0.12 s: 0.66 m in one step, 19.8 m/s; the world level check had 46 reports refused as too fast at 11 frames in a second).
 * Each entry moves the reported position to the character at these speeds at most (m/s): after a long frame the reports
 * follow the path of the character some steps later. A larger distance (a travel) goes at once. Decisions of an agent,
 * below the limits of the backend (world/position_report.h: 20 m/s for one step horizontal and up, 120 m/s down).
 */
constexpr float kStepSeconds = 1.0F / 30.0F;
constexpr float kReportHorizontalSpeed = 10.0F;
constexpr float kReportRiseSpeed = 10.0F;
constexpr float kReportFallSpeed = 60.0F;
constexpr float kReportJump = 20.0F;
/** @return The value of a half float (IEEE 754 binary16). */
float half_value(std::uint16_t bits) {
    const std::uint32_t sign = (bits & 0x8000U) << 16;
    const std::uint32_t exponent = (bits >> 10) & 0x1FU;
    const std::uint32_t mantissa = bits & 0x3FFU;
    float value = 0.0F;
    if (exponent == 0) {
        value = std::ldexp(static_cast<float>(mantissa), -24);
    } else if (exponent == 31) {
        value = mantissa == 0 ? std::numeric_limits<float>::infinity() : std::numeric_limits<float>::quiet_NaN();
    } else {
        value = std::ldexp(static_cast<float>(mantissa | 0x400U), static_cast<int>(exponent) - 25);
    }
    return sign != 0 ? -value : value;
}

/** @return The bits of a half float (IEEE 754 binary16) for a value from 0 to 65504; the low bits are cut. */
std::uint16_t half_bits(float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    const std::int32_t exponent = static_cast<std::int32_t>((bits >> 23) & 0xFFU) - 127 + 15;
    const std::uint32_t mantissa = bits & 0x7FFFFFU;
    if (value <= 0.0F || exponent < -10) {
        return 0;
    }
    if (exponent <= 0) {
        // A subnormal half float.
        return static_cast<std::uint16_t>((mantissa | 0x800000U) >> (14 - exponent));
    }
    if (exponent >= 31) {
        return 0x7BFF;
    }
    return static_cast<std::uint16_t>((static_cast<std::uint32_t>(exponent) << 10) | (mantissa >> 13));
}

std::uint64_t read_be(std::span<const std::uint8_t> value) {
    std::uint64_t out = 0;
    for (const std::uint8_t byte : value) {
        out = (out << 8) | byte;
    }
    return out;
}

std::string text_at(std::span<const std::uint8_t> value, std::size_t skip) {
    if (value.size() < skip) {
        return {};
    }
    Reader reader(value.subspan(skip));
    return reader.string();
}

float f32_at(std::span<const std::uint8_t> value) {
    Reader reader(value);
    return reader.f32();
}

std::int64_t steady_ns(Clock::time_point now) {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
}

/** Reads a list of a cooldown map in the full form: 00, stamp, count, entries of 20 bytes (protocol notes 4.22). */
template <class Entry>
bool read_cooldown_list(std::span<const std::uint8_t> value, std::vector<Entry>& out, Entry (*read)(Reader&)) {
    Reader reader(value);
    if (reader.size() != 0) {
        return false;
    }
    if (reader.u8() == 1) {
        reader.size64();
    }
    const std::uint32_t count = reader.size();
    if (!reader.ok() || count > 1000) {
        return false;
    }
    out.clear();
    for (std::uint32_t index = 0; index < count && reader.ok(); ++index) {
        out.push_back(read(reader));
    }
    return reader.ok();
}

std::uint64_t slot_map(std::span<const std::uint8_t> value) {
    std::uint64_t map = 0;
    for (std::size_t index = 0; index < value.size() && index < 8; ++index) {
        map |= static_cast<std::uint64_t>(value[index]) << (8 * index);
    }
    return map;
}

std::string position_text(const WorldPosition& position) {
    char text[96];
    std::snprintf(text, sizeof(text), "%.2f %.2f %.2f", position.x, position.y, position.z);
    return text;
}

} // namespace

RepClient::RepClient(Options options) : m_options(std::move(options)) {}

const char* RepClient::phase_name(Phase phase) {
    switch (phase) {
    case Phase::idle:
        return "Idle";
    case Phase::waitingForRepConnection:
        return "WaitingForREPConnection";
    case Phase::waitingForActorGameConnection:
        return "WaitingForActorGameConnection";
    case Phase::waitingForSpawnPoint:
        return "WaitingForSpawnPoint";
    case Phase::waitingForPlayerSpawn:
        return "WaitingForPlayerSpawn";
    case Phase::inGame:
        return "InGame";
    case Phase::ended:
        return "Ended";
    }
    return "?";
}

void RepClient::log(Event::Level level, std::string text) {
    m_events.push_back({level, std::move(text)});
}

void RepClient::set_phase(Phase phase) {
    if (phase == m_phase) {
        return;
    }
    log(Event::Level::info, std::string("state ") + phase_name(m_phase) + " -> " + phase_name(phase));
    m_phase = phase;
}

void RepClient::end(const std::string& cause, bool disconnect) {
    if (m_phase == Phase::ended) {
        return;
    }
    m_endCause = cause;
    log(Event::Level::warning, "session end: " + cause);
    if (disconnect) {
        m_carrier.disconnect();
    }
    set_phase(Phase::ended);
}

void RepClient::start(Clock::time_point now) {
    if (m_phase != Phase::idle) {
        return;
    }
    m_carrier.connect();
    m_lastGameMessage = now;
    set_phase(Phase::waitingForRepConnection);
}

void RepClient::leave(const std::string& cause) {
    end(cause, true);
}

void RepClient::receive(std::span<const std::uint8_t> datagram, Clock::time_point now) {
    m_carrier.receive(datagram);
    for (const std::string& problem : m_carrier.take_problems()) {
        log(Event::Level::warning, "carrier: " + problem);
    }
    for (Carrier::Delivery& delivery : m_carrier.take_delivered()) {
        std::vector<ServerMessage> messages;
        std::string problem;
        if (!read_server_frames(delivery.data, messages, problem)) {
            log(Event::Level::warning, "a message on channel " + std::to_string(delivery.channel) + " (" + std::to_string(delivery.data.size())
                                           + " bytes) does not have the form of a server frame: " + problem);
        }
        for (const ServerMessage& message : messages) {
            m_lastGameMessage = now;
            ++m_counts.messages;
            on_message(message, now);
        }
    }
    if (m_carrier.state() == Carrier::State::closed) {
        end("the server closed the carrier connection", false);
    }
}

void RepClient::on_message(const ServerMessage& message, Clock::time_point now) {
    if (m_phase == Phase::ended) {
        return;
    }
    const bool first = m_seenTypes.insert(message.type).second;
    if (message.type == kRegistrationResponse.index) {
        const auto response = read_registration_response(message.body);
        if (!response) {
            end("the registration reply does not have its form", true);
            return;
        }
        if (response->result != 0) {
            // The retail client shows the text of the result and ends the connection (protocol notes 3.6).
            end("registration refused, result " + std::to_string(response->result) + " " + registration_result_text(response->result), true);
            return;
        }
        log(Event::Level::info, "registration accepted, server version " + response->serverVersion);
        // The server clock of the reply (nanoseconds): the first value of the clock of the client (protocol notes 4.43).
        m_serverOffsetNs = static_cast<std::int64_t>(response->serverTime) - steady_ns(now);
        m_hasServerClock = true;
        return;
    }
    if (message.type == kTimeSynch.index) {
        // REPClient::TimeSynchMsg: serverTime:u64 in nanoseconds, each 3 s (protocol notes 4.43).
        Reader reader(message.body);
        const std::uint64_t serverTime = reader.u64();
        if (!reader.ok()) {
            return;
        }
        const std::int64_t target = static_cast<std::int64_t>(serverTime) - steady_ns(now);
        const std::int64_t difference = target - m_serverOffsetNs;
        if (!m_hasServerClock || difference > kClockJumpNs || difference < -kClockJumpNs) {
            m_serverOffsetNs = target;
            m_clockCorrectionNs = 0;
            m_hasServerClock = true;
        } else {
            m_clockCorrectionNs = difference;
        }
        if (m_clockLogs < 3 || (difference > kClockJumpNs || difference < -kClockJumpNs)) {
            ++m_clockLogs;
            log(Event::Level::info, "clock correction (335): " + std::to_string(difference / 1000) + " us");
        }
        return;
    }
    if (message.type == kTimeAndPhase.index || message.type == kDayPhase.index) {
        on_world_time(message);
        return;
    }
    if (message.type == kSelfIdentification.index) {
        m_self = read_self_identification(message.body);
        if (!m_self) {
            end("the self identification does not have its form", true);
            return;
        }
        log(Event::Level::info, "self identification: session actor " + hex_of(m_self->sessionActor) + ", player manager "
                                    + hex_of(m_self->playerManager.actor) + " (hub " + std::to_string(m_self->playerManager.hub) + ")");
        set_phase(Phase::waitingForSpawnPoint);
        return;
    }
    if (message.type == kLevelInfoChanged.index) {
        const auto info = read_level_info(message.body);
        if (!info) {
            end("the level information does not have its form", true);
            return;
        }
        if (m_level && m_level->version == info->version) {
            return; // The retail client does nothing for a version that it has (protocol notes 3.5).
        }
        m_level = info;
        m_levelLoad = false;
        log(Event::Level::info, "level information: level " + info->level + ", terrain " + info->terrain + ", instance "
                                    + std::to_string(info->instance) + ", reload " + (info->reload ? "1" : "0") + ", version "
                                    + std::to_string(info->version));
        return;
    }
    if (message.type == kReceivePlayerSpawnPoint.index) {
        log(Event::Level::info, "spawn point notice");
        return;
    }
    if (message.type == kReplicatedStateBundle.index) {
        on_bundle(message.body, now);
        return;
    }
    if (message.type == kDamageDealt.index) {
        if (m_options.keepOtherRecords) {
            m_otherUpdates.push_back({false, 0, {}, message.type, message.body});
        }
        return;
    }
    if (message.type == kManaForceSync.index) {
        // 4289 (backend mana_messages.cpp): target:u64 entity:u64 current:f32 maximum:f32 delay:f32 rate:f32, big-endian.
        Reader reader(message.body);
        (void)reader.u64();
        const std::uint64_t entity = reader.u64();
        OwnEntity::ManaSync sync;
        sync.value = std::bit_cast<float>(reader.u32());
        sync.maximum = std::bit_cast<float>(reader.u32());
        sync.delay = std::bit_cast<float>(reader.u32());
        sync.factor = std::bit_cast<float>(reader.u32());
        sync.atNs = steady_ns(now);
        if (reader.ok() && entity == m_own.group) {
            m_own.manaSync = sync;
            ++m_own.manaSyncRevision;
        }
        return;
    }
    if (message.type == kHealedPlayer.index) {
        // 4298 (backend health_messages.cpp): target:u64 entity:u64 healedTargetId:u64 amount:f32.
        Reader reader(message.body);
        (void)reader.u64();
        const std::uint64_t entity = reader.u64();
        const std::uint64_t healed = reader.u64();
        const float amount = std::bit_cast<float>(reader.u32());
        if (reader.ok() && entity == m_own.group) {
            m_own.heals.push_back({amount, healed, steady_ns(now)});
            if (m_own.heals.size() > 16) {
                m_own.heals.erase(m_own.heals.begin());
            }
            ++m_own.healRevision;
        }
        return;
    }
    if (message.type == kDamageTaken.index) {
        const auto damage = read_damage_taken(message.body);
        if (!damage || damage->call.entity != m_own.group) {
            log(Event::Level::warning, "a damage message (3601) that does not have its form or does not name the own entity");
            return;
        }
        m_own.damages.push_back({*damage, steady_ns(now)});
        if (m_own.damages.size() > 16) {
            m_own.damages.erase(m_own.damages.begin());
        }
        ++m_own.damageRevision;
        return;
    }
    if (message.type == kPlayerDeath.index) {
        const auto death = read_player_death(message.body);
        if (!death || death->call.entity != m_own.group) {
            log(Event::Level::warning, "a death notice (3950) that does not name the own entity");
            return;
        }
        ++m_own.deathNotices;
        m_own.respawnScreenTime = death->time;
        m_own.deathNoticeNs = steady_ns(now);
        char text[96];
        std::snprintf(text, sizeof(text), "death notice (3950): the respawn screen in %.1f s", static_cast<double>(death->time));
        log(Event::Level::info, text);
        return;
    }
    if (message.type == kTeleportWithLoadingScreen.index || message.type == kPlayerTeleport.index) {
        const auto call = read_component_call(message.body);
        if (!call || call->entity != m_own.group) {
            return;
        }
        if (message.type == kTeleportWithLoadingScreen.index) {
            m_own.loadingScreen = true;
            log(Event::Level::info, "teleport with the loading screen (3951)");
        } else {
            m_own.loadingScreen = false;
            ++m_own.teleportNotices;
            log(Event::Level::info, "player teleport (3954): the loading screen ends");
        }
        return;
    }
    if (message.type == kCancelCurrentInteraction.index) {
        const auto call = read_component_call(message.body);
        if (!call || call->entity != m_own.group) {
            return;
        }
        ++m_own.interactionCancels;
        // The client cancels its committed interaction and sends 2473 (handler 0x490EDA0, 0x4916150; protocol notes 4.20).
        if (m_own.interaction) {
            log(Event::Level::info, "the server ends the interaction (3746): the client cancels it (2473)");
            cancel_interaction();
        } else {
            log(Event::Level::info, "the server ends the interaction (3746): no interaction");
        }
        return;
    }
    if (message.type == kEquipItemFail.index) {
        // The client shows its paperdoll slots again (handler 0x687BB60; it does not read the reason).
        const auto call = read_component_call(message.body);
        if (call && call->entity == m_own.group) {
            ++m_own.equipFails;
            log(Event::Level::info, "the server refused a request of the equipment (3626)");
        }
        return;
    }
    if (message.type == kCutsceneEndFinalized.index) {
        const auto call = read_component_call(message.body);
        if (call && call->entity == m_own.group) {
            ++m_own.lockEnds;
            log(Event::Level::info, "the end of a lock of the input (6709)");
        }
        return;
    }
    if (message.type == kInteractionResult.index) {
        const auto result = read_interaction_result(message.body);
        // The client compares the reference with its request; a different reference changes nothing (0x49175F0).
        if (!result || result->call.entity != m_own.group || !m_own.interaction || !(result->reference == m_own.interaction->reference)) {
            log(Event::Level::info, "an interaction result (3743) for no current request");
            return;
        }
        ++m_own.interactionResults;
        m_own.lastInteractionResult = result->result;
        m_own.lastInteractionAction = result->action;
        log(Event::Level::info, "interaction result (3743): action " + std::to_string(result->action) + ", result "
                                    + std::to_string(result->result) + (result->result == 1 ? " (accepted)" : " (refused)"));
        if (result->result != 1) {
            // A refusal clears the records of the client (protocol notes 4.35); no 2473.
            m_own.interaction.reset();
        }
        return;
    }
    if (message.type == kKickClient.index || message.type == kPlayerManagerRejected.index) {
        const auto reason = read_reason(message.body);
        end(std::string(message.type == kKickClient.index ? "kicked" : "rejected") + ", reason "
                + (reason ? std::to_string(*reason) : std::string("?")),
            true);
        return;
    }
    if (first) {
        log(Event::Level::info, "message type " + std::to_string(message.type) + " (" + std::to_string(message.body.size())
                                    + " bytes) has no handler in this client");
    }
}

void RepClient::on_bundle(std::span<const std::uint8_t> body, Clock::time_point now) {
    ++m_counts.bundles;
    if (!m_level) {
        // The retail client drops a bundle before the level load ("clientContext == nullptr").
        ++m_counts.droppedBundles;
        log(Event::Level::warning, "a state bundle before the level information was dropped");
        return;
    }
    const CheckedBundle checked = read_state_bundle(body);
    if (!checked.headerOk) {
        ++m_counts.droppedBundles;
        log(Event::Level::warning, "a state bundle (" + std::to_string(body.size()) + " bytes) has no correct header");
        return;
    }
    const decoder::Bundle& bundle = checked.bundle;
    if (bundle.instance != m_level->instance) {
        ++m_counts.droppedBundles;
        log(Event::Level::warning, "a state bundle for a previous client context: expected " + std::to_string(m_level->instance)
                                       + ", incoming " + std::to_string(bundle.instance));
        return;
    }
    // The reliable bundles of one connection are 1, 2, 3, ... with no gap: the retail client drops each bundle after a gap.
    if (m_bundleGap || bundle.sequence != m_bundleSequence + 1) {
        ++m_counts.droppedBundles;
        if (!m_bundleGap) {
            log(Event::Level::error, "a gap in the state bundles: expected " + std::to_string(m_bundleSequence + 1) + ", incoming "
                                         + std::to_string(bundle.sequence) + ": each later bundle is dropped");
        }
        m_bundleGap = true;
        return;
    }
    m_bundleSequence = bundle.sequence;
    if (!m_levelLoad) {
        m_levelLoad = true;
        log(Event::Level::info, "level load: " + m_level->level);
        if (m_phase == Phase::waitingForSpawnPoint) {
            set_phase(Phase::waitingForPlayerSpawn);
        }
    }
    if (!checked.complete) {
        ++m_counts.incompleteBundles;
        if (m_unknownStateTypes.insert(checked.failedType).second) {
            log(Event::Level::warning, "state type " + std::to_string(checked.failedType) + " (slot " + std::to_string(checked.failedSlot)
                                           + ", interest " + std::to_string(checked.failedInterest) + ", bundle "
                                           + std::to_string(bundle.sequence)
                                           + ") is not known to the state reader: the rest of each such bundle is skipped");
        }
    }
    for (const std::uint32_t interest : bundle.stops) {
        m_others.erase(interest);
        if (m_options.keepOtherRecords) {
            m_otherUpdates.push_back({true, interest, {}});
        }
    }
    for (const decoder::Record& record : bundle.records) {
        ++m_counts.records;
        if (record.interest == 1) {
            apply_own(record, now);
        } else {
            apply_other(record);
            if (m_options.keepOtherRecords) {
                m_otherUpdates.push_back({false, record.interest, record});
            }
        }
    }
    if (m_otherUpdates.size() > kOtherUpdateLimit) {
        m_otherUpdates.erase(m_otherUpdates.begin(), m_otherUpdates.begin() + static_cast<std::ptrdiff_t>(kOtherUpdateLimit / 2));
        if (!m_otherOverflow) {
            m_otherOverflow = true;
            log(Event::Level::warning, "nobody takes the records of the other entities: the oldest half went away");
        }
    }
}

std::vector<RepClient::OtherUpdate> RepClient::take_other_updates() {
    std::vector<OtherUpdate> out;
    out.swap(m_otherUpdates);
    return out;
}

void RepClient::apply_own(const decoder::Record& record, Clock::time_point now) {
    bool newPosition = false;
    for (const decoder::Fragment& fragment : record.fragments) {
        if (fragment.slot == kSlotMetadata && fragment.type == kStateMetadata.index) {
            const auto asset = decoder::field_of(fragment, 0, 0);
            const auto actor = decoder::field_of(fragment, 0, 1);
            if (asset.size() == 20 && actor.size() == 16) {
                std::copy(asset.begin(), asset.begin() + 16, m_own.sliceAsset.begin());
                m_own.sliceSubId = static_cast<std::uint32_t>(read_be(asset.subspan(16, 4)));
                std::copy(actor.begin(), actor.end(), m_own.actor.begin());
                m_own.group = group_of(m_own.actor);
                if (m_own.hasMetadata) {
                    log(Event::Level::warning, "a second metadata state for the own entity (the retail client closes its connection)");
                }
                m_own.hasMetadata = true;
            }
        } else if (fragment.slot == kSlotPlayer && fragment.type == kStatePlayer.index) {
            if (const auto id = decoder::field_of(fragment, 2, 0); !id.empty()) {
                m_own.characterId = text_at(id, 1);
            }
            if (const auto name = decoder::field_of(fragment, 2, 1); !name.empty()) {
                m_own.name = text_at(name, 0);
            }
            if (const auto connected = decoder::field_of(fragment, 2, 3); !connected.empty()) {
                m_own.connected = connected[0] != 0;
            }
        } else if (fragment.slot == kSlotMovement && fragment.type == kStateMovement.index) {
            ++m_counts.movementStates;
            // The client ignores a movement state with no time anchor (protocol notes 4.4).
            const auto anchor = decoder::field_of(fragment, 0, 40);
            if (anchor.size() != 8) {
                continue;
            }
            m_own.timeAnchor = read_be(anchor);
            m_lastMovementState = now;
            if (const auto position = decoder::field_of(fragment, 0, 27); position.size() == 10) {
                m_own.position = read_world_position(position);
                m_own.hasPosition = true;
                newPosition = true;
            }
            if (const auto rotation = decoder::field_of(fragment, 0, 11); !rotation.empty()) {
                m_own.rotation = read_quaternion(rotation);
            }
            if (const auto look = decoder::field_of(fragment, 0, 12); !look.empty()) {
                m_own.lookDirection = read_quaternion(look);
            }
            if (const auto teleport = decoder::field_of(fragment, 0, 31); teleport.size() == 1) {
                m_own.teleportId = teleport[0];
                // The client takes the place of a state only when its teleport number is new (protocol notes 4.16, 0x486D89B).
                if (takes_travel(m_clientTeleport, teleport[0])) {
                    m_clientTeleport = teleport[0] & 0x0F;
                    ++m_own.travelRevision;
                    if (m_phase == Phase::inGame) {
                        log(Event::Level::info, "own entity: a travel to " + position_text(m_own.position) + " (teleport number "
                                                    + std::to_string(m_clientTeleport) + ")");
                    }
                }
            }
            if (const auto bits = decoder::field_of(fragment, 0, 41); bits.size() == 1) {
                m_own.dataBits = bits[0];
            }
            if (const auto bits = decoder::field_of(fragment, 0, 42); bits.size() == 1) {
                m_own.moreDataBits = bits[0];
            }
            const auto idRel = decoder::field_of(fragment, 0, 0);
            const auto idAbs = decoder::field_of(fragment, 0, 25);
            if (idRel.size() == 1 && idAbs.size() == 1) {
                m_own.stateId = static_cast<std::uint16_t>((idAbs[0] << 8) | idRel[0]);
            }
        } else if (fragment.slot == kSlotVitals && fragment.type == kStateVitals.index) {
            const float healthBefore = m_own.health;
            if (const auto health = decoder::field_of(fragment, 0, 0); health.size() == 4) {
                m_own.health = f32_at(health);
            }
            // healthChangeFlags (group 0, field 3; backend vitals_state.cpp): bit 1 gives the heal number of the owner, the new health
            // less the health that the client had (protocol notes 4.31).
            if (const auto flags = decoder::field_of(fragment, 0, 3); flags.size() == 1 && (flags[0] & 0x02) != 0 && m_own.health > healthBefore) {
                m_own.heals.push_back({std::floor(m_own.health - healthBefore), 0, steady_ns(now)});
                if (m_own.heals.size() > 16) {
                    m_own.heals.erase(m_own.heals.begin());
                }
                ++m_own.healRevision;
            }
            if (const auto maximum = decoder::field_of(fragment, 0, 4); maximum.size() == 4) {
                m_own.healthMax = f32_at(maximum);
            }
            if (const auto value = decoder::field_of(fragment, 0, 1); value.size() == 4) {
                m_own.vitalsStamina = f32_at(value);
            }
            if (const auto value = decoder::field_of(fragment, 0, 6); value.size() == 4) {
                m_own.vitalsStaminaMax = f32_at(value);
            }
            if (const auto value = decoder::field_of(fragment, 0, 2); !value.empty()) {
                m_own.dead = (value[0] & 0x01) != 0; // vitalsData: bit 0 of its flags (client getter 0x684AC70).
            }
            if (const auto value = decoder::field_of(fragment, 1, 0); value.size() == 4) {
                m_own.vitalsMana = f32_at(value);
            }
            if (const auto value = decoder::field_of(fragment, 1, 3); value.size() == 4) {
                m_own.vitalsManaMax = f32_at(value);
            }
        } else if (fragment.slot == kSlotStamina && fragment.type == kStateStamina.index) {
            if (const auto value = decoder::field_of(fragment, 0, 0); value.size() == 4) {
                m_own.stamina = f32_at(value);
                m_own.hasStamina = true;
            }
            if (const auto value = decoder::field_of(fragment, 0, 1); value.size() == 4) {
                m_own.staminaMax = f32_at(value);
            }
            if (const auto value = decoder::field_of(fragment, 0, 2); value.size() == 4) {
                m_own.staminaWinded = f32_at(value);
            }
            if (const auto value = decoder::field_of(fragment, 0, 3); value.size() == 4) {
                m_own.staminaRegenDelay = f32_at(value);
            }
            if (const auto value = decoder::field_of(fragment, 0, 5); value.size() == 4) {
                m_own.staminaRegenMultiplier = f32_at(value);
            }
            m_own.staminaAtNs = steady_ns(now);
            ++m_own.staminaRevision;
        } else if (fragment.slot == kSlotMana && fragment.type == kStateMana.index) {
            if (const auto value = decoder::field_of(fragment, 0, 0); value.size() == 4) {
                m_own.mana = f32_at(value);
                m_own.hasMana = true;
            }
            if (const auto value = decoder::field_of(fragment, 0, 1); value.size() == 4) {
                m_own.manaMax = f32_at(value);
            }
            if (const auto value = decoder::field_of(fragment, 0, 2); value.size() == 4) {
                m_own.manaDelay = f32_at(value);
            }
            if (const auto value = decoder::field_of(fragment, 0, 3); value.size() == 4) {
                m_own.manaRate = f32_at(value);
            }
            m_own.manaAtNs = steady_ns(now);
            ++m_own.manaRevision;
        } else if (fragment.slot == kSlotAbility && fragment.type == kStateAbility.index) {
            if (const auto value = decoder::field_of(fragment, 0, 0); !value.empty()) {
                const decoder::WireBars bars = decoder::read_bars(value);
                if (bars.ok) {
                    m_own.abilityBars.clear();
                    for (const decoder::WireBar& bar : bars.tables) {
                        m_own.abilityBars.push_back({bar.table, bar.abilities, bar.buttons});
                    }
                }
            }
        } else if (fragment.slot == kSlotSpeedTable && fragment.type == kStateSpeedTable.index) {
            // A full table (group 1 field 0): deltaCount:size 0, hasSequence:u8, sequence:size64, count:size, entries of key:u8
            // value:i32 (big-endian, the multiplier times 10000) m_syncVitals:u8 (backend stat_multiplier_state.cpp).
            if (const auto value = decoder::field_of(fragment, 1, 0); !value.empty()) {
                Reader reader(value);
                if (reader.size() == 0) {
                    if (reader.u8() != 0) {
                        (void)reader.size64();
                    }
                    const std::uint32_t count = reader.size();
                    std::map<std::uint8_t, float> table;
                    for (std::uint32_t index = 0; index < count && reader.ok(); ++index) {
                        const std::uint8_t key = reader.u8();
                        const auto raw = static_cast<std::int32_t>(reader.u32());
                        (void)reader.u8();
                        table[key] = static_cast<float>(raw) / 10000.0F;
                    }
                    if (reader.ok()) {
                        m_own.speedTable = std::move(table);
                        ++m_own.speedRevision;
                    }
                }
            }
        } else if (fragment.slot == kSlotStatuses && fragment.type == kStateStatuses.index) {
            // The full map of the owner (group 1 field 0): 00, the stamp (01 sequence:size64), count:size, entries of statusIndex:u16
            // wallEnd:u64 potency:f16 stacks:u8 flags:u8 (backend status_state.cpp). A map with an older stamp changes nothing.
            if (const auto value = decoder::field_of(fragment, 1, 0); !value.empty()) {
                Reader reader(value);
                if (reader.size() == 0 && reader.u8() == 1) {
                    const std::uint64_t stamp = reader.size64();
                    const std::uint32_t count = reader.size();
                    std::vector<OwnEntity::Status> statuses;
                    for (std::uint32_t index = 0; index < count && reader.ok(); ++index) {
                        OwnEntity::Status status;
                        status.index = reader.u16();
                        status.wallEndNs = reader.u64();
                        status.potency = half_value(reader.u16());
                        status.stacks = reader.u8();
                        (void)reader.u8();
                        statuses.push_back(status);
                    }
                    if (reader.ok() && (m_own.statusRevision == 0 || stamp >= m_own.statusStamp)) {
                        m_own.statuses = std::move(statuses);
                        m_own.statusStamp = stamp;
                        ++m_own.statusRevision;
                    }
                }
            }
        } else if (fragment.slot == kSlotPoints && fragment.type == kStatePoints.index) {
            const std::array<std::span<const std::uint8_t>, 4> fields{decoder::field_of(fragment, 0, 0), decoder::field_of(fragment, 0, 1),
                                                                      decoder::field_of(fragment, 0, 2), decoder::field_of(fragment, 0, 3)};
            if (!fields[3].empty()) {
                const decoder::WirePools pools = decoder::read_pools(fields);
                if (pools.ok) {
                    m_own.learnedNodes.clear();
                    for (const decoder::WireNode& node : pools.nodes) {
                        if (node.level > 0) {
                            m_own.learnedNodes.push_back(node.node);
                        }
                    }
                    ++m_own.nodesRevision;
                }
            }
        } else if (fragment.slot == kSlotCooldowns && fragment.type == kStateCooldowns.index) {
            for (unsigned place = 0; place < m_own.cooldowns.size(); ++place) {
                if (const auto value = decoder::field_of(fragment, 0, place); !value.empty()) {
                    read_cooldown_list<OwnEntity::Cooldown>(value, m_own.cooldowns[place], [](Reader& reader) {
                        OwnEntity::Cooldown entry;
                        entry.ability = reader.u32();
                        entry.endUs = reader.u64();
                        entry.minimumEndUs = reader.u64();
                        return entry;
                    });
                }
            }
            if (const auto value = decoder::field_of(fragment, 0, 3); !value.empty()) {
                // key:u32 end:u64 a:u32 b:u32 (the two values are 0).
                read_cooldown_list<std::pair<std::uint32_t, std::uint64_t>>(value, m_own.itemCooldowns, [](Reader& reader) {
                    const std::uint32_t key = reader.u32();
                    const std::uint64_t end = reader.u64();
                    reader.u32();
                    reader.u32();
                    return std::pair<std::uint32_t, std::uint64_t>(key, end);
                });
            }
        } else if (fragment.slot == kSlotAppearance && fragment.type == kStateAppearance.index) {
            for (unsigned field = 0; field < m_own.appearance.size(); ++field) {
                if (const auto value = decoder::field_of(fragment, 0, field); value.size() == 1) {
                    m_own.appearance[field] = value[0];
                    m_own.hasAppearance = true;
                }
            }
            ++m_own.lookRevision;
        } else if ((fragment.slot == kSlotBag || fragment.slot == kSlotSelectedContainer) && fragment.type == kStateContainer.index) {
            // The bag of the character and the selected Container (the open bag): field 0, the full vector or a journal.
            const bool bag = fragment.slot == kSlotBag;
            if (const auto value = decoder::field_of(fragment, 0, 0); !value.empty()) {
                if (apply_container(value, bag ? m_own.bag : m_own.selectedContainer)) {
                    ++(bag ? m_own.bagRevision : m_own.selectedRevision);
                } else {
                    log(Event::Level::warning, std::string("the container of slot ") + std::to_string(fragment.slot) + " does not have its form");
                }
            }
        } else if (fragment.slot == kSlotLootTracker && fragment.type == kStateLootTracker.index) {
            if (const auto value = decoder::field_of(fragment, 0, 0); !value.empty()) {
                if (auto claims = read_loot_tracker(value)) {
                    m_own.lootClaims = std::move(*claims);
                    ++m_own.lootRevision;
                }
            }
        } else if (fragment.slot == kSlotGlobalStorage && fragment.type == kStateGlobalStorage.index) {
            if (const auto value = decoder::field_of(fragment, 0, 0); !value.empty()) {
                if (auto keys = read_global_storage(value)) {
                    m_own.globalStorage = std::move(*keys);
                    ++m_own.storageRevision;
                }
            }
        } else if (fragment.slot == kSlotGathering && fragment.type == kStateGathering.index) {
            if (apply_gathering(fragment, m_own.gathering)) {
                ++m_own.gatheringRevision;
            }
        } else if (fragment.slot == kSlotAchievement && fragment.type == kStateAchievement.index) {
            if (const auto value = decoder::field_of(fragment, 0, 0); !value.empty()) {
                if (auto bytes = read_bit_vector(value)) {
                    m_own.achievements = std::move(*bytes);
                    ++m_own.achievementRevision;
                }
            }
        } else if (fragment.slot == kSlotPaperdoll && fragment.type == kStatePaperdoll.index) {
            // The owner view: two pairs of parallel lists (the slots with a look, the other slots). An entry is a change
            // of its slot; a slot with no entry keeps its item (client code 0x68382D0).
            const auto apply_lists = [this](std::span<const std::uint8_t> slotBytes, std::span<const std::uint8_t> itemBytes) {
                if (slotBytes.empty() || itemBytes.empty()) {
                    return;
                }
                Reader slotReader(slotBytes);
                Reader itemReader(itemBytes);
                std::vector<std::uint32_t> slots;
                std::vector<decoder::WireItem> items;
                if (!decoder::read_number_list(slotReader, slots) || !decoder::read_item_list(itemReader, items)
                    || slots.size() != items.size()) {
                    return;
                }
                for (std::size_t index = 0; index < slots.size(); ++index) {
                    m_own.equipment[slots[index]] = items[index].empty() ? 0 : items[index].index;
                    if (items[index].empty()) {
                        m_own.paperdollItems.erase(slots[index]);
                    } else {
                        m_own.paperdollItems[slots[index]] = items[index];
                    }
                }
                ++m_own.paperdollRevision;
            };
            apply_lists(decoder::field_of(fragment, 2, 3), decoder::field_of(fragment, 1, 1));
            apply_lists(decoder::field_of(fragment, 1, 3), decoder::field_of(fragment, 1, 2));
            if (const auto value = decoder::field_of(fragment, 2, 0); !value.empty()) {
                m_own.sheatheMap = slot_map(value);
            }
            if (const auto value = decoder::field_of(fragment, 2, 2); !value.empty()) {
                m_own.activeMap = slot_map(value);
            }
            ++m_own.lookRevision;
        } else if (fragment.slot == kSlotProgression && fragment.type == kStateProgression.index) {
            if (const auto value = decoder::field_of(fragment, 0, 0); value.size() == 4) {
                Reader reader(value);
                m_own.level = reader.u32();
                m_own.hasProgression = true;
            }
            if (const auto value = decoder::field_of(fragment, 1, 0); value.size() == 4) {
                Reader reader(value);
                m_own.experience = reader.u32();
            }
        } else if (fragment.slot == kSlotCurrency && fragment.type == kStateCurrency.index) {
            if (const auto value = decoder::field_of(fragment, 0, 0); !value.empty()) {
                if (const auto coin = read_currency(value)) {
                    m_own.coin = *coin;
                    m_own.hasCoin = true;
                }
            }
        }
    }
    if (newPosition && m_phase == Phase::inGame) {
        log(Event::Level::info, "own entity: new position " + position_text(m_own.position) + " (teleport " + std::to_string(m_own.teleportId) + ")");
    }
    if (m_phase == Phase::waitingForPlayerSpawn && m_own.hasMetadata && !m_own.characterId.empty()) {
        // Only the character id of the login makes the entity the local player (client code 0x6929DF0).
        if (m_own.characterId != m_options.token.characterId) {
            log(Event::Level::error, "the record of interest 1 has the character id " + m_own.characterId + ", not the character of the login");
            return;
        }
        log(Event::Level::info, "own entity: interest 1, actor " + hex_of(m_own.actor) + ", entity group " + hex_of(std::span(m_own.actor).first(8))
                                    + ", character " + m_own.characterId + ", slice asset " + hex_of(m_own.sliceAsset) + " sub "
                                    + std::to_string(m_own.sliceSubId) + ", connected " + (m_own.connected ? "1" : "0"));
        if (m_own.hasPosition) {
            char text[160];
            std::snprintf(text, sizeof(text), "own entity: position %s, rotation (%.3f %.3f %.3f %.3f), permissions %02x %02x",
                          position_text(m_own.position).c_str(), m_own.rotation.x, m_own.rotation.y, m_own.rotation.z, m_own.rotation.w,
                          m_own.dataBits, m_own.moreDataBits);
            log(Event::Level::info, text);
        }
        set_phase(Phase::inGame);
        m_nextInput = now;
        m_inputNumber = 0;
        m_recentInputs.clear();
        if (!m_timeRequested) {
            // The request of the world time (4248): one time for each world entry, when the Time facet of the own entity is known
            // (protocol notes 4.43; the retail client sends it after the mission load of the world level).
            m_timeRequested = true;
            send_call(kTimeRequest, time_request_body(m_own.group));
            log(Event::Level::info, "time request (4248) for the Time facet of the own entity");
        }
    }
}

void RepClient::apply_other(const decoder::Record& record) {
    OtherEntity& entity = m_others[record.interest];
    for (const decoder::Fragment& fragment : record.fragments) {
        if (fragment.slot == kSlotMetadata && fragment.type == kStateMetadata.index) {
            const auto asset = decoder::field_of(fragment, 0, 0);
            const auto actor = decoder::field_of(fragment, 0, 1);
            if (asset.size() == 20 && actor.size() == 16) {
                std::copy(asset.begin(), asset.begin() + 16, entity.sliceAsset.begin());
                entity.sliceSubId = static_cast<std::uint32_t>(read_be(asset.subspan(16, 4)));
                std::copy(actor.begin(), actor.end(), entity.actor.begin());
            }
        } else if (fragment.type == kStateMovement.index) {
            if (const auto position = decoder::field_of(fragment, 0, 27); position.size() == 10) {
                entity.position = read_world_position(position);
                entity.hasPosition = true;
            }
        } else if (fragment.type == kStatePositionInTheWorld) {
            if (const auto place = decoder::field_of(fragment, 0, 0); place.size() == 10) {
                entity.place = read_object_place(place);
                entity.hasPlace = true;
            }
            if (const auto rotation = decoder::field_of(fragment, 0, 1); !rotation.empty()) {
                entity.placeRotation = read_place_rotation(rotation);
            }
        }
    }
}

void RepClient::send_registration() {
    const Bytes body = registration_body(m_options.token, m_options.steamId, m_options.ticketHex);
    const Bytes frame = client_frame(kNoRoute, kRegistrationRequestV3.index, body);
    m_carrier.send(kChannelReliable, frame, true);
    log(Event::Level::info, "registration request (type " + std::to_string(kRegistrationRequestV3.index) + ", " + std::to_string(body.size())
                                + " bytes) for character " + m_options.token.characterId);
}

WorldPosition RepClient::next_reported(const std::optional<WorldPosition>& last, const WorldPosition& character) {
    if (!last) {
        return character;
    }
    const float dx = character.x - last->x;
    const float dy = character.y - last->y;
    const float dz = character.z - last->z;
    const float horizontal = std::sqrt(dx * dx + dy * dy);
    if (horizontal > kReportJump || std::fabs(dz) > kReportJump) {
        return character;
    }
    const float limit = kReportHorizontalSpeed * kStepSeconds;
    const float share = horizontal > limit ? limit / horizontal : 1.0F;
    WorldPosition next = *last;
    next.x += dx * share;
    next.y += dy * share;
    next.z += std::clamp(dz, -kReportFallSpeed * kStepSeconds, kReportRiseSpeed * kStepSeconds);
    return next;
}

WorldPosition RepClient::reported_position(const WorldPosition& character) {
    m_reported = next_reported(reported(), character);
    m_hasReported = true;
    return m_reported;
}

void RepClient::send_input(Clock::time_point now) {
    if (now < m_nextInput) {
        return;
    }
    // One report for each step of 1/30 s; after a stop of the caller the next step starts now.
    m_nextInput += kInputInterval;
    if (m_nextInput < now) {
        m_nextInput = now + kInputInterval;
    }
    InputEntry entry;
    entry.sequence = ++m_inputNumber;
    entry.flags = kInputFlagsWorld;
    entry.permissions = kRestPermissions;
    entry.groundDistance = kRestGroundDistance;
    entry.actionWords = kActionWords;
    // With no movement state for 2 s the retail client clears the actions of each new input (protocol notes 4.6).
    if (!m_movementStale) {
        entry.actionBits = kRestActionBits;
    }
    entry.stepTime = 1.0F / 30.0F;
    if (m_motion) {
        // The player of the game thread: the camera with its look, the keys, and the position field (flag 0x8000, as
        // the retail client with the game hook of Soulwarden sends it; protocol notes 4.7).
        entry.flags |= kInputFlagPosition;
        entry.hasCharacter = true;
        entry.character = reported_position(m_motion->character);
        entry.camera = m_motion->camera;
        entry.hasLook = true;
        entry.yaw = m_motion->yaw;
        entry.pitch = m_motion->pitch;
        if (m_motion->airHeight >= 0.0F) {
            entry.groundDistance = half_bits(std::min(m_motion->airHeight, kMaxAirHeight));
        }
        if (m_motion->depthClass > 0) {
            entry.depthClass = m_motion->depthClass;
        }
        if (m_motion->forward && !m_movementStale) {
            entry.actionBits.insert(entry.actionBits.end(), kForwardActionBits.begin(), kForwardActionBits.end());
        }
        if (m_motion->pressSerial != m_pressSerial) {
            // A key press: its bits in one entry (protocol notes 4.15: the weapon keys).
            m_pressSerial = m_motion->pressSerial;
            entry.actionBits.insert(entry.actionBits.end(), m_motion->pressBits.begin(), m_motion->pressBits.end());
        }
        if (m_options.inputStep) {
            std::vector<ShotRequest> shots;
            m_options.inputStep(static_cast<std::uint16_t>(entry.sequence), steady_ns(now), entry.actionBits, shots);
            for (const ShotRequest& request : shots) {
                // The shot item (protocol notes 4.27): the origin `forward` m along the look and `up` m above the feet of the reported
                // character (Gravity Well: 0 and 1.5 m; a bow shot: 1.0 m and 1.46 m, live), the direction to the aim point of the
                // camera (else the look; a positive pitch is down), no receiver (the server flies the shot).
                ShotItem shot;
                const float level = std::cos(m_motion->pitch);
                const float lookX = -std::sin(m_motion->yaw) * level;
                const float lookY = std::cos(m_motion->yaw) * level;
                const float flat = std::hypot(lookX, lookY);
                const float forwardX = flat > 1e-4F ? lookX / flat : 0.0F;
                const float forwardY = flat > 1e-4F ? lookY / flat : 1.0F;
                shot.origin = {entry.character.x + forwardX * request.forward, entry.character.y + forwardY * request.forward,
                               entry.character.z + request.up};
                shot.direction = {lookX, lookY, -std::sin(m_motion->pitch)};
                if (m_motion->hasAimPoint) {
                    const float dx = m_motion->aimPoint.x - shot.origin.x;
                    const float dy = m_motion->aimPoint.y - shot.origin.y;
                    const float dz = m_motion->aimPoint.z - shot.origin.z;
                    const float length = std::sqrt(dx * dx + dy * dy + dz * dz);
                    if (length > 0.5F) {
                        shot.direction = {dx / length, dy / length, dz / length};
                    }
                }
                shot.attack = request.attack;
                shot.row = request.row;
                shot.charge = request.charge;
                shot.ammo = request.ammo;
                entry.shots.push_back(shot);
            }
            m_counts.shots += entry.shots.size();
        }
        // The interaction request: its block in each entry while it is current.
        if (m_own.interaction) {
            entry.interaction = interaction_block(*m_own.interaction);
        }
        // The melee contacts of the player: in this entry only (the entry also has the position field).
        entry.contacts.swap(m_pendingContacts);
        m_pendingContacts.clear();
        m_counts.meleeContacts += entry.contacts.size();
    } else {
        m_hasReported = false;
        // The camera of a character at rest at its position, north, no pitch: the camera estimate of the backend keeps
        // the position (protocol notes 4.7). No position field (the flag 0x8000).
        entry.camera = {m_own.position.x + kCameraRight, m_own.position.y - kCameraBehind, m_own.position.z + kCameraUp};
    }
    if (m_motion.has_value() != m_sentMotion) {
        m_sentMotion = m_motion.has_value();
        char text[160];
        std::snprintf(text, sizeof(text), m_sentMotion ? "input %u: the position of the character (flag 0x8000), the camera, and the keys of the player"
                                                       : "input %u: the camera of a character at rest, no position field",
                      static_cast<unsigned>(entry.sequence));
        log(Event::Level::info, text);
    }
    m_recentInputs.insert(m_recentInputs.begin(), entry);
    if (m_recentInputs.size() > kEntriesPerReport) {
        m_recentInputs.resize(kEntriesPerReport);
    }
    const std::uint64_t target = m_own.group + kActionListComponent;
    const Bytes body = input_report_body(m_recentInputs, target, m_own.group);
    m_carrier.send(kChannelUnreliable, client_frame(m_own.actor, kClientInput.index, body), false);
    ++m_counts.inputReports;
    if (m_counts.inputReports == 1) {
        char text[128];
        std::snprintf(text, sizeof(text), "first input report (type %u, %zu bytes), target %016llx, entity %016llx", kClientInput.index,
                      body.size(), static_cast<unsigned long long>(target), static_cast<unsigned long long>(m_own.group));
        log(Event::Level::info, text);
    }
}

void RepClient::on_world_time(const ServerMessage& message) {
    // The record (7036) and the phase (4247) of the own Time facet: G + kTimeComponent and the entity G (protocol notes 4.43).
    const bool record = message.type == kTimeAndPhase.index;
    std::optional<TimeAndPhase> full = record ? read_time_and_phase(message.body) : std::nullopt;
    std::optional<DayPhaseCall> phase = record ? std::nullopt : read_day_phase(message.body);
    const ComponentCall call = full ? full->call : phase ? phase->call : ComponentCall{};
    if ((!full && !phase) || call.entity != m_own.group || call.target != m_own.group + kTimeComponent) {
        log(Event::Level::warning, std::string(record ? "a time record (7036)" : "a day phase (4247)") + " that does not have its form or "
                                       "does not name the own Time facet");
        return;
    }
    if (phase) {
        m_own.dayPhase = phase->phase;
        log(Event::Level::info, std::string("day phase (4247): ") + day_phase_text(phase->phase));
        return;
    }
    if (!usable(full->record)) {
        log(Event::Level::warning, "a time record (7036) with lengths that the hour formula cannot use");
        return;
    }
    m_own.timeRecord = full->record;
    m_own.dayPhase = full->phase;
    ++m_own.timeRevision;
    char text[160];
    std::snprintf(text, sizeof(text), "time record (7036): anchor %llu ns, offset %.9f, day %u s, night %u s, phase %s",
                  static_cast<unsigned long long>(full->record.anchor), static_cast<double>(full->record.offset), full->record.dayLength,
                  full->record.nightLength, day_phase_text(full->phase));
    log(Event::Level::info, text);
}

void RepClient::send_call(const MessageType& type, const Bytes& body) {
    m_carrier.send(kChannelReliable, client_frame(m_own.actor, type.index, body), true);
}

void RepClient::request_respawn(bool random, const std::string& id) {
    if (m_phase != Phase::inGame || !m_own.hasMetadata) {
        return;
    }
    m_own.loadingScreen = true;
    send_call(kRequestRespawn, respawn_request_body(m_own.group, random, id));
    log(Event::Level::info, std::string("respawn request (1938): ") + (random ? "Random Location" : "the home point " + id) + "; the loading screen shows");
}

void RepClient::take_all_items() {
    if (m_phase == Phase::inGame && m_own.hasMetadata) {
        send_call(kTakeAllItems, take_all_items_body(m_own.actor));
        log(Event::Level::info, "take all items (2939) of the open bag");
    }
}

void RepClient::take_item(const std::array<std::uint8_t, 16>& instance, std::uint32_t quantity) {
    if (m_phase == Phase::inGame && m_own.hasMetadata) {
        send_call(kTakeItem, take_item_body(m_own.actor, instance, quantity));
        log(Event::Level::info, "take item (2942) " + hex_of(instance) + ", quantity " + std::to_string(quantity));
    }
}

void RepClient::equip_item(std::int32_t bagSlot, std::uint32_t paperdollSlot, std::uint32_t quantity, const ItemInstanceId& instance) {
    if (m_phase == Phase::inGame && m_own.hasMetadata) {
        send_call(kEquipItem, equip_item_body(m_own.group, bagSlot, paperdollSlot, quantity, instance));
        log(Event::Level::info, "equip (3895): bag slot " + std::to_string(bagSlot) + " to paperdoll slot " + std::to_string(paperdollSlot)
                                    + ", quantity " + std::to_string(quantity));
    }
}

void RepClient::unequip_item(std::uint32_t paperdollSlot, std::int32_t bagSlot, std::uint32_t quantity) {
    if (m_phase == Phase::inGame && m_own.hasMetadata) {
        send_call(kUnequipItem, unequip_item_body(m_own.group, paperdollSlot, bagSlot, quantity));
        log(Event::Level::info, "unequip (3896): paperdoll slot " + std::to_string(paperdollSlot) + " to bag slot " + std::to_string(bagSlot)
                                    + ", quantity " + std::to_string(quantity));
    }
}

void RepClient::swap_items(std::uint32_t slotTo, std::uint32_t slotFrom, const ItemInstanceId& instanceTo, const ItemInstanceId& instanceFrom) {
    if (m_phase == Phase::inGame && m_own.hasMetadata) {
        send_call(kSwapItems, swap_items_body(m_own.group, slotTo, slotFrom, instanceTo, instanceFrom));
        log(Event::Level::info, "swap (3900): paperdoll slot " + std::to_string(slotFrom) + " to slot " + std::to_string(slotTo));
    }
}

void RepClient::use_item(std::uint32_t bagSlot) {
    if (m_phase == Phase::inGame && m_own.hasMetadata) {
        send_call(kUseItem, use_item_body(m_own.group, bagSlot));
        log(Event::Level::info, "use item (2917): bag slot " + std::to_string(bagSlot));
    }
}

void RepClient::drop_items(const std::vector<DropItemEntry>& entries) {
    if (m_phase == Phase::inGame && m_own.hasMetadata && !entries.empty()) {
        send_call(kDropInventoryItems, drop_inventory_items_body(m_own.group, entries));
        log(Event::Level::info, "drop (1838): " + std::to_string(entries.size()) + " entries, the first: bag slot "
                                    + std::to_string(entries.front().slot) + ", quantity " + std::to_string(entries.front().quantity));
    }
}

void RepClient::drop_paperdoll_item(std::uint32_t paperdollSlot, std::uint32_t quantity) {
    if (m_phase == Phase::inGame && m_own.hasMetadata) {
        send_call(kDropPaperdollItem, drop_paperdoll_item_body(m_own.group, paperdollSlot, quantity));
        log(Event::Level::info, "drop (826): paperdoll slot " + std::to_string(paperdollSlot) + ", quantity " + std::to_string(quantity));
    }
}

void RepClient::set_interaction(const std::optional<InteractionRequest>& request) {
    if (request.has_value() == m_own.interaction.has_value()
        && (!request || (request->reference == m_own.interaction->reference && request->optionId == m_own.interaction->optionId))) {
        return;
    }
    m_own.interaction = request;
    if (request) {
        log(Event::Level::info, "interaction request: action " + std::to_string(request->action) + ", option "
                                    + std::to_string(request->optionId) + ", object " + hex_of(request->reference.actor));
    }
}

void RepClient::cancel_interaction() {
    if (!m_own.interaction) {
        return;
    }
    const InteractionReference reference = m_own.interaction->reference;
    m_own.interaction.reset();
    if (m_phase == Phase::inGame && m_own.hasMetadata) {
        send_call(kCancelCommittedInteraction, interaction_cancel_body(m_own.group, reference));
        log(Event::Level::info, "interaction cancel (2473): object " + hex_of(reference.actor));
    }
}

void RepClient::end_cutscene() {
    if (m_phase == Phase::inGame && m_own.hasMetadata) {
        send_call(kCutsceneEnded, cutscene_ended_body(m_own.group));
        log(Event::Level::info, "the end of the cutscene (6705): the input stays blocked until 6709");
    }
}

std::uint64_t RepClient::server_time_ns(Clock::time_point now) const {
    return m_hasServerClock ? static_cast<std::uint64_t>(steady_ns(now) + m_serverOffsetNs) : 0;
}

std::vector<Bytes> RepClient::tick(Clock::time_point now) {
    // The slow correction of the clock: 2 ms for each tick until the difference of the last 335 is applied.
    if (m_clockCorrectionNs != 0) {
        const std::int64_t step = std::clamp(m_clockCorrectionNs, -kClockStepNs, kClockStepNs);
        m_serverOffsetNs += step;
        m_clockCorrectionNs -= step;
    }
    if (m_phase == Phase::waitingForRepConnection && m_carrier.state() == Carrier::State::connected) {
        log(Event::Level::info, "carrier connection established");
        send_registration();
        m_lastGameMessage = now;
        set_phase(Phase::waitingForActorGameConnection);
    }
    if (m_phase != Phase::ended && m_phase != Phase::idle && now - m_lastGameMessage > kGameMessageTimeout) {
        end("connection_ingame_timeout: no game message for 30 s", true);
    }
    if (m_phase == Phase::inGame) {
        const bool stale = now - m_lastMovementState > kMovementStateTimeout;
        if (stale != m_movementStale) {
            m_movementStale = stale;
            log(stale ? Event::Level::warning : Event::Level::info,
                stale ? "no movement state for 2 s: the input has no actions" : "movement states again: the input has its actions");
        }
        if (m_options.sendInput && m_own.hasMetadata && m_own.hasPosition) {
            send_input(now);
        }
    }
    return m_carrier.tick(now);
}

std::vector<RepClient::Event> RepClient::take_events() {
    std::vector<Event> out;
    out.swap(m_events);
    return out;
}

} // namespace swc::net
