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

## Not done, open points

- The loot bag (part 2): the creature track shows the bag entity of a kill and gives a query of the nearest usable bag; the
  player track does the interaction (TakeAllOption, OpenStorageOption), the take messages 2939 and 2942, and the loot panel
  with the UI track.
- The objects of the level data (the lore pages, the chests, the persons, the stations): the client makes no object from the
  level data yet.
- InteractionAlignment of the action DelayedInteraction (the character turns and moves to the object) is not in the client.
- The keg has no body in the client (the slice has a mesh; the view of the server objects is not in this stage).
- The side-by-side check with the retail client is open.
