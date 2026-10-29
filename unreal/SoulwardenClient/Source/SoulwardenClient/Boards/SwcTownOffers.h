// The town offers of a mission board (C-12 of ut567; QG6a; Soulwarden docs/systems/mission-boards.md "The town offers"; the backend
// world/missions.cpp town_seed, seconds_to_next_seed, next_seed_ns, Missions::candidate, Missions::town_offers). The client computes the
// offers itself (GetCurrentCommunityGoals 0x4955A90 -> 0x3A34C70 -> 0x3A362A0); the server computes the same list and accepts only an offer
// of the current seed. Pure C++ (no engine type): the tests run it against the backend generator (tests/net/board_offer_tests.cpp).
//
// The rules [client, the static reads of the backend: OFFERS.md]:
//   seed       the low 32 bits of FNV-1a 64 of the characterId text, plus the territory, plus the server clock minute rounded to the 30-minute
//              interval with the half-interval phase (signed 32-bit), plus the seed addition 0.
//   countdown  60 * (P - ((m + P/2) mod P)) - s (GetSecondsToNextSeed 0x4957F20).
//   selection  for each active project of the town: the projects of its type with a level no greater than its level are allowed; the LCG of
//              java.util.Random starts from (projectCrc + seed) mod 2^32 xor 0x5DEECE66D; each bucket with candidates (Kill 4, Gather 1,
//              Explore 5, in the order of the data) takes one step r = (r * 0x5DEECE66D + 11) mod 2^48 and the candidate (r >> 16) mod count.
//   candidate  a town row, enabled, of an allowed project, the territory rule, MinLevel and MaxLevel (0: no bound) against the level of the
//              screen, the trade skill rank (RequiredTradeskill, TradeskillLevel), the achievement conditional.
//   available  false when the stored seed of the town entry is the current seed and the mission is in its unavailable list (0x3A3684C).
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace swc::boards {

/** The minutes of the offer interval (javelin.territory-progression-mission-cooldown-min, default 30) and the seed addition (0). */
inline constexpr std::int64_t kIntervalMinutes = 30;
inline constexpr std::uint32_t kSeedAddition = 0;
inline constexpr std::uint64_t kLcgMultiplier = 0x5DEECE66DULL;
inline constexpr std::uint64_t kLcgMask = (1ULL << 48) - 1;

/** The values of a mission row that the candidate filter reads (the town table only). */
struct MissionRow {
    std::uint32_t crc{0};
    std::uint32_t project{0};
    bool town{false};
    bool enabled{true};
    std::vector<std::uint16_t> territories;
    bool excludeTerritories{false};
    std::uint32_t minLevel{0};
    std::uint32_t maxLevel{0};
    /** The trade skill id text (RequiredTradeskill; empty: no rule) and the rank that it needs. */
    std::string tradeskill;
    std::uint32_t tradeskillLevel{0};
    /** RequiredAchievementConditional (empty: no rule). */
    std::string conditional;
};

struct Project {
    std::uint32_t crc{0};
    std::string type;
    std::uint32_t level{0};
};

struct Bucket {
    std::uint8_t key{0};
    std::vector<std::uint32_t> missions;
};

struct TownData {
    std::unordered_map<std::uint32_t, MissionRow> rows;
    std::vector<Project> projects;
    std::vector<Bucket> buckets;
    [[nodiscard]] const Project* project(std::uint32_t crc) const;
};

/** The town entry of a character (field 3 of the provider state): the stored seed and its unavailable missions. */
struct TownEntry {
    std::uint32_t seed{0};
    std::vector<std::uint32_t> unavailable;
};

/** The character values of the filter. No character (nullptr): a row with a trade skill rule or a conditional is not a candidate. */
struct TownCharacter {
    /** The rank of a trade skill (its id text). */
    std::function<std::uint32_t(const std::string&)> rank;
    /** The value of an achievement conditional. */
    std::function<bool(const std::string&)> conditional;
};

struct TownOffer {
    std::uint32_t mission{0};
    std::uint8_t bucket{0};
    std::uint32_t index{0};
    std::uint32_t candidates{0};
    std::uint32_t project{0};
    bool available{true};
};

struct TownOffers {
    std::uint32_t seed{0};
    std::vector<TownOffer> offers;
};

/** @return The town seed of the client for the characterId text, the territory, and the server clock (steady nanoseconds). */
[[nodiscard]] std::uint32_t town_seed(std::string_view text, std::uint16_t territory, std::uint64_t serverNs);
/** @return The seconds to the next seed (the countdown of the screen). */
[[nodiscard]] std::uint64_t seconds_to_next_seed(std::uint64_t serverNs);
/** @return The server clock nanoseconds of the start of the next seed. */
[[nodiscard]] std::uint64_t next_seed_ns(std::uint64_t serverNs);
/**
 * @return The offers of the active projects of a town for a character: the seed and, for each project in its order, one offer of each
 *         bucket with candidates. `level` is the level of the screen; `entry` the town entry of the territory (nullptr: none).
 */
[[nodiscard]] TownOffers town_offers(const TownData& data, std::string_view characterId, std::uint16_t territory, std::uint64_t serverNs,
                                     const std::vector<std::uint32_t>& projects, std::uint32_t level, const TownEntry* entry,
                                     const TownCharacter* character);

} // namespace swc::boards
