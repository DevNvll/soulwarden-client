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

```sh
tools/convert-persons.sh write --near 8880 4250 300   # the persons of Oxboro (no --near: the main world)
tools/convert-persons.sh convert                      # the bodies, the items, the clips (then tools/import-player.sh)
tools/test-net.sh                                     # the conversation and the bytes of 6705
```

## The persons of the level data

| Rule | Source |
| --- | --- |
| A person is an active skinned entity of a slice whose model is a `.cdf` on a person skeleton (the player skeletons: `npc_skel.chr` is byte-identical to the player skeleton). The slices of the level, their children, and the children of a PrefabSpawner with `m_spawnOnInitialize` (a client spawner). | research p48-persons, `census.py` |
| The variation row (three tables) replaces the CharacterDefinition, the idle timeline, the name, the NPC id, the AttachedMesh, and the scale. The name falls back to the GenericName of the NPCData row. The texts come from the en-us localization. | `resolve.py` |
| The presence: a person with a SwapAchievementId shows only with ShowOnAchievementLocked for a character with no achievement (region.metadata, NPCData). | `presence.json` |
| The body: the visible CA_SKIN attachments of the `.cdf` (Flags bit 0: hidden) and the render skins of the CA_CLOTH attachments, all in one mesh node (Unreal makes one skeletal mesh for each glTF mesh node). The items: the CA_BONE attachments (a `.cgf` with its `.cgfheap`) at RelPosition and RelRotation on their bones, and the AttachedMesh of the row in the right hand. | `cdf-oxboro.json`, `bone-items.json` |
| The idle: the first clip of the PlayAnim layer of the start timeline Play_idleLoop (RunOnActivate), from time 0, in a loop. | `timelines.json` |
| The place and the facing: the matrix of the slice chain; the yaw of the data is counterclockwise from north (+y). The bodies have no collision. | `projection-check.json` |

Oxboro (`write --near 8880 4250 300`): 48 persons, 39 visible for a new character, 33 with Talk, 4 with interaction data, 32 with
a line and a camera preset, 39 bodies, 6 items, 47 clips (the idle and the talk timelines).

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

## Not done, open points

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

- The conversation screen (the line, the keys E Next and Escape Back, the HUD hidden): the UI track, with the original canvas and
  scripts. The hooks: `GetConversationLine`, `EndConversation`. Until then E ends the conversation.
- The next lines of a conversation tree, the services of a person (the shops, the stations, the missions): later stages.
- The persons of the client spawners have no interaction data (the artificer at the forge of Oxboro): the retail client shows the
  prompt, sends the request, gets 3743 result 2, and closes the conversation. The client has no request for them yet (the ids of a
  spawner child are not in the object layer): no prompt.
- The order of the talk timelines is a client rule from their names: the slice has empty event lists for them, and no retail video
  of a Talk exists yet (only the stills L1-10, L1-11, L1-12, L7-01, F-11).
- The look-at of the persons (`useLookIK`), the lip sync and the audio of the line, the ambient gestures: not done.
- The persons have no collision (the retail rule is not measured).
