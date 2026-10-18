# The interactions of the player

Stage P2.10 (Linear AST-165), part 1. The player uses an object of the server with the key E: the request goes into the input
report, the server replies, and the client or the server ends the use. The sources are the Soulwarden protocol notes 4.20,
4.35, 4.36, and 4.42, and the interaction data of the backend copy (`interaction-data.json`: the same data as the server).
The wire bytes are in [NETWORK.md](NETWORK.md), "The life, the travel, and the interactions".

## The parts

| Part | Job |
| --- | --- |
| `converter/swc_convert/actions.py interactions` | Writes `SWC_CONTENT/player/interactions.json`: the templates of the fixtures of the backend (the powder keg, the camps) and of the personal loot bag, with their Interact bounds and options. |
| `Interact/SwcInteraction` | Plain C++ (tested by `tools/test-net.sh`): the ids of an object, the focus, and the use of one option (`swc::interact::Use`). |
| `Interact/SwcInteractor` | Reads `interactions.json` (on the worker thread of the player tables), finds the focus each frame, drives the use with the key E, and gives the request and the cancel to the connection. |
| `Net/Codec/SwcRepClient` | The interaction block of the input entries, the reply 3743, the end 3746 (the client answers with 2473 at once), and the cancel 2473. |
| `Character/SwcPlayerActions` | The kneel of a timed use (`Delayed_Interact`, [ACTIONS.md](ACTIONS.md)). |

```sh
tools/convert-player.sh interactions   # interactions.json from the backend copy
```

## The ids of an object of the server

The record of an object has its actor id (the allocator of the server), the slice asset with its sub id, and its place
(PositionInTheWorld, field 0). G is the first 8 bytes of the actor id (little-endian). With the root entity of the slice of
the template (protocol notes 4.2, 4.36):

- K = root XOR G;
- the instance id of an entity of the slice: the entity XOR K;
- the facet target of the Interact bound: the instance id of its entity + its component id (a 64-bit sum);
- the option entity of the block: the option entity of the data XOR K.

The test has the worked example of the backend rule for a keg with G `4C554F5340000100`: the target `69F8F60912B6BC1F` and
the option entity `4C54561C65E43ABF`. In the live check the backend accepted each request of the client with these ids.

## The focus

The focus is the nearest Interact bound of an object with a template whose 3D squared distance to the feet is not more than
its `rangeSq` (25 m² for the keg and the loot bag; equal passes; the check of the backend). The option of the focus is the
first option of the bound. The place of a bound is the place of its object plus the place of the bound in the frame of the
slice (both templates have their bound at the origin: the rotation of the object is not used). The death and a loading screen
give no focus.

## The use of an option

| Step | Rule | Source |
| --- | --- | --- |
| The press | the key E at a focus. An option with a hold time (`hold`, the loot bag 0.5 s) needs the key down for that time; a release before it sends nothing | interaction-data.json |
| The request | the block (mask bit 8) in each input entry while the request is current: the reference, the action and the base action (a committed option: its own action), the option entity, the committed flag, the hold time (the Delay Time, as the live client sends it), the option id | protocol notes 4.35, 4.36 |
| The timed use | an option with a Delay Time (the keg: Trigger, action 33, 5 s): the progress while the key is down; the kneel; no movement | protocol notes 4.36 (live L6) |
| The execution | at the end of the Delay Time (at the request for an option with no Delay Time) | protocol notes 4.36: 4.99 s to 5.00 s after the press |
| The end with the reply 1 | the request ends with no message 0.03 s after the execution, and not before the Delay Time after the arrival of the reply | the retail client: the block goes away 5.03 s after the press; the timer of the backend starts at its accept (a block that goes away before its due time is a release: no completion) |
| A release | before the execution: the cancel 2473; after the execution with no reply: 2473 (the client keeps its request until the release) | protocol notes 4.36 (live L6) |
| The reply 2 | the request ends (the records clear); no 2473 | protocol notes 4.35 |
| 3746 | the server ends the use (a command, damage for a timed option, the range, the removal of the source, the death, a travel, a respawn): the client clears the block and sends 2473 at once | protocol notes 4.20, 4.36 (live L7) |
| The death | the server sends 3746; the client ends the kneel at the death (the action Death: DisableInteractor) | playeractions_death.actionlist |

The key E is a key of the game map `player` (the action `give_up` of `actionmaps.json`; the HUD has its own `ui_interact`).
While a screen of the UI turns that map off (the escape menu), the input pre-processor of the UI keeps the key from the game
(stage P2.2), and the controller asks the same rule for an injected key of a drive script (`UiKeyToGame("e")`). Check:
`tests/drive/menu-hold.txt` with `-SwcUiDrive=tests/drive/menu-hold-ui.txt` (E in the menu: no use; after it: the use).

The prompt of the UI (the text of the option, the ring of the progress) comes in part 2 with the UI track; a development run
shows the prompt and the progress in the debug text (`E Trigger (powderKeg), 0 %`).

## Check (development run, 2026-10-18)

`tests/drive/keg.txt` with the routes `tests/drive/keg-routes.txt` (`tools/creature-check.sh keg 44 ...`, `Swordwarden` on the
test floor; the route `fixture powderKeg 0 3 0`: a keg of the backend 3 m to the north, the permission rule 1). Evidence:
`~/Games/soulwarden-client/evidence/P2.10/keg.mp4`, `keg-client.log`, `keg-backend.log`.

| Case | The client | The backend |
| --- | --- | --- |
| E for 2 s | the request, the reply 1, the kneel; at the release 2473 | "ended: the client cancelled it (2473)" |
| E for 5.5 s | the execution at 5.0 s, the request ends with no message | "completed after 5.0 s (completion 1 of the object)", then "ended: the client ended its request" |
| The route `cancel` 1 s into the use | 3746, the client sends 2473 at once, the kneel ends | "ended: a command", the late 2473: "no change" |
| The route `hurt 100000` 1 s into the use | 3746 and 2473, the death | "ended: the death of the character" |

Before the rule of the reply time, the request of the full use ended 4.997 s after the accept of the server: the backend
counted a release, not a completion.

The backend accepted each position report of the session (1,029, 0 refused). The tests: `tools/test-net.sh` (the ids, the
focus, the release, the full use with and with no reply, 3746, the reply 2, the death, the hold of the loot bag).

## The loot bag (the take by the prompt)

The creature view has the bag entities of the kills of the character (`USwcCreatureSubsystem::FindNearestLootBag`: the nearest
bag with an enabled Interact bound in its range; [CREATURES.md](CREATURES.md), "The loot bags"). The interactor takes the nearest
usable bag as the focus when it is nearer than the focus of the other objects:

| Step | Rule | Source |
| --- | --- | --- |
| The key E | the first option of the bound: TakeAllOption (action 28, committed, no Delay Time, the hold of the prompt 0.5 s); the reference, the target, and the option entity of the bag query | interaction-data.json; protocol notes 4.42 (live L5) |
| The take | the server moves the items of the part of the character into its bag (the reply 1; the bag of slot 61 changes) and gives the claim of the bag the state Looted (the LootTracker of slot 33); the client sends no 2939 for E | protocol notes 4.42 |
| The end of the use | the client keeps its request after the execution and the reply until the Looted claim (or the stop of the record of the bag), then it sends 2473 | protocol notes 4.42 (live L6: 2473 7 ms after the Looted claim) |
| A Looted bag | no prompt at once; its model hides 1.5 s after the claim (HideMeshDelay of the slice, `USwcCreatureSubsystem::SetLootBagHidden`) unless the server removes it before (its end 1.25 s after the last item) | interaction-data.json `lootContainers.personal` |

The options of the bag query are in the order of the bound of the data: the key of a pair is the index of the option in the
options of `interaction-data.json` (80, 30), not its CRC-32 id; the interactor takes the first option by its place.

The panel of the open bag (OpenStorageOption with T, the take of one item and the Take All button: messages 2939 and 2942) is
a part of the inventory screen of the UI track (stage P2.4). The client has the messages and the states in its read path
([NETWORK.md](NETWORK.md)).

Check (development run on the world level, 2026-10-18; `tests/drive/loot.txt`, the routes `tests/drive/loot-routes.txt`,
`tools/creature-check.sh loot ...` with `SWC_DEV_PLACE=8872:4250:138.25`; evidence `loot.mp4`, `loot-client.log`,
`loot-backend.log`): the kill of a grey wolf, its bag with the claim Rolled; E at 4.9 m: after the hold of 0.5 s the request, the
reply 1, the bag of the character from 6 to 7 items, the claim Looted, the end of the use (2473); the backend: "took 1 item(s) of
the bag", "ended: the client cancelled it (2473)", and the end of the bag 1.25 s later ("each part is empty").

## The objects of the level data and the gather use (stage P4.1 part 2)

The creature track added this part (docs/OBJECTS.md); the player track owns the files.

- The focus: after the bag, `USwcObjectSubsystem::FindNearestObject` (the object layer) gives the nearest usable bound of a lore
  page, a chest, a node, a station, a person. It is the focus when it is nearer than the focus of the fixtures and the bag. Its
  first option and its ready request come from the layer (ReadLore of a lore page, GatherOption of a node or a chest).
- The gather or open use (GatherOption, action 14): a timed use of the server. The client keeps its request (the press keeps it as
  for a bag: the reply 1 and the release of the key do not end it). The Gathering state of the owner (type 81, slot 36; the client
  rule: fields 0 to 3 only with a new field 4, fields 6 and 7 only with a new field 8) gives Success with remaining / speed seconds
  of work, then Done at the completion, or Failure. Done and Failure end the use (2473: live check L3 of the backend stage
  3.5b.1). Limits: no Success 5 s after the press, or no end 2 s after the time of the work, end it too
  (`swc::interact::GatherWatch`). A release before the time ends the work with no item (chests.md, ST-R1).
- `tools/test-creatures.sh` (the object program) tests the reader of the Gathering state with the backend encoder
  (`chest_states.cpp`) and the end rule.
- The action of a gather (stage P5.2, [OBJECTS.md](OBJECTS.md)): `GetUseKind()` is the use fragment of the gather
  (`gather|ACTION|TOOLTAGS`), `GetUseTool()` the paperdoll slot of the tool, and `GetUsePlace()` the place of the object.
  `USwcPlayerActions::SetTimedUse` plays the fragment (again while the work lasts), puts the tool into the hand
  (`ASwcPlayerCharacter::SetToolInHand`), and turns the character to the object in 0.2 s (InteractionAlignment,
  AlignFacingBlendTime). With no tool for a row (ToolRequired) the prompt has the refusal text and E sends nothing.

## Not done, open points

- The loot panel: stage P2.4 of the UI track (the inventory screen `lyshineui/inventory/newinventory`).
- A bag with no claim of the character in the LootTracker shows (the retail client shows nothing); the server sends the record
  only to its eligible characters, with the claim.
- InteractionAlignment: the turn to the object is in the client (stage P5.2, AlignFacingBlendTime 0.2 s, also for
  DelayedInteraction); the move to the object (AlignPositionSpeed) is not.
- The keg has no body in the client (the slice has a mesh; the view of the server objects is not in this stage).
- The side-by-side check with the retail client is open.
