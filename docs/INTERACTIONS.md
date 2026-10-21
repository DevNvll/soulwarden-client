# The interactions of the player

Stage P2.10 (Linear AST-165), part 1. The player uses an object of the server with the key E: the request goes into the input
report, the server replies, and the client or the server ends the use. The sources are the Soulwarden protocol notes 4.20,
4.35, 4.36, and 4.42, and the interaction data of the backend copy (`interaction-data.json`: the same data as the server).
The wire bytes are in [NETWORK.md](NETWORK.md), "The life, the travel, and the interactions".

## The parts

| Part | Job |
| --- | --- |
| `converter/swc_convert/actions.py interactions` | Writes `SWC_CONTENT/player/interactions.json`: the templates of the fixtures of the backend (the powder keg, the camps) and of the personal loot bag, with their Interact bounds and options. |
| `Interact/SwcInteraction` | Plain C++ (tested by `tools/test-net.sh`): the ids of an object, the focus, the range rule of the server, and the use of one option (`swc::interact::Use`). |
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

AST-198 (research `~/Games/soulwarden-client/research/prompt-range.md`, a static study of the retail client): the area of the
prompt is not the validation range. Each Interact bound has a candidate shape of its own in its slice, and the range (`rangeSq`)
is only the check of the use ("The range", below).

- The candidates: the bounds whose shape has the point of the character (the position field of the next report, as the
  range check). `swc::interact::contains`: a sphere is strict (retail `IsPointInside` 0xCC2BE0: distance² < radius²), a box is
  inclusive (0xCC21F0: half of `Dimensions` on each axis), a capsule or a cylinder stands on its local z. The shape turns with
  its object (a server child, the body of a creature) and with the basis of the bound in its slice (rotation, scale).
- The focus: of the candidates of the three sources (the fixtures of the server, the loot bags, the objects of the level data
  with the bodies), the bound with the smallest score of the camera line (`swc::interact::line_score`, retail 0x490FED0): the
  squared distance from the bound to the forward line of the camera; no score for a bound at or behind the camera plane; an
  equal score keeps the first one. Thus a farther bound on the camera line wins over a nearer one beside it.
- The option of the focus is the first option of the bound. The place of a bound is the place of the bound in the frame of the
  slice, turned by the rotation of its object and moved to its place (PositionInTheWorld, fields 0 and 1; the bound of a camp is
  0.84 m from the origin of its slice). The death and a loading screen give no focus.

| Bound (the data of the converter, `converter/swc_convert/bounds.py`) | Shape | Range (the use) |
| --- | --- | --- |
| A tree (`master_tree`, the entity Healthy; AspenTree_c, poplars, beeches) | sphere 1.8 m (the variant column InteractionRadius; IronwoodTree_a 3.5 m) | 10 m |
| An iron vein (`master_minerals`) | sphere of the variant: Vein_Iron_a 5 m, _b 4 m, _c 3 m (the slice has 2 m) | 10 m |
| A plant (`master_plants`) | sphere 1.8 m (Hemp_a 1.5 m) | 10 m |
| A chest (`master_lootcontainer`) | sphere 3 m, line of sight | 10 m |
| The body of a Grey Wolf (`wolf_grey`, GatherBound) | box 2.5 x 2.5 x 2.5 m, 1 m above the feet, turned with the wolf | 10 m |
| The personal loot bag (`instanced_loot_container`, Interaction) | box 2 x 2 x 2 m (axis-aligned: the view has no yaw of the bag) | 5 m |
| The powder keg | box 2 x 2 x 3 m | 5 m |

Check (development runs, 2026-10-20; `SWC_WORK/evidence/AST-198/ast198-prompt-check.mp4`, the parts of the runs
`ast198-chest-iron`, `ast198-tree`, `ast198-body`; drives `tests/drive/ast198-*.txt`): a slow walk (2 m/s, 6.7 cm each frame) into
each shape and out. The log line `interact: focus` gives the distance at each change (the first frame inside):

| Object | Shape | The prompt shows at | Before AST-198 |
| --- | --- | --- | --- |
| Chest LootLargeT1 | sphere 3 m | 2.99 m (two times) | 10 m |
| Small iron vein Vein_Iron_c | sphere 3 m (variant) | 3.00 m | 10 m |
| Beech tree | sphere 1.8 m | 1.73 m in 3D (the feet 0.8 m above the root), 1.53 m horizontal | 10 m |
| Dead Grey Wolf (knife in slot 35) | box 2.5 m, 1 m above the feet | 1.19 m to 1.24 m horizontal (5 times) | 10 m |
| Loot bag (no knife: no body prompt) | box 2 m | 0.94 m to 0.99 m (4 times) | 5 m |

A living wolf that bites the character for 6 s has no prompt; the prompt of its body shows 0.5 s after its death. With the knife,
the body and the bag are both candidates, and the body has the smaller camera line score (its centre is 1 m higher): the bag shows
only with no body prompt. Retail has a Take All priority switch for this case (its default is not known).

Not copied (the open points of the study): the player proxy of the trigger path of retail (the bodies, the bag, the stations:
the client uses the point test of the marker path for both); the marker activation of the trees, the ore, and the chests (a
larger Marker sphere); the Take All priority switch; the forced focus; the arc (off on each sampled bound). A placement object
of the layers keeps the basis of its bound in its slice, not the rotation of its placement (the data has no rotation of a
placement; a sphere and a standing capsule do not need it). The study names the type b0c6aa97 a capsule (the forge
InteractBound1), the navigation reader of the vendor folder names it a cylinder: `bounds.py` keeps the type id.

## The range

The rule of the server (backend `world/interactions.cpp`, `Interactions::check`; Soulwarden `docs/systems/interactions.md`).
The client uses the same rule for the hold and the request (`swc::interact::in_range`); the prompt uses the candidate shape ("The
focus", above).

| Part | Rule | Source |
| --- | --- | --- |
| The position of the character | the position field of the input report that has the request: the server takes the position of a report before its request. A character with no position field (a camera estimate) is refused | `server/rep/rep_session.cpp`: `report_position`, then `observe_interaction` |
| The height of the character | the 16 bits of the field (-100 m to 1000 m, steps of 1100 / 65535 m = 1.68 cm): the client cuts the height off, and the server reads it back (`swc::net::position_field`) | `server/rep/client_input.cpp` |
| The place of the bound | a level object: the bound place of the data (`interaction-data.json`; the object layer has the same values); a natural node: the node place plus the translation of the bound of its slice, no rotation; a server child: its place plus its rotation times the bound place in the slice (`transform_point`) | `world/interactions.cpp`, `world/gathering.cpp`, `gamedata::transform_point` |
| The check | the squared 3D distance in float (dx² + dy² + dz², in this order) is not more than `m_interactRangeSq`: equal passes, no margin | `world/interactions.cpp` `distance_sq` |
| The time | at the request, not at the press. In the hold of the prompt the client measures in each frame from the position field of the next report (`RepClient::next_reported`, then `position_field`); a hold out of the range ends with no request and no cancel (`Use::moved`). After the request the server ends a committed use when the character goes out of its range (3746) | `world/world.cpp` `place` (`Interactions::moved`) |

The cause of the refusal in the P5.1 run of the creature track (E at a saltpeter node "at 9.4 m"; the server: "the character is
not in the range of the object"): the press came 0.1 s after the loading screen of a teleport, in a fall of 1.1 m, at 9.28 m. The
landing 0.36 s later was 10.08 m from the bound, and the hold of 0.5 s ended after it: the request had the position of the landing.
The client measured only at the press.

Limits:

- The place of a server object comes with its height in steps of 2100 / 65535 m (PositionInTheWorld field 0): the bound place
  in the client can be 1.6 cm from the place of the server. The rotation has steps of 1 / 32767.5; the test measured no
  difference in the float places of the world.
- The object layer (`USwcObjectSubsystem::FindNearestObject`, a file of the creature track) finds its nearest bound with double
  values from the feet. The interactor takes it only when the float rule passes, and it measures the distance with that rule.
- The Gather fragment of a gather press stops the move input: in a hold only a fall or the server can move the character.

Tests (`tools/test-net.sh`): the plain tests (the hold out of the range, the turned bound of a camp, the next position field)
and `tests/net/interaction_range_tests.cpp` against the world of the backend (its core library; the input reports of the made-up
client of the backend tests, with the position and the request): the edge on one axis, in 3D, and in the steps of the height
(equal passes; one float step or one height step more is refused), a report with the request on the other side of the edge than
the report before it, the case of the P5.1 run, and the bounds of a turned object through the PositionInTheWorld state of the
backend encoder (23 checks, 0 failed).

Check (development run, 2026-10-18; `Devwarden` with the pick `1hPickT1` in slot 36; `SWC_STAGE=interact-range`; evidence
`~/Games/soulwarden-client/evidence/interact-range/`):

| Case | The client | The backend |
| --- | --- | --- |
| `tests/drive/interact-fall.txt` (`SWC_DEV_PLACE=8485.76:5298.65:186.3`, the place of the P5.1 run): E in the fall of the spawn | the focus at 9.91 m; the hold ends at 10.02 m with no request; E on the ground: no prompt | no request |
| `tests/drive/interact-range.txt` (`SWC_DEV_PLACE=8500.054:5305.6:186.5`): E at rest 9.26 m from a node | the request, the reply 1, the gather (Done) | accepted |

A first run from the place of the P5.1 run also had E at a second node at 9.93 m of 10 m: the reply 1 and the gather (its logs
were not kept).

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
shows the prompt and the progress in the debug text (`E Trigger (powderKeg), 0 %`). From stage WF7 the UI reads the prompt with
`FSwcInteractor::GetPrompt` (`USwcBackendSubsystem::GetInteractPrompt`): the kind of the focus (a bag, an object, a fixture), the
name of the row, the options of the bound with their hold times, the phase and the progress, the tool refusal (the gathering type
and the text key `ui_notool` + type), and the place; it shows the original prompt card.

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
([NETWORK.md](NETWORK.md)). The UI opens a bag through the interactor (`USwcBackendSubsystem::OpenLootBag(Interest)`,
`FSwcInteractor::OpenBag`): at its next update, with no use that runs, the bag in its range with no Looted claim gets the
request of its second option, OpenStorageOption (action 26), with no hold (the open of the panel). The use stays until the
panel closes (`CloseLootBag`: 2473), the Looted claim, or the end of the bag; `GetOpenLootBag` gives its bag ([UI.md](UI.md)).
The key T of the prompt is not bound yet (Tab near a bag opens it: the inventory script).

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

## The options of a card and the key T (stage MVP-F)

- The option rows of the original card (lyshineui/markers/unifiedinteract) have two key hints: Icon_E (the action ui_interact: E)
  and Icon_N (ui_interact_sec: T). The first option of the card has E, the second has T. The interactor gives the press of T to
  the second option of an object (`FSwcInteractor::SetSecondKey`; the input action IA_InteractSecond of the key table, the drive
  command `interact2`): T then works as E for the hold, the request, and the release. A bag keeps its rule: T opens its panel
  (swc/interact.lua).
- The rows of the card (AST-231; the native rules of the study `~/Games/soulwarden-client/research/prompt-options.md`): the card
  has each option of the bound with a true authored show flag (`m_showOnUnifiedInteractUI` of its binding in the slice; the UI
  population of the retail program skips a false flag). On an entity with the NPC component (type `4bdb17d5-3b1e-474d-a783-29f47a7acd53`,
  by the component set of the entity, not by the kind "npc") the card has only the action 63 (OpenConversation): the predicate of
  the NPC client facet. The inn keeper thus shows "E Talk" (OpenInnScreen and SetSpawnPointOption stay in the data; the inn
  service opens after Talk), and a shrine shows "Fast Travel" (OpenMap, show true, no NPC component). The client keeps the full
  option list of the bound for the use and the service.
- The converter (`converter/swc_convert/bounds.py` card_rules, `objects.py`) writes the rules in the bounds of
  `world-objects/regions/REGION.json`: `"hidden"` (the positions of the options with a false flag) and `"npc": true`. Counts of
  the data: 145 NPC bounds, 128 hidden options (SetSpawnPointOption at 19 inns, the entries of the dungeons and the trials, the
  claim purchase, the summoning stones, SetSecurityLevelOption). An option with no binding of its name in the slice of the bound
  (96 bounds: some doors, teleporters, and storages, where the slice has the same component id in two places) keeps its row.
- The header of a person card (AST-231; unifiedinteractcard.luac OnInteractFocus): the name of the person (interactName) and its
  title (NpcComponentRequestBus GetNpcData of the root entity: swc/interact.lua). The interactor takes both from the person layer
  (`USwcPersonSubsystem::FindByInteraction`) for a bound with the NPC component; the prompt line has the title as its field 16.
- The label of OpenMap is "Map" (`@ui_openmap` of the option record). The text "Fast Travel" of the brief came from the shrine
  labels of the retail respawn map, not from a card. No retail picture of a shrine card is in the evidence.
- Check (`tests/drive/ast231-cards-routes.txt`, evidence AST-231/ast231-cards5.mp4 and ast231-crops.png): the inn keeper of
  Windsward shows "VERILDIS JANNSEN", "Innkeeper of the Southern Breeze", and "E Talk" (retail F6: "OSEYE KAMAU", its title,
  "E Talk"); the shrine 3f1619c9 shows "E Map".
- Check (`tests/drive/mvpf-options.txt`, evidence MVP-F/mvpf-options.mp4): the inn keeper of Windsward shows "E Talk"; E opens the
  conversation. The lore page next to it shows "E Read" and "T Add To Journal"; E gives ReadLore and T gives AddLoreToJournal
  (both accepted). No gathering node has two options in the data.

## Not done, open points

- The loot panel: stage P2.4 of the UI track (the inventory screen `lyshineui/inventory/newinventory`).
- A bag with no claim of the character in the LootTracker shows (the retail client shows nothing); the server sends the record
  only to its eligible characters, with the claim.
- InteractionAlignment: the turn to the object is in the client (stage P5.2, AlignFacingBlendTime 0.2 s, also for
  DelayedInteraction); the move to the object (AlignPositionSpeed) is not.
- The keg has no body in the client (the slice has a mesh; the view of the server objects is not in this stage).
- The side-by-side check with the retail client is open.
