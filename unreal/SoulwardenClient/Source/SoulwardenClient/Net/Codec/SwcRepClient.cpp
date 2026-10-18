#include "Net/Codec/SwcRepClient.h"

#include <cstdio>

namespace swc::net {
namespace {

namespace decoder = soulwarden::backend::tests;

/** The route of the registration request: the backend reads no route for it. */
constexpr ActorId kNoRoute{};
/** The entries of one input report: the newest one and the 4 before it (a live client, protocol notes 4.7). */
constexpr std::size_t kEntriesPerReport = 5;
/** The action bits of an entry with no input after the loading screen (protocol notes 4.7, the captures). */
const std::vector<unsigned> kRestActionBits{475, 497, 506, 569};
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
    }
    for (const decoder::Record& record : bundle.records) {
        ++m_counts.records;
        if (record.interest == 1) {
            apply_own(record, now);
        } else {
            apply_other(record);
        }
    }
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
    // The camera of a character at rest at its position, north, no pitch: the camera estimate of the backend keeps the
    // position (protocol notes 4.7). No position field yet (the flag 0x8000): the client has no movement of its own.
    entry.camera = {m_own.position.x + kCameraRight, m_own.position.y - kCameraBehind, m_own.position.z + kCameraUp};
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

std::vector<Bytes> RepClient::tick(Clock::time_point now) {
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
