// The town offers of the client (Boards/SwcTownOffers, C-12 of ut567) against the generator of the backend (world/missions.cpp
// town_seed, seconds_to_next_seed, next_seed_ns, Missions::town_offers) with the real mission data: the backend copy mission-data.json in
// the board fixture of the backend tests, and the converted ui/gamedata/missiontown.json (tools/convert-missions.sh) in the client generator.
// The characters have no trade skill rank and no achievement (the fixture passes no character): the rows with such a rule are no candidates
// on both sides. Usage: swc-board-offer-tests MISSION-DATA.json MISSIONTOWN.json
#include <cstdio>
#include <fstream>
#include <string>

#include "Boards/SwcTownOffers.h"
#include "support/mission_fixture.h"

namespace {

int g_checks = 0;
int g_failures = 0;

void check(bool condition, const std::string& text) {
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::printf("  FAIL  %s\n", text.c_str());
    } else {
        std::printf("  ok    %s\n", text.c_str());
    }
}

namespace backend = soulwarden::backend;
using nlohmann::json;

swc::boards::TownData client_data(const std::string& path) {
    std::ifstream input(path);
    const json document = json::parse(input);
    const json& town = document.at("town");
    swc::boards::TownData data;
    for (const json& row : town.at("rows")) {
        swc::boards::MissionRow each;
        each.crc = row[0].get<std::uint32_t>();
        each.project = row[1].get<std::uint32_t>();
        each.town = true;
        each.enabled = row[2].get<bool>();
        for (const json& id : row[3]) {
            each.territories.push_back(id.get<std::uint16_t>());
        }
        each.excludeTerritories = row[4].get<bool>();
        each.minLevel = row[5].get<std::uint32_t>();
        each.maxLevel = row[6].get<std::uint32_t>();
        each.tradeskill = row[7].get<std::string>();
        each.tradeskillLevel = row[8].get<std::uint32_t>();
        each.conditional = row[9].get<std::string>();
        data.rows[each.crc] = each;
    }
    for (const json& project : town.at("projects")) {
        data.projects.push_back({project[0].get<std::uint32_t>(), project[1].get<std::string>(), project[2].get<std::uint32_t>()});
    }
    for (const json& bucket : town.at("buckets")) {
        swc::boards::Bucket each;
        each.key = bucket.at("key").get<std::uint8_t>();
        each.missions = bucket.at("missions").get<std::vector<std::uint32_t>>();
        data.buckets.push_back(std::move(each));
    }
    return data;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: swc-board-offer-tests MISSION-DATA.json MISSIONTOWN.json\n");
        return 2;
    }
    std::printf("the town offers of the client against the backend generator (C-12)\n");
    // 1. The seed, the countdown, and the next seed on a grid of clocks around the boundaries (the signed 32-bit phase of the client).
    const std::string ids[] = {"", "4aae16c4-e766-4365-b93b-0eaacbe2ea70", "b0a4d000-0000-4000-8000-000000000001", "Azoth"};
    bool seeds = true;
    std::uint64_t clocks = 0;
    for (const std::string& id : ids) {
        for (std::uint16_t territory : {std::uint16_t{2}, std::uint16_t{6}, std::uint16_t{17}}) {
            for (std::uint64_t minute = 0; minute < 200'000'000ULL; minute += 7'777'777ULL) {
                for (std::uint64_t extra : {0ULL, 1ULL, 14ULL * 60'000'000'000ULL, 15ULL * 60'000'000'000ULL, 44ULL * 60'000'000'000ULL + 59'000'000'000ULL}) {
                    const std::uint64_t ns = minute * 60'000'000'000ULL + extra;
                    ++clocks;
                    seeds = seeds && swc::boards::town_seed(id, territory, ns) == backend::world::town_seed(id, territory, ns, 30.0F, 0)
                            && swc::boards::seconds_to_next_seed(ns) == backend::world::seconds_to_next_seed(ns, 30.0F)
                            && swc::boards::next_seed_ns(ns) == backend::world::next_seed_ns(ns, 30.0F);
                }
            }
        }
    }
    check(seeds, "the seed, the countdown, and the next seed equal the backend on " + std::to_string(clocks) + " clocks");

    // 2. The offers with the real data: the backend fixture with the mission data of the backend copy, the client generator with the
    // converted file; the always available projects of the backend (town_projects), 13 territories, 4 characters, 3 levels, 12 windows.
    backend::tests::missionfix::Boards shop(":memory:", false, false);
    check(shop.host.missions.load(argv[1]), std::string("the backend loads the mission data ") + argv[1]);
    shop.start();
    backend::world::Missions& missions = shop.missions();
    const swc::boards::TownData data = client_data(argv[2]);
    check(data.rows.size() > 13000 && data.projects.size() == 50 && data.buckets.size() == 3, "the converted town data: "
          + std::to_string(data.rows.size()) + " rows, " + std::to_string(data.projects.size()) + " projects, " + std::to_string(data.buckets.size()) + " buckets");
    const std::vector<std::uint32_t> projects = missions.town_projects(6);
    check(!projects.empty(), "the always available projects of the towns (" + std::to_string(projects.size()) + ")");
    int runs = 0, offers = 0, mismatches = 0;
    std::string first;
    for (const std::string& id : ids) {
        for (std::uint16_t territory : {2, 4, 5, 6, 8, 10, 11, 12, 13, 14, 15, 16, 17}) {
            for (std::uint32_t level : {1U, 17U, 60U}) {
                for (std::uint64_t window = 0; window < 12; ++window) {
                    backend::world::TownInputs inputs;
                    inputs.seedText = id;
                    inputs.territory = territory;
                    inputs.serverNs = 1'800'000'000'000'000ULL + window * 30ULL * 60'000'000'000ULL;
                    inputs.projects = projects;
                    inputs.level = level;
                    const backend::world::TownOffers expected = missions.town_offers(inputs, nullptr, nullptr);
                    const swc::boards::TownOffers actual =
                        swc::boards::town_offers(data, id, territory, inputs.serverNs, projects, level, nullptr, nullptr);
                    ++runs;
                    bool same = expected.seed == actual.seed && expected.offers.size() == actual.offers.size();
                    for (std::size_t i = 0; same && i < expected.offers.size(); ++i) {
                        same = expected.offers[i].mission == actual.offers[i].mission && expected.offers[i].bucket == actual.offers[i].bucket
                               && expected.offers[i].index == actual.offers[i].index && expected.offers[i].candidates == actual.offers[i].candidates
                               && expected.offers[i].project == actual.offers[i].project;
                    }
                    offers += static_cast<int>(actual.offers.size());
                    if (!same) {
                        ++mismatches;
                        if (first.empty()) {
                            first = "territory " + std::to_string(territory) + " level " + std::to_string(level) + " window " + std::to_string(window)
                                    + " id '" + id + "': backend " + std::to_string(expected.offers.size()) + " offers, client "
                                    + std::to_string(actual.offers.size());
                        }
                    }
                }
            }
        }
    }
    check(mismatches == 0 && offers > 0, "the offers equal the backend in " + std::to_string(runs) + " runs (" + std::to_string(offers)
                                             + " offers)" + (first.empty() ? "" : "; the first difference: " + first));
    // 3. The unavailable rule (0x3A3684C): a mission of the stored seed is unavailable; of another seed it is available.
    backend::world::TownInputs inputs;
    inputs.seedText = ids[1];
    inputs.territory = 6;
    inputs.serverNs = 1'800'000'000'000'000ULL;
    inputs.projects = projects;
    inputs.level = 60;
    const swc::boards::TownOffers now = swc::boards::town_offers(data, ids[1], 6, inputs.serverNs, projects, 60, nullptr, nullptr);
    check(!now.offers.empty(), "Windsward has offers for the level 60");
    if (!now.offers.empty()) {
        const swc::boards::TownEntry stored{now.seed, {now.offers[0].mission}};
        const swc::boards::TownEntry older{now.seed + 1, {now.offers[0].mission}};
        check(!swc::boards::town_offers(data, ids[1], 6, inputs.serverNs, projects, 60, &stored, nullptr).offers[0].available
                  && swc::boards::town_offers(data, ids[1], 6, inputs.serverNs, projects, 60, &older, nullptr).offers[0].available,
              "a hand-in of the stored seed makes the mission unavailable; another seed does not");
    }
    std::printf("Result: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
