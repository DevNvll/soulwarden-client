#include "Boards/SwcTownOffers.h"

#include <algorithm>
#include <unordered_set>

namespace swc::boards {

namespace {

constexpr std::uint64_t kMinuteNs = 60'000'000'000ULL;

bool candidate(const MissionRow& row, std::uint16_t territory, std::uint32_t level, const std::unordered_set<std::uint32_t>& allowed,
               const TownCharacter* character) {
    // The filter 0x3A09900 (backend Missions::candidate): a town row, enabled, an admitted project, the territory rule, the level bounds,
    // the trade skill rank, the achievement conditional.
    if (!row.town || !row.enabled || allowed.count(row.project) == 0) {
        return false;
    }
    if (!row.territories.empty()) {
        const bool listed = std::find(row.territories.begin(), row.territories.end(), territory) != row.territories.end();
        if (listed == row.excludeTerritories) {
            return false;
        }
    }
    if ((row.minLevel != 0 && row.minLevel > level) || (row.maxLevel != 0 && row.maxLevel < level)) {
        return false;
    }
    if (!row.tradeskill.empty() && (character == nullptr || !character->rank || character->rank(row.tradeskill) < row.tradeskillLevel)) {
        return false;
    }
    if (!row.conditional.empty() && (character == nullptr || !character->conditional || !character->conditional(row.conditional))) {
        return false;
    }
    return true;
}

} // namespace

const Project* TownData::project(std::uint32_t crc) const {
    for (const Project& each : projects) {
        if (each.crc == crc) {
            return &each;
        }
    }
    return nullptr;
}

std::uint32_t town_seed(std::string_view text, std::uint16_t territory, std::uint64_t serverNs) {
    // 0x39D2A70: the minute of the server clock (signed division), plus half of the interval, minus its remainder (signed 32-bit), plus the
    // territory and FNV-1a 64 of the text; 0x3A36220 adds the seed addition (u32 wrap).
    const std::int64_t p = kIntervalMinutes;
    const std::int64_t minute = static_cast<std::int64_t>(serverNs) / static_cast<std::int64_t>(kMinuteNs);
    const auto phase = static_cast<std::int32_t>(static_cast<std::uint32_t>(minute + p / 2));
    const std::int32_t remainder = static_cast<std::int32_t>(phase % static_cast<std::int32_t>(p));
    const auto rounded = static_cast<std::int32_t>(static_cast<std::uint32_t>(phase) - static_cast<std::uint32_t>(remainder));
    std::uint64_t hash = 0xCBF29CE484222325ULL;
    for (const char letter : text) {
        hash = (hash ^ static_cast<std::uint8_t>(letter)) * 0x100000001B3ULL;
    }
    return static_cast<std::uint32_t>(hash + territory + static_cast<std::uint64_t>(static_cast<std::int64_t>(rounded)) + kSeedAddition);
}

std::uint64_t seconds_to_next_seed(std::uint64_t serverNs) {
    const std::int64_t p = kIntervalMinutes;
    const std::int64_t minute = static_cast<std::int64_t>(serverNs / kMinuteNs);
    const std::int64_t second = static_cast<std::int64_t>((serverNs % kMinuteNs) / 1'000'000'000ULL);
    return static_cast<std::uint64_t>(60 * (p - ((minute + p / 2) % p)) - second);
}

std::uint64_t next_seed_ns(std::uint64_t serverNs) {
    const auto p = static_cast<std::uint64_t>(kIntervalMinutes);
    const std::uint64_t minute = serverNs / kMinuteNs;
    const std::uint64_t phase = (minute + p / 2) % p;
    return (minute + p - phase) * kMinuteNs;
}

TownOffers town_offers(const TownData& data, std::string_view characterId, std::uint16_t territory, std::uint64_t serverNs,
                       const std::vector<std::uint32_t>& projects, std::uint32_t level, const TownEntry* entry,
                       const TownCharacter* character) {
    TownOffers result;
    result.seed = town_seed(characterId, territory, serverNs);
    for (const std::uint32_t projectCrc : projects) {
        const Project* active = data.project(projectCrc);
        if (active == nullptr) {
            continue;
        }
        // 0x3A362A0, step 1: the project definitions of the same type with a level no greater than the level of the active project.
        std::unordered_set<std::uint32_t> allowed;
        for (const Project& project : data.projects) {
            if (project.type == active->type && project.level <= active->level) {
                allowed.insert(project.crc);
            }
        }
        // Step 4: the LCG from projectId + currentSeed (u32), xor the multiplier (the seed of java.util.Random).
        std::uint64_t random = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(projectCrc + result.seed)) ^ kLcgMultiplier) & kLcgMask;
        for (const Bucket& bucket : data.buckets) {
            std::vector<std::uint32_t> candidates;
            for (const std::uint32_t mission : bucket.missions) {
                const auto row = data.rows.find(mission);
                if (row != data.rows.end() && candidate(row->second, territory, level, allowed, character)) {
                    candidates.push_back(mission);
                }
            }
            if (candidates.empty()) {
                continue;
            }
            random = (random * kLcgMultiplier + 11) & kLcgMask;
            const auto index = static_cast<std::uint32_t>((random >> 16) % candidates.size());
            TownOffer offer;
            offer.mission = candidates[index];
            offer.bucket = bucket.key;
            offer.index = index;
            offer.candidates = static_cast<std::uint32_t>(candidates.size());
            offer.project = projectCrc;
            offer.available = entry == nullptr || entry->seed != result.seed
                              || std::find(entry->unavailable.begin(), entry->unavailable.end(), offer.mission) == entry->unavailable.end();
            result.offers.push_back(offer);
        }
    }
    return result;
}

} // namespace swc::boards
