#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <span>
#include <string>

#include "Net/Codec/SwcRepMessages.h"

// The world time of the backend (Soulwarden protocol notes 4.43, docs/systems/world-time-and-weather.md; research world-time.md 2):
// the request, the record, the phase, and the time of day that the client calculates itself from one record and its copy of the
// server clock (the registration reply and 335). The server sends no hour stream. Plain C++ with no engine header, and header only
// (each tool that builds SwcRepClient.cpp needs no other source). All numbers of a body are big-endian.
//
//   4248 TimeComponentServerFacet_SyncToClient   (client to server)  target:u64 entity:u64
//   7036 TimeComponentClientFacet_SyncTimeAndPhase (server to client) target:u64 entity:u64 record:60 phase:u32   (80 bytes)
//   4247 TimeComponentClientFacet_SyncDayPhase    (server to client)  target:u64 entity:u64 phase:u32            (20 bytes)
//
//   record := a:f32 b:f32 offset:f32 dawnTime:u32 dawnDuration:u32 duskTime:u32 duskDuration:u32 dayLength:u32 nightLength:u32
//             anchor:u64 dawnStart:f32 dayStart:f32 duskStart:f32 nightStart:f32
//
// The target is the Time facet of the own player: G + kTimeComponent. The client sends 4248 one time for each world entry (live check
// L0 of the backend stage 2.2); a travel and a respawn keep the record (live check L2).

namespace swc::net {

/** The Time component of the root entity of the player slice: the target of its facet is G plus this id (modulo 2^64). */
inline constexpr std::uint64_t kTimeComponent = 0xD41EB32C0F154004ULL;

/** The phases of the day (client code 0x6A08270): night 0, dawn 1, day 2, dusk 3. */
inline constexpr std::uint32_t kPhaseNight = 0;
inline constexpr std::uint32_t kPhaseDawn = 1;
inline constexpr std::uint32_t kPhaseDay = 2;
inline constexpr std::uint32_t kPhaseDusk = 3;

/** One record of the Time component (the 60 bytes of a 7036; client reader 0x69F2530). The defaults are the client defaults. */
struct TimeRecord {
    /** No reader of a and b is known; the backend sends -1. */
    float a{-1.0F};
    float b{-1.0F};
    /** A part of 365 cycles that the client adds to the time from the anchor (0x6A0ADC0). */
    float offset{0.0F};
    /** The game second of the day at which the day part starts (06:00), and its visual length in real seconds. */
    std::uint32_t dawnTime{21600};
    std::uint32_t dawnDuration{60};
    /** The game second of the day at which the night part starts (19:00), and its visual length in real seconds. */
    std::uint32_t duskTime{68400};
    std::uint32_t duskDuration{60};
    /** Real seconds of the day part and of the night part (26 and 44 game seconds in one real second). */
    std::uint32_t dayLength{1800};
    std::uint32_t nightLength{900};
    /** A time of the server clock (nanoseconds) at which the cycle is at the part 0, before the offset. */
    std::uint64_t anchor{0};
    /** The phase edges: parts of the cycle (0 to 1). dawnStart and nightStart are also the edges of the hour formula. */
    float dawnStart{0.0F};
    float dayStart{0.0F};
    float duskStart{0.0F};
    float nightStart{0.0F};
};

struct TimeAndPhase {
    ComponentCall call;
    TimeRecord record;
    std::uint32_t phase{kPhaseDay};
};

struct DayPhaseCall {
    ComponentCall call;
    std::uint32_t phase{kPhaseDay};
};

namespace detail {

inline constexpr std::size_t kTimeAndPhaseSize = 80;
inline constexpr std::size_t kDayPhaseSize = 20;
inline constexpr double kDaySeconds = 86400.0;
inline constexpr std::uint64_t kSecondNs = 1'000'000'000ULL;

inline ComponentCall read_call(Reader& reader) {
    ComponentCall call;
    call.target = reader.u64();
    call.entity = reader.u64();
    return call;
}

} // namespace detail

/** @return The name of a phase of the day. */
inline const char* day_phase_text(std::uint32_t phase) {
    switch (phase) {
    case kPhaseNight:
        return "night";
    case kPhaseDawn:
        return "dawn";
    case kPhaseDay:
        return "day";
    case kPhaseDusk:
        return "dusk";
    default:
        return "unknown";
    }
}

/** @return The body of 4248 for the own entity G (16 bytes). */
inline Bytes time_request_body(std::uint64_t group) {
    Writer body;
    body.u64(group + kTimeComponent);
    body.u64(group);
    return body.data();
}

/** @return The 7036 of a body of exactly 80 bytes, or no value. */
inline std::optional<TimeAndPhase> read_time_and_phase(std::span<const std::uint8_t> body) {
    if (body.size() != detail::kTimeAndPhaseSize) {
        return std::nullopt;
    }
    Reader reader(body);
    TimeAndPhase out;
    out.call = detail::read_call(reader);
    TimeRecord& record = out.record;
    record.a = reader.f32();
    record.b = reader.f32();
    record.offset = reader.f32();
    record.dawnTime = reader.u32();
    record.dawnDuration = reader.u32();
    record.duskTime = reader.u32();
    record.duskDuration = reader.u32();
    record.dayLength = reader.u32();
    record.nightLength = reader.u32();
    record.anchor = reader.u64();
    record.dawnStart = reader.f32();
    record.dayStart = reader.f32();
    record.duskStart = reader.f32();
    record.nightStart = reader.f32();
    out.phase = reader.u32();
    if (!reader.ok() || reader.left() != 0) {
        return std::nullopt;
    }
    return out;
}

/** @return The 4247 of a body of exactly 20 bytes, or no value. */
inline std::optional<DayPhaseCall> read_day_phase(std::span<const std::uint8_t> body) {
    if (body.size() != detail::kDayPhaseSize) {
        return std::nullopt;
    }
    Reader reader(body);
    DayPhaseCall out;
    out.call = detail::read_call(reader);
    out.phase = reader.u32();
    if (!reader.ok() || reader.left() != 0) {
        return std::nullopt;
    }
    return out;
}

/** @return True for a record that the formulas can use: the lengths above 0 (the client divides by them), dawn before dusk. */
inline bool usable(const TimeRecord& record) {
    return record.dayLength > 0 && record.nightLength > 0 && record.dawnTime < record.duskTime && record.duskTime < 86400U;
}

/**
 * @return The part of the cycle (0 to 1) at a time of the server clock (client code 0x6A0ADC0, the steps of the client): the absolute
 * time from the anchor as one float32, plus the float32 year (365 cycles) times the offset, as a whole number of nanoseconds, then the
 * remainders of the year and of the cycle.
 */
inline double cycle_part(const TimeRecord& record, std::uint64_t serverNs) {
    // 0x6A0ADC0: CVTSI2SS of the absolute time from the anchor, MULSS of the float year and the offset, ADDSS, a whole number, and the
    // remainders of the year and of the cycle. Each step in its own statement: no fused multiply and add.
    const std::uint64_t cycleNs = (static_cast<std::uint64_t>(record.dayLength) + record.nightLength) * detail::kSecondNs;
    if (cycleNs == 0) {
        return 0.0;
    }
    const std::uint64_t year = cycleNs * 365;
    const std::uint64_t difference = serverNs >= record.anchor ? serverNs - record.anchor : record.anchor - serverNs;
    const auto elapsed = static_cast<float>(difference);
    const auto yearFloat = static_cast<float>(year);
    const float moved = yearFloat * record.offset;
    const float sum = elapsed + moved;
    const auto whole = static_cast<std::uint64_t>(sum);
    return static_cast<double>((whole % year) % cycleNs) / static_cast<double>(cycleNs);
}

/**
 * @return The game second of the day (0 to 86,400) at a part of the cycle (client code 0x6A09E60): the night part before dawnStart,
 * the day part to nightStart, and the night part after it.
 */
inline double game_second(const TimeRecord& record, double part) {
    // 0x6A09E60: the game seconds of the night part, the real seconds from 00:00 to dawnTime, and the three parts of the cycle.
    const double night = (detail::kDaySeconds - record.duskTime) + record.dawnTime;
    const double cycle = static_cast<double>(record.dayLength) + record.nightLength;
    const double before = record.dawnTime / night * record.nightLength;
    const double x = cycle * part;
    if (part <= record.dawnStart) {
        return x / record.nightLength * night;
    }
    if (part <= record.nightStart) {
        return (x - before) / record.dayLength * (detail::kDaySeconds - night) + record.dawnTime;
    }
    return (x - before - record.dayLength) / record.nightLength * night + record.dawnTime + (detail::kDaySeconds - night);
}

/** @return The phase at a part of the cycle (client code 0x6A08270): night before dawnStart and after nightStart, then dawn, day, dusk. */
inline std::uint32_t phase_at(const TimeRecord& record, double part) {
    if (part < record.dawnStart || part > record.nightStart) {
        return kPhaseNight;
    }
    if (part < record.dayStart) {
        return kPhaseDawn;
    }
    if (part < record.duskStart) {
        return kPhaseDay;
    }
    return kPhaseDusk;
}

/**
 * @return The hour of the engine (0 to 24) at a game second (client code 0x6434700 with the three bytes of 0x6A09E60: the hour, the
 * minute, and the whole second): it moves in steps of one game second.
 */
inline double engine_hour(double gameSecond) {
    const double whole = std::floor(std::fmod(std::max(gameSecond, 0.0), detail::kDaySeconds));
    return whole / 3600.0;
}

/** @return "HH:MM:SS" of a game second of the day (the whole second). */
inline std::string clock_text(double gameSecond) {
    const auto whole = static_cast<unsigned>(std::floor(std::fmod(std::max(gameSecond, 0.0), detail::kDaySeconds)));
    char text[16];
    std::snprintf(text, sizeof(text), "%02u:%02u:%02u", whole / 3600, whole / 60 % 60, whole % 60);
    return text;
}

} // namespace swc::net
