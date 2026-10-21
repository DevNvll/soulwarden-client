# The persons

Stage P4.8 (Linear AST-191): the persons of the settlements and the Talk. The backend sends no record of a person: the client
makes each person itself from the level data, as the retail client does (Soulwarden `docs/systems/interactions.md`: 146 persons
with interaction data, and the persons of the client spawners). The services that a person opens (a shop, a station) come later.

The research: `~/Games/soulwarden-client/research/p48-persons/` (the census, the bodies, the timelines, the presence) and
`~/Games/soulwarden-client/research/p48-talk/` (the conversation rows, the texts, the camera presets, the camera check).

## The parts

| Part | Job |
| --- | --- |
| `converter/swc_convert/persons.py`, `tools/convert-persons.sh` | `write`: the persons of the level data with their names, places, bodies, items, timelines, presence, and the line and the camera of their conversation into `SWC_CONTENT/persons/persons.json`. `convert`: the bodies, the items, and the clips with the player pipeline. Then `tools/import-player.sh`. |
| `Persons/SwcPersonSubsystem` | The persons near the view (spawn within 150 m, 4 for each 0.25 s; remove beyond 200 m), their bodies, items, and clips; the Talk clips. |
| `Persons/SwcPersonAnimInstance` | One clip of a timeline at a time, with the blend-in time of the timeline. |
| `Interact/SwcConversation` | Plain C++ (tested by `tools/test-net.sh`): the conversation of the client (the open, the three ends, the lock until 6709) and the camera point of a preset. |
| `Interact/SwcInteractor` | OpenConversation of a person of the object layer (kind `npc`): the request, the conversation, the key E, the ends. |
| `SwcCameraManager`, `Character/SwcPlayerActions`, `SwcBackendSubsystem` | The camera of the line (a cut), the lock of the input, HidePlayerAvatar, the line for the debug text and the UI. |
| `Net/Codec` | Message 6705 (`RepClient::end_cutscene`). |
| `Lua/swc/conversation.lua`, `UI/SwcUiConversation` | The conversation screen (the original canvas ConversationScreenV2): its state, its native buses, the end request. |

```sh
tools/convert-persons.sh write                        # the persons of the main world (--near X Y R: a circle; --out FILE)
tools/convert-persons.sh convert                      # the bodies, the items, the clips (then tools/import-player.sh)
tools/test-net.sh                                     # the conversation and the bytes of 6705
```

## The persons of the level data

| Rule | Source |
| --- | --- |
| A person is an active skinned entity of a slice whose model is a `.cdf` on a person skeleton (the player skeletons: `npc_skel.chr` is byte-identical to the player skeleton). The slices of the level, their children, and the children of a PrefabSpawner with `m_spawnOnInitialize` (a client spawner). | research p48-persons, `census.py` |
| The variation row (three tables) replaces the CharacterDefinition, the idle timeline, the name, the NPC id, the AttachedMesh, and the scale. The name falls back to the GenericName of the NPCData row. The texts come from the en-us localization. | `resolve.py` |
| The variation row: with no variation data on the slice (only the NPC key of the NpcComponent), the row whose VariantID is the NPC id applies [inference; the commander of Oxboro: the row 0407a_Commander gives the idle malek_standing_ambient (a lean back) and the malek greet, interact, and bye timelines, as the user saw in the retail client]. 28 persons of the world change with this rule. | `persons.py` `resolve` |
| The presence: the swap of a person (region.metadata NPCData, else SwapAchievementId of the row) is an expression of achievement ids with `!`, `&&`, `\|\|`, and parentheses (887 expressions). An id is a bit of the Achievement state of the character (javelindata_achievements AchievementIndex); an id with `*` is a non-persisted achievement (javelindata_achievements_nonpersisted: the objective flow of a quest), false for the backend, which keeps no quest state. The person shows while the expression is true, or while it is false with ShowOnAchievementLocked. The converter writes the expression in postfix order (`presenceRule`); the client evaluates it with the Achievement state at the entry and at each change (`swc::interact::presence_visible`), and the loading gate waits for that state. | `presence.json`, `persons.py` `presence_rule` |
| The body: the visible CA_SKIN attachments of the `.cdf` (Flags bit 0: hidden) and the render skins of the CA_CLOTH attachments, all in one mesh node (Unreal makes one skeletal mesh for each glTF mesh node). The items: the CA_BONE attachments (a `.cgf` with its `.cgfheap`) at RelPosition and RelRotation on their bones, and the AttachedMesh of the row in the right hand. | `cdf-oxboro.json`, `bone-items.json` |
| The idle: the first clip of the PlayAnim layer of the start timeline Play_idleLoop (RunOnActivate), from time 0, in a loop. | `timelines.json` |
| The place and the facing: the matrix of the slice chain; the yaw of the data is counterclockwise from north (+y). The bodies have no collision. | `projection-check.json` |

The main world (`write`, stage MVP-H, 2026-10-21): 1,651 persons in 22 of the 49 regions (the other 27 regions have none), 836
shown for the level 70 character of the user (5 achievements, no quest state), 1,313 with Talk, 92 with an npc object of the
interaction data (the other 54 npc objects are rally boards, quest letters, and a cage: no body), 1,029 with a line, 1,002 with a
camera preset, 724 bodies, 39 items, 301 clips. The conversion: 724 bodies (9 with no skin that is not in the game copy:
femininechest forearms and the cape of Lord Galbrion), 37 of 39 items (2 tool meshes with no geometry), 301 clips; the import: all
but 4 bodies (17_iq_invisible, 17zqb_hiddennpc, blank: no visible skin; 17zqu_mayorbogdan: no mesh in its glTF).

| Region | Persons | Shown for the level 70 character | With an npc object | With Talk |
| --- | --- | --- | --- | --- |
| r_+01_+02 | 141 | 68 | 0 | 73 |
| r_+01_+03 | 91 | 44 | 2 | 50 |
| r_+02_+02 | 23 | 2 | 0 | 22 |
| r_+02_+03 | 126 | 21 | 5 | 104 |
| r_+02_+04 | 22 | 14 | 5 | 21 |
| r_+03_+00 | 30 | 29 | 4 | 30 |
| r_+03_+01 | 77 | 44 | 5 | 61 |
| r_+03_+02 | 62 | 29 | 0 | 55 |
| r_+03_+03 | 61 | 26 | 5 | 60 |
| r_+03_+04 | 64 | 39 | 0 | 53 |
| r_+04_+00 | 109 | 48 | 5 | 77 |
| r_+04_+01 | 148 | 78 | 7 | 129 |
| r_+04_+02 | 139 | 93 | 8 | 124 |
| r_+04_+03 | 159 | 79 | 10 | 135 |
| r_+04_+04 | 77 | 27 | 5 | 64 |
| r_+05_+01 | 36 | 26 | 2 | 31 |
| r_+05_+02 | 70 | 48 | 12 | 53 |
| r_+05_+03 | 98 | 37 | 4 | 93 |
| r_+05_+04 | 42 | 18 | 5 | 36 |
| r_+06_+02 | 33 | 32 | 6 | 16 |
| r_+06_+03 | 40 | 32 | 2 | 23 |
| r_+06_+04 | 3 | 2 | 0 | 3 |
| all (22 regions) | 1651 | 836 | 92 | 1313 |

Oxboro alone (`write --near 8880 4250 300`, stage P4.8): 48 persons.

The loading (2026-10-20, the loading gate of the world track (docs/WORLD.md "The loading")): during a loading screen the persons spawn at the place of the character
(all near persons in each frame, no budget), and the screen ends only when the visible persons within the spawn distance
(150 m) have their actor. In play the spawn distance is still 150 m with synchronous loads (open point: async loads and a
larger distance, so that a person does not appear at 150 m in view).

## The Talk

The key E at a person with interaction data (the object layer, kind `npc`): the option OpenConversation (action 63, committed,
hold 0.5 s, range 5 m). The sequence (Soulwarden `docs/systems/travel-and-conversations.md`, protocol notes 4.20; the retail live
checks L1 and L7 of the Soulwarden stages 2.5a and 2.5b):

| Step | Client |
| --- | --- |
| The request | At the end of the hold the request goes into the input entries and stays until the end of the conversation (a use that keeps its request). The client opens the conversation at once, before the reply. |
| The open | The cut to the camera of the line, HidePlayerAvatar, the lock of the input of the character (no move, no action, no camera turn, no prompt). The person plays the clip of GreetTimeline, then the loop of InteractTimeline. A two-shot preset shows the dummy of the player (below). |
| The end by the player | E (until the conversation screen of the UI: its Next key at the last line and its Back key call `USwcBackendSubsystem::EndConversation`): 6705, then 2473. |
| A refusal | 3743 result 2: 6705, no 2473. |
| The end by the server | 3746 (the session client sent 2473): 6705. The death and a loading screen end it in the same way. |
| The close | The cut back to the state camera, the player shows again, the person plays the clip of ByeTimeline, then the idle. The input stays blocked until the answer 6709. |

The line: the conversation state is the last state of the row with no Required field; the line is its localized text (the gesture
tags removed). The camera: the preset of the camera state of the line (`NPC_CameraPresets`): the origin and the look-at point in the
frame of the person (the side axis (cos yaw, sin yaw), the forward axis (-sin yaw, cos yaw), up), the vertical FOV of `NPC_FOVnn`,
a cut (blend 0). A preset with no look-at point looks at the person at the height of the camera (a client rule). The line goes to
the debug text and to `USwcBackendSubsystem::GetConversationLine` for the conversation screen of the UI.

The dummy of the player (the two-shot presets, 26 of the 32 lines of Oxboro): DummyPresetPos `s .25 1.3 0` and DummyPresetRot
`s 0 0 180` of the preset are a place and a rotation in the frame of the speaker ("s"), DummyIsArmed false. HidePlayerAvatar hides
the character, and the dummy stands at that place, turned to the person: the client moves the body of the character (the leader
mesh, its parts, its gear) there for the scene and hides the gear of an unarmed dummy (`ASwcPlayerCharacter::SetDummy`). The
character, its capsule, and the position of its input reports do not move.

## The Talk of a person with no data

A person with the option OpenConversation and no npc object of the interaction data (the persons of client spawners, the persons
of capital children: 1,221 of the 1,313 with Talk) gets a prompt within 3 m (the retail prompts of persons: 2.07 to 2.98 m, set 5)
and the key E sends the request of OpenConversation (the option of the interaction data). The rule of the retail ids of such a
person is not known: the client sends ids that no object of the data has (G from the key of the person; the actor is G in the other
byte order, then 8 zero bytes; the option entity is G with 2 added to its high word, as in the retail requests). The backend refuses
the request (3743 result 2), as it refuses the retail request of the artificer of Oxboro: the client closes the conversation at once
(6705, no 2473), and the answer 6709 frees the input. The Talk key of such a person is `person:` + its key of persons.json.

## The services of the persons

The backend handles only the presentation options of a person (`apps/backend/src/world/interactions.cpp` kPresentationOptions:
OpenConversation): it has no handler for OpenInnScreen, SetSpawnPointOption, OpenMissionBoardOption, or the shops (OpenWolfShop,
OpenLionShop, OpenHorseShop) and refuses them (3743 result 2). The option lists of the 146 npc objects: OpenConversation 89;
OpenConversation, OpenMissionBoardOption 38; OpenInnScreen, SetSpawnPointOption, OpenConversation 14 (the inn keepers); the rest
4. The prompt executes the first option: E at an inn keeper sends OpenInnScreen, the backend refuses it, nothing opens, and the
input stays free. The client adds no quest, shop, or inn flow.

## The conversation screen

The original canvas ConversationScreenV2 with its script (conversationscreenv2.luac), through `swc/conversation.lua` and
`UI/SwcUiConversation` (stage MVP-H). Each frame the client compares the Talk scene (`USwcBackendSubsystem::GetConversationSceneKey`)
with the screen:

| Step | Rule |
| --- | --- |
| The open | `Swc.HostConversation("open", KEY, SPEAKER, LINE, NAME, TITLE)`. The first open loads the canvas as the retail dynamic canvas loader does (autohide, FULLSCREEN_DRAW_ORDER; that loader does not run in the client). One conversation state goes to `DynamicBus.ConversationScreenV2` with the copy of dynamiccanvasloader.luac: the dialogue "@LINE", the speaker attribute, and one option Continue ("@ui_continue", ContinueDialogue). The script sets the state ConversationScreenV2 (the HUD hides) and shows the subtitle "SPEAKER: TEXT". |
| E (Next) | The center hint of the script: RequestConversationOption; the state after the last line has no option; the script closes the screen and calls TryCloseInteraction (HandleOnCloseConversation): the request "conversation end": `EndConversation` (6705, then 2473). The key E also reaches the interactor, which ends the scene in the same way (one end). |
| Escape (Back) | OnEscapeKeyPressed: CloseScreen, TryCloseInteraction: the same request. The escape menu then opens too (as in the retail client, set 5). |
| The close by the client | A refusal, the end by the server, a death, a loading screen: `Swc.HostConversation("close")`: a state with no option closes the screen at once, with no request. |

The native values of the screen that the client gives: the npc data of the person (its name and title; no faction), no VO, no
cinematic, no timeline, no camera override (the client sets the Talk camera), an empty top progression, the faction cooldown "now",
and the four subtitle options that the options data does not decode (the saved options of the retail client: speaker color 0, text
color 0, text size 1, border 0). `Vector3.Normalize(v)` and `CrossZAxis` of the script: the value class of the runtime.

## Check (development runs, 2026-10-19)

| Check | Result | Evidence |
| --- | --- | --- |
| The bytes of 6705 | The reader of the backend reads the call of the cutscene facet of the character (the layout of its 6709). | `tools/test-net.sh` (219 checks) |
| The conversation | The open at the request, the three ends with their messages, the lock until the next 6709, no second 6705. | `tools/test-net.sh` |
| The camera of the commander (0407a_Commander) | The camera point is 0.00 m from the live camera of the retail client in the horizontal plane. | `tools/test-net.sh`, research p48-talk `camera-check.txt` |
| A Talk with E | The request at the end of the hold, the reply 1, the cut, the hidden player, the greet and the talk clips; E: 6705, 2473, the answer 6709 in the same frame, the walk. | `evidence/P4.8/p48-talk.mp4`, its logs |
| A Talk ended by the server | 3746, 2473 of the session client, 6705, 6709; the walk starts 0.28 s after the W key, at the answer. | `evidence/P4.8/p48-talk-cancel.mp4`, its logs |
| The view of the Talk against the retail picture L1-10 | The same framing (the camera, the person, the fence, the stairs); the person faces the camera with the talk clip. | `evidence/P4.8/talk-side.png` |
| The matched views of the Talk pictures of the Soulwarden live checks (`tools/match-view.sh` with a folder of two views: the live retail camera, the look-at point and the FOV of the line, the hour 12:00) | L1-10: the commander at the same place and size in the frame, with the same background. L7-01: the artificer (0405a, a person of a client spawner) at the same place and size, the lantern, the mug, the tree; the character at the dummy place shows where the retail dummy stands. | `evidence/P4.8/talk-refs/`, `talk-match/` |
| The matched views of set 1 (Oxboro, noon, and the inn) | 40 of the 48 persons spawn near the views (39 visible). In the inn view the person outside the door stands at the same place as in the retail picture; the lane views have no person near the camera, in the retail pictures and in the client. | `evidence/P4.8/match-2/` |
| A Talk with a two-shot preset (1225_Giacomo_EF) | The dummy at its place, turned to the person (yaw of the person + 180), no weapons; the person plays the watcher greet, interact, and bye clips; the body goes back at the end. | `evidence/P4.8/p48-dummy.mp4`, its logs |

## Check of stage MVP-H (development runs with a copy of the user's database, 2026-10-21)

| Check | Result | Evidence |
| --- | --- | --- |
| The fixture `tests/ui/fixtures/conversation.lua` | The open (the state, the subtitle "Zeng Lingyun: The Empress needs ..."), E: one request, Escape: one request, the close of the client: no request; no Lua error. | `tools/test-ui.sh --fixture conversation` (17 checks) |
| The presence evaluator | `a && !b`, ShowOnAchievementLocked, a non-persisted id, a malformed rule. | `tools/test-net.sh` |
| Windsward | The persons of the region spawn in the loading gate (836 of 1,651 shown, 0 changed by the Achievement state); the inn keeper Verildis Jannsen: OpenInnScreen refused; Moira Chapman (no npc object): the request, 3743 result 2, 6705 with no 2473, 6709, the walk. | `evidence/MVP-H/mvph-windsward1.mp4`, its logs |
| Monarch's Bluffs | Commander Purcell: the Talk, the screen with his line in two lines, the HUD hidden; E (Next): the screen request, 6705, 2473, 6709, the walk. | `evidence/MVP-H/mvph-talk-monarch.mp4` |
| Weaver's Fen | The commander Ozian Atreus: the screen; Escape (Back): the same end, then the escape menu. | `evidence/MVP-H/mvph-talk-weaversfen.mp4` |
| Oxboro | The commander plays the idle and the talk timelines of her row (malek_standing_*). | `evidence/MVP-H/mvph-talk-oxboro.mp4` |
| The matched views of set 5 (the commander, the artificer, Giacomo at idle) | The persons at the retail places. Differences: below. | `evidence/MVP-H/match-set5/` |

The differences of the matched views of set 5 (`evidence/MVP-H/match-set5/side/`, close crop `crop-commander-idle.png`; the place,
the size, and the facing of the three persons agree):

1. The commander: the retail armor is gold and steel, the client armor is matte brown (the specular map, the metal color of this
   material model, is not used); the retail hair is black, the client hair is light brown (the hair tint of the mask is not used).
2. The commander: the pose at the moment of the picture differs (retail: upright, the arms down, the head to the camera; client: a
   hand at the belt, the body turned). The client plays the row idle malek_standing_ambient_idle (63 s) from time 0 at the spawn; the
   retail phase is not known, and the look-at (useLookIK) is not done.
3. Giacomo: the retail hands are low, the client hand is at the forehead (a phase of settle_watcher_ambient_01); the retail hair is
   black, the client hair is grey.
4. The artificer: the clothes are lighter in the client (the same material causes); her place at the stall agrees.
5. Not persons: the forge shed left of the artificer and the shed behind the commander are missing in the client run (level
   objects; the world track), and the development character of the run has no gear.

## Not done, open points

- The key hints of the conversation screen: the retail screen shows "E Next" and "ESC Back" at the bottom right; they are the
  HelpBar screen (a screen of the UI loader, group 8, census stage P6.9) that the client does not load yet.
- An inn keeper: the Talk is the third option; the prompt executes only the first option (OpenInnScreen, refused).
- The retail Talk opens with no hold, with a black frame and a fade (set 5); the client keeps the hold of the data and a cut.

The Talk work stops here (the priority of the user, 2026-10-19: the world, the graphics, and the gameplay fidelity first; the
interactions later). The retail reference of the Talk is set 5
(`~/Games/soulwarden/work/evidence/unreal-references/set5-talk/`, README.md with the time lines). Its results that the client
does not have yet:

- The open: the retail client opens the Talk at the key, with no hold (an 80 ms tap; the request 0.13 s after the key; the hold
  0.5 s of OpenConversation in the data does not apply). The picture goes black 0.07 to 0.22 s after the key with no fade-out,
  stays black 0.40 to 0.53 s, and fades in to the Talk camera in 0.55 to 0.65 s; the subtitle and the keys fade in 0.40 to 0.45 s
  after the black. A first version (the open at the key, black 0.48 s, the fade-in 0.60 s, the camera after the end) is on the
  local branch `talk-set5-wip`; it is not checked in a run.
- The end: no fade; the first world frame comes 0.08 to 0.16 s after the key; 6705, then 2473; the input is free about 0.1 s
  after the key. The player camera keeps the heading and the pitch of the Talk camera; the body keeps its heading until the first
  move, which turns it to the camera heading. Escape also opens the game menu bar.
- The prompt: the retail prompt of a person shows only near it (2.07 m for the commander, 2.56 m for the artificer, 2.98 m for
  Giacomo; none from the street below the deck of the commander): a trigger area, not the range of 5 m that the client uses.
- The persons of client spawners (the artificer): the retail client shows the prompt and sends the request; the backend replies
  3743 result 2 ("no object of the data has this reference"), the client sends 6705 0.02 s later (no 2473), shows one black frame,
  and no conversation screen. The fields of the retail request (backend logs of development runs; level-data ids, the same in each
  run):

  | Person | Actor | G | Target | Option entity | Option | Reply |
  | --- | --- | --- | --- | --- | --- | --- |
  | 0407a_Commander | 4388a748b22e58760000000000000000 | 76582eb248a78843 | ab709fc1bb40ad99 | 76582eb448a78843 | 507cac68 | 3743 result 1 |
  | 0405a_ArtificerPetrowski | b7a7a8a0101431950000000000000000 | 95311410a0a8a7b7 | ca4985201341cd0d | 95311412a0a8a7b7 | 507cac68 | 3743 result 2 |

  The actor is G in the other byte order, then 8 zero bytes; the option entity is G with 2 added to its high 32-bit word. The
  block has the slice name after the byte 0x40 ("0407_Commander", "0405a_ArtificerPetrowski"; the byte before 0x40 is the length
  of the name + 1); the block of the artificer has 4 more bytes at its end (ef5b4fe0). The rule of G and of the target of a
  spawner child is not known yet.
- The colors of the persons (the idle pictures of set 5 with their cameras): the retail hair is black (the commander, Giacomo)
  and dark brown (the artificer), the skin and the items are darker, and the armor of the commander has gold and steel parts.
  The causes that the research found: the player pipeline uses only the diffuse and the normal map of a .mtl. Thus (1) the hair
  tint of the mask of the material is not applied (the commander: `female_hairstyle_short_blacka.mtl`, Mask_R 1 and Mask_R_Color
  0.048); the hair material of the artificer has no mask color (the source of her hair color is not known); (2) the specular map,
  which has the color of a metal in this material model, is not used: the metal parts show the matte diffuse color. The skin
  texture of the commander is the l4 tone (a medium tan); the check of the skin needs the color report of the person band.
- The clips (a visual check of set 5): the commander lowers her head after the black, then her arms, and makes open-hand gestures
  from about 3 s into the line; after the end she lowers her arms in about 0.4 s. Giacomo points forward with the right arm for
  about 1 s, then puts his hands on his hips (also after the end).

- The next lines of a conversation tree, the services of a person (the shops, the stations, the missions): later stages.
- The persons of the client spawners: the prompt and the refusal (above, "The Talk of a person with no data"); the retail ids of
  their request are not known.
- The order of the talk timelines is a client rule from their names: the slice has empty event lists for them, and no retail video
  of a Talk exists yet (only the stills L1-10, L1-11, L1-12, L7-01, F-11).
- The look-at of the persons (`useLookIK`), the lip sync and the audio of the line, the ambient gestures: not done.
- The persons have no collision (the retail rule is not measured).
