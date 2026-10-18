#include "Net/Codec/SwcRepMessages.h"

#include <algorithm>
#include <cmath>

namespace swc::net {
namespace {

constexpr std::uint8_t kEnvelopeTarget = 0x01;
constexpr std::uint8_t kEnvelopeSource = 0x02;
constexpr std::uint8_t kEnvelopeExtension = 0x04;

/** The text of the 6 entries of the version map of the registration request (the made-up client of the backend tests). */
constexpr const char* kClientVersion = "1.400.6031.6004151";

/** The height of a world position on the wire: 16 bits of the range -100 m to 1000 m. */
constexpr float kHeightMin = -100.0F;
constexpr float kHeightRange = 1100.0F;

std::string hex_text(std::string_view bytes) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (const unsigned char byte : bytes) {
        out += digits[byte >> 4];
        out += digits[byte & 15];
    }
    return out;
}

bool read_actor(Reader& reader, ActorId& actor) {
    const auto bytes = reader.raw(actor.size());
    if (!reader.ok()) {
        return false;
    }
    std::copy(bytes.begin(), bytes.end(), actor.begin());
    return true;
}

/** identity := flags:u8, then a 16-byte id when bit 0 is set, else a text. */
bool read_identity(Reader& reader, ActorId* actor) {
    const std::uint8_t flags = reader.u8();
    if ((flags & 1) != 0) {
        ActorId id{};
        if (!read_actor(reader, id)) {
            return false;
        }
        if (actor != nullptr) {
            *actor = id;
        }
    } else {
        reader.string();
    }
    return reader.ok();
}

bool read_reference(Reader& reader, ActorReference& reference) {
    reference.hub = reader.u32();
    return read_actor(reader, reference.actor) && read_actor(reader, reference.type);
}

void write_position(Writer& out, float x, float y, std::uint16_t height) {
    out.f32(x);
    out.f32(y);
    out.u16(height);
}

/** The height of the camera: rounded (the made-up client of the backend tests). */
std::uint16_t camera_height(float z) {
    const float clamped = std::min(std::max(z, kHeightMin), kHeightMin + kHeightRange);
    return static_cast<std::uint16_t>(std::lround((clamped - kHeightMin) / kHeightRange * 65535.0F));
}

/** The height of the character: cut off, as the retail client does (protocol notes 4.7). */
std::uint16_t character_height(float z) {
    const float clamped = std::min(std::max(z, kHeightMin), kHeightMin + kHeightRange);
    return static_cast<std::uint16_t>((clamped - kHeightMin) / kHeightRange * 65535.0F);
}

void write_vector(Writer& out, const WorldPosition& value) {
    out.f32(value.x);
    out.f32(value.y);
    out.f32(value.z);
}

void write_input_entry(Writer& w, const InputEntry& entry, const InputEntry* before) {
    std::uint32_t mask = 0;
    Writer fields;
    if (entry.flags != (before != nullptr ? before->flags : 0)) {
        mask |= 0x01;
        fields.size64(entry.flags);
    }
    if (entry.permissions >= 0) {
        mask |= 0x02;
        fields.u8(static_cast<std::uint8_t>(entry.permissions));
    }
    if (entry.groundDistance >= 0) {
        mask |= 0x10;
        fields.u16(static_cast<std::uint16_t>(entry.groundDistance));
    }
    // The action words: in pairs, and one word alone when the count is odd.
    std::vector<std::uint32_t> words(entry.actionWords, 0);
    for (const unsigned bit : entry.actionBits) {
        if (bit / 32 < words.size()) {
            words[bit / 32] |= 1U << (bit % 32);
        }
    }
    fields.u8(static_cast<std::uint8_t>(entry.actionWords));
    for (unsigned index = 0; index < entry.actionWords; index += 2) {
        const std::uint64_t high = index + 1 < entry.actionWords ? words[index + 1] : 0;
        fields.size64(words[index] | (high << 32));
    }
    for (int axis = 0; axis < 4; ++axis) {
        fields.u8(0x80);
    }
    if (entry.stepTime != 0.0F) {
        mask |= 0x20;
        fields.f32(entry.stepTime);
    }
    if (entry.hasLook) {
        mask |= 0x40;
        fields.f32(entry.yaw);
        fields.f32(entry.pitch);
    }
    if (entry.hasCamera) {
        mask |= 0x80;
        write_position(fields, entry.camera.x, entry.camera.y, camera_height(entry.camera.z));
    }
    fields.u8(3); // The target kind 3: no target data.
    if ((entry.flags & 0x1) != 0) {
        fields.size64(0);
        fields.size64(0);
        fields.size64(0);
    }
    // The mask bits after bit 8 have no fixed number: a flag takes its bits also when its field is not in the entry.
    unsigned bit = 9;
    if ((entry.flags & 0x800) != 0) {
        ++bit;
    }
    if ((entry.flags & 0x100) != 0) {
        bit += 2;
    }
    ++bit; // The value of entry +0x380: not sent (0).
    if ((entry.flags & 0x4000) != 0) {
        ++bit;
    }
    fields.size(0); // The hits of a projectile with no flight.
    fields.size(0); // The shots.
    fields.size(static_cast<std::uint32_t>(entry.contacts.size())); // The melee contacts.
    for (const MeleeContactItem& contact : entry.contacts) {
        write_melee_contact(fields, contact);
    }
    if ((entry.flags & kInputFlagPosition) != 0) {
        if (entry.hasCharacter) {
            mask |= 1U << bit;
            write_position(fields, entry.character.x, entry.character.y, character_height(entry.character.z));
        }
        ++bit;
    }
    if (entry.value470) {
        mask |= 1U << (bit + 1);
        fields.u8(1);
    }
    w.u8(static_cast<std::uint8_t>(mask));
    w.u8(static_cast<std::uint8_t>(mask >> 8));
    w.u8(static_cast<std::uint8_t>(mask >> 16));
    w.u16(entry.sequence);
    w.raw(fields.data());
}

} // namespace

void write_melee_contact(Writer& out, const MeleeContactItem& contact) {
    out.u32(contact.provider);
    out.raw(contact.groupActor);
    out.u64(contact.group);
    out.raw(contact.actor);
    out.u64(contact.entity);
    out.u64(contact.facet);
    write_vector(out, contact.point);
    write_vector(out, contact.normal);
    write_vector(out, contact.direction);
}

std::uint64_t group_of(const ActorId& actor) {
    std::uint64_t value = 0;
    for (int index = 7; index >= 0; --index) {
        value = (value << 8) | actor[static_cast<std::size_t>(index)];
    }
    return value;
}

std::string hex_of(std::span<const std::uint8_t> bytes) {
    return hex_text(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
}

bool read_server_frames(std::span<const std::uint8_t> data, std::vector<ServerMessage>& out, std::string& problem) {
    Reader reader(data);
    while (reader.ok() && reader.left() > 0) {
        const std::uint32_t size = reader.size();
        const auto envelope = reader.raw(size);
        if (!reader.ok()) {
            problem = "a server frame is longer than its message";
            return false;
        }
        Reader message(envelope);
        const std::uint8_t flags = message.u8();
        if ((flags & kEnvelopeTarget) != 0) {
            message.raw(8);
        }
        if ((flags & kEnvelopeSource) != 0) {
            message.raw(8);
        }
        if ((flags & kEnvelopeExtension) != 0) {
            problem = "an envelope with an extension (flags " + std::to_string(flags) + ")";
            return false;
        }
        const std::uint8_t present = message.u8();
        if (!message.ok() || present != 1) {
            problem = "an envelope with no message";
            return false;
        }
        ServerMessage item;
        item.type = message.size();
        if (!message.ok()) {
            problem = "an envelope with no type";
            return false;
        }
        const auto body = envelope.subspan(message.position());
        item.body.assign(body.begin(), body.end());
        out.push_back(std::move(item));
    }
    return reader.ok();
}

Bytes client_frame(const ActorId& route, std::uint32_t type, std::span<const std::uint8_t> body) {
    Writer payload;
    payload.raw(route);
    payload.u8(0); // Envelope flags.
    payload.u8(1); // A message is present.
    payload.size(type);
    payload.raw(body);
    return soulwarden::backend::server::hub::encode_frame(payload.data());
}

std::string local_ticket_hex(const std::string& steamId, const std::string& personaName) {
    const std::string personaHex = hex_text(std::string_view(personaName).substr(0, 128));
    return hex_text("SOULWARDEN-LOCAL-TICKET;1;" + steamId + ";" + personaHex);
}

Bytes registration_body(const LoginToken& token, const std::string& steamId, const std::string& ticketHex) {
    Writer w;
    w.u32(1);
    w.size(6); // The version map.
    for (std::uint32_t key = 0; key < 6; ++key) {
        w.u32(key);
        w.string(kClientVersion);
    }
    w.string("");
    // The login token (writers 0x7D5F30 to 0x7D42C0).
    w.string(token.signature);
    w.string(token.repAddress);
    w.string(token.worldId);
    w.string(token.characterId);
    w.string(token.personaId);
    w.u32(token.steamAppId);
    w.string(token.steamUserId);
    w.string(token.channelId);
    w.string(token.ticketId);
    w.string("");
    w.string("");
    w.u32(token.issueTime);
    w.u32(token.generateTime);
    w.u32(token.accountAge);
    w.string("");
    w.u32(token.tokenVersion);
    w.u8(0);
    w.string("");
    w.u8(1); // present, with the flag 0 (the captures).
    w.u8(0);
    w.string("");
    w.string("");
    w.string("");
    // The client data (writer 0x7D54A0): u32 1, u8 1, the Steam application, the Steam id, the ticket.
    w.u32(1);
    w.u8(1);
    w.u32(token.steamAppId);
    w.string(steamId);
    w.string("steam|" + ticketHex);
    w.string("");
    w.u32(0);
    for (int index = 0; index < 5; ++index) {
        w.string("");
    }
    // Two identities in the text form and one flag.
    w.u8(0);
    w.string("");
    w.u8(0);
    w.string("");
    w.u8(0);
    return w.data();
}

std::optional<RegistrationResponse> read_registration_response(std::span<const std::uint8_t> body) {
    Reader reader(body);
    RegistrationResponse response;
    response.result = static_cast<std::int32_t>(reader.u32());
    response.serverTime = reader.u64();
    response.serverVersion = reader.string();
    reader.string();
    reader.u8();
    reader.u8();
    reader.u8();
    response.antiCheatRequired = reader.u8() != 0;
    if (!reader.ok()) {
        return std::nullopt;
    }
    return response;
}

const char* registration_result_text(std::int32_t result) {
    switch (result) {
    case 0:
        return "accepted";
    case 37:
        return "CapacityLimitReached";
    case -10:
        return "ClaimConnectionTicketFailed";
    case -80:
        return "VersionCheckFailed";
    case -90:
        return "TypeIndexMismatch";
    case -160:
        return "LoginTicketInvalid";
    case -200:
        return "EasyAntiCheatFailure";
    case -210:
        return "EOSIsRequired";
    case -900:
        return "HubNotReady";
    default:
        return "";
    }
}

std::optional<SelfIdentification> read_self_identification(std::span<const std::uint8_t> body) {
    Reader reader(body);
    SelfIdentification message;
    if (!read_identity(reader, &message.sessionActor) || !read_reference(reader, message.playerManager)
        || !read_reference(reader, message.clientProxy) || !read_reference(reader, message.third)) {
        return std::nullopt;
    }
    // The level block: instance:u32 list<u32> flag flag u32 u32.
    message.instance = reader.u32();
    const std::uint32_t count = reader.size();
    for (std::uint32_t index = 0; index < count && reader.ok(); ++index) {
        reader.u32();
    }
    reader.u8();
    reader.u8();
    reader.u32();
    reader.u32();
    if (!read_identity(reader, nullptr) || reader.left() != 0) {
        return std::nullopt;
    }
    return message;
}

std::optional<LevelInfo> read_level_info(std::span<const std::uint8_t> body) {
    Reader reader(body);
    LevelInfo info;
    info.level = reader.string();
    info.terrain = reader.string();
    for (int index = 0; index < 4; ++index) {
        reader.u32();
    }
    reader.u64();
    // A list of {u8, u32, u64, u32}.
    const std::uint32_t count = reader.size();
    for (std::uint32_t index = 0; index < count && reader.ok(); ++index) {
        reader.u8();
        reader.u32();
        reader.u64();
        reader.u32();
    }
    reader.u8();
    info.instance = reader.u8();
    info.reload = reader.u8() != 0;
    reader.u8();
    info.version = reader.u64();
    if (!reader.ok() || reader.left() != 0) {
        return std::nullopt;
    }
    return info;
}

std::optional<std::uint32_t> read_reason(std::span<const std::uint8_t> body) {
    Reader reader(body);
    const std::uint32_t reason = reader.u32();
    if (!reader.ok()) {
        return std::nullopt;
    }
    return reason;
}

WorldPosition read_world_position(std::span<const std::uint8_t> value) {
    Reader reader(value);
    WorldPosition position;
    position.x = reader.f32();
    position.y = reader.f32();
    position.z = kHeightMin + static_cast<float>(reader.u16()) / 65535.0F * kHeightRange;
    return position;
}

Quaternion read_quaternion(std::span<const std::uint8_t> value) {
    constexpr float kStep = 0.0055459356F;
    constexpr float kLowest = 0.70710677F;
    Quaternion out;
    if (value.empty()) {
        return out;
    }
    const std::uint8_t header = value[0];
    const unsigned largest = (header >> 5) & 3U;
    float components[4]{};
    float sum = 0.0F;
    std::size_t next = 1;
    for (unsigned component = 0; component < 4; ++component) {
        if (component == largest || (header & (2U << component)) != 0) {
            continue;
        }
        const float part = next < value.size() ? static_cast<float>(value[next]) * kStep - kLowest : 0.0F;
        ++next;
        components[component] = part;
        sum += part * part;
    }
    const float big = std::sqrt(std::max(0.0F, 1.0F - sum));
    components[largest] = (header & 1U) != 0 ? -big : big;
    out.x = components[0];
    out.y = components[1];
    out.z = components[2];
    out.w = components[3];
    return out;
}

CheckedBundle read_state_bundle(std::span<const std::uint8_t> body) {
    namespace decoder = soulwarden::backend::tests;
    CheckedBundle out;
    decoder::Bundle& bundle = out.bundle;
    Reader reader(body);
    if (reader.u8() != 1) {
        return out;
    }
    bundle.sequence = reader.size64();
    bundle.instance = reader.u8();
    reader.u8();
    const std::uint8_t unreliable = reader.u8();
    const std::uint8_t control = reader.u8();
    if (unreliable > 1 || control > 1) {
        return out;
    }
    if (control == 1) {
        // The control block: stopCount:size count:size (interest:u16)*count.
        const std::uint32_t stopCount = reader.size();
        const std::uint32_t count = reader.size();
        if (stopCount > count || count > 100) {
            return out;
        }
        for (std::uint32_t index = 0; index < count; ++index) {
            (index < stopCount ? bundle.stops : bundle.pauses).push_back(reader.u16());
        }
    }
    const std::uint32_t size = reader.size();
    if (!reader.ok() || reader.position() + size != body.size()) {
        return out;
    }
    out.headerOk = true;
    const auto payload = body.subspan(reader.position());
    std::size_t position = 0;
    while (position < payload.size()) {
        Reader head(payload.subspan(position));
        decoder::Record record;
        record.interest = head.size();
        const unsigned count = head.u8();
        if (!head.ok()) {
            return out;
        }
        position += head.position();
        for (unsigned index = 0; index < count; ++index) {
            Reader fragmentHead(payload.subspan(position));
            decoder::Fragment fragment;
            fragment.slot = fragmentHead.size();
            fragment.type = fragmentHead.size();
            position += fragmentHead.position();
            if (!fragmentHead.ok() || !decoder::detail::read_state(payload, position, fragment)) {
                out.failedInterest = record.interest;
                out.failedSlot = fragment.slot;
                out.failedType = fragment.type;
                if (!record.fragments.empty()) {
                    bundle.records.push_back(std::move(record));
                }
                return out;
            }
            record.fragments.push_back(std::move(fragment));
        }
        bundle.records.push_back(std::move(record));
    }
    out.complete = position == payload.size();
    bundle.complete = out.complete;
    return out;
}

Bytes input_report_body(const std::vector<InputEntry>& entries, std::uint64_t target, std::uint64_t entity) {
    Writer w;
    w.u64(target);
    w.u64(entity);
    // The header: 4 bytes, 3 numbers, u8, the optional flag, u64, u8, 3 numbers, u8, the pair count, 4 numbers.
    w.zeros(4);
    w.size64(0);
    w.size64(0);
    w.size64(0);
    w.u8(0);
    w.u8(0);
    w.u64(0);
    w.u8(0);
    w.size64(0);
    w.size64(0);
    w.size64(0);
    w.u8(0);
    w.size64(0);
    w.size64(0);
    w.size64(0);
    w.size64(0);
    w.size64(0);
    w.u8(static_cast<std::uint8_t>(entries.size()));
    for (std::size_t index = 0; index < entries.size(); ++index) {
        write_input_entry(w, entries[index], index == 0 ? nullptr : &entries[index - 1]);
    }
    return w.data();
}

} // namespace swc::net
