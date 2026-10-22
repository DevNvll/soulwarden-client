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
| The terms risk | Personal project. Public source code since 2026-10-19: no game data, no converted content, no imported asset in the repository. |
| Where the code goes | A local git repository until 2026-10-19; then public on GitHub (github.com/DevNvll/soulwarden-client, MIT). |
| For whom | The user only: the content is converted and imported on this computer. |
| Connection | The Soulwarden backend with no change: the client speaks its original wire protocol (option (a)). |
| Fidelity | The feel of the gameplay and the UI must match. The look could differ until 2026-10-18. |
| World fidelity (2026-10-18) | "Focus on the terrain and world fidelity, with the original game." The terrain, the ground cover, the materials of the world meshes, the vegetation, the light, the sky, the time of day, and the water must match the retail client. The retail reference pictures are the development-run pictures of the Soulwarden project (`~/Games/soulwarden/work/evidence`). Research: `research/terrain.md`, `research/world-materials.md`, `research/world-environment.md` (outside the repository). |
| Unreal where it is better (2026-10-18) | "The lighting of the unreal game seems better. Let's keep what Unreal does better like Unreal." The content follows the retail client: the terrain layers, the ground cover, the placements, the materials and their colors, the vegetation, the roads and decals, the time of day (the sun and moon path from the backend clock), and the light sources of the data. The rendering keeps the Unreal way where it looks better: the lighting itself (shadows, Lumen global illumination, reflections), the physical sky and atmosphere, the fog and the clouds, the exposure, and the water rendering. A retail picture is the reference for the content and the colors, not a target for the brightness or the film look. The user judges the look. |
| The original colors (2026-10-19) | "The original game colors should be copied to the client." This narrows the decision above. The color result of the picture copies the retail client: the color grade (the color balance, the saturation, the film response and its shoulder, the source LUTs), the white balance, the sky, fog, sun, and moon colors of the time-of-day data, the exposure level, the material and terrain colors. The technique stays Unreal where it is better (Lumen global illumination, shadows, reflections, volumetric clouds, water rendering). The check is numeric: the color statistics of the matched views against the retail references at the same place and hour. |
| The focus (2026-10-19) | "For now focus on the world, graphics and gameplay fidelity, deprioritize interactions." First: the world (the terrain, the ground cover, the placements, the water), the graphics (the materials, the colors, the light, the sky, the weather), and the gameplay (the movement, the camera, the combat, the creatures), each against the retail client. Later: the interactions (the Talk and the conversation screen, the persons of client spawners, the services of a person, the use of the world objects beyond what works now). The work that is done stays; its open points wait. |
| World | An open world client: the whole world, streamed. A small area is only a first test place. |
| Identity | The Steam account of the user, read at run time; the Steam authentication functions are stubs. |
| Characters | The same characters as the retail game: a player run uses the data folder of the user. |
| Roles | The research thread of AST-135 is the orchestrator of this project; the Soulwarden orchestrator does only the backend. |
| UI logic (D1) | The original Lua 5.1 scripts run in an embedded Lua 5.1.5 (MIT), with an emulation of the buses that grows screen by screen behind a stub layer that logs each missing call. |
| First HUD (D2) | Stage 1.10 runs the original HUD scripts (size L); it is the go or no-go point of D1. |
| Canvases (D3) | The canvases are converted as data, with one runtime widget that follows the LyShine rules. |
| Goal (2026-10-18) | "Don't stop until you have the new world open world with creatures spawning and a character loading its gear." The orchestrator continues with no stop; research runs in parallel on GPT-6.1-Sol; a feature parity plan with the official client follows (AST-155). |

## Status

The status of each stage is in the Linear project "Soulwarden Client" (P-AST-7): P0 AST-136, 1.1 AST-137, 1.2 AST-138,
1.3 AST-139, 1.4 AST-140, 1.5 AST-141, the feel research AST-142, 1.6 AST-143, 1.7 AST-144, 1.8 AST-145, 1.9
AST-146, 1.10 AST-147, the UI research AST-148, milestone 2 AST-149, milestone 3 AST-150, milestone 4 AST-151.

## Sizes

As in the Soulwarden roadmap: `S` one stage run, `M` two or three, `L` four or more, `XL` open.

## Milestone 0: foundation (stage P0)

| Id | Content | Size | Status |
| --- | --- | --- | --- |
| P0 | Converter (pak, catalog, meshes, skins, skeletons, clips, textures, materials, terrain, placements, manifest); Unreal project with a C++ module; the import commandlet; check: one command converts and imports the four proofs of the research | M | done (2026-10-18) |

## Milestone 1: enter the open world

The client enters the world of the backend at the place of the character of the user, anywhere in the world (the
characters are the same as in the retail game). Thus the world is complete from milestone 1: all regions of the
main world, streamed. The order puts the feel first after the world: the connection, the character, the movement,
and the camera.

The world: 7 x 7 regions of 2,048 m (14.3 km x 14.3 km), 26 of them in `playable.json`. Upper limit of the static
meshes: 21,351 base meshes and 23,278 merged meshes of slices in the paks (pak index). Measured in stage P0: 763 meshes
of one 300 m area import in about 6 minutes with 4 cores. A world census (1.2) gives the real numbers and the disk
budget (269 GB free on 2026-10-18).

| Id | Content | Size | Needs |
| --- | --- | --- | --- |
| 1.1 | Space and scale: the map from engine space (m, Z up, right-handed) through glTF to Unreal (cm, Z up, left-handed); one test with an asymmetric mesh at a known place | S | P0 |
| 1.2 | World census and shared assets: the placements of all regions (a census of the unique meshes, textures, and materials, and the disk budget); each texture imported one time; meshes with no own texture copies; material instances of one base material (the look stays simple); an incremental pipeline (only new or changed assets) | M | 1.1 |
| 1.3 | The open world level: one World Partition level of the main world; 49 Landscape actors (Z scale 400 keeps the 16-bit step of the source) with the terrain cookies of the capitals; the placements of each region as instanced static meshes in the streaming cells; HLOD for the distance; the 26 playable regions first, then all 49; a fly-through video | L | 1.2 |
| 1.4 | Backend connection, part 1: the player run (`tools/play.sh`: the Steam account of the user read at run time, the data folder of the user with the `run.lock` of the Soulwarden launcher, the release backend copy, one network namespace with loopback only for the backend and the client) and the development run (own data folder, a seed character); the HTTP login path (Soulwarden protocol notes 2) with stubs for the Steam authentication; the character list | M | P0 |
| 1.5 | Backend connection, part 2: DTLS 1.2, the carrier, the type index, the registration, the world entry at the place of the character (self identification, level information, state bundles, the metadata state, the movement state), the 30 s and 2 s timers of the client contract | L | 1.4 |
| 1.6 | The character of the user: the player skeleton, the body parts, the head, the hair, the skin tone from the appearance state (12 index bytes and the creation databases), the armor from the paperdoll state and the item catalog; modular skeletal meshes | M | 1.5 |
| 1.6b | The character loads all its gear: the weapons in the hand and at the sheathe place (paperdoll group 2), shield, bow and quiver, the cloth parts without a simulation, the hide rules, the dyes and face details if cheap (AST-152; research AST-153) | M | 1.6 |
| 1.7 | Movement feel: the movement values of the player slice and the client data (speeds, acceleration, turn, jump, sprint, slopes), the input report 1589 at 30 Hz with the position, the locomotion graph from the player clips and the blend spaces | L | 1.6, R2, R6 |
| 1.8 | Camera feel: the camera values of the client (distance, offsets, lag, field of view, the combat and aim modes) | M | 1.7, R1 |
| 1.9 | Creatures of the backend in the whole world: records -> slice -> character model, movement interpolation, action fragments -> montages, name plate and health bar | L | 1.5, R3 |
| 1.10 | First HUD with the original HUD scripts (D1 to D3): the canvas converter as data, the runtime widget, the embedded Lua 5.1.5 with the bus emulation and the stub log, the HUD data from the backend states | L | 1.5, R4, R5 |

Two tracks can run at the same time: the world (1.1 to 1.3) and the connection (1.4, 1.5). They meet at 1.5 (the
client enters the open world at the place of the character).

Go or no-go points: after 1.3 (the user flies through the open world: streaming, frames per second, disk), after 1.5
(the client enters the world of the unchanged backend with a character of the user), after 1.8 (the user judges the
feel of walking and of the camera), and after 1.10.

## After milestone 1: the parity plan

The milestones after milestone 1 come from the parity plan (AST-155): [PARITY.md](PARITY.md). P2 (first fight loop
and essential UI) is next; then P3 to P9. The earlier milestones 2 to 4 of this file (the UI, combat feel, the rest of
the world) are now parts of P2, P3, and P4.

## The MVP plan (2026-10-20)

The decision of the user (2026-10-20): a functional MVP game. Play the current level 70 character of the user anywhere
in the whole world; no sound; only the five weapons that work now (sword and shield, rapier, bow, great axe, war
hammer); no quests or shops; progression later. The plan is `~/Games/soulwarden-client/research/mvp-gaps.md` (outside
the repository). The status of each item is in Linear (parent AST-212).

| Item | Linear | Status (2026-10-22) |
| --- | --- | --- |
| MVP-A: the supported-action list of the backend, the owner-state read path | AST-214 | Done |
| MVP-B: all item looks prepared before play, the appearance preflight | AST-215 | Done |
| MVP-C: the world map, the compass, the travel selector, death and respawn | AST-213 | Done |
| MVP-D: the collision of the whole world, the load failures | AST-216 | Done |
| MVP-E: the combat gaps of the five weapons, the screens, the consumables, the rune | AST-217 | Done |
| MVP-F: the gathering gaps | AST-221 | Done |
| MVP-G: the skill trees, the bar edits, the rank screens | AST-222 | Done |
| MVP-H: the persons of the whole world, the conversation screen | AST-223 | Done |
| MVP-I: the fight feedback with no sound, the water rules | AST-225 | Done |
| MVP-J and J2: the crafting loop, the craft mods, the prompt card rule, the key hints | AST-226, AST-231 | Done |
| MVP-L: the skill effects on the animation timeline (the user's request) | AST-230 | Done (open look points: AST-246) |
| God rays (the user's request) | AST-232 | Done |
| The materials of the skinned bodies and the weapons, the wolf size | AST-227, AST-233, AST-247 | Done (part c of AST-247 open) |
| MVP-K: the cooked build of the whole MVP and its freshness checks | AST-229 | Done (2026-10-22, main af18192) |
| The one-hour acceptance (MVP order 17): the user plays with tools/play.sh | - | Next |

## Quests (after the backend parts 1.8a to 1.8e)

The request of the user (2026-10-21): plan the quests in the client and add them to this roadmap. The backend quest
engine has five parts: part 1.8a is released (backend commit fc5677e, database schema 15), and part 1.8c is released
(backend commit 096ab18, database schema 16); 1.8b, 1.8d, and 1.8e follow. The
plan is `~/Games/soulwarden-client/research/quest-plan.md` (outside the repository; GPT-6.1-Sol, 2026-10-21). The
backend owns the progress and the rewards; the client shows them with the original canvases and scripts. The user
decides the place of these steps in the order. The decision of the user (2026-10-22): Q1 and Q6 next, beside the MVP,
tested with a development character; the other steps after the MVP. Linear: parent AST-235.

| Step | Backend part | The client work | Size |
| --- | --- | --- | --- |
| Q1 | 1.8a (released) | The owner decoders of the Objectives state (slot 22, type 3857) and the pin readiness (slot 73, type 5606); the quest data export and model; the messages 2709 and 1458; the original quest tracker of the HUD, the journal, and the banners with real buses | L |
| Q2 | 1.8a, updates in b and c | The quest icons on the map and the compass, the world task pins, the objective areas; the journal and the map use one pin list | L |
| Q3 (F7) | 1.8a (the new character start) | The original character creation: the archetype, the appearance, the name check, the create request, the entry on the Windsward beach | L |
| Q4 | 1.8b (the person base 2.5c is released) | First the client id of the spawner persons (AST-248: the Talk of 1,221 persons, the topics 3158 and the close 5397, backend 2.5c); then the offers, the hand-in, and the next offer in the conversation screen; the person markers; the first reward screen | L |
| Q5 | 1.8b | The trigger areas, the scene activation, the cutscene handshake and playback (or a declared stand-in), the whisper subtitles with no sound | XL |
| Q6 | 1.8c (released) | The real progress of the kill, item, gather, craft, and place tasks; abandon and reset; the quest objects (for example the three treasures of a shipwreck); the client messages 2297 and 6416 (the entry and exit cues of a point of interest), 2617 (the open of a screen), 3836 (abandon), 5600 (reset). After Q1, the counters and the quest objects of the start quest do not need Q4 and Q5 | L |
| Q7 | 1.8d | The reward previews and screens with the real changes; the availability of the offers; a full bag | L |
| Q8 | 1.8d | The timers, the failed quests, the repeats | L |
| Q9 | 1.8e | The coverage report and the acceptance: a new character plays the ten quests from 06_getoffbeach to the first settlement | L |

For Q1 and Q2, a development character made by the development create tool can test the display before Q3. The
existing character of the user gets a quest only by the admin command `apply_backstory` (its achievements and its start
objective only).

## Research items

R4 and R5 are answered (AST-148, 2026-10-18): `~/Games/soulwarden-client/research/ui.md` (outside the repository),
with the decisions D1 to D3 above.

R1, R2, R3, and R6 are answered (AST-142, 2026-10-18): `~/Games/soulwarden-client/research/feel-values.md` (outside
the repository). Its section 3 lists the open points of each stage; most need a side-by-side video with the retail
client. Such a video comes from a retail reference run of this project (AGENTS.md, the rule of the retail reference
runs) or from the user.

| Id | Question | Needed by |
| --- | --- | --- |
| R1 | Where are the camera values of the client (slices, datasheets, console values, client code)? | 1.8 |
| R2 | Where are the movement values (the character component of the player slice, the stamina data, the client code)? | 1.7 |
| R3 | Mannequin in Unreal terms: `.adb` fragments, tags, `.bspace`, `.comb`, action lists, additive and aim clips | 1.9 |
| R4 | The canvas format in detail (the element components of LyShine 1.12 and of the fork) | 1.10, 2.1 |
| R5 | The UI logic: run the original Lua 5.1 scripts in an embedded Lua with an emulation of the buses that they call, or write the logic of each screen again | 2.3 |
| R6 | The key bindings of the client (`.inputbindings`, the action maps of the canvases) | 1.7 |
| R7 | Closed by the user (2026-10-18): the Steam account of the user, stub authentication, the same characters | - |

## Open points of stage P0

- 4 meshes of the settlement area are not in the game copy; 3 effect meshes have a vertex layout that the reader
  does not read; effect cards (`fx_fogcard_*`) are converted but need an effect material.
- Closed in stage 1.2: each texture is imported one time, and the meshes use shared material instances
  ([WORLD.md](WORLD.md)).
- The specular maps of the source have no slot in the glTF material (metallic workflow); the look is not a goal now.
- Closed in stage 1.1: the node matrices of multi-chunk meshes are applied, and each mesh is one glTF node (one
  static mesh asset).

Rules found in stage P0 (they are in the converter):

- A mesh component with `Visible` = 0 is not placed (for example the helper cubes of the level design).
- A sub-material with the shader `Nodraw` or the name `shadow_proxy` is not drawn; the shader `Glass` is transparent.
- Some source vertices have NaN half floats (116 UV values in one building): the reader makes them 0.
- Unreal folders are short and have only letters, digits, and `_` (`/Game/Imported/Meshes/STEM_HASH`): the game
  paths are too long for the name budget of Interchange, and some have spaces and commas.
