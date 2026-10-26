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

The native rules: the study `research/quest-conversation.md` (brief `research/briefs/q4-conversation-objectives.md`). The tags [R] [D]
[I] [U] of the study apply. The code: `swc/conversation.lua` (the result), `swc/npcstates.lua` (the NPC states), `swc/markers.lua` (the
overhead marker).

- The state selection: the validity of a record (backend stage 1.8f): docs/PERSONS.md "The conversation state".
- The messages (protocol notes 3772, 794; backend `server/rep/quest_messages.h`; the type ids of the client message table): 3772
  RequestAcceptObjective and 794 RequestCompleteObjective, each the CRC-32 of the lower-case ObjectiveID on the Conversation facet of the
  player root (20 bytes; `conversation_objective_body`). The screen sends them with the host requests "quest accept CRC" and "quest
  complete CRC" (UI/SwcUiQuests). The server gives no answer: the Objectives state (a new instance; the completion notice in field 2) is
  the result; a refusal gives nothing. Tests: `tools/test-net.sh` (the backend reader reads the bodies).

- The conversation result (study 3 to 6): one typed result {title, dialogue, responseType, dialogueId, options} as the native
  builder 0x37ACF10. Response values: 1 Default, 2 Topic, 3 Proposal, 4 Acceptance, 5 Completion, 6 CompletionAvailable, 7 InProgress,
  8 OnCooldown. Option values: 1 Leave, 2 Topic, 3 ObjectiveDetails, 4 Accept, 5 Complete, 6 Back, 7 Continue.
  1. The ready hand-in has the first priority: the first active objective (in its stored order) with NpcDestinationId this person and
     its root Complete or CompleteActive gives CompletionAvailable 6 (or InProgress 7), its Title, its CRC as dialogueId (the details
     panel shows the quest and its rewards), and CompleteObjective 5 with DestinationCompletionAvailablePrompt. The scan of the offers
     does not run.
  2. Else the LinearObjectiveId of the selected state go through the classifier 0x37AE910 in their order: Proposal 3 gives an
     ObjectiveDetails 3 option (PlayerPrompt), InProgress 7 the InProgressResponse. With the linear setting
     (`javelin.enable-linear-conversations`) the scan stops at the first result.
  3. Then the topics, and Leave 1 (`@NPC_EndConversation`) last.
  4. Continue 7 is a local page action: the next page of a line chain, with its `dialogue-prompt` as the option text (0x3779AE0;
     conversations.json "prompt"). The real option type stays under the page chain: the Accept or Complete option shows on the last page.
  5. ObjectiveDetails gives Proposal 3 with AcceptObjective 4 (`@NPC_AcceptObjective`). Accept checks the capacity of the type
     (`HasRoomForObjective`), shows Acceptance 4 at once, removes the option, and sends 3772. Complete shows the prepared Completion 5,
     removes the option, and sends 794. There is no timeout and no retry: a refusal gives no answer and nothing changes (study 5.5).
  6. The refresh (study 5.4): after an owner update (the quest state, the Achievement state) the client selects the state again, one
     time in the next frame. A result with objective content is built again; else the prepared result stays. Thus after 794 the same
     conversation shows the next greeting and the knife offer (retail F1-12).
  A person with no NPC row keeps its line chain and gets the default result with Leave. `HasRoomForObjective`,
  `GetObjectiveDifficultyLevel`, and `RecipeDataManagerBus.GetRecipeIdByAchievementId` (0 with no recipe) serve the screen.
  OnObjectiveCompleted has the objective CRC and the creation params (bannertriggers.luac). The converter adds the line data of the
  objective texts (the columns *Prompt and *Response) and the `dialogue-prompt` of each line.
- The NPC states (study 7): `swc/npcstates.lua` N.Query is the native query 0x37AC8C0, an ordered decision tree: (1) the ready hand-in:
  TurnIn 4 when DestinationCompletionAvailableResponse has text, else NoObjective 0; (2) else the LinearObjectiveId in marker mode
  (HideIncompleteTurninMarker hides an incomplete result): HasObjective 1, InProgress 5, OnCooldown 16; (3) else an active talk-to task of
  the person: HasTalkToTask 13 [I: the task codes 16 and 17]; (4) else an active objective with NpcDestinationId this person and no
  HideIncompleteTurninMarker: InProgress 5; (5) else the first service with a state (Inn 7, Event_Store 8, Horse_Store 9, Wolf_Store 10,
  Lion_Store 11, Expansion_Modal 12, Transmog 14, Barbershop 15, Open_Mission_Board 18) [I: Faction_Board 3 and Open_Shop 17 have gates
  that the client does not have: not used]. The persons: `Swc.HostPersons` (UI/SwcUiSubsystem.cpp, the persons of the presence with a
  Talk and an NPC id, at each change of the presence revision of USwcPersonSubsystem). The states are computed again each second and
  after each change of the quests and the achievements [I].
- The three views (study 8):
  - The compass: OnNpcHasAvailableQuestChanged(npcCRC, state, worldPosition, objectiveType) at each change, for the persons within
    200 m of the player [I: the native publisher runs for the NPC entities of the stream]. A person that leaves the range or the
    presence (the walkaway Aelstrom after the knife offer) gets the clear form (NoObjective, type 0). RequestPublishNpcStates sends
    each state again and keeps the published list. MapComponentBus.GetNpcData gives factionType None (compass.luac indexes
    factionInfoTable with it).
  - Over the person: a Conversation marker in the static pool w after the 5 quest pins [I: the allocation order], with the NPCState
    object {State, ObjectiveType, ConversationObjectiveInstanceId}; the params of the type: wZ 1.9 m, the fade at 125 m [U: the exact
    boundary], the scale 1 to 0.35 from 0 to 40 m. staticworldmarker.luac registers its MarkerComponentId callback with no execute, so a
    slot whose element has no marker yet sends its data again each second (the quest pins too). The static markers do not show while a
    conversation is open [I: retail F1-12 and F1-13 show no marker over Aelstrom; markercommon.luac hides its canvas only for one other
    screen state].
  - The map: GetNPCMarkers (0x3A3CEB0, the incomplete destinations with no HideIncompleteTurninMarker, not ready) and
    GetAvailableQuestsByTerritoryId [I: the offers (HasObjective) of the persons of the presence in the territory; the map helper filters
    HideAvailableMapIcon]. The ready destination comes from the pins of Q2.
- The beach case (study 9.5), the client log: Aelstrom TurnIn 4 (the ready 06_getoffbeach), then HasObjective 1 (the knife offer
  06_skinningknife), then NoObjective 0 after the accept.
- Fixture `tests/ui/fixtures/questpersons.lua` (52 checks: the result, the pages, the accept and the complete, the refresh, the NPC
  states, the compass events, the clear form, the marker over Aelstrom in the middle of the view).
- The live check (2026-10-23, the drives `tests/drive/ast239-exit4*.txt`, a new development character in its own data folder; the ready
  state with the check aid below): the green ready marker over Aelstrom, the compass, the tracker, and the map; the hand-in pages, the
  reward panel (200 XP, 1.50 coin), 794; the next greeting and the knife offer in the same conversation; 3772; the Basic Needs banner and
  the knife pin at Urda; after a new login: level 2, 200 XP, 1.50 coin, 06_skinningknife active, the journal with Basic Needs. The pairs
  with the retail 1.8b references P1-01, P1-03, F1-11, F1-12, F1-13, F1-15, F1-16 are in the evidence of AST-239.

### The check aid of a quest cutscene (development runs only)

`swc.DevCutscene NAME|CRC [SECONDS]` (console; a drive can send it with `exec`) sends the real cutscene messages of a quest task with no
playback: 6703 with the CRC-32 of the lower-case cutscene name; at the grant 6707 of the same CRC (the backend grants only when an active
TaskCutsceneComplete of the character names the cutscene), 6706; after SECONDS (default 3), 6705 (the server answers 6709). The backend
completes the cutscene task by its own rules. It is a check aid, not a product path: it runs only with `SWC_RUN=development`
(`tools/dev-run.sh`; `tools/play.sh` sets `player`, and the command is refused there), and it never runs without the command. The Q4 live
check uses it for the ready state of 06_getoffbeach (the beach cutscene `4817dd08`, the decision of the orchestrator, 2026-10-23). The
natural path (the activation of the scene by the achievements, the playback, the skip, the camera) is stage Q5. The log lines start with
"dev cutscene:".

## The scenes, the cinematics, and the whispers (Q5, AST-240)

The native rules: the studies `research/quest-scenes.md` (brief `research/briefs/q5-scenes-whispers.md`) and `research/scene-curves.md`.
The code: `Scenes/SwcScenes` (the activation and the handshake), `Scenes/SwcScenePlayer` (the play), `Scenes/SwcSceneCurve.h` (the key
curves), `swc/scenes.lua` (the cutscene screen, the subtitles, the whispers, the video stand-in). The data: `tools/convert-scenes.sh`
(`converter/swc_convert/scenes.py`).

- The data (`tools/convert-scenes.sh write`): `quests/scenes.json` (the 58 placed cutscene spawners with their AchievementEvents compiled
  to rules of the Achievement bits, the AOI radius, the trigger child; each scene: the actors, the clip files of the aliases, the length),
  `quests/scenes/NAME.json` (CutsceneInfo, the start and stop events, the entities with their slice values, attachments, and cinematic id,
  the timelines with the layer bindings in hex), `quests/whispers.json` (WhisperData, the whisper timelines, the cinematic registry with the
  Bink header), and `ui/gamedata/scenes.json` (for Lua: the cinematics, the whispers by CRC with their command clips, the scene command
  clips, the loc attributes speaker, VO_Type, start, end, dialogue-next). Each output is written by a rename (a check copy is a link tree).
  `tools/convert-scenes.sh convert`: the actors of the beach chain (the bodies of the player and person skeleton: the player pipeline,
  PersonBodies; one clip pack clips|male|scenes WITH the root track, each clip of each call; the other skeletons as characters).
- The activation (study 1): a spawner runs an AchievementEvent when its rule becomes true while the player is in its AOI radius, or at the
  entry of the radius with the rule true [I: the AOI radius as the load gate]; after the event delay, entity event 0 on a trigger with
  playOnLoad or entity event 2 requests the scene [I: 0 activates, 2 triggers, 1 deactivates]. No 2 s timer. One pending 6703 for 30 s at
  most (then the failure, 6705, no second 6703).
- The handshake (study 2.1, 3, 5, 6): the grant 6707 of the same CRC builds the scene, runs the start events, the cutscene screen
  (OnCutsceneStarted), then 6706; a grant of another CRC sends 6705. One common end: the deactivation of the slice root (the normal end;
  the destroy timeline at 29.1 s of the beach), a skip (the cutscene screen: a hold of ui_cancel 2.25 s, RequestStopCurrentCutscene: the
  stop events, the main entry simulates its end), a cancel 6708, a travel (a new travel revision of the server) [I: no retail travel
  handler]. Then the scene goes, the exit camera IGC_CameraState_Generic until the next move or look input [U: the input event], the
  screen gets OnCutsceneEnded, 6705 goes out once; the hard input lock (the move, the actions, the focus and the prompt) stays until 6709.
  A world exit ends the scene locally with no message (a new login requests it again: the rule stays true).
- The play (study 2.3, 2.4, 3; research/scene-curves.md): the slice root at the world matrix of the trigger; each entity at its slice value;
  the bodies of the scene CDFs; the dummy of the player is a copy of the body parts of the local character (playercharacterupdate.luac
  copies the look and the equipment; dummyIsArmed false) [I: the copy of the parts of this client]; the real character is hidden
  (m_hidePlayerAvatar) and keeps its position. The timeline actions 0 to 6, the forward clip dispatch in steps of 0.05 s, the layer Ignore,
  the layer bindings (LayerEntityAssignments), ShowMesh, HideMesh, ActivateEntity, DeactivateEntity, TriggerEntity, UnTriggerEntity, the
  animation clips (only Layer 0 [I: no layered blend: the face clip of the statue scene does not play]), the static props, the props on a
  bone. The key curves of research/scene-curves.md (the right key mode, Bezier by subdivision; `tools/test-scenes.sh`: the worked beach
  pair). The transform clip is the local transform under the parent entity (the camera under its Camera Group), Euler order 0 = Ry Rz Rx,
  the camera looks along +Y [I: the live close-ups of Aelstrom at 0.7 to 1.7 m fit the retail C1 and I1 shots; the slice frame gave 5 to
  8 m]. The vertical FOV of the CameraComponent (the beach FOV layer has Ignore) [U: the projection axis].
- The subtitles (study 7): the CommandClip PlayWhisperSubtitle_@KEY of a scene timeline: a Subtitle notification as whispers.luac (the
  speaker as title, maximumDuration the clip duration, inGameCutscene for VO_Type igc_). The subtitle option is on by default [I].
- The whispers (study 8): 5070 (the CRC-32 of the lower-case WhisperId on the Whisper child) plays the command clips of its whisper
  timeline on its own clock with no audio; the silent VO token ends after the later of the row Duration and the last command end + 0.25 s.
  A new whisper replaces the active one [U: no FIFO is proved].
- The cinematic (study 9; research quests-18e cinematic): a TriggerEntity on an entity with the cinematic component (the statue lead-in at
  9.7 s) starts its video after the scene [I: the native deferral while a cutscene is current]. The declared stand-in: the original
  videoplayerscreen.luac on PlayCinematicVideo with a Bink clock of the header length (TempestRising 106.74 s): 6606 at the start of the
  clock, 6542 at its end or at a skip (a hold of ui_cancel 2.25 s); a black image with the cinematic subtitles (no decoder, no audio). It is
  not full video parity (a decision of the user is needed to replace it in Q9).
- The development preview: console `swc.ScenePreview NAME` (development runs only): a scene with no handshake and no quest effect.
- Fixtures: `tests/ui/fixtures/scenes.lua` (26 checks: the cutscene screen, the skip hold and the tap, the scene subtitle, W1 and its VO
  token, the TempestRising stand-in: 6606, the skip, 6542, the natural end). Converter tests: `tests/converter/test_scenes.py`.
- The live checks (2026-10-23, new development characters, the drives `tests/drive/ast240-*.txt`; evidence AST-240):
  - ast240-exit2: the natural beach scene at the world entry (6703, 6707, 6706, the five subtitles, the root at 29.1 s, 6705, 6709), the
    placed Aelstrom after it, the walk, the natural hand-in of Q4 (no check aid: 794, 200 XP, 1.50 coin, level 2) and the knife offer (3772).
  - ast240-skip (the hold of Escape: the stop at 11.6 s, 6705, the task complete); ast240-cancel3 (a travel at 16 s: the end Travel, 6705,
    6709); ast240-exitcancel and ast240-relog (a world exit in the scene: no message; the next login plays it again and completes it).
  - ast240-grace4: the whisper W2 (52f36e05, the subtitle 11.1 s), the Grace intro scene (848caecf) with its props, 18.3 s.
  - ast240-raise2: the statue whisper (871ceb38), the statue scene (1dc4b4f7), the TempestRising stand-in (6606, 106.74 s, 6542: "a quest
    task changed"), the Corinth flyover (126b8567, 263 props): the root of 06_raisealarm is complete.
  - The pairs with the retail references 18b C1, I1, C2, W1, W2 and 18e C1: `ast240-retail-client-sheet.png`.
- Declared limits: the lights, the particle effects, the material clips, the fades of the helper timelines (fade from black and from
  white), the audio and the lip sync are not played; the face clips (Layer 1) do not play; the dummy has the male clips (a female character
  needs the female scene CDFs and clips) [I].

## The rewards and the offers (Q7, AST-242)

The sources: the backend stage 1.8d (Soulwarden `docs/systems/quests.md`), the study `research/quest-full-bag.md` (brief
`research/briefs/q7-full-bag-handin.md`), and the retail 1.8d references. The code: `swc/conversation.lua`.

- The availability of an offer (`unavailable`; the client checks 0x37AE910 and 0x3A4BFE0; backend `Quests::available`), in this order:
  1. an active instance of the row, or an active variant with the same completion achievement: no offer;
  2. the completion achievement is set: no offer (the cooldown of a repeat is Q8);
  3. a gate whose rule the client and the backend do not have (RequiredGameModeId, QuestArcTag, RequiredWorldTags, RequiredProgression):
     no offer;
  4. RequiredLevel above the level: no offer;
  5. the conditional of RequiredAchievementId is false: no offer. A non-persisted achievement (an NPA row; `quests.json` "npa") is its
     own conditional; it is false with a status requirement, an empty conditional, or at the depth 8. The archetype variants of
     06_boarfeast follow from this rule (`S_skinningknife && Archetype_X`);
  6. the faction mask of RequiredFaction does not include the faction of the character: no offer. Masks: blank or None 0xF, Any 0xE,
     Syndicate 0x2, Marauder 0x4, Covenant 0x8; a leading "!" gives the complement (blank negated 0xE). Bit 0 lets each character pass.
     The character is unaligned (faction 0): the backend has no faction of a character.
  The client hides an offer that fails (no locked offer; retail F-31).
- The reward previews: `objectivedatahelper.luac GetRewardDataFromObjectiveData` with GameEventRequestBus.GetGameSystemData and
  ObjectivesDataManagerBus.GetObjectiveRewards (`swc/quests.lua`). 06_getoffbeach: 200 XP, 1.50 coin; 06_skinningknife: 275 XP,
  2.25 coin, the Meat Pouch; 06_raisealarm: 120 territory standing. A blank ItemRewardQty is 1; an explicit 0 is no item.

### The full bag (a client decision, not a retail rule)

The retail client sends 794 with no check of the bag [R] (study 2). The backend refuses a hand-in whose reward items do not fit, and it
sends no answer (backend decision D5 option A). Thus a retail-like client would show a false completion. The rule of this project (study
5.3, the decision of the orchestrator, 2026-10-25) is an adaptation:

- Before the reward celebration and 794, the client plans the reward items with the rules of the backend: the item of the success event,
  then the item of the row (a blank quantity is 1, 0 is no item); the gives come before the takes of the return tasks, so a return item
  frees no slot; each item goes first into the stacks of the same item below the stack size with the same bind flag and the same perks,
  then into new stacks in the empty slots, up to 500 used slots (`Swc.bagCapacity`). The pouch itself counts, not its contents. An item
  that rolls perks gets no place in a stack. No margin.
- When the items do not fit: the retail notice `@ui_inventory_warning_toofullforrewards` ("Too many items in inventory to receive
  rewards.", Minor, 10 s; the text and the values of notifications.luac NotifyIfInventoryTooFullToRecieveGameModeReward). The ready
  dialogue and the Complete option stay. No 794, no celebration, no sound.
- A quest with no item reward (06_getoffbeach) sends 794 also with a full bag.
- The guard wraps the screen script method `RequestConversationOption` (it plays the celebration before it calls the bus,
  conversationscreenv2.luac PC 110 to 152). A second guard in the bus handler keeps the option for another caller (no second notice).
- The rewards and the completion show only after the owner states confirm them. A second press after a send has no option.

### The open of the Meat Pouch (a loot container)

- The bag action Open (dynamictooltip_commands.luac OnOpen, BoxOpenHelper) and a double click (newinventory.luac) call
  `LocalPlayerUIRequestsBus.SalvageItem(slot, 1, the inventory entity)`. For an item of the class LootContainer, `swc/items.lua` sends the
  host request "open SLOT QUANTITY INSTANCE"; `USwcBackendSubsystem::OpenContainerItem` sends 1733 (CraftingComponentServerFacet_
  InitiateSalvage, 48 bytes: the Crafting facet, the entity, the container type 1, the bag slot, the instance, the location quantity 0,
  the quantity; protocol notes 4.57). The salvage of other items is a later stage: no request.
- BoxOpenHelper waits for `DynamicCanvasLoaderBus.PrepareState(1000163614)`. This client loads its screens at the start, so
  `swc/manager.lua` answers true for each state.
- The answer is the bag state: the pouch one unit less (cause 2) and the items of the roll with the cause OpenRewardChest (26). For each
  item with the cause 26 the client sends `OnRewardChestOpened(slot, the added quantity)` to rewardcelebrationpopup.luac (the reward chest
  branch of the native dispatcher; no loot notice). The popup shows its title (the name of the pouch, "Essential Supplies Cache") and the
  items. The canvas `lyshineui/inventory/rewardcelebrationpopup` loads with the HUD screens (census class active).
- `ItemDataManagerBus.GetItemData` gives `salvageGameEventId` 0 (the column is not in items.tsv; the pouch has none): with another value
  the popup waits for the event data of its game event.
- The item card of the popup sets the color of an image sequence: the runtime has `sequence.color` (UiImageSequenceBus SetColor).

### The territory standing

The standing of a territory is an entry of the categorical progression state (slot 65) with the id CRC-32 of the territory number
(Windsward "6": 1DB87A14), the rank, and the points (protocol notes 4.57). The client keeps it with the masteries (`Swc.HostRanks`).
territorydatahandler.luac reads GetRank, GetMaxRank, and GetRankData (the title); the map shows GetProgression against
GetMaxPointsForRank. The rank table `Territory_Standing` (javelindata_territory_standing) is in `crafting.json` ("ranks" and "rankNames":
rank 0 has MaximumInfluence 150 and the title @territory_standing_title_1). `swc/skills.lua` keeps the methods of `swc/crafting.lua` for a
progression that is not a weapon mastery.

### The tests

- Fixture `tests/ui/fixtures/questrewards.lua` (125 checks): the order of the checks, the faction masks, the NPA rows, the boarfeast
  variants with each archetype, the reward data, the full bag (the knife with no compatible stack: refused locally with the notice; with a
  bound pouch stack below 10000: sent; a pouch stack that is not bound or full: no place; the first quest with a full bag: sent), the open
  of the pouch (the command Open, the request, the popup with the two items after the cause 26), and the standing (rank 0, 120 of 150,
  the title).
- `tools/test-items.sh`: the body of 1733, read by the reader of the backend. `tools/test-movement.sh` (the own states): the standing
  entry of the encoder of the backend (rank 0, 120 points) next to the masteries.
- The messages 3435 and 5684: the backend copy (2026-10-22-60bea89) sends neither. No reader in Q7.

## The timers, the failures, and the repeats (Q8, AST-243)

The sources: the backend stage 1.8d (Soulwarden `docs/systems/quests.md`: TaskTimer, the end of an instance, the repeat) and the static
timer study `~/Games/soulwarden/work/research/scratch/quests-18e/timer/ANSWERS.md`. The code: `swc/quests.lua`, `swc/conversation.lua`.

### The timer row (a client decision, not a retail rule)

The retail client publishes `Hud.LocalPlayer.MissionTimers.<instance>.TargetTime` at the bind of each TaskTimer, before its activation, as
a copy of a zero deadline; nothing publishes it again. pinnedobjective.luac and journal/objective.luac show their countdown row when the
node exists (also for a hidden timer) and read it once. Thus the retail live I3 row shows a red 00:00:00, because no known server field
updates it [R]. A counting row helps the player. The rule of this client (the decision of the orchestrator, 2026-10-25):

- The node is the real deadline of the server: the start of the task (field 0 of the Objectives state, the synced wall clock in Unix ns)
  plus its Duration (with its fraction), only for a running TaskTimer that is not hidden (Active; a failure timer runs as CompleteActive).
- The node goes when that task no longer runs. A hidden timer (the knife quest, 06_survivors) gets no node and no row.
- The client sets the node before the events of a new objective (the pin reads it in SetObjectiveId) and gives a changed value to the rows
  that read it before (their cached end time, the row, the tick).
- Only the server ends the timer and the quest. After a relog the start of field 0 stays: a past deadline shows 00:00:00 until the server
  state ends the task.
- `GetTaskStartTime` gives the start of field 0. The task TimeLimit of GetUIData stays 0 (retail: no writer of the field), so the task text
  has no countdown of its own.
- If a later study finds the retail field that updates the node, the client follows it.
- The data: 112 TaskTimers, 19 not hidden, none with FailOnCondition and not hidden. On the chain only 06_raisealarm_01_03_TaskTimer1 (6 s)
  shows a row.

### The failure

A removal of an instance with no completed entry is a failure (a failure timer, TaskDie): OnObjectiveFailed, and bannertriggers.luac
shows "Objective Failed" with the title of the quest. Fixed in Q8: the client removed the record before the event, the script stopped at
`GetObjectiveData(...).flagPvp`, and no failure card showed. The removed records stay readable during the removal events now.

### The repeat

- The classifier (0x37AE910): with `javelin.enable-repeatable-quest-cooldowns` on, a Repeatable row (1, true, TRUE) whose completion
  achievement is set answers OnCooldown 8: the dialogue ObjectiveOnCooldownResponse, no option of the row; the NPC state OnCooldown 16. A
  row that is not repeatable is completedBefore (hidden). The backend keeps the completion achievement to the start of the next cycle.
- `quests.json` "schedules" (javelindata_schedules: the start rule, the window, the weekdays, the UTC flag, the description key
  ScheduledQuestDescription). The objective data has `repeatable` and `scheduleId` (the CRC-32 of the lower-case id).
- `ScheduleDataManagerBus`: GetNextScheduleStartTimeSeconds, GetSecondsRemainingUntilNextScheduleStart (Hourly: the next full hour;
  Daily: 05:00 UTC; Weekly: Tuesday 05:00 UTC; the shipped defaults of the client, no region offset; a window: its start date),
  GetScheduleDescription (the description key), IsKeyInScheduleNow (a window that contains now).
- The backend supports no repeatable row now (their rewards: a standing rank-up, Azoth): the repeat is tested offline only.

### The messages 3435 and 5684

The backend copy sends neither (the warning 3435, the disable 5684): no reader.

### The tests

- `questtimers` (19 checks): the visible timer of 06_raisealarm (the node, the countdown of the tracker, GetTaskStartTime), the end of the
  timer task, the hidden knife timer (no row), a relog after the end (00:00:00), the journal row, the failure banner, and a hand-in (no
  failure).
- `questrepeat` (25 checks): OnCooldown and its dialogue, the flag, a row that is not repeatable, the objective data, the schedule bus
  with fixed times. The schedule checks need `quests.json` with "schedules" (a check file, deployed after the word of the orchestrator);
  without it they are skipped with a note.
- The live check (2026-10-25, evidence AST-243, `q8a`): a new character in TriggerVol_06_HeadStatueWhisper, the grant of 06_raisealarm.
  The tracker row counts down (00:00:04, 00:00:02, 00:00:01, 00:00:00); the server ends the timer task 6.05 s after the grant; the row
  goes and the next task shows; the journal has no row after the end; 0 Lua errors. The relog during the timer: see Q9 "The live checks".
  The failure banner and the repeat have no live path (no visible failure timer; no repeatable row in the backend): offline only.

## The coverage report and the destructible quest objects (Q9, AST-244)

### The coverage report

`tools/convert-ui.sh questsupport MANIFEST [--out FILE]` (`swc_convert/questsupport.py`) writes `quests/quest-client-support.json`
(outside git). MANIFEST is the support manifest of the backend (stage 1.8e; Soulwarden `tools/world quest support FILE`). For each row
the report adds the client side: the data (the row, the tree, the tasks, the title text), the grants (the NPC row, the state with the
offer, a placed person), the gate columns and their client rules, the client part of each task type (`TASK_PARTS`) and the client data
of its supplier (a person, a POI tag, a quest object of the event, a scene, a cinematic, a game event, a recipe, the creatures), the
reward kinds and their display, and the tests (`TESTS`). A quest object of an event: the gatherable row of the event (its objects have
the tag `IO_ROW`) or a destructible quest object row (its objects have their own tags); it counts only with drawn meshes. The class of a
row: "unavailable" (the backend does not support it), "missing data", "client stage" (a task type of a later client stage; none since
Q8), "untested", "tested". The counts are given for all rows, the supported rows, and each history of the manifest.
`knownGaps` lists the gaps that are not a part of Q9.

The result (the manifest of the 1.8f evidence, the game data of 2026-10-22; the content with the check file of the objects):
- 2,147 rows; 160 supported: 11 tested, 142 untested, 7 missing data (with Q8 the timer task is a client part; before Q8 22 rows were
  "client stage").
- The new character of each archetype (15 rows): 4 tested (offline fixtures), 10 untested, 1 missing data (a known gap).
- Known gaps: 9932_0104_RelicReclamation (its offer is a note, 9932_VK_RomacNote, with no placed person), two housing reward items that
  are not in items.tsv (0330_0103_pickingupthepieces, 1225_0401_siteinspection).
- The repeat check of the acceptance is blocked: backend 1.8f supports no repeatable objective (the nearest rows,
  C10A_S5R_LightningBottle and C10A_S7R_AnimalInstincts, stop at the rank-up of their standing: 450 points against the 150 of rank 0).

### The destructible quest objects

The source: Soulwarden docs/systems/quests.md "The destructible quest objects" (stage 1.8e and 1.8f). The objects are the capital
placements of the variation table Gatherable_Quest_Damageable (169; the four corruption nodules of 06_survivors, the monolith of
06_demonstorm2).
- `tools/convert-objects.sh quest-objects [--out FILE]` (`swc_convert/quest_objects.py`) writes `world-objects/quest-objects.json`: each
  object (the key, the managed actor, the InteractTag, InteractOnlyOnQuest, the place and the rotation of its capital entry, the gatherable
  row and its display name), the slices of its states (the placed slice; the variant visuals Visual_Healthy, Visual_NotOnRequiredQuest,
  Visual_Basemesh, Visual_Depleted), the marker (the type DamageableGatherable of the slice, the creature type override, the forced
  health bar), the collision override, and the effects of the variant; the render parts of each slice (the rule of
  `server_visuals.slice_parts`); the meshes that the plugin does not have ("missing") and that the manifest does not have ("unconverted").
  This build: 129 of 169 objects have meshes; all meshes of the two chain objects are imported; 32 meshes of 16 other variants need a
  conversion and an import.
- `Objects/SwcQuestObjects` (USwcQuestObjectSubsystem) draws the objects within 150 m, each 0.25 s:
  - InteractOnlyOnQuest and no Active task with its tag (`USwcBackendSubsystem::GetQuestTags`): nothing, no collision (the retail client
    shows nothing before the task; backend live I0);
  - active: the placed slice, Visual_Healthy, and Visual_Basemesh; the collision blocks only the movement of the player (the object type
    and the channel Pawn, as the body block of a creature: the camera and the traces do not see it); a plate of the AI pool (the marker
    type DamageableGatherable; the health bar at 1 of 1, the fallback of the Lua code; the interest 0xF0000000 + index);
  - destroyed (a Looted claim of its actor in the LootTracker state, slot 33): Visual_Depleted (often none), no collision, no plate.
    The respawn removes the claim, and the object is active again.
  The server decides each hit (the slice has no HitVolume: the client sends no contact; the normal attacks are the input).
  `-SwcNoQuestObjects`, `swc.QuestObjects 0`. Visual_NotOnRequiredQuest is in the file and not drawn [U: the retail rule of this
  visual; the live retail result shows nothing before the task]. The effects (VFX_Idle, DepletedVFX) are not drawn yet.
- Fixtures: `questobjects` (the plate of a nodule in the AI pool, its name "Destroy Corruption Nodule", the full forced bar; no plate
  after the destroy), `questsupport` (the availability rules of the client along the Soldier history of the manifest: 15 of 15 rows
  offered at their turn, no second offer after a hand-in, the boarfeast variants of the other archetypes hidden).
- The cooked build reads these files at run time: `tools/lib/loose-data.txt` now has `world-objects/server-visuals.json`,
  `world-objects/quest-objects.json`, `quests/scenes.json`, and `quests/scenes/*.json`. The meshes are in the always cooked folder
  `/SwcGameContent/Meshes`.

### The live checks (2026-10-25, evidence AST-244)

The order of the orchestrator limited the engine time. These checks use a development grant (they prove the client parts, not the
natural chain):
- `q9a`: a new Soldier, the grant of 06_survivors_soldier in FirstWhisperWW_IGC_Trigger, the whisper of Isabella, the Grace scene, the
  2 s timer: the four nodules go from hidden to active at the start of task 43 (none before). Light attacks: the server counts 4 hits for
  each nodule and destroys it; the client shows it destroyed; 33 s later the server gives it back and the client shows it active again.
  The tracker counts "2 / 4". Two nodules took no hit: the view step came before the travel reset the facing (a drive error).
- `q9b`: the same character logs in again with task 43 at 2 of 4: the four nodules are active again; the last two are destroyed; task 43
  ends, all four go hidden, the Grace outro scene plays, and the root waits for its hand-in.
- `q9c`: a relog at this ready hand-in: the Talk with Grace O'Malley and the hand-in (794 accepted: 450 XP, 3.25 coin,
  SoldierArchetypeLegs_Upgrade).
- `q9d` and `q9e`: a relog during the 6 s timer of 06_raisealarm (the exit at 9.5 s, the timer runs): at the next login the server ends
  the timer (76.6 s after its start, the time away counts) before the world entry; the client shows the next task at once, no stale row.
- 0 Lua errors in all runs. The plates of the nodules: the AI pool (4) was full near the wreck ("no pool slot") or the plate was occluded
  in most frames.
- The natural chain (2026-10-26, the cooked build of ac442a1, evidence AST-244 `c1` to `c23`, the drives in
  `~/Games/soulwarden-client/work/q9/chain`): a new Soldier (ChainS, its own data folder) played the ten quests to S_introtosettlement
  and 0625_0101_truffles with no quest grant and no achievement command. The aids: ignore on (off for the fights), the hour, the travels
  (with a short walk for the place reports), a standing boar and five Corrupted Mariners spawned in front of the character, the knife
  by a route before the camp fix (crafted at the camp later), and the equip of the knife and the sickle by the route (the client item
  menu of a tool has no Equip: a gap). Each hand-in has the reward of retail 18e: 06_getoffbeach 200 XP 1.50; 06_skinningknife 275 XP
  2.25 and the Meat Pouch; 06_boarfeast_soldier 350 XP 2.50, boots, 5 potions; 06_crewmates_soldier 400 XP 3.00, gauntlets;
  06_survivors_soldier 450 XP 3.25, legs; 06_pillageship_soldier 500 XP 4.50, chest; 06_recoveritems_soldier 550 XP 4.75, head;
  06_demonstorm 5 potions; 06_demonstorm2_soldier 650 XP 5.50, longsword; 06_raisealarm 800 XP 6.00, the amulet; truffles 400 XP 5.00,
  the ingredients box. Natural parts: the beach scene, the gathers (flint, bush, the body of a boar, the treasures, the flag and the
  wheel, five truffles), the crafts at Urda's Campfire (the Light Ration, the knife), the kills, the four nodules and the monolith,
  the lore page, the topic of Grace 03 (3158, 5397), the whispers, the scenes, the TempestRising stand-in, the offers.
- The relog at a scene (`c18`, `c19`): the run ended in the Corinth scene; at the next login the scene played again and completed.
- The camp station: the converter left out each Camp1 station; fixed (docs/CRAFTING.md "The open"), deployed 2026-10-26 05:34.
- Found (open): the item menu of a tool has only Lock and Drop (retail: Equip, Lock, Drop, Link to Chat, Salvage; and the tooltip has
  the gear score and "200% Gather Speed"); the craft result screen has no left buttons (Continue to Craft, Equip Tool, Salvage, Exit
  Station) and no summary of the trade skill XP, and shows the knife with "Gather Speed 135%" (retail 200%); craftingstatspanel.luac
  UpdateStatsInfo: "attempt to compare number with nil" at a tool recipe (2 errors); videoplayerscreen.luac ProcessSubtitleText:
  "attempt to concatenate a nil value" at 79 s of the TempestRising stand-in (1 error). No other Lua error in the 23 runs.
- The repeat stays blocked (no repeatable row in the backend).

## Open points

- The cinematics (TempestRising): the stand-in counts for the Q9 acceptance (the decision of the orchestrator, 2026-10-25); the quest result
  is correct, only the video picture is missing. Follow-up: find the video format of the cinematics (Bink 1 or Bink 2, from the file
  header; the paths end in .bk2). If ffmpeg decodes it, an offline conversion and a Media Player play replace the black image.
- The banner shows in the HUD state: an inference (`manager.lua`).
- `javelin.objectives-max-*` exist only for the sections of the retail picture.
- The journal has a hidden hand-in row "Talk to " (the retail script hides it with SetTaskVisibility).
- The Map button of the journal shows only when the objective layer of the map has a place for the quest
  (`ObjectivesLayer.GetObjectivePosition`): the pins of stage Q2 give it (live check open). The task timers are stage Q8.
- Q2 (done, AST-237): the open points of the study are in "The pins (Q2, AST-237)". The live hand-in and the ready NPC state are done
  in Q4 (AST-239). Still open: the retail compass shows the POI icon of the watchtower near the beach start, the client compass shows no
  POI icon.
- Q4 (AST-239) [U] of the study 10: the refresh schedule and the packet order, the global completion flag, the service 12 enum, the
  names of the NPC states 7 and 15, the exact 125 m boundary, the payment of each reward kind (the backend contract). The icons of the
  services (Inn, stores, barber) over the persons are not checked live.
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
