# The objects of the level data

Stage P4.1 part 2 (Linear AST-174) and stage P5.2 (AST-175, the gathering of each tool family). The client makes the interactable objects of the level data itself: the lore pages, the
chests, the gathering nodes (the natural nodes of the distribution files and the capital nodes of the layers), the stations, the
persons, the doors, the shrines, the furniture. The server sends a record of such an object only when its state is not the state
of the data (a depleted node, a disabled bound). The object layer gives each object its ids by the rule of the backend, keeps its
server state, shows the state visuals near the character, and gives the interactor of the player a query. The meshes stay in the
world level of the world track (docs/WORLD.md). The sources are the Soulwarden documents `docs/systems/interactions.md`,
`gathering.md`, and `chests.md` (read only), the backend encoders (`apps/backend/src/server/rep/states`), and the game data of
the backend copy.

## The parts

| Part | Job |
| --- | --- |
| `converter/swc_convert/objects.py`, `tools/convert-objects.sh` | The data of the layer in `SWC_CONTENT/world-objects` from the backend copy and the variation tables of the game (below). |
| `Objects/SwcObjectRules` | Plain C++: the id rules (the actor of a placement UUID, the offline id G of a natural node, the facet target and the option entity of a bound), and the reader of the node file. |
| `Objects/SwcObjectSubsystem` | A world subsystem: the regions near the character, the states, the visuals, the query `FindNearestObject` and `GetObjectsNear`. |
| `Objects/SwcLevelInstances` | A world subsystem: the instances of the level at a place (`FindInstancesAt`) and their hide (`SetInstanceHidden`), with read-only use of the cell actors of the level build. The world track reviewed it. |
| `Creatures/SwcEntityTable` | The records of the objects: the GatherableController state (type 12) and the Interact state (type 2930), by the actor of the metadata state. |
| `Interact/SwcGatherUse`, `Interact/SwcInteractor` | The focus from the layer, and the gather or open use that keeps its request until the Gathering state ends the work (docs/INTERACTIONS.md). |
| `RepClient::OwnEntity` | The Achievement state (3210, slot 66: the read lore pages) and the Gathering state (81, slot 36) of the owner. |

## The data

```text
backend copy gamedata/interaction-data.json   the objects of the layers, their Interact bounds, the options, the achievements
backend copy gamedata/chest-data.json         the chests (a part of the gathering objects)
backend copy gamedata/gathering-data.json     the gathering templates, the capital nodes, the natural node regions
backend copy gamedata/gathering/REGION.swgn   the records of the distribution files (the natural nodes)
game copy, the variation tables               the StartMesh and the DepletedMesh of each variant of a node
   --tools/convert-objects.sh write---> content/world-objects/objects.json        the tables
                                        content/world-objects/regions/REGION.json the objects of a region with their ids
                                        content/world-objects/nodes/REGION.bin    the natural nodes in cells of 64 m
   --tools/convert-objects.sh meshes--> the DepletedMesh of each variant (the shared manifest), and the key list
                                        content/world-objects/depleted-meshes.json
   --tools/import.sh --keys content/world-objects/depleted-meshes.json--> /SwcGameContent/Meshes
   --tools/convert-objects.sh write---> again: the object paths of the imported depleted meshes
```

The folder is `world-objects`, not `objects`: the converted meshes of the game paths `objects/...` are in `content/objects`.

The retail data: 42,759 objects with an option (33,071 capital nodes, 5,519 chests, 1,157 lore pages, 2,178 other gathering
objects, 834 other objects), 2,450,322 natural nodes in 49 regions, 509 node visuals. The converter checks the id rules of the
client against the ids of the backend data: each actor from its UUID, and the facet target and the option entity of each capital
node from its template. A mismatch stops the tool.

## The ids

| Object | Rule (source) |
| --- | --- |
| A placement of a layer (a lore page, a chest, a capital node, a station) | Actor: XOR the two halves of the UUID, reverse the 8 bytes, set the low 3 bits of the first byte to 3, then 8 zero bytes. G is the first 8 bytes (little-endian). (Research P4.1, backend test case IT10.) |
| A natural node | G = ((counter & 0x7FFFF) << 13) \| ((regionY & 0x1F) << 8) \| ((regionX & 0x1F) << 3) \| 1, counter = record + 1. Actor: G (little-endian) and 8 zero bytes. (gathering.md, client 0x69D5090, backend test case GAT2.) |
| A bound | Facet target = (entity XOR root XOR G) + component; option entity = option XOR root XOR G (protocol notes 4.2). A data object has them in the data; a natural node gets them from its template. The place of a bound of a natural node is the place of the record plus the translation of the bound (no rotation). |

`tools/test-creatures.sh` (the object program) tests the rules with the values of the backend data: a lore page, a chest, a
capital node, IT10, and the natural node of GAT2 (record 18627 of r_+04_+02: G 0x09188221, target b0b2beac0f9664ab).

## The states

| State | Source | Effect |
| --- | --- | --- |
| Node state | The record of the node: GatherableController (type 12) field 0, Healthy 4, Depleted 8 (backend `gatherable_state.cpp`). The server makes the entity at the first depletion and keeps the record at the respawn (Healthy). | Depleted: no prompt, the node visual changes (below). A record that stops is the data state again. |
| Enabled | The record of the object: Interact (2930) field 0, by the slot of the bound. | False: no prompt for that bound. |
| Chest claim | The LootTracker of the character (982, slot 33): Looted 2, NotOnRequiredQuest 4, Locked 5 (chests.md). | No prompt; the light of the loot goes off. |
| Read lore page | The Achievement state of the character (3210, slot 66): the bit of the lore row (byte `bit >> 3`, mask `1 << (bit & 7)`). | `bRead` of the query. The page stays usable (a page can be read again). |

The layer reads the entity table of the creature view (`USwcCreatureSubsystem::GetEntityTable`) when it changes, and the own
entity each 0.2 s.

## The visuals

Only within 100 m of the character (the interest radius of the server: a record of an object starts at 100 m and stops at 120 m).

- A depleted node (natural or capital): its healthy level instance at its place hides (`SetInstanceHidden`: the scale 0; the level
  transform comes back at the show). The depleted mesh shows: the depleted level instance at its place if the level has one, else
  an own mesh component with the transform of the healthy instance. The respawn (Healthy) or a far character gives the level
  state back. The client code hides the node at Depleted and shows it at Healthy (gatherable_state.cpp; a tree falls in the
  retail client, a stump stays).
- A healthy node: a depleted level instance at its place hides (the level before the final pass of the world track places both
  meshes of a node; after that pass it places only the StartMesh). Far nodes outside the radius still show both meshes until that
  pass (accepted by the orchestrator).
- A node instance of the level: an instance within 0.3 m (horizontal) and 1.5 m (height) of the place of the node whose mesh is in
  the folder of the StartMesh or of the DepletedMesh of a visual of the node. The visuals of a node: the visual of the variant of
  its record (a natural node) or of its variation table (a capital node), and the visuals of the meshes of its template slice. The
  level places the default meshes of the master slice at a distribution record, not the meshes of its variant (for example
  honeysuckle_a at a record of the variant Bush_Pawpaw_c_Fall01). The layer keeps an index of these instances near the character
  (one search of the level each 20 m of movement or at each new load of a level).
- The own depleted mesh: the DepletedMesh of the visual whose StartMesh the level placed at the node (the look of the level), else
  the DepletedMesh of the variant of the record.
- A chest that the character can loot: a point light 0.4 m above it (a stand-in for the Idle_VFX of the variation row; the
  particles are not converted). The Looted claim removes it. A supply chest does not change its mesh when it is looted (chests.md,
  live check L9): the layer does not hide the loot props of the level.
- A lore page: no visual (no proven change).

## The query

`USwcObjectSubsystem::FindNearestObject(From, FSwcWorldObjectTarget&)`, as `FindNearestLootBag` of the creature view: the nearest
bound with an option in its range (the 3D squared distance, equal passes: the backend check), Enabled, and Ready (not a depleted
node, not a Looted or Locked chest). A gathering object that is not a chest and not a node (mostly `master_quest_instanced`, which
the backend refuses) has no prompt. The result has the key, the kind, the actor, G, the slot, the facet target, the place, the
range, the state, the lore bit and its read mark, and each option with its ready request (`swc::net::InteractionRequest`).
`GetObjectsNear` gives the bounds within a radius in each state.

The cost: the grid cells of 64 m within the largest range of the data (100 m: rangeSq 10000 of 24 bounds) around the point. The
layer loads the region files within 300 m of the character (about 150 ms for a full region) and removes them beyond 500 m.

## Check (development run, 2026-10-18)

`tests/creatures/objects.txt` (the drive) and `tests/creatures/objects-routes.txt` (the routes), with
`SWC_DEV_CHARACTER=Devwarden SWC_STAGE=4.1 tools/creature-check.sh objects 64 tests/creatures/objects-routes.txt tests/creatures/objects.txt -- -SwcObjectProbe`;
evidence `~/Games/soulwarden-client/evidence/4.1/objects.mp4`, `objects-client.log`, `objects-backend.log`, the frames `objects-*.png`.

- The lore page bdf81e34: the prompt "E ReadLore" at 1.9 m (actor c3099bdc2acb20b5, target 8cf752924a848ca2, option entity
  b520cb2edc9b09c3); the hold of 0.5 s, the request, the reply 1, the end with no message. The Achievement state gets the bit of the
  page (a first run: 1 bit to 2 bits); the query then has "read".
- The small bush r_+04_+02#18772 (BushSmall, no tool; G 00000000092aa221, target bdc538d73abe68bb): the hold, the request, the reply
  1, the Gathering state Success (0.50 s, speed 1), Done, the end of the use (2473). The backend: "gathered the natural node ...
  1 WoodT1". The record: node state Depleted; the layer hides the healthy level instance and shows the depleted level instance. The
  route `respawn 00000000092aa221 6`: Healthy 11 s later; the layer shows the healthy instance again and hides the depleted one.
- The supply chest 1ce79c8f (G d4885cd2a16137a3): the light of the loot; the hold, the request, Success (2.50 s), Done, 2473; the
  LootTracker claim Looted; the light goes off.
- `tools/test-creatures.sh`: objects 64 checks, 0 failures; `tools/test-net.sh`: 179 checks, 0 failed.

The route commands of the check (`tools/lib/dev_world.py`): `gathering [RADIUS]`, `chests [RADIUS]`, `respawn NODE SECONDS` (a
UUID, REGION#RECORD, or the G in 16 hexadecimal digits; in a script file "#" starts a comment: use the G), `chest-clear CHEST`.

## Not done, open points

- The variant meshes of the natural nodes: the level shows the default meshes of the master slice (for example the flint of
  master_minerals at an ore record). The node file gives the variant of each record, so the level build can place the StartMesh of
  the variant at a small cost. In the layer it costs about 700 own mesh instances within 100 m and a visible change at the edge of
  100 m: not done (a look, not a feel). The world track can take the variants from `content/world-objects/nodes` and `objects.json`
  (or the gathering data of the backend copy).
- The line of sight of a bound (`bLineOfSight`, for example each chest): done in stage P5.1 (below).
- The requirements of an option (a level, an item, an achievement): the client does not check them; the server refuses.
- The Idle_VFX of a chest and the depletion effect of an elite chest are not converted (the lights are stand-ins; stage P5.1). A lore
  page has no visual. A tree has no fall (the retail client: the trunk falls, the stump stays): the layer swaps the meshes at once.
- The objects of the kind `gatherable` (mostly `master_quest_instanced`) have no prompt; the quests are a later stage.
- Far nodes (beyond 100 m) show the level state; until the final level pass of the world track that is both meshes of a node.
- The side-by-side check with the retail client is open.

## The gathering (stage P5.2)

The sources: Soulwarden `docs/systems/gathering.md` and protocol notes 4.44 (read only), `playeractions_gather.actionlist`, the
GatherableData tables (GatheringType, GatheringAction, DisplayName), and `gathering-data.json` of the backend copy.

| Part | Rule (source) |
| --- | --- |
| The rows | `objects.json` member `gathering`: each gatherable row (its id, GatheringType: the tool family, GatheringAction: the tag of the Gather fragment, Restriction, Tradeskill and its rank, BaseGatherTime, DisplayName), the tools by item index (the position in `item-catalog.json` plus 1: the value of the paperdoll) with their weapon tags and gathering types, the bodies (the slice of each creature with skinning: its template and row), and the texts of the prompts (from the converted localization of the UI track). |
| The tool | The paperdoll slot of the GatheringType: Chopping 33 (the logging axe), Cutting 34 (the sickle), Dressing 35 (the skinning knife), Mining 36 (the pick). The item of the slot counts when its gathering types have the type (client 0x48FF240 FindFirstGatheringTool). A tool in the bag does not count. |
| The refusal | ToolRequired and no tool: the prompt is "NAME  No equipped Axe" (the text `ui_notool<type>`), and E sends no request (live check L6). ItemRequired with no item (BushSmall) and NoToolNecessary have no gate. A body (the skinning) has no prompt with no knife. The rank gate is a check of the server: its refusal is the reply 3743 result 2 (the use ends; no text of the client). |
| The work | GatherOption keeps its request until the Gathering state (type 81) has Done or Failure (`Interact/SwcGatherUse`, docs/INTERACTIONS.md). The time is remaining / speed of that state: the speed of the tool (T1: 2) with the rank and the effects of the server. |
| The action | The use fragment `gather|ACTION|TOOLTAGS` of `actions.json` (converter `gather_actions.py`: the variant of "Gather" for the tag of the action and the tags of the tool, for example `player_axe.adb` Axe+Chopping, `player_pick.adb` 2H_Melee+Pick+Mining, `player.adb` Cutting, Dressing; with no tool the default kneel). The weapons go to their sheaths, the tool goes into the hand, and the fragment plays again while the work lasts (the data has one swing; the repeat is an inference). The character turns to the object in 0.2 s (InteractionAlignment, AlignFacingBlendTime; also for the timed use of DelayedInteraction). A chest (GatheringAction LootContainer) plays the default kneel. |
| The skinning | A dead creature whose slice has a skinning row: its GatherableController (type 12, slot 3) is Healthy at the death, Depleted after the skinning. The ids: G of the creature entity and the XOR rule with the template of its slice; the place: the feet of the creature plus the translation of the bound. The prompt has the name of the creature ("E CHALKY RABBIT"). |
| The fall of a tree | At a depletion that the character sees (a Healthy to Depleted change of a node in view, GatheringType Chopping), a copy of each healthy instance turns 85 degrees around the base of the tree, away from the character, in 2 s, lies 2.5 s, and goes; the stump (the depleted mesh) stays. The retail client lets the cut trunk of the CutTrunks table fall (gathering.md); the copy is a stand-in (a look, not a value of the data). |
| The respawn | The server sends Healthy at the time of the run clock (gathering.md); the layer shows the healthy mesh again. |

### Check (development run, 2026-10-18)

`tests/creatures/gathering.txt` (the drive) and `tests/creatures/gathering-routes.txt` (the routes), with
`SWC_DEV_CHARACTER=Devwarden SWC_STAGE=P5.2 tools/creature-check.sh gathering 122 tests/creatures/gathering-routes.txt tests/creatures/gathering.txt`;
evidence `~/Games/soulwarden-client/evidence/P5.2/gathering.mp4`, `gathering-client.log`, `gathering-backend.log`, the frames
`gathering-*.png` and `sheet-*.png`. The nodes are in r_+04_+02 near 9260 4177 (rank 0).

- The refusal: the young tree 27406 with no axe: the prompt "YOUNG TREE  No equipped Axe", E sends no request.
- The tree with the axe (route `tool 1hHatchetT1 33`): the fragment Axe+Chopping with the axe in the hand, the character turns to
  the tree; the work 25 s / speed 2.8; Done, 2473; the node Depleted: the tree falls away from the character, the stump stays.
- The ore vein 27254 with the pick (2H_Melee+Pick+Mining): 15 s / 2.3; the vein turns into its fragments.
- The cranberry 27423 with the sickle (Cutting): 12 s / 2.2; the bush hides; the route `respawn 000000000d640221 5`: Healthy
  again 9 s later.
- The Chalky Rabbit (route `spawn`, `kill`): "E CHALKY RABBIT" with the knife (Dressing): 7 s / 2.1; the body Depleted.
- The powder keg (the timed use of DelayedInteraction): the character turns to it and kneels; the execution after 5 s.
- The backend: each gather "gathered ..." with its items; 3,636 position reports accepted, 0 refused.
- Tests: `tools/test-creatures.sh` (objects 77, entity 97 with the body record of the backend encoder), `tools/test-net.sh` 179,
  `tools/test-actions.sh` 8,734 checks: 0 failures.

### Not done, open points (P5.2)

- The move keys stay locked during a gather (the lock of a timed use); the retail input Stop_Gathering (Continue_Gather) ends it.
- The impact of a swing (the mark GatherMomentOfImpact: the wood chips, the sounds) and the position part of InteractionAlignment
  (AlignPositionSpeed) are not in the client.
- The rank gate has no prompt of the client; the server refusal shows only as the end of the use.
- The cut trunk of the CutTrunks table is not converted (the fall is a stand-in), and the depletion effects of the nodes.
- The gather of another player (the observer action: type 11, not derived by the backend).
- The side-by-side check with the retail client is open.

## The chests and the loot (stage P5.1)

The sources: Soulwarden `docs/systems/chests.md` (the chest families of stage 3.5b.1) and `loot-bags.md` (the Discarded bag of stage
3.5b.2), protocol notes 4.41 (read only), and the chest data of the backend copy (`chest-data.json`).

| Part | Rule (source) |
| --- | --- |
| The chest data | `objects.json`, the regions: each chest has `[kind, gatherable row, state, name, elite]` (the converter `objects.py`). The names go into the texts of the prompts ("E SUPPLY CACHE", "E ELITE SUPPLY CHEST"). Elite: the kind `elite` of the chest data or the mark `elite` of its gatherable row. |
| The families | An ordinary chest (supply, provision, alchemy) and an elite chest open the same way: GatherOption (action 14), the Gathering state (type 81) runs the time of the row, Done ends the use (P5.2). The loot goes straight into the bag: there is no container for a chest (chests.md). |
| The state | The claim of the chest in the LootTracker map of the owner (982, slot 33): Looted 2 removes the prompt; NotOnRequiredQuest 4 and Locked 5 give a locked chest; no claim, Initial, Rolled, and Unlocked: ready (`swc::objects::chest_state`). |
| The loot light | A point light 0.4 m above each ready chest within 60 m (a stand-in for the Idle_VFX of the variation row: the particles are not converted). It goes off at Looted. A supply chest does not change its mesh (chests.md, live check L9). |
| The depletion effect | At the change of the claim of an elite chest to Looted (`swc::objects::chest_change`): a light of 4000 cd 1 m above it that fades in 1.5 s (a stand-in for the DepletedFX of the variation row, `cFX_Loot.Chest_Big_Celebration*`). The first LootTracker map after the world entry has the claims of before the entry: no effect for it. |
| The reset | The server ends the claim at the reset time on the run clock (60 min for an ordinary chest, 82,800 s for an elite chest: chests.md): it sends the map with no entry for the chest (live check L8). The prompt and the light come back at once. |
| The loot notice | The items that the bag gained (a chest, a node, a bag) as lines "+N ITEM" at the right of the screen for 4 s, and in the log (a stand-in for the notice of the HUD of the UI track; the item id, not its name). The UI calls `SetUiLootNotice(true)` when it shows the original loot ticker (stage WF7): the lines go off, the log line stays. |
| The Discarded bag | A drop from the bag (1838 DropInventoryItems) or from the paperdoll (826 DropPaperdollItem; the writers of the UI track) makes a Discarded bag (LootTracker kind 5) at the feet: only the owner sees it. It is the personal container of a kill bag (the same slice, P4.x): the creature layer shows it, and E (TakeAllOption, action 28) takes all its items. The bag panel of the UI track (P2.4) is not on main yet. |
| The line of sight | A bound with the mark `lineOfSight` (each chest) has a prompt only when a line from the eye (the feet + 1.6 m) to the aim (the place + 0.5 m) is free (Visibility traces on the level collision: the Landscape and the mesh collision of the cells). A hit within 0.75 m of the aim is the chest. A hit on a mesh of the object itself (at most 4 m, its origin within 1.2 m of the place, or its box holds the place; `swc::objects::own_mesh`) does not block: the line goes on behind its box (`box_exit`). The server does not check it (chests.md, owns_sight). The values are an inference: the LOSRayCastCheck entity of the slice is not read. |

The plain rules are in `Objects/SwcObjectRules` (`chest_state`, `chest_change`, `own_mesh`, `box_exit`), tested by
`tools/test-creatures.sh` with the LootTracker state of the backend encoder (`chest_states.cpp`: a Looted chest, a Locked chest, and
a Discarded bag in one map, and the empty map of a reset). The drop bodies (1838, 826) have their tests with the backend readers in
`tools/test-net.sh` (UI track).

### Check (development run, 2026-10-18)

`tests/creatures/chests.txt` (the drive) and `tests/creatures/chests-routes.txt` (the routes), with
`SWC_DEV_CHARACTER=Devwarden SWC_STAGE=P5.1 tools/creature-check.sh chests 142 tests/creatures/chests-routes.txt tests/creatures/chests.txt -- -SwcObjectProbe=log -SwcDrop=40:WoodT1`;
evidence `~/Games/soulwarden-client/evidence/P5.1/chests.mp4`, `chests-client.log`, `chests-backend.log`, the frames `chests-*.png`
and `sheet-1.png` to `sheet-3.png`. The town has the mesh collision of the world track (r_+04_+02).

- The supply chest 1ce79c8f (the prompt "E SUPPLY CACHE"): the open (2.5 s), the light of the loot goes off, the notice
  "+24 SandpaperT5". The route `chest-reset ... 6`: the claim resets, the light comes back.
- The drop (`-SwcDrop=40:WoodT1`): 1838 with 109 WoodT1; the backend makes the Discarded bag at the feet. E at the bag (TakeAllOption,
  action 28): "+109 WoodT1", the bag ends.
- The provision chest 1a3e6e98 on a ledge: from below (10.0 m) the terrain blocks the line: "no line of sight: no prompt" (the camera
  looks at the cliff in this run; the log is the evidence). On the ledge (2.4 m): in sight, the open, "+4 MilkT1".
- The elite chest fea931b2 in an abandoned house of the town: from 7.1 m west, behind the wall of the house: "no line of sight: no
  prompt"; E does nothing. In the house (2.4 m): "E ELITE SUPPLY CHEST", the open (5 s), the depletion effect, the light of the loot
  goes off, the notice (four items). The focus then goes to the lore page next to the chest.
- The focus skips a chest with no line of sight and takes the next usable object in range: on the ledge, E took a saltpeter node at
  9.4 m (the server refused it: "not in the range of the object"; see the open points).
- The backend: 3078 position reports accepted, 0 refused.
- Tests: `tools/test-creatures.sh` (objects 110, entity 97, effects 94, combat 26): 0 failures.

### Not done, open points (P5.1)

- The particles of the chests (Idle_VFX, DepletedFX) and the sounds: the lights are stand-ins.
- The loot notice of the HUD (the UI track) and the bag panel of a loot bag (P2.4): the notice lines and the Take All of the prompt
  are stand-ins until then.
- The line of sight uses the level collision. The other 48 regions get their mesh collision from the world track region by region;
  until then the Landscape blocks there and a house does not.
- A locked chest (a key, a quest) shows no text of its own; the server refuses its open.
- The range at its edge: the client measures 9.4 m to a node with the range 10 m, the server refused the request ("not in the range of
  the object"). The server measures from its own copy of the place (a travel, the height); the client has no margin. A point for the
  interaction rules (P4.1, docs/INTERACTIONS.md).
- The side-by-side check with the retail client is open.

## Development aids

| Switch | Effect |
| --- | --- |
| `-SwcObjectProbe` | Each 1 s: the nearest object in the log (its ids, its first option), and the bounds within 15 m (green: usable, red: not). |
| `-SwcObjectProbe=log` | The same log with no spheres, and a line at each change of the line of sight of a bound in range ("in sight", "no line of sight: no prompt"). |
| `-SwcSightSearch[=SECONDS]` | One time (default 12 s after the world entry): around each chest within 250 m, 12 directions at 3, 5, 7, and 9 m, the stand places with no line of sight, with the mesh that blocks (its size and the distance of its origin). |
| `-SwcDrop=SECONDS:ITEM` | SECONDS after the world entry: the stack of ITEM (its id) of the bag goes into a Discarded bag (1838). |
| `-SwcInstanceProbe=METERS` | Each 2 s: hide or show again the level instances within METERS of the pawn. With `-SwcInstanceProbeKeep`: hide them one time and again after each new load (a stream out and in). |
