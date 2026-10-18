# Roadmap of the Soulwarden client

## The goal

A client in Unreal Engine 5.8 that plays on the Soulwarden backend, with the feel of the gameplay and the UI of
the retail client. The look can be different for now. The basis is the research AST-135
(`~/Games/soulwarden/work/research/unreal-port.md`): the formats are readable, Unreal imports the converted
content, and the large cost is the client logic (animation logic, UI, combat client, camera, effects).

## Decisions of the user (2026-10-18)

| Question of the research | Decision |
| --- | --- |
| Port or not | A parallel project (this repository); the Soulwarden project continues with the retail client. |
| The terms risk | Personal project, local only. |
| Where the code goes | A local git repository, no remote. |
| For whom | The user only: the content is converted and imported on this computer. |
| Connection | The Soulwarden backend with no change: the client speaks its original wire protocol (option (a)). |
| Fidelity | The look can differ; the feel of the gameplay and the UI must match. |

## Sizes

As in the Soulwarden roadmap: `S` one stage run, `M` two or three, `L` four or more, `XL` open.

## Milestone 0: foundation (stage P0)

| Id | Content | Size | Status |
| --- | --- | --- | --- |
| P0 | Converter (pak, catalog, meshes, skins, skeletons, clips, textures, materials, terrain, placements, manifest); Unreal project with a C++ module; the import commandlet; check: one command converts and imports the four proofs of the research | M | done (2026-10-18) |

## Milestone 1: walk in the start area (the vertical slice)

The order puts the feel first: the connection, the character, the movement, and the camera before the look of
the world.

| Id | Content | Size | Needs |
| --- | --- | --- | --- |
| 1.1 | Space and scale: the map from engine space (m, Z up, right-handed) through glTF to Unreal (cm, Z up, left-handed); one test with an asymmetric mesh at a known place | S | P0 |
| 1.2 | The level of one region: a World Partition level, a Landscape from `height16.png` (Z scale 400 keeps the 16-bit step of the source), the placements as instanced static meshes (a level builder commandlet), the macro map as a base material; the terrain cookies of the capitals (the `navcore` rule of Soulwarden) | M | 1.1 |
| 1.3 | Backend connection, part 1: a launcher that starts a backend instance of this project and the client in one network namespace with loopback only; the HTTP login path (Soulwarden protocol notes 2); the identity (a made-up development identity first) | M | P0 |
| 1.4 | Backend connection, part 2: DTLS 1.2, the carrier, the type index, the registration, the world entry (self identification, level information, state bundles, the metadata state, the movement state), the 30 s and 2 s timers of the client contract | L | 1.3 |
| 1.5 | The character of the user: the player skeleton, the body parts, the head, the hair, the skin tone from the appearance state (12 index bytes and the creation databases), the armor from the paperdoll state and the item catalog; modular skeletal meshes | M | 1.4 |
| 1.6 | Movement feel: the movement values of the player slice and the client data (speeds, acceleration, turn, jump, sprint, slopes), the input report 1589 at 30 Hz with the position, the locomotion graph from the player clips and the blend spaces | L | 1.5 |
| 1.7 | Camera feel: the camera values of the client (distance, offsets, lag, field of view, the combat and aim modes) | M | 1.6, R1 |
| 1.8 | Creatures of the backend: records -> slice -> character model, movement interpolation, action fragments -> montages, name plate and health bar | L | 1.4, R3 |
| 1.9 | First HUD from the converted canvases: health, stamina, mana, the compass, the interaction prompt | M | R4 |

Go or no-go after 1.4 (the client enters the world of the unchanged backend), after 1.7 (the user judges the feel of
walking and of the camera), and after 1.9 (the slice).

## Milestone 2: the UI

| Id | Content | Size |
| --- | --- | --- |
| 2.1 | Canvas converter: LyShine canvases (binary object streams) -> UMG widgets: transforms, anchors, images and sprites (9-slice), texts with the original fonts and styles, layouts, masks | L |
| 2.2 | The screen system: the states of the screen manager of the client (open, close, escape, mouse owner, draw layers) | M |
| 2.3 | The logic of the screens: the decision of R5, then the core screens (inventory, paperdoll, character, map, crafting, death, loot, interaction) | XL |

## Milestone 3: combat feel

Weapons in the hand, the basic attacks from the attack graph of the data, the abilities, the hit reactions, the
dodge, the stamina, the projectiles, the first effects (a converter of the particle libraries to Niagara for the
common parameters). The backend already has these systems; the client plays their states and fragments. Size XL.

## Milestone 4: the world

All regions of the main world (49 Landscape actors in one World Partition level), the grass (`.vegetation`), the
water, the time of day from the time of day XML and the world clock of the backend, the weather volumes, the other
levels (dungeons). Size XL.

## Later: the look

Master materials (`Illum`, `HumanSkin`, hair, terrain layers, foliage with wind, water), only when the user asks.

## Research items

| Id | Question | Needed by |
| --- | --- | --- |
| R1 | Where are the camera values of the client (slices, datasheets, console values, client code)? | 1.7 |
| R2 | Where are the movement values (the character component of the player slice, the stamina data, the client code)? | 1.6 |
| R3 | Mannequin in Unreal terms: `.adb` fragments, tags, `.bspace`, `.comb`, action lists, additive and aim clips | 1.8 |
| R4 | The canvas format in detail (the element components of LyShine 1.12 and of the fork) | 1.9, 2.1 |
| R5 | The UI logic: run the original Lua 5.1 scripts in an embedded Lua with an emulation of the buses that they call, or write the logic of each screen again | 2.3 |
| R6 | The key bindings of the client (`.inputbindings`, the action maps of the canvases) | 1.6 |
| R7 | The identity: a development identity, or the Steam id and persona of the user read as the Soulwarden launcher reads them (values never in the repository); and if the client may use the database of the Soulwarden project of the user (only when the retail client does not run) | 1.3 |

## Open points of stage P0

- 4 meshes of the settlement area are not in the game copy; 3 effect meshes have a vertex layout that the reader
  does not read; effect cards (`fx_fogcard_*`) are converted but need an effect material.
- Interchange imports the textures of each glTF again for each mesh (no shared texture assets). Stage 1.2 needs a
  shared texture and material step (import each texture one time, then material instances).
- The specular maps of the source have no slot in the glTF material (metallic workflow); the look is not a goal now.
- Multi-chunk meshes are placed at the identity (no node transform of the chunk file yet).

Rules found in stage P0 (they are in the converter):

- A mesh component with `Visible` = 0 is not placed (for example the helper cubes of the level design).
- A sub-material with the shader `Nodraw` or the name `shadow_proxy` is not drawn; the shader `Glass` is transparent.
- Some source vertices have NaN half floats (116 UV values in one building): the reader makes them 0.
- Unreal folders are short and have only letters, digits, and `_` (`/Game/Imported/Meshes/STEM_HASH`): the game
  paths are too long for the name budget of Interchange, and some have spaces and commas.
