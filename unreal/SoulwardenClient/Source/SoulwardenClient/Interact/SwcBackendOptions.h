#pragma once

#include <string_view>

// The interaction options that the fixed backend serves (a copy of its handlers: Soulwarden apps/backend/src/world/interactions.cpp
// kLoreOptions, kPresentationOptions, kStationOptions, kTimedFixtureOptions; loot_bags.cpp kTakeAllOption, kOpenOption; chests.cpp
// and gathering.cpp kGatherOption). A request of another option gets the refusal 3743 result 2 (interactions.md: "no handler").
//
// The use (stage MVP-F, docs/INTERACTIONS.md "The options of a card"): an interim rule for the persons only. The retail client with
// this backend shows only "E Talk" at an inn keeper (the options OpenInnScreen, SetSpawnPointOption, OpenConversation; the
// Soulwarden evidence fight/implement/F6-first-run-death-screen-respawn-at-inn.png), but it shows "Fast Travel" at a shrine, which
// the backend does not serve either. The rule of the retail program that hides the inn options is open (a brief for the static
// analysis); until then a person shows only the options of this list.

namespace swc::interact {

inline bool backend_serves_option(std::string_view name)
{
    constexpr std::string_view kServed[] = {
        "ReadLore", "AddLoreToJournal",                                                          // lore pages
        "OpenConversation", "ChairInteractionAnimation", "BedInteractionAnimation",              // presentation
        "PlayInteractionAnimation", "PlayPetAnimation", "PlayCinematicVideo",
        "OpenCraftingOption", "OpenCraftingOptionLevel60",                                       // stations and camps
        "Trigger",                                                                               // the timed fixture (keg)
        "TakeAllOption", "OpenStorageOption",                                                    // loot bags
        "GatherOption",                                                                          // chests and gathering
    };
    for (const std::string_view Each : kServed)
    {
        if (Each == name)
        {
            return true;
        }
    }
    return false;
}

} // namespace swc::interact
