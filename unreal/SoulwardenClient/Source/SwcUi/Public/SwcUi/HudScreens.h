#pragma once

// The screens that the client loads with the loader of the game (UI/SwcUiSubsystem.cpp: Runtime::Options::allowedCanvases); only these
// load. One list for the client and the offline tests (tests/ui/ui_tests.cpp --client-screens: the start of the client with no engine).

namespace swc::ui {

/** The screens of the first HUD (stage 1.10) and of the later stages: the loader of the game loads each screen; only these load. */
inline constexpr const char* kHudScreens[] = {
    "lyshineui/hud2024/vitals/vitals2024",
    "lyshineui/hud2024/weaponquickslots/weaponquickslots",
    "lyshineui/compass/compass",
    "lyshineui/hud2024/editmode/hudeditmode",
    "lyshineui/escapemenu/escapemenu",
    // The menu bar of the states Esc, Map, Objectives, Skills, and Options (the game menu; display only, swc/gamemenu.lua).
    "lyshineui/navbar/navbar",
    "lyshineui/popup/popup",
    // The front end (stage F2 of docs/FRONTEND.md): the landing screen of the main menu (a native load, swc/frontend.lua).
    "lyshineui/main menu/landingscreenv2",
    // The character creation (stage Q3): the state CharacterCreationScreen of an empty slot (swc/creation.lua, UI/SwcUiCreation).
    "lyshineui/main menu/charactercreation",
    "lyshineui/respawn/respawn",
    // The world map (stage MVP-C): M opens the state Map (magicmap.luac and its layers; the map data of swc/worldmap.lua).
    "lyshineui/worldmap/magicmap",
    // The card popup of a territory standing (QG1, step 3 of ut567): the state Map loads it (swc/territory.lua).
    "lyshineui/territory/territorystanding/territorybonuspopup",
    // The town board (C-12 of ut567): the state 640726528 of the board use of action 51 (swc/boards.lua, UI/SwcUiBoards).
    "lyshineui/territory/territoryplanning/townprojects",
    // The faction board (C-12 phase 2): the state 2609973752 of the board use of a representative (the conversation option 8).
    "lyshineui/oldworldguilds/owmissionboard",
    // The entrance window of a story instance (C-13 step 1b): the state 4119896358 of the use of OpenDungeonMenu (action 71; swc/gamemodes.lua).
    "lyshineui/dungeons/dungeonenterscreen",
    // The conversation screen (stage MVP-H): the Talk of a person (swc/conversation.lua, UI/SwcUiConversation).
    "lyshineui/conversation/conversationscreenv2",
    // The in-game scenes and the cinematic video stand-in (stage Q5, AST-240; swc/scenes.lua).
    "lyshineui/cutscene/cutscenescreen",
    "lyshineui/videos/videoplayerscreen",
    // The crafting screens (stage MVP-J): the state CraftingStation of a station use (swc/crafting.lua, UI/SwcUiCrafting).
    "lyshineui/crafting/craftingscreenv4",
    "lyshineui/stationproperties/stationproperties",
    // The character window (stage MVP-G): the attributes, the weapon mastery with its trees, the trade skills (swc/skills.lua).
    "lyshineui/skills/skills",
    // The faction selection of the Bio tab (QG7, 2a): the state 1040880896 of Change Faction (swc/factions.lua loads it).
    "lyshineui/changefaction/factionselection",
    "lyshineui/tooltip/tooltip",
    // The inventory (stage P2.4): the bag, the equipment, the weight of the action bar, the action menu of an item.
    "lyshineui/inventory/newinventory",
    "lyshineui/equipment/equipmentv3",
    "lyshineui/hud2024/actionbar/encumbrance",
    "lyshineui/flyoutmenu/flyoutmenu",
    // The reward popup of the open of a loot container (stage Q7, AST-242: the Meat Pouch of the knife quest; swc/items.lua).
    "lyshineui/inventory/rewardcelebrationpopup",
    // The panel of an open loot bag (the state Container).
    "lyshineui/container/catcontainer",
    // The world views (stage WF7): the markers (the AI plates), the prompt cards of the focus, the damage numbers, the loot ticker.
    "lyshineui/markers/markermanager",
    "lyshineui/markers/unifiedinteract",
    "lyshineui/hud/damagenumbers/damagenumbers",
    "lyshineui/hud2024/lootticker/lootticker2024",
    // The fight feedback (stage MVP-I): the hit indicator (the red edge and the arrow toward the source of a hit).
    "lyshineui/hitindicator/hitindicator",
    // The status tray of the own character (stage MVP-I).
    "lyshineui/statuseffects/statuseffectsv2",
    // The notifications (the minor messages of the inventory, the loot).
    "lyshineui/notifications/notification",
    // The options screen (stage P2.3): its categories, the key page (the rebinds go to the key table of the client).
    "lyshineui/options/options",
    // The fight HUD (stage P3.H): the consumable quick slots (3 to 6), the level and XP meter under the vitals, the reticle.
    "lyshineui/hud2024/consumablequickslots/consumablequickslots",
    "lyshineui/hud2024/xp/xp",
    "lyshineui/hud2024/reticles/reticlescanvas",
    // The help bar (AST-231, the old stage P6.9): the key hints of the screens ("ESC Back", "ESC Close"); the screens push their
    // actions to it (DynamicBus.HelpBar PushToStack, SetGlobalActionsToDefault).
    "lyshineui/helpbar/helpbar",
    // The chat widget (AST-231): its key hint "ENTER Chat" at the bottom left of the HUD and of the screens (the chat itself is not
    // loaded: no chat service).
    "lyshineui/hud/chat/chatwidget",
    // The quests (AST-236): the tracker of the HUD, the journal (J), and the banners (swc/quests.lua, UI/SwcUiQuests).
    "lyshineui/objectives/hud/objectiveshud",
    "lyshineui/objectives/journal/journalscreen",
    "lyshineui/banner/banner",
};

} // namespace swc::ui
