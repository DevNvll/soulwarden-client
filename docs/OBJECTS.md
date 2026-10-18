# The objects of the level data

Stage P4.1 part 2 (Linear AST-174). The client makes the interactable objects of the level data itself: the lore pages, the
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
- The line of sight of a bound (`bLineOfSight`, for example each chest): the retail client checks it with a ray before it shows the
  prompt (chests.md); the query gives the mark, and the interactor does not check it yet.
- The requirements of an option (a level, an item, an achievement): the client does not check them; the server refuses.
- The Idle_VFX of a chest and the depletion effect of an elite chest are not converted (the light is a stand-in). A lore page has no
  visual. A tree has no fall (the retail client: the trunk falls, the stump stays): the layer swaps the meshes at once.
- The objects of the kind `gatherable` (mostly `master_quest_instanced`) have no prompt; the quests are a later stage.
- Far nodes (beyond 100 m) show the level state; until the final level pass of the world track that is both meshes of a node.
- The side-by-side check with the retail client is open.

## Development aids

| Switch | Effect |
| --- | --- |
| `-SwcObjectProbe` | Each 1 s: the nearest object in the log (its ids, its first option), and the bounds within 15 m (green: usable, red: not). |
| `-SwcInstanceProbe=METERS` | Each 2 s: hide or show again the level instances within METERS of the pawn. With `-SwcInstanceProbeKeep`: hide them one time and again after each new load (a stream out and in). |
