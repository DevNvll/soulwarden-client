#include "Net/Codec/SwcRepClient.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

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
            if (const auto health = decoder::field_of(fragment, 0, 0); health.size() == 4) {
                m_own.health = f32_at(health);
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
        } else if (fragment.slot == kSlotMana && fragment.type == kStateMana.index) {
            if (const auto value = decoder::field_of(fragment, 0, 0); value.size() == 4) {
                m_own.mana = f32_at(value);
                m_own.hasMana = true;
            }
            if (const auto value = decoder::field_of(fragment, 0, 1); value.size() == 4) {
                m_own.manaMax = f32_at(value);
            }
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
                }
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

WorldPosition RepClient::reported_position(const WorldPosition& character) {
    const float dx = character.x - m_reported.x;
    const float dy = character.y - m_reported.y;
    const float dz = character.z - m_reported.z;
    const float horizontal = std::sqrt(dx * dx + dy * dy);
    if (!m_hasReported || horizontal > kReportJump || std::fabs(dz) > kReportJump) {
        m_hasReported = true;
        m_reported = character;
        return m_reported;
    }
    const float limit = kReportHorizontalSpeed * kStepSeconds;
    const float share = horizontal > limit ? limit / horizontal : 1.0F;
    m_reported.x += dx * share;
    m_reported.y += dy * share;
    m_reported.z += std::clamp(dz, -kReportFallSpeed * kStepSeconds, kReportRiseSpeed * kStepSeconds);
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
        if (m_motion->forward && !m_movementStale) {
            entry.actionBits.insert(entry.actionBits.end(), kForwardActionBits.begin(), kForwardActionBits.end());
        }
        if (m_motion->pressSerial != m_pressSerial) {
            // A key press: its bits in one entry (protocol notes 4.15: the weapon keys).
            m_pressSerial = m_motion->pressSerial;
            entry.actionBits.insert(entry.actionBits.end(), m_motion->pressBits.begin(), m_motion->pressBits.end());
        }
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
