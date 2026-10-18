// Tests of the world time of the client (Net/Codec/SwcWorldTime.h; Soulwarden protocol notes 4.43, docs/systems/world-time-and-weather.md)
// with the bytes and the values that the backend makes: the record 7036 and the phase 4247 of its encoders (server/rep/time_messages.cpp)
// with the records of its day cycle (world/world_time.cpp: day_cycle::record), the request 4248 against the checks of its session
// (rep_session.cpp, handle_time_request: 16 bytes, the target G + D41EB32C0F154004, the entity G), the part of the cycle against the
// client model of the backend (day_cycle::client_part), and the hour and the phase against the backend cycle (day_cycle::game_second,
// phase_of) over two cycles. tools/test-environment.sh builds it with the system compiler; the Soulwarden sources are read only.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "Net/Codec/SwcWorldTime.h"
#include "server/hub/wire.h"
#include "server/rep/time_messages.h"
#include "world/world_time.h"

namespace {

namespace backend = soulwarden::backend;
namespace rep = soulwarden::backend::server::rep;
namespace day_cycle = soulwarden::backend::world::day_cycle;
using backend::world::DayCycle;
using backend::world::DayPhase;

int g_failures = 0;
int g_checks = 0;

void check(bool condition, const std::string& what) {
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::printf("FAIL: %s\n", what.c_str());
    }
}

constexpr std::uint64_t kSecond = 1'000'000'000ULL;
constexpr std::uint64_t kGroup = 0x0200000053534F55ULL;

/** @return The position of a time of the day "HH:MM:SS" (game seconds) in the backend cycle. */
std::uint64_t position_of(const DayCycle& cycle, double gameSecond) {
    return day_cycle::position_of(cycle, gameSecond);
}

/** @return The difference of two game seconds of the day over the midnight wrap. */
double day_difference(double a, double b) {
    double difference = std::fabs(a - b);
    return std::min(difference, 86400.0 - difference);
}

/** @return The 7036 of the backend for a position at a time of its server clock, read by the client. */
std::optional<swc::net::TimeAndPhase> backend_record(const DayCycle& cycle, std::uint64_t serverNow, std::uint64_t position, backend::world::TimeRecord& out) {
    out = day_cycle::record(cycle, serverNow, position);
    const backend::server::hub::Bytes body = rep::encode_time_and_phase(kGroup, out, day_cycle::phase_of(cycle, position));
    check(body.size() == 80, "7036: the backend body has 80 bytes");
    return swc::net::read_time_and_phase(body);
}

void test_record_fields() {
    const DayCycle cycle;
    backend::world::TimeRecord made;
    const std::uint64_t now = 100'000ULL * kSecond;   // the server time of the research vector (1e14 ns)
    const auto read = backend_record(cycle, now, position_of(cycle, 43200.0), made);
    check(read.has_value(), "7036: the client reads the body of the backend");
    if (!read) {
        return;
    }
    check(read->call.target == kGroup + swc::net::kTimeComponent && read->call.target == rep::time_target(kGroup) && read->call.entity == kGroup,
          "7036: the target of the own Time facet and the entity G");
    const swc::net::TimeRecord& record = read->record;
    check(record.a == -1.0F && record.b == -1.0F && record.offset == made.offset, "7036: the fields a, b (-1), and the offset");
    check(record.dawnTime == 21600 && record.dawnDuration == 60 && record.duskTime == 68400 && record.duskDuration == 60 && record.dayLength == 1800
              && record.nightLength == 900,
          "7036: the cycle (06:00, 19:00, 60 s, 1800 s of day, 900 s of night)");
    check(record.anchor == made.anchor && record.anchor == now - 2700ULL * kSecond, "7036: the anchor one cycle before the server clock");
    check(record.dawnStart == made.starts[0] && record.dayStart == made.starts[1] && record.duskStart == made.starts[2]
              && record.nightStart == made.starts[3],
          "7036: the four phase edges");
    check(read->phase == swc::net::kPhaseDay && swc::net::usable(record), "7036: the phase day at noon; a usable record");
    // A body of a different size is not the message.
    const backend::server::hub::Bytes body = rep::encode_time_and_phase(kGroup, made, DayPhase::day);
    check(!swc::net::read_time_and_phase(std::span(body).first(79)).has_value(), "7036: 79 bytes do not read");
    std::vector<std::uint8_t> longer(body.begin(), body.end());
    longer.push_back(0);
    check(!swc::net::read_time_and_phase(longer).has_value(), "7036: 81 bytes do not read");
    // A record with no day length cannot be used (the client divides by it).
    swc::net::TimeRecord zero = record;
    zero.dayLength = 0;
    check(!swc::net::usable(zero), "a record with no day length is not usable");
}

void test_day_phase_and_request() {
    for (const DayPhase phase : {DayPhase::night, DayPhase::dawn, DayPhase::day, DayPhase::dusk}) {
        const backend::server::hub::Bytes body = rep::encode_day_phase(kGroup, phase);
        const auto read = swc::net::read_day_phase(body);
        check(read && read->phase == static_cast<std::uint32_t>(phase) && read->call.entity == kGroup && read->call.target == rep::time_target(kGroup),
              std::string("4247: the phase ") + swc::net::day_phase_text(static_cast<std::uint32_t>(phase)));
    }
    check(!swc::net::read_day_phase(rep::encode_clock_sync(5)).has_value(), "4247: a body of 8 bytes does not read");
    // 4248: the checks of the backend session (handle_time_request): 16 bytes, the target of the Time facet, the entity G.
    const swc::net::Bytes request = swc::net::time_request_body(kGroup);
    backend::server::hub::Reader reader(request);
    const std::uint64_t target = reader.u64();
    const std::uint64_t entity = reader.u64();
    check(request.size() == rep::kTimeRequestSize && target == rep::time_target(kGroup) && entity == kGroup && reader.ok() && reader.left() == 0,
          "4248: 16 bytes with the target of the Time facet and the entity G (the checks of the backend session)");
    // 335: the clock of the server (the client reads it in SwcRepClient).
    const backend::server::hub::Bytes sync = rep::encode_clock_sync(123'456'789'012ULL);
    swc::net::Reader syncReader(sync);
    check(sync.size() == 8 && syncReader.u64() == 123'456'789'012ULL, "335: the server clock in nanoseconds");
}

void test_hour_against_backend() {
    const DayCycle cycle;
    const std::uint64_t cycleNs = day_cycle::length_ns(cycle);
    struct Case {
        const char* name;
        std::uint64_t serverNow;
        double gameSecond;
    };
    // The start of a backend (an early server clock: the anchor 1), a long uptime, and the times of the edges.
    const Case cases[] = {{"noon at 1e14 ns", 100'000ULL * kSecond, 43200.0},
                          {"midnight at 1e14 ns", 100'000ULL * kSecond, 0.0},
                          {"05:59:30 at 1e14 ns", 100'000ULL * kSecond, 21570.0},
                          {"18:59:00 at 1e14 ns", 100'000ULL * kSecond, 68340.0},
                          {"23:59:59 at 1e14 ns", 100'000ULL * kSecond, 86399.0},
                          {"07:13:20 at 1000 s (anchor 1)", 1000ULL * kSecond, 26000.0},
                          {"14:52:52 at 3 days", 259'200ULL * kSecond, 53572.0}};
    for (const Case& item : cases) {
        backend::world::TimeRecord made;
        const std::uint64_t position = position_of(cycle, item.gameSecond);
        const auto read = backend_record(cycle, item.serverNow, position, made);
        check(read.has_value(), std::string(item.name) + ": the record reads");
        if (!read) {
            continue;
        }
        double largest = 0.0;
        int phaseMismatches = 0;
        int partMismatches = 0;
        // Two cycles in steps of 7.3 s: the part of the client against the client model of the backend (the same float steps), and the
        // hour and the phase against the backend cycle at the same position.
        for (std::uint64_t elapsed = 0; elapsed <= 2 * cycleNs; elapsed += 7'300'000'000ULL) {
            const std::uint64_t now = item.serverNow + elapsed;
            const double part = swc::net::cycle_part(read->record, now);
            if (part != day_cycle::client_part(made, now)) {
                ++partMismatches;
            }
            const std::uint64_t at = (position + elapsed) % cycleNs;
            const double second = swc::net::game_second(read->record, part);
            largest = std::max(largest, day_difference(second, day_cycle::game_second(cycle, at)));
            // The phase: equal away from an edge (1 ms of the float steps of the client).
            const std::uint64_t toEdge = day_cycle::to_next_edge(cycle, at);
            const std::uint64_t fromEdge = std::min<std::uint64_t>(day_cycle::to_next_edge(cycle, (at + cycleNs - 1'000'000ULL) % cycleNs), cycleNs);
            if (toEdge > 1'000'000ULL && fromEdge > 1'000'000ULL
                && swc::net::phase_at(read->record, part) != static_cast<std::uint32_t>(day_cycle::phase_of(cycle, at))) {
                ++phaseMismatches;
            }
        }
        char text[192];
        std::snprintf(text, sizeof(text), "%s: the part of the client equals the client model of the backend (%d mismatches)", item.name, partMismatches);
        check(partMismatches == 0, text);
        // The float steps of the client: below 1 ms of real time for two cycles (backend test case WT1), at most 44 game seconds in one
        // real second.
        std::snprintf(text, sizeof(text), "%s: the hour of the client agrees with the backend cycle (largest difference %.4f game s)", item.name, largest);
        check(largest < 0.05, text);
        std::printf("  %s\n", text);
        std::snprintf(text, sizeof(text), "%s: the phase of the client agrees with the backend cycle (%d mismatches)", item.name, phaseMismatches);
        check(phaseMismatches == 0, text);
        // The hour at the server clock of the record.
        const double first = swc::net::game_second(read->record, swc::net::cycle_part(read->record, item.serverNow));
        std::snprintf(text, sizeof(text), "%s: the clock of the client at the record (%s)", item.name, swc::net::clock_text(first + 0.0005).c_str());
        check(day_difference(first, item.gameSecond) < 0.05, text);
    }
}

void test_rates_and_text() {
    const DayCycle cycle;
    const std::uint64_t now = 100'000ULL * kSecond;
    backend::world::TimeRecord made;
    // By day 26 game seconds in one real second, by night 44 (the defaults of the client).
    auto day = backend_record(cycle, now, position_of(cycle, 8 * 3600.0), made);
    auto night = backend_record(cycle, now, position_of(cycle, 22 * 3600.0), made);
    check(day && night, "the records of 08:00 and 22:00 read");
    if (!day || !night) {
        return;
    }
    const auto advance = [](const swc::net::TimeRecord& record, std::uint64_t at, std::uint64_t seconds) {
        return swc::net::game_second(record, swc::net::cycle_part(record, at + seconds * kSecond))
               - swc::net::game_second(record, swc::net::cycle_part(record, at));
    };
    check(std::fabs(advance(day->record, now, 10) - 260.0) < 0.1, "by day: 260 game seconds in 10 real seconds");
    check(std::fabs(advance(night->record, now, 10) - 440.0) < 0.1, "by night: 440 game seconds in 10 real seconds");
    check(swc::net::clock_text(43200.0) == "12:00:00" && swc::net::clock_text(86399.9) == "23:59:59" && swc::net::clock_text(3725.0) == "01:02:05",
          "the clock text (the whole second)");
    check(swc::net::engine_hour(45000.7) == 45000.0 / 3600.0, "the engine hour moves in steps of one game second");
}

} // namespace

int main() {
    test_record_fields();
    test_day_phase_and_request();
    test_hour_against_backend();
    test_rates_and_text();
    std::printf("world time: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
