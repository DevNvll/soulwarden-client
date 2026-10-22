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

The writers of 2297 and 6416 (the entry and the exit of a place) are in the codec; their use is stage Q6 (AST-241).

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
- `tools/test-ui.sh --fixture quests`: 17 checks; `--fixture questprogress`: 12 checks (the counter, Reset and its popup, the screen
  queue, the tags of the quest objects).
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

## Open points

- The banner shows in the HUD state: an inference (`manager.lua`).
- `javelin.objectives-max-*` exist only for the sections of the retail picture.
- The journal has a hidden hand-in row "Talk to " (the retail script hides it with SetTaskVisibility).
- The Map button of the journal shows only when the objective layer of the map has a place for the quest
  (`ObjectivesLayer.GetObjectivePosition`): stage Q2, with the world pins and the compass pin. The task timers are stage Q8.
- A change of an Auto-Pin switch does not go to the backend (2709 is sent once).
- The top menu bar of the retail journal (Company, Journal, Mount, ...) is a different canvas: not in the client.
- The Documents tab (JournalDataManagerBus.GetTopicEntries) and the season pass banner of bannertriggers.luac have script errors.
