# The quests

Stage Q1 (Linear AST-236; parent AST-235). The player sees the quests of the character with the original screens of the game: the
quest tracker of the HUD (ObjectivesHud), the journal (JournalScreen, J), and the banners (Banner) with their scripts
(`lyshineui/objectives/**/*.luac`, `lyshineui/banner/*.luac`) in the Lua runtime of the UI. The client gives these scripts the native
part: the quest state of the server, the quest data, and the quest requests. The server owns each quest. The sources are the plan
`~/Games/soulwarden-client/research/quest-plan.md`, the Soulwarden document `docs/systems/quests.md` (read only), the protocol notes 4.53
and 4.54, the backend copy (`quest-data.json`, release 2026-10-21-096ab18, schema 16), and the evidence of the backend stages 1.8a and
1.8c (`~/Games/soulwarden/work/evidence/quests/`).

## The parts

| Part | Job |
| --- | --- |
| `converter/swc_convert/ui.py quests`, `tools/convert-ui.sh quests` | `SWC_CONTENT/ui/gamedata/quests.json` and the textures of the quest screens (below). |
| `Net/Codec/SwcRepMessages`, `SwcRepClient` | The readers of the Objectives state (slot 22, type 3857) and the pin readiness (slot 73, type 5606); the writers of 2709, 1458, 2617, 3836, 5600, 2297, and 6416. |
| `SwcBackendSubsystem` | `EnableObjectives`, `UpdateTrackedObjectives`, `NotifyOpenUiScreen`, `AbandonObjective`, `ResetObjective`. |
| `UI/SwcUiQuests` | The quest state to the scripts (`Swc.HostQuests`, `Swc.HostQuestPinReady`), 2709 once in each world session, the pinned list once after the readiness, and the quest requests of the scripts. |
| `Lua/swc/quests.lua` | ObjectivesComponentRequestBus, ObjectiveRequestBus, ObjectiveTaskRequestBus, ObjectivesDataManagerBus, the quest events of GameEventRequestBus, and the load of the journal canvas. |
| `tests/net/quest_tests.cpp` | The readers with the bytes of the backend encoders, and the request bodies with the backend readers (`tools/test-net.sh`). |
| `tests/ui/fixtures/quests.lua` | The tracker, the banner, the journal, the unpin and the pin with the start state of a new character (`tools/test-ui.sh --fixture quests`). |

```sh
tools/convert-ui.sh quests             # quests.json and the textures (the backend copy: tools/backend-update.sh first)
tools/test-net.sh                      # the codec tests, the range tests, the quest tests
tools/test-ui.sh --fixture quests      # the fixture tests/ui/fixtures/quests.lua
```

## The wire

The backend sends the two quest states only after the first 2709 of the client (the auto-pin flags of the options). The client sends
2709 once in each world session, when the HUD runs: 36 flags, all on but CommunityGoal (type 6), as the retail default (live check L1
of stage 1.8a). Then:

- The Objectives state (slot 22) has one group with five fields in the full form: the task start times, the tracked instances, the
  completed notices, the active objectives (objective, instance, mission, accept time, origin territory, parent, ...), and the task
  elements (instance, index, task, progress, state). A field that is present replaces its list. A read that fails changes nothing.
  The reader checks the limits of the backend (`RepClient::read_objectives`).
- The pin readiness (slot 73) is one byte. Before it the tracker shows no pin, as the retail client.
- After the readiness the client sends the pinned list of the state back once (1458): the backend must not keep an empty start list
  (research quest-plan.md "Wire and display contract"). The backend refuses 1458 before its first Objectives state.
- The scripts send the other requests through host requests: "quest pins I,J" (1458, at most 8), "quest screen CRC" (2617),
  "quest abandon I" (3836), "quest reset I" (5600).

- Field 2 (the completed instances, backend stage 1.8b) names the instance of a hand-in in the record that removes it: the client
  shows a completion, not "Objective Failed" (vendored decoder of ef7daf6).

The place cues 2297 and 6416: see "The place cues" below (stage Q6).

## The data

`quests.json` (format 1) comes from `quest-data.json` of the backend copy: 2,147 objectives (by the lower-case ObjectiveID: the id,
the CRC-32, the cells of the row, the root task, the task tree in the child-first index order of the backend, the hidden tasks),
15,563 tasks, 1,082 game events, and the territories. The task index of the wire is the index in that tree. The textures are the
folders `lyshineui/images/objectives/`, `icons/objectives/`, `journal/`, and `banner/`.

## The adapter

- The ids are userdata, as in the retail client (`objectivedifficultyhelper.luac` tests the type "userdata"): an objective id has the
  instance; a task id has the instance and the task index. The same key gives the same id.
- The state of a container task (simple, consecutive, partial) and its progress come from its children, as in the backend.
- A state change gives the notices of the retail component: OnObjectiveAdded, OnObjectiveRemoved, OnObjectiveCompleted,
  OnObjectiveFailed, the task notices, OnObjectiveChanged, the tracked notices, and OnObjectiveSortingChanged. They go to the player,
  the root, and the objectives ids (bannertriggers.luac connects at the root).
- GetMissionData gives an empty mission (no overrides): the tracker reads the title of the objective.
- The journal canvas: the retail dynamic canvas loader loads it when the state 1823500652 starts. That loader does not run in the
  client; `quests.lua` loads it through `Swc.stateCanvases` (`manager.lua`). J (toggleJournalComponent) has no script that binds it:
  `actions.lua` sets or leaves the journal state.
- The largest number of objectives of each journal section (`javelin.objectives-max-*`) are the values of the retail journal picture.

## The checks

- `tools/test-net.sh`: 27 quest checks (the beach start state, a timer, a large list: 60 objectives, 8 tracked, 512 task elements; an
  empty list; the view of an observer; a cut field; two owners; 5606; the request bodies).
- `tools/test-ui.sh --fixture quests`: 17 checks; `--fixture questprogress`: 16 checks (the counter, the place name of a text, Reset and
  its popup, the screen queue, the tags of the quest objects, the hand-in row "Talk to Urda", Abandon of a mission with 3836).
- `tools/test-net.sh`: the quest program also checks field 2 of a hand-in and the 2297 body of POI 40142 (the reference bytes).
- The live check: a new character of the development account in its own data folder (`SWC_DEV_DATA=~/Games/soulwarden-client/dev-data-quests`;
  `SWC_DEV_NEW=1` of `tools/creature-check.sh` makes it with no saved place, so the backend puts it on the Windsward beach). The drives
  `tests/drive/ast236-quest-*.txt`: the first entry (the banner, the tracker, J, Unpin, Pin, Escape), then a second entry of the same
  character (the same instance and the same pin). Evidence `~/Games/soulwarden-client/evidence/AST-236/`: the videos `ast236-q1.mp4` and
  `ast236-q2.mp4`, the crops `ast236-tracker-retail-client.png` (retail, first entry, second entry) and `ast236-journal-retail-client.png`
  against the Soulwarden evidence `quests/18a/implement/live/I2-new-character-*.png`.

- The live check K1 of Q6 (`tests/drive/ast241-k1*.txt`; evidence `~/Games/soulwarden-client/evidence/AST-241/`): the development
  character in the shipwreck 40142 with 06_crewmates; Corrupted Mariners spawned 2.4 m north with full health (a creature with 1 health
  of a development command gives no kill credit: the damage must be a part of its health); each credited kill gives a new state, and
  the tracker shows "0 / 5", "1 / 5", "2 / 5 - Defeat Corrupted Mariners" (`ast241-counter-0-1-2.png`, `ast241-counter-retail-client.png`
  against K1-2of5). A relog keeps the instance and the pins (1458 [5, 4]). J sends 2617 (6cb0696c, the journal); Reset, Yes sends
  5600, and the backend sends a new instance; the HUD sends 2617 a1126b78.

- The live check G1 of Q6 (`tests/drive/ast241-g1*.txt`): the development character with 06_pillageship. At treasure B (IO_MSQ_Treasure_B,
  InteractOnlyOnQuest) while the lore task is Active: no focus. E at the lore page of the ship's manifest: ReadLore (3743 result 1);
  the treasure tasks start. At treasure B again: the prompt "CANDELABRA"; E: the gather (accepted), the quest drop
  0601_0104_Treasure_B (task 6), a new state; then no focus (the task is complete). Crops `ast241-quest-tracker-retail-client.png` (the
  tracker against G1-4: the same texts), `ast241-quest-object-prompt-retail-client.png`, `ast241-quest-object-before-active-after.png`.

## Notes

- The icon of a pinned quest has the number of its place in the pinned list (GetObjectiveSortingPos from 0, -1 with no pin;
  `objectivetypedata.luac`): "1" for the one pin of a new character.
- The Auto-Pin switches of the journal show with the data node `javelin.enable-auto-pin-options`; their states are the option nodes
  `Hud.LocalPlayer.Options.Misc.EnableAutoPinningObjectives.TYPE`, with the flags of 2709.

## The task progress, the screens, and the quest objects (stage Q6, AST-241)

- The counters: the progress of a task element of the state, and its TargetQty (the tracker shows "2 / 5 - TEXT"). The texts of the
  tasks have values that the native GetDescription fills (`Q.FillTaskText`) [inference from the texts and the retail picture K1-2of5 of
  the backend stage 1.8c]: `{targetName}` is the vitals category of KillEnemyType (`@vc_corrupted`: "Corrupted"), else the item;
  `{itemName}` the item of ItemName; `{POITags}` the name of the point of interest of the first POITag (`quests.json` "pois": the
  `poi-data.json` of the backend copy); `{destinationName}` the NPC name of ItemName; `{targetAmount}` the TargetQty.
- The screens (2617): each change of the current UI state goes to a queue (`swc/manager.lua`, `Swc.TakeQuestScreen`); UI/SwcUiQuests
  sends it in the world after 2709. The value is the UI state id, the CRC-32 of the lower-case state name (the HUD: a1126b78); the
  backend counts TaskOpenUiScreen with the UIScreenTag of the task.
- Reset and abandon: the journal buttons and the confirm popup of the original scripts send 5600 and 3836 (CanBeAbandoned: Abandon;
  else Reset). The backend sends the new state.
- The quest objects: an object of the interaction layer with InteractOnlyOnQuest (`quest` of the object layer: the capital entries of
  `gathering-data.json` with 4 values) has a focus only while a task of the character with its InteractTag is Active
  (`Swc.QuestActiveTags`, `USwcBackendSubsystem::SetQuestTags`; backend gathering.md: else the refusal notOnQuest).

## The place cues (AST-241; research poi-cues.md)

- The data: `tools/convert-ui.sh quests` writes `gamedata/poi-cues.json`: for each point of interest of `poi-data.json` its placement
  of `regions/*/capitals/pois/pois.capitals.json` (by the world position), the reference of its Territory component (the actor and the
  group of the UUID rule; local = group + 0x4FB021A86DF7AE7A for the base POI slice), the territory field (the POI id), the discovery
  radius (0 with no discovery achievement), the shape, and the two achievements (the CRC-32 of the full lower-case name of the cells
  DiscoveredAchievement and ChartedAchievement, and the AchievementIndex bit). 390 of 476 points use the base slice; the other 86 have
  no local (no cue: an open point). The records of 40140, 40142, and 40144 are equal to the saved retail packets of the backend live
  check POI1.
- The detector (`FSwcUiQuests::FeedPlaces`, 5 tests a second from the feet of the character): the sphere (3D distance to the placement)
  and the shape (the polygon, 180 m high). An entry sends 2297 with the CRC of its achievement while its bit is not set (54 bytes; the
  achievement 0 for a shape with no charted achievement); each exit sends 6416 (50 bytes). The margins of the retail detectors are not
  known (no hysteresis here). The backend uses the cues only as a cue: its place tasks use its own position and shapes.
- The live check POI1 (`tests/drive/ast241-poi1*.txt`): a travel into the shape of 40142 sends 2297 d66d9b33 (the sphere) and 2297
  39eec8c7 (the shape), the pair of the retail packets; a travel out sends two 6416. The backend reads them ("the shape of the data says
  inside").
- The live check C1 (`tests/drive/ast241-c1*.txt`): E at treasure C, then a walk back before the work ends: the backend ends the gather
  ("the client cancelled it"), no item, no progress; the full gather after it: the quest drop 0601_0104_Treasure_C. The live check AB1
  (the same drives, label ast241-ab1): the faction mission fm_06_exp_02_objective; J, the section Faction Missions, Abandon, the popup
  "ABANDON MISSION" (crop `ast241-abandon-retail-client.png` against AB-2), Yes: 3836, the backend ends the mission. The journal row of
  a scheduled mission needs `PlayerDataManagerBus.GetRegionData` (UTC, no daylight saving time [inference]); with no value the journal
  list stayed empty.
- Two characters (`ast241-two-characters.png`): QuestOne has only its 06_getoffbeach (instance 1); QuestThree has five quests with their
  own progress.
- The item, kill, and equip tasks (`tests/drive/ast241-b1*.txt`): the development items FlintT1, WoodT1, and the knife complete the tasks
  of 06_skinningknife (its root is complete: the hand-in row "Talk to Urda"); a credited boar kill and the equip of the knife move the
  tracker of 06_boarfeast ("Hunt down a Boar ..." to "Equip a Skinning Knife" to "Skin a Boar to Collect Pork";
  `ast241-boarfeast-kill-equip.png`).
- The hand-in row of the tracker: the root task Complete or CompleteActive makes the objective ready (`IsObjectiveReadyForCompletion`);
  the row is "Talk to NAME" with the NPC of NpcDestinationId (`GetNpcDestinationGenericName`; retail K1-2of5: "Basic Needs / Talk to Urda").

## The pins (Q2, AST-237)

The rules come from the study `research/quest-pins.md` (370 lines; [R] retail code, [D] data, [I] client rule, [U] open). One list of
the published task positions (`Lua/swc/questpins.lua`, `Swc.questPins.list`) feeds the map, the compass, the journal Map action, the Map
button of the tracker, and the world pins. The original scripts read it through two events of `ObjectivesComponentNotificationsBus`
(study 1.1):

- `OnObjectiveTaskPositionAdded(taskId, position, isTracked, poiId)`: the map layer (objectiveslayer.luac) keeps an icon for each task
  handle (on the POI of poiId when it is not 0); the compass (compass.luac) caches the position and shows it when isTracked.
- `OnObjectiveTaskPositionsRemoved(taskId, isTracked)`.

The task id has the fields `objectiveInstanceId` and `taskIndex`. The journal Map action and the tracker Map button ask the map layer
(`DynamicBus.ObjectivesLayer.GetObjectivePosition`).

The publisher [R] (study 1.2, 2.1, 2.2):

- Each objective of the character, each task that is enabled and unfinished: state Active or CompleteActive and progress < target. No
  IsHidden or HideChildren test: a hidden active container with a source publishes next to its child. isTracked is an argument.
- The source: the nearest point (full XYZ) of the OverrideLocation, poiId 0; else, with POITag and no GameModeID, the nearest POI point of
  the tags (inside TerritoryID when the task has one; else the first POI point), poiId the POI; else no position. TerritoryID is not a
  position. A conversation task (type code 16): the override, else the nearest place of the NPC of ItemName.
- A ready objective (the root Complete or CompleteActive): one more position on the root task id [I], at the NpcDestinationId NPC place
  nearest to the player [I: the native completion resolver is not traced]. Before that, no hand-in position (HideIncompleteTurninMarker).
- The changes: after each change of the state, the pinned list, or the pin readiness, and each 2 s with the player position [I];
  `RequestPublishPoiLocations` sends all positions again (the map asks when it opens).

The queries [R] (study 3): `GetUIData(task, "OverrideLocation")` gives a list of Vector3 (empty with no cell), `StayActive` a boolean,
`ID` the task id. `GetObjectivesOrdered(Distance)` gives groups of objective ids (by the squared XYZ distance of the nearest published
position; a second group with no position). `GetTaskTerritoryId`: the authored TerritoryID, else the territory of the POI.
`GetTerritoryIdByPoiTag` and `GetTerritoryDefinitionByPoiTag`: the map definition with the tag.

The world pins [R] (`swc/markers.lua` M.TickQuestPins; study 4): the marker type DiegeticObjectivePin of the static pool `w`
(staticworldmarker.luac shows the objective icon of the data node `DiegeticPinObjectiveInstanceId`; it asks `HasObjectiveInstanceId`).
A candidate is a published task position of a task with ShowDiegeticTaskPin (isTracked is not a filter). One pool of 5, the nearest by
squared XYZ distance, none beyond 150 m; the allocation each 2 s and at each change of the list; a slot stays with its candidate. The
height: a DiegeticPinZOverride of at least -100 replaces Z; blank or 0 gives Z + 1.75 m. The scale (1 at 4 m to 0.7 at 120 m) and the
alpha (1 at 100 m to 0 at 150 m): markerdata.luac. The log has "quest pin on: KEY at X Y Z (wN, D m)" and "quest pin off: KEY";
`Swc.QuestPinPoint(KEY)` gives the screen point.

The data: `gamedata/quest-pins.json` of format 2 (`python3 -B -m swc_convert.questpins write`; the inputs quests.json, worldmap.json, and
persons.json): the OverrideLocation points by the native parser (comma entries; up to three numbers; a missing number is 0: "1" is
(1, 0, 0)); the POI ids of the tags and their points; TerritoryID; GameModeID; the pin cells; the conversation NPC; the hand-in NPC of
each objective; the NPC places. The POI point, an interim rule [U] (accepted by the orchestrator, 2026-10-23, until the provider point
is traced): the native point comes from the territory data provider, whose load path is not proved; the client uses the POI placement of
the backend copy (quests.json "pois": the Territory component of the POI placement). The map shape positions (worldmap.json) differ from
these placements in 329 of 476 POIs, by up to 162 m: neither is proved, and the client uses no shape position and no bounds centre. A tag
with only a map definition gives the id and no point.
No objective area: the study found no search-area display rule [U].

The compass places its icons in `OnCrySystemPostViewSystemUpdate` (compass.luac, through uitick.luac and DynamicBus.UITickBus). The
client did not send this event before AST-237, so each compass icon stayed at the north point of the strip; `Swc.Tick` sends it now
after `TickBus.OnTick`.

The start quest 06_getoffbeach (study 7): the root 16 (9613.6416 2329.4722 43.0) and the trigger task 9 (9621.61 2309.48 40.24) both
publish, poiId 0, 21.70 m apart; the world pin is only on task 9, at Z 41.99; the trigger complete removes task 9 and its pin, the root
stays; the root complete gives the hand-in place of 01_AelstromWW_01 (9613.36 2329.27 42.43).

The check: the fixture `tests/ui/fixtures/questpins.lua` (52 checks: the two handles, the events and their arguments, the typed UI data,
the compass cache and the compass icon in the middle of the strip, the world pin of task 9 at 41.99 m in the middle of the view and no pin
on the root, the map layer position, the trigger completion, the hand-in, the removal; pictures questpins-compass, questpins-world, and
questpins-map). The data report (2026-10-23): 7272 task records, 2707 overrides (2731 points; 109 cells with short entries), 4090 tasks
with POI ids (2600 with a backend point, 1490 with no point, 199 unknown tags), 969 ShowDiegeticTaskPin, 17 DiegeticPinZOverride, 44
conversation NPCs, 955 objectives with a hand-in NPC (24 NPCs with no place).

Open [U] (study 8): the task suppression byte +0x302, the game mode and mutation test, the POI provider point, the native completion
resolver, the dynamic mission locations, the gatherable entity pins, the TP_HideOnCompassOnEnterPOI reader, the quest search area, and the
NPC state priority (the ready NPC marker state of the hand-in: Q4 owns the NPC states).

## The persons, the offers, and the hand-in (Q4, AST-239)

Work in progress (the native conversation rules: the study `research/quest-conversation.md`, brief
`research/briefs/q4-conversation-objectives.md`).

- The state selection: the validity of a record (backend stage 1.8f): docs/PERSONS.md "The conversation state".
- The messages (protocol notes 3772, 794; backend `server/rep/quest_messages.h`; the type ids of the client message table): 3772
  RequestAcceptObjective and 794 RequestCompleteObjective, each the CRC-32 of the lower-case ObjectiveID on the Conversation facet of the
  player root (20 bytes; `conversation_objective_body`). The screen sends them with the host requests "quest accept CRC" and "quest
  complete CRC" (UI/SwcUiQuests). The server gives no answer: the Objectives state (a new instance; the completion notice in field 2) is
  the result; a refusal gives nothing. Tests: `tools/test-net.sh` (the backend reader reads the bodies).

### The check aid of a quest cutscene (development runs only)

`swc.DevCutscene NAME|CRC [SECONDS]` (console; a drive can send it with `exec`) sends the real cutscene messages of a quest task with no
playback: 6703 with the CRC-32 of the lower-case cutscene name; at the grant 6707 of the same CRC (the backend grants only when an active
TaskCutsceneComplete of the character names the cutscene), 6706; after SECONDS (default 3), 6705 (the server answers 6709). The backend
completes the cutscene task by its own rules. It is a check aid, not a product path: it runs only with `SWC_RUN=development`
(`tools/dev-run.sh`; `tools/play.sh` sets `player`, and the command is refused there), and it never runs without the command. The Q4 live
check uses it for the ready state of 06_getoffbeach (the beach cutscene `4817dd08`, the decision of the orchestrator, 2026-10-23). The
natural path (the activation of the scene by the achievements, the playback, the skip, the camera) is stage Q5. The log lines start with
"dev cutscene:".

## Open points

- The banner shows in the HUD state: an inference (`manager.lua`).
- `javelin.objectives-max-*` exist only for the sections of the retail picture.
- The journal has a hidden hand-in row "Talk to " (the retail script hides it with SetTaskVisibility).
- The Map button of the journal shows only when the objective layer of the map has a place for the quest
  (`ObjectivesLayer.GetObjectivePosition`): the pins of stage Q2 give it (live check open). The task timers are stage Q8.
- Q2 (done, AST-237): the open points of the study are in "The pins (Q2, AST-237)". Also open, for Q4 (AST-239): the retail compass
  shows the POI icon of the watchtower near the beach start, the client compass shows no POI icon; the live hand-in (the root complete)
  is checked only in the fixture; the ready NPC marker state of the hand-in (the NPC states are Q4).
- A change of an Auto-Pin switch does not go to the backend (2709 is sent once).
- The top menu bar of the retail journal (Company, Journal, Mount, ...) is a different canvas: not in the client.
- The skinning of a boar (AST-250): the trigger rule of the bodies (docs/INTERACTIONS.md); live `tests/drive/ast250-c2*.txt`: the quest
  drop PorkT1 and the next task "Cook a Light Ration at Urda's Campfire". Not shown live yet: the food craft (the camp of the watchtower is a later backend stage; a kitchen needs a travel), the natural gather of flint
  and wood (the items came from the development route), five kills (two credited kills shown).
- 06_getoffbeach of a new character with backend 1.8b: in the trigger area the visible task is complete and the hidden cutscene task is
  active, so the tracker shows only the title until the cutscene (stage Q5).
- The place cues of the 86 points of interest of other slices (no local), and of the areas (the 2297 of 9011 in the retail packets).
- The card of a quest object: the marker type Master_Quest and the gatherable card with no option text: "E ASTROLABE" as retail (AST-250).
- The world pins of the quest objects (the retail "1" over the treasure): stage Q2.
- Fixed (AST-237): pinnedobjective.luac called GetPositionY of a task id on each task change. quests.lua sent
  ObjectiveTaskNotificationBus.OnTaskUpdated(taskId); no script handles that event, and the tracker has a method of that name for its
  task rows (OnTaskUpdated(taskTable)). quests.lua does not send it now; OnTaskChanged stays.
- The Documents tab (JournalDataManagerBus.GetTopicEntries) and the season pass banner of bannertriggers.luac have script errors.
