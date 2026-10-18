# The creatures

Stage 1.9 (Linear AST-146). The client shows the creatures of the backend: each other entity of the session gets an entry
in an entity table, the slice of its metadata state gives its model, and its movement and vitals states give its place,
its action, its name plate, and its health bar. The sources are the Soulwarden protocol notes 4.14 and 4.17, the research
files `~/Games/soulwarden-client/research/feel-values.md` (part R3, Mannequin) and `creature-visuals.md` (AST-154), and the
backend encoders (`apps/backend/src/server/rep/states`).

## The parts

| Part | Job |
| --- | --- |
| `converter/swc_convert/creature.py`, `tools/convert-creatures.sh` | The census of the creature slices, and the conversion of their bodies, skeletons, clips, weapons, and fragments into `SWC_CONTENT/creatures`, with entries in the shared manifest. |
| `tools/import.sh creature-` | The import into the shared content plugin `/SwcGameContent` (the pipeline of the world track, docs/WORLD.md). |
| `Creatures/SwcEntityTable` | Plain C++: the table of the other entities from the records and the stops of the state bundles. `tools/test-creatures.sh` tests it with the states that the backend encoders make. |
| `Creatures/SwcCreatureData` | Reads the converted data: the model of each slice, the animation sets, the names, the scales. |
| `Creatures/SwcCreature` | The actor of one creature: the body, the weapons, the interpolation, the actions of the server. |
| `Creatures/SwcCreatureAnimInstance` | The Mannequin fragments of the actions with the timing of their variants, and the blend spaces with the weight grid of the data. |
| `Creatures/SwcCreatureSubsystem` | A world subsystem: the entity table, the actors, the log lines, the name plates, the melee contacts, and the damage numbers. |
| `Creatures/SwcHitShapes` | The shape test of the melee contacts: a point, a segment, or a box with a radius (Unreal world space). |
| `Creatures/SwcMeleeContact` | Plain C++: the facet target of a hit volume and the fields of the item of list 3 (the writer is the one of the input codec). |
| `Creatures/SwcDamageMessage` | Plain C++: the reader of the damage number of the attacker (type 2071). |
| `RepClient::take_other_updates`, `FSwcRepConnection::TakeOtherUpdates`, `USwcBackendSubsystem::TakeOtherUpdates` | The read path of the records of the other entities and of the damage numbers (Net/ of the player track; option `keepOtherRecords`). |

## The pipeline

```text
backend copy gamedata/creatures.json (the 503 slices of the backend: asset id, path, action table, tags)
game copy (paks)
   --tools/convert-creatures.sh census---> content/creatures/census.json   (each slice: body, skeleton, controller, database)
   --tools/convert-creatures.sh convert--> content/creatures/skeletons/KEY.gltf   (bones, one body, the clips of the sets)
                                           content/creatures/bodies/KEY.gltf      (each other body and material variant)
                                           content/creatures/weapons/KEY.gltf     (the default weapons, static)
                                           content/creatures/animations/KEY.json  (the fragments of one animation set)
                                           content/creatures/slices.json, names.json, census.json
                                           content/manifest.json: creature-skeleton:*, creature-body:* (kind character),
                                           creature-weapon:* (kind static, with the shared materials and textures)
   --tools/import.sh creature-------------> /SwcGameContent/Characters/..., /SwcGameContent/Meshes/...
client (a run) --writes--> SWC_WORK/creatures/wanted.json   (the slices with no converted model)
   --tools/convert-creatures.sh wanted --> the same files for these slices
```

- `convert` takes the slices of the spawn points near a place (`--near X Y R`; default the start place 8880 4250 with 300 m),
  the slices whose path has a text (`--slices TEXT`), or each slice (`--all`). The files of earlier conversions stay: a
  skeleton file has the union of the clips of all its converted animation sets, and the import takes only the files whose
  hash changed.
- A skeleton file and a body file are kind `character` of the shared manifest (the default glTF pipeline: one folder each,
  with its own skeleton asset, materials, and textures). The body of the skeleton file has the animation; each other body
  follows its pose (Leader Pose: the bones by name). A weapon is kind `static` with the shared materials of stage 1.2.
- `slices.json` has the object path of each asset, by the naming rule of Interchange: `FOLDER/STEM/SkeletalMeshes/STEM` (a
  skeletal mesh), `FOLDER/STEM/SkeletalMeshes/STEMNAME` (a clip with the glTF animation NAME), `FOLDER/STEM/StaticMeshes/STEM`
  (a static mesh); FOLDER is the folder of the manifest entry, STEM the file stem of the glTF.
- The names are short: Interchange shortens an asset name when the path is too long (about 162 characters; a kite shield
  became `wep_mel_shld_1h_ki_devilknight_46df73`). A file stem is a letter and 8 hexadecimal digits of the hash of its key
  (`s_` a skeleton, `b_` a body, `w_` a weapon), and the animation name of a clip is `_` and 8 digits of the hash of the clip
  name; `slices.json` has the map from each clip name to its asset.
- A slice with no converted model, or a model that does not load, shows a placeholder (a cylinder of 0.6 m by 1.8 m) with its
  name plate, and the client adds its key to the wanted list.

## The chain from a record to a model

| Step | Source | Rule |
| --- | --- | --- |
| Slice | metadata state (slot 0, type 10): the asset id and the sub id | The key `GUID:SUB` (the byte order of the asset text of creatures.json). |
| Body | the `Skinned Mesh` field of the SkinnedMeshComponent of the slice (the `.cdf`) | Not `ActionList.m_cdfPath`: the two differ in 479 of 503 slices (AST-154). Grey wolf: `guarddog.cdf`; its ActionList names `timberwolf.cdf`. |
| Skeleton, skins | `.cdf`: `Model.File`, each `CA_SKIN` with its `Material` (else the model material) | All skins of a body are the sections of one skeletal mesh. |
| Scale | vitals state: vitalsId -> `VitalsLevelVariantData.BaseVitalsId` -> `VitalsBaseData.CharacterScale` | For each instance (actor scale); Wolf_Grey_7 1.05. |
| Material variant | vitals state: vitalsCategoryId -> `VitalsCategoryData.MtlOverride` (`FemaleMtlOverride` for a female body) | A body variant: the same skins with the material of the row (1,245 rows have one). The exact precedence is open. |
| Weapons | the default items of the Paperdoll component (`m_slotType`, `m_itemName`): slot 24 the main hand, 27 the off hand | The weapon row (`WeaponItemDefinitions`: `MeshOverride`, the skin and material overrides) and its equip type (`equiptypesdatabase.equipdb`: the socket); the socket of the body (`CA_BONE`: bone, `RelPosition`, `RelRotation` w x y z). A static mesh on the bone. |
| Name | vitalsCategoryId: the English name of the category row (the kind of creatures.json), else the vitals row | A Critter has no level mark. |
| Animation set | ActionList: `m_controllerDefinition`, `m_animationDatabase`, `m_defaultTags`; the tags of creatures.json | One set for each controller, database, tag state, and skeleton. |

The clips of a skeleton: the animation list of its `.chrparams` (`$Include`, `#filepath`, the pattern `*/*.caf`: each file below
the folder, the name is the file stem; the first entry of a name wins).

## The entity table

`swc::creatures::EntityTable` reads each record of an interest number that is not 1:

| State | Fields | The table keeps |
| --- | --- | --- |
| metadata (10) | asset id (field 0), actor id (field 1) | the slice key, the actor id |
| movement (11) | worldPosAbs 27, rotation 11, timeAnchor 40 (server us), dataBits 41, teleport 31 | up to 16 samples (time, position, rotation) |
| movement (11) | scopeData 32, scopeInfoBlob 33, scopeTimeBlobEx 29, globalFragTags 34, the time of scope 0 (28, 38, 49, 50) | the record of each scope of the mask, its time, the tags |
| vitals (15) | HealthAmount 0, vitalsData 2 (bit 0 dead), healthChangeFlags 3, HealthMax 4, vitalsId 8, vitalsCategoryId 9, vitalsLevel 10, invulnerability 11 | the health, the dead mark, the ids, the level |
| marker (5399) | enabled 0 | the plate can show |

- The scope fields count only when the state has scopeTimeBlobEx; a state with no scope fields keeps the records; a mask with
  no bit of a scope ends its action (protocol notes 4.14).
- A record: start count (u16), fragment + 1 and action + 1 (size codes), the playback scale (half float), the option (u8).
  The time of scope 0 is in ticks of 1/30 s; a scope of 2 or more has a half float in seconds in scopeTimeBlobEx.
- A stop of the control block removes the entity; a new record of the same number is a new entity (a new serial).
- `tools/test-creatures.sh`: the encoders of the backend (`encode_metadata_state`, `encode_creature_movement_state`,
  `encode_creature_vitals_state`, `encode_marker_state`) make the states of a grey wolf: the first record, the bite with its
  time, a step with no scope fields, the timed action again, the end of the action, the additive reaction over the run, a
  health change, the death, and a stop. The vendored state reader reads each state to its last byte.

## The view

- Interpolation: the actor shows the samples at the render time = the server clock now less 0.25 s (the backend sends a
  movement state for each step of 200 ms of a way). Between two samples: linear position, slerp of the rotation. After a
  pause (more than 0.4 s between two samples) a step starts 200 ms before its state. After the last sample the creature
  stays at it. The position is exact: the client does not move a creature to the ground (protocol notes 4.14).
- The facing: the rotation of the movement state turns north (engine +Y) about the vertical axis; the mesh has the yaw +90
  of the player mesh.
- The actions: the start of a record on the server clock is the time anchor of its state less the time of the record. The
  actor starts the fragment when the render time reaches that start, at the fragment time of the render time. A new start
  count starts the fragment again; the same record again starts it again only when its start is more than half the fragment
  length from the start that plays (the one-shot rule of the client, 0x2A2BC20). The fields with no record play the idle of
  the client (fragment `Idle`); a dead creature keeps its pose.
- The fragments: the variant of the tag state of the slice (the most tags, the first in the files at a tie; a variant with
  FragTags is not taken); its first layer: each item with its start (the ExitTime sum, or the end of the clip before it), its
  start time in the clip, its blend-in, its speed, its loop flag. The empty animation is the end: the pose of the item before
  it stays (the client keeps the last pose; the backend then sends the idle record).
- Blend spaces: the weight grid of the `.bspace` (VGrid) with `FSwcBlendSpace::Evaluate` of the player track; one phase for
  all examples. The parameters from the shown motion: MoveSpeed (m/s, SmoothCD 0.1 s), TurnSpeed (rad/s, positive to the
  left, 0.75 s), TravelAngle (rad, 0.1 s); TravelSlope 0. A `.comb` takes its first blend space for a turn to the left.
- The clips have no root track: the server moves a creature, also in its attacks (the retail client plays a remote lunge
  in place, LC-Q1-1).

## The melee contacts (stage P2.8)

The retail client finds the contacts of a melee weapon and sends them; the server checks each one and computes the damage
(protocol notes 4.7, 4.15, 4.32). The work is in two parts (agreed through the orchestrator):

- The player track: the damage windows of an attack (`weapon-actions.json`), the weapon shape of each step, the call of
  `FindMeleeContacts`, one contact for each creature in one strike, and list 3 of the input entry in its input codec.
- This track: the hit volumes, `USwcCreatureSubsystem::FindMeleeContacts`, and the item writer.

The hit volumes: the `HitVolume` components (type `6d3e998c-fc19-4c9d-b8f6-77c3fe985d29`) of the entities of a slice (Head,
Torso, Leg_L_Foot, ...): the bone (`m_targetBoneName`), the center on the bone (`m_center`, engine space), the shape (capsule:
`m_Radius`, `m_Height` with its two ends, `m_axis`; sphere: `m_Radius`; box: `m_Box` half sizes), the marks `m_isHeadshot`,
`m_isLegshot`, and `m_damageMult`. Two entities can have the same component ids (the left and the right leg of the
werewolf): a volume is the pair of its entity and its component. `slices.json` keeps for each volume its component id and the
local id of its entity (the entity id XOR the root id of the slice). The world: 4,714 volumes (4,234 capsules, 453 spheres,
27 boxes), each one a component of the slice of the backend (`creatures.json`).

The interface (decision of the orchestrator): the shape is `FSwcMeleeShape` of the player track (`Combat/SwcMeleeShape.h`,
Unreal world space), the item is `swc::net::MeleeContactItem` of the input codec, and its writer is
`swc::net::write_melee_contact` (one writer). `USwcBackendSubsystem::UpdateLook` connects the query to the player actions:

```cpp
Avatar->GetActions()->SetContactQuery(/* USwcCreatureSubsystem::QueryMeleeContacts(Shape, Out) */);
```

- `FindMeleeContacts(const FSwcMeleeShape&, TArray<FSwcMeleeContact>&)` tests the hit volumes at the pose that the creature
  shows (the bones of its leader mesh, with the scale of the creature). One contact for each creature: the touched volume
  nearest to the middle of the shape. No contact for a dead creature, a creature with the mark "invulnerable" (the server
  refuses it), a placeholder, or a point behind a wall: a line from the chest of the attacker (its feet + 1 m, a value of this
  client) to the point hits the static world (`ECC_WorldStatic`). `bNoSight` (DisableLOSCheck of the clip) skips the line.
- The point is on the surface of the volume toward the weapon; the normal is the normal of the surface there; the direction
  is the cast direction of the shape.
- The item: provider 0xFFFFFFFF, the actor id and G of the creature two times (the group and the receiver), the facet target
  `(local id XOR G) + component id` (`swc::creatures::facet_target`, `melee_contact_item`), and the point, the normal, and the
  direction in engine space. `tools/test-creatures.sh` checks the bytes against the test client of the backend and reads them
  with the reader of the backend (`server/rep/client_input.cpp`).
- The player side keeps one contact for each target for each strike, and puts the contacts into list 3 of the next input
  entry. The query writes one log line for each creature in each strike (`creature N: melee contact: visit V strike S ...`).
- `-SwcFarContact[=SECONDS]` (development runs): when the shape touches no creature, one contact with the nearest living
  creature 6 to 10 m from the attacker (from SECONDS after the start of the world). The server refuses it: "too far" (reach
  3.5 m + 1.5 m).
- `-SwcHitVolumes` draws the volumes of the creatures within 20 m (red: head, cyan: legs, yellow: the others) and their body
  capsules (white). `-SwcVolumeProbe` tests a capsule
  in front of the character each 0.5 s and writes a log line for each contact; it sends nothing.

The body capsule: each living creature with a model has the capsule of its slice (the `QueryShapeCapsule` of its
`CharacterComponent`; `capsule` of the backend `creatures.json`, 501 of 503 slices): the segment of the length `height` on
the axis in the frame of the creature (x right, y front, z up), the radius around it, the offset in the same frame, and its
lowest point at the feet (the rule of the backend, `world/projectiles.cpp` `capsule_of`; no scale). The wolf: radius 0.7 m,
segment 0.9 m on the front axis, along its body. The capsule blocks only the movement of the player (object type Pawn, a block
for the channel Pawn, no other channel): not the camera, not the ground traces, not the strike shapes, not the wall line of the
contacts. The player cannot stand on it. A dead creature has none. The reason: in the retail game the creatures block the
players, and without the capsule a chain of attacks moves the character through the creature. This is an inference of the
orchestrator, not a fact of the client code: the side-by-side check with the retail client is open. Check (development run,
`melee3`): the character stays 1.67 m in front of the wolf through two light chains and a heavy attack, and the backend
accepts each position report (821 accepted, 0 refused).

The damage number: the server sends `DamageReceiverComponentClientFacet_OnDamageDealt` (type 2071) only to the client of
the attacker: the actor id of the creature, the flags (bit 3: the kill; bit 6: a critical hit; bit 8: a headshot), and the
damage entries (type, amount, part). The view shows the whole part of the sum at the creature: a simple number that rises
70 cm and fades in 1.2 s (white; a critical hit larger and yellow). These values are not from the data: the original is the
damage number canvas of the game (later, through the UI runtime). Each message has a log line
(`creature N: NAME: damage number 64.0 slash, flags 0x0000`).

The overlays: a record of a scope other than 0 (the additive reaction of scope 4 of the wolf, `Reaction_Additive`) plays as
an overlay over the pose of scope 0. It blends in with the blend-in of its first item, and out with the blend-out of its
fragment at its end or when the mask has no bit of its scope. A layer after the first one of a variant changes only the
bones that its clips animate; an additive clip (keys that are differences from the identity) adds its rotations.

## The read path of the other tracks

- `USwcCreatureSubsystem::GetTargets(TArray<FSwcCreatureTarget>&)`: each creature of the table with an actor: its interest
  number, its feet (Unreal cm, the shown place after the interpolation), and the dead mark. The target lock of the camera
  (stage 1.8, docs/CAMERA.md) can take its candidates from it in place of the positions of the snapshot.
- `USwcCreatureSubsystem::GetTable()`: the entity table (the records of the server, with no delay).
- `USwcCreatureSubsystem::FindMeleeContacts(const FSwcMeleeShape&, TArray<FSwcMeleeContact>&)` and `QueryMeleeContacts`: the
  melee contacts of a damage shape (above).

## The name plate

A simple plate on the canvas of the game viewport: the name and the level (no level for a Critter), and the health bar
(health / HealthMax; grey while the creature is immune). The original is the `AiMarker` template of
`lyshineui/markers/markermanager.dynamicuicanvas` with `lyshineui/markers/marker.luac` (for the UI track).

| Rule | Value | Source |
| --- | --- | --- |
| Near range | the plate shows within `m_nearRange` (6 m for most slices) | MarkerComponent client facet of the slice; protocol notes 4.14 |
| Crosshair | within `m_nearAngle` (0.1309 rad) at the near range, down to `m_farAngle` (0.01745 rad) at `m_angleShrinkDistance` (20 m), up to 50 m | the same facet; the 50 m limit is an inference |
| Health bar | not with `m_neverShowHealthBar` | the same facet |
| Dead | the plate goes away 3 s after the dead mark; the body 300 s after it | protocol notes 4.14 (the times: "some seconds", the delayed event of 300 s) |
| `-SwcPlates=all` | each plate within 40 m (development pictures) | a development aid |

## Counts and disk

| Set | Slices | Bodies (.cdf) | Skeletons | Spawn points |
| --- | ---: | ---: | ---: | ---: |
| 300 m around the start place | 19 | 16 | 14 | 142 |
| 800 m around the start place | 69 | 52 | 23 | 1,041 |
| The world (creatures.json) | 503 | 245 | 71 | 32,840 |

The conversion of the world (`tools/convert-creatures.sh convert --all`, 2026-10-18): 503 slices and 503 animation sets, 71
skeleton files with 5,097 clips, 245 bodies and 112 material variants, 127 weapon models; 9 minutes. The import
(`tools/import.sh creature-`): 357 skeletal assets and 127 static meshes with their textures and materials, 0 failed;
about 13 minutes in all.

| Disk | GB |
| --- | ---: |
| Converted content (`content/creatures`: glTF, animation sets, census) | 1.2 |
| Plugin (`/SwcGameContent`: the creature folders) | 6.0 |

- The clips of a skeleton are the clips of the fragments of the converted animation sets on it (the fragments of the action
  tables, the reaction tables, and the idle), not the whole bank of its `.chrparams` (13,825 files in the world).
- Interchange names 101 of the 5,097 clips `STEM` + `Null` + `NAME`: the client loads that second form when the first one
  is not there. One clip is not in the plugin.
- 8 skeletons have clip names that their bank does not have (for example `upyr_base_skel` has no `.chrparams`; the seraphim
  of `isabella_t2_skel` names clips of a different bank): those items have no clip.
- One weapon has no drawn triangle (`spriggan_empty_weapon.cdf`).

## The checks

```sh
tools/test-creatures.sh                     # the entity table (63 checks); the contact item and the damage number (26 checks)
export SWC_DEV_DATA=~/Games/soulwarden-client/dev-data-creatures SWC_AGENT_DESKTOP=soulwarden-client-creatures
tools/creature-check.sh creatures-world 80 tests/creatures/fight.txt tests/drive/creatures.txt
```

`tools/creature-check.sh` starts a window run with the development routes (`SWC_SESSION_BACKEND_ARGS=--dev-commands`), runs
the script of `tools/lib/dev_world.py` (`tools/creature-world.sh`: spawn, kill, health, action, heal, level, ignore), plays the
drive script, and records the headless output of the agent desktop (`wf-recorder`). The video and the logs go to
`SWC_WORK/evidence/1.9`.

Exit check (development run, 2026-10-18, the world level at the start place 8880 4250): `creatures-world.mp4` (80 s). The
script gives the character the level 60 (a lunge of the level 18 wolf makes 1,079 damage) and makes a grey wolf and a
skeleton with a club to the east, a turkey, a Risen, and a knight with a great axe and a shield to the west, on the street
(the navigation floor of the backend at the height of the ground). The four fighters take the character as their target,
run to it, and attack; the turkey walks; the script kills each one, and each one plays its death and stays on the ground. A
grey wolf of a spawn point patrols its route 95 m away (Move and Idle in turn: 4 walks in the run). The client log and the backend log agree for each
action: the Risen 12 attacks (Attack_Swipe_L, Attack_Swipe_R, Attack_Lunge_L, Attack_Lunge_R, Attack_Rampage), the wolf 6
Attack_Bite and 2 Attack_Lunge, the knight 12 attacks, the skeleton 8 attacks, and 5 deaths; 6 creatures, all with their
bodies (0 placeholders). The white wire spheres in the video are the markers of the target lock of the camera (development
runs).

## Differences and open points

- Not shown yet: the additive scope (the flinch of `Reaction_Additive`), the layers after the first one of a variant (head,
  tail, and mouth layers), the procedural clips (sounds, effects, the death material, the dissolve), the weapon scope
  animations (the bow string), the cloth and the bound props of a `CA_BONE` binding, the look poses.
- The material precedence, the variant ties (tag priority), the option byte, and the remote motion parameter formula are
  open points of the research (AST-154 O1 to O3, R3 O1, O2, O5). The interpolation delay (0.25 s) is a value of this client.
- The name plate is a simple plate on the canvas of the viewport; the original is the `AiMarker` template (the UI track).
  The 50 m limit of the crosshair rule and the 3 s of the plate of a dead creature are inferences.
- A creature body has the materials of the default glTF pipeline (its own textures), not the shared materials of stage 1.2.
- The side-by-side check with the retail client is open (the feel of the movement, the attacks, and the deaths).
