# The creatures

Stage 1.9 (Linear AST-146). The client shows the creatures of the backend: each other entity of the session gets an entry
in an entity table, the slice of its metadata state gives its model, and its movement and vitals states give its place,
its action, its name plate, and its health bar. The sources are the Soulwarden protocol notes 4.14 and 4.17, the research
files `~/Games/soulwarden-client/research/feel-values.md` (part R3, Mannequin) and `creature-visuals.md` (AST-154), and the
backend encoders (`apps/backend/src/server/rep/states`). Stage P2.8 (Linear AST-163) adds the hit volumes, the melee contacts of
the player, the body capsule, the overlays of the other scopes, the reactions, and the damage numbers. Stage P2.9 (AST-164) adds
the weapon scope (the bow string), the shots, and the danger areas.

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
| `Creatures/SwcEffects` | Plain C++: the flight of a shot, and the phases of a danger area (stage P2.9). |
| `Creatures/SwcEffectActor` | The actor of a shot or a danger area (stage P2.9). |
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

## The weapon scope (stage P2.9)

The controller of a creature has the scopes `Weapon` (context `WeaponContext`) and `OffHandWeapon`. A fragment whose
FragmentDef lists such a scope (`Fire_Bow`: `FullBody+Audio+VFX+Weapon`) plays on the weapon too: Mannequin takes the variant
of the animation database of the weapon row (`AnimDbPath`, for example `NPC_Damned_BowAnims.adb`) for the tag state of the
creature. The backend sends only the record of scope 0 (and the additive scope): the client plays the weapon scope itself
(the rule of Mannequin; an inference, not checked side by side with the retail client).

- The converter: a default weapon with a skeleton (`.chr` of its `.cdf`) and a database with clips for the fragments of its
  creature is an animated weapon. Its skeleton file has its bones, its first body, and its clips (manifest key
  `creature-skeleton:CHR`); a different body on the same skeleton follows its pose. Its animation set has the variants of the
  weapon database. In the world: 22 animated weapons (bows; one sword) on 2 weapon skeletons (`wep_ran2h_bowa.chr`: 19 clips,
  the bow string; `lh_weapon_skel.chr`: 3 clips). The other weapons stay static meshes.
- The client: an animated weapon is a skeletal mesh on the socket of its creature with its own animation instance. Each
  fragment of scope 0 plays its variant on the weapon at the same time and playback scale; a fragment with no variant stops
  the weapon animation (the rest pose). The log line of the action has ", the weapon scope on N weapon(s)".
- Check (development run, `SWC_WORK/evidence/P2.9/archer3.mp4`, `tests/creatures/archer.txt`): a Moonshadow Archer (skeleton)
  and a Vulkgard Archer play `FireBow` and `Archer_Fast_Ranged_Attack` 3 m from the character (the route `action`); the bow
  bends and the string moves with the draw and the release.

## The shots and the danger areas (stage P2.9)

The shots and the areas of the creatures are entities of their own in the state bundles (protocol notes 4.25, 4.37, 4.39;
backend stages 3.4f-A to D). The entity table reads their states; `ASwcEffectActor` shows them on the render clock of the
creatures (the server time less 0.25 s).

| Record | States | The client |
| --- | --- | --- |
| A shot (`basic_ai_arrow`, `arcane_ai_arrow`, `mage_projectile_slow`, ...) | metadata (the projectile slice); Projectile (type 16): the launch (field 0 the anchor, 2 the velocity as half floats, 4 the time anchor, 10 the owner, 13 the ammunition key), then the collision (flags bit 0: field 0 the contact place, 4 its time) | flies from the anchor with the velocity and the acceleration of the slice (`m_accel`, not the gravity of the profile); holds at the contact; the stop of the record removes it |
| A strike (`undead_shaman_lightning_strike`) | metadata; Spell (type 2912: the key of the spell row, the place, the rotation, the caster, the spawn time, the radius scale); DelayedEvent (type 3139) for each event slot | the danger area of the first detection volume of the slice |
| A status area (`mage_ranged_aoe`, the mushroom) | metadata; PositionInTheWorld (type 13); DelayedEvent | the same; its start comes from the state of a delayed event (the time that is left, less the delay of the event) |

- The places of these states have the height over −100 to 2000 m (the movement state: −100 to 1000 m).
- The danger area: the shape of the first detection volume of the spell slice (a sphere, a cylinder, or no shape: the shape
  of the spell row, `ShapeTypes` and `Radius`, for example the strike: 2.5 m). Before the volume is active it is a warning
  (orange); while it is active it is the danger (red); after its deactivation it is gone. The times are the delays of the
  events of the slice (the strike: the warning from 0 s, the danger at 0.9 s, the deactivation at 0.95 s), or the states of
  the delayed events (the time that is left; a completed event is in the past; a held activation, which the server sends for
  an activation that passed, is in the past too: a late session shows no bolt). A danger shows at least 0.3 s (a value of
  this client). The stop of the record removes the area (the strike: 5 s after its start).
- The data: `tools/convert-creatures.sh effects` (also at the end of `convert`) writes `SWC_CONTENT/creatures/effects.json`:
  the projectile slices (108) and the spell slices (1,058) of the backend data by the key of their metadata state (the asset
  catalog of the game), and the spell rows (1,777) by the CRC-32 of their lower-case SpellID.
- The look is simple and readable (the effects of the game, their particles and their sounds, are for a later stage): a shot
  is a thin shaft along its flight, an area is a flat disc on the ground (an engine material with the parameter Color).
- The log lines: `effect N: a shot of creature M NAME (SLICE), launch X Y Z, velocity ...`, `the shot holds at the contact`,
  `an area SPELL of creature M NAME (SLICE), radius R m`, `the warning of the area ... at 0.00 s`, `the danger of the area at
  0.92 s`, `the end of the area at 1.22 s`, `removed (the stop of its record)`.
- The route tool has `shoot CREATURE` and `area CREATURE` (the development routes of stages 3.4f-A and 3.4f-C): the creature
  starts its basic shot or its next area attack at the character now.

Exit check of stage P2.9 (development run, 2026-10-18, after the rebase on main a1af9e5; `SWC_WORK/evidence/P2.9/exit2.mp4`,
50 s, and the logs; the picture `exit1-strike.png` of the same check before the rebase; `tests/creatures/p29.txt`): a grey
wolf 3 m east of the character bites and lunges (melee: 12 attacks); a Vulkgard Archer 8 m east shoots 13 arrows (`FireBow`
with the weapon scope on its bow; 11 hold at the contact at the character, 2 end with no contact; each one goes at the stop of
its record); a Greedfall Grotto Shaman casts 4 lightning strikes (the route `area`): each one shows the orange warning disc of
2.5 m at the character, the red danger at 0.91 s to 0.99 s, the end at 1.21 s to 1.28 s, and the removal at the stop. The
tests: `tools/test-creatures.sh` (the shot, the strike, and the status area with the states of the backend encoders: 60
checks).

## The loot bags (stage P2.10, the creature part)

The personal loot bag of a kill (backend stage 3.5b.2; protocol notes 4.42; Soulwarden `docs/systems/loot-bags.md`) is an entity
of the world: the slice `slices/instanced_loot_container.dynamicslice` (`9844F2DE-C3B1-5BD3-98FA-8768B5445906:2`) with the
metadata, PositionInTheWorld (slot 1), Interact (type 2930, slot 5), and LootDrop (type 2027, slot 6: the rarity). The server
sends the record of a bag only to its eligible characters: each bag of the table is a bag of the local character.

- The data: `effects.json`, member `lootContainers` (from `interaction-data.json` format 3 of the backend copy): the slice, the
  root of the slice, the model (the first static mesh of the slice: `jav_prp_hangingsackssm_b.cgf`, a shared manifest entry),
  the lifetime (180 s) and the end after the last item (1.25 s), and the Interact bound (slot 5, component `2953d2fecfa764bd` of
  the entity `000000dd28f454df`, `m_interactRangeSq` 25, two options).
- The view: the model at the place of PositionInTheWorld, with no collision. The stop of the record removes it (the server ends
  a bag at its lifetime or after its last item). Not done: the hide of the model 1.5 s after the Looted entry (HideMeshDelay;
  the LootTracker entries of the own player state), and the rarity particles.
- The query for the player track (the interaction, the take, and the panel are its work):
  - `USwcCreatureSubsystem::FindNearestLootBag(From, FSwcLootBag&)`: the nearest bag whose Interact bound is enabled (the
    Interact state) and whose 3D distance from `From` (the place of the character in its input report, Unreal cm) is not more
    than the range of the bound (the check of the server: the squared distance against `m_interactRangeSq`).
  - `GetLootBags(From, TArray<FSwcLootBag>&)`: each bag with its distance.
  - `FSwcLootBag`: the interest number, the actor id and G of the bag (the reference of the interaction request), the place
    (Unreal and engine space), the rarity, the slot of the Interact state, the facet target of the bound ((entity XOR root XOR
    G) + component: the rule of a server child, backend `Interactions::spawn`), the range, the Enabled mark, and the options (the
    index of the option in the options table of `interaction-data.json`, 80 and 30, not a CRC-32 id; and the instance id of the
    option entity: entity XOR root XOR G).
- The library of the creatures loads at the start of the connection (a bag or a shot can come before the first creature).
- Development aids: `-SwcLootProbe` writes the result of `FindNearestLootBag` when it changes (each 1 s) and draws the bounds of
  each bag (green); `-SwcLootModel` puts the model of the bag 2 m in front of the character one time. The route tool has
  `loot on|off` (each kill rolls loot; the roll can still give no item: then no bag) and `bags` (the bags of the character).
- Check (development run, 2026-10-18; `SWC_WORK/evidence/P2.10/lootbag4.mp4` and its logs, `tests/creatures/lootbag.txt`): the
  character kills three wolves; three bags show at the places of the wolves 0.2 s to 0.4 s after the kills (the items of the
  backend log: a void gauntlet, medium feet, heavy feet; rarity 1), the query finds each one in range (0.7 m to 3.7 m from the
  character, the target `75a9224f0fa76523` for the first one, as the rule of the backend gives), and the stop removes each one
  180 s after its kill. The corpse of a wolf covers the bag at its place; `lootmodel.mp4` shows the model alone.
  `tools/test-creatures.sh`: the record of a bag with the backend encoders (14 checks).

## Forced motion, statuses, the lock point, and the spells of the players (stage P3.5, the creature part)

Linear AST-173. The facts of the backend: Soulwarden `docs/systems/forced-movement.md`, `status-effects.md`, `spells.md`, and the
protocol notes 4.22, 4.25, 4.26.

- **Forced motion** (the pull, the push; backend stage 3.3b-S4): the server sends no flag and no special record, only more
  movement states (a state each 50 ms while the force moves the creature; a walk has a state each 200 ms) with the record of the
  reaction that plays (the same start count in each state). The retail client does not move a server creature by the impact of a
  hit; it shows the slide from the places of the server. The client interpolates the states; after a pause a step starts one step
  of the motion after it before its state (50 ms for a forced motion, at most 200 ms), not always 200 ms.
- **The reactions**: records of scope 0 (the action `Reaction` with the fragment of the reaction table, `r_R2_F` front, `r_R2_B`
  back, `r_R2_L` left), the knockdown (`r_R4_F`, 2 s) and the stagger (`r_R2_F`, 1 s) of `Creatures::knock`, and the additive
  reaction of its own scope. The vortex pull has no reaction record.
- **The statuses of a creature**: the StatusEffects state (type 4236), the compact map of group 3 field 1 (the rows with
  ShowInNameplates: the status index, the stacks, the potency 1; a larger stamp replaces the list). The name plate shows a line
  of their names ("Stunned", "Rooted", "Slowed 20%", or the row id; a simple form: the icons of the rows are for the UI track).
  The data: `effects.json` member `statusRows` (1,258 rows of `ability-data.json`). The server shows the effects on the movement
  itself: a stun is the action `Stun` (`r_Stun_Loop`) until its end, a root has no new places, a slow has slower steps.
- **The lock point** of the camera: the CameraLockTargetComponent of the slice (type `b7f335ff-…`; the backend does not keep it;
  `slices.json` member `lock` for each of the 503 slices): `m_Offset` (engine m, in the frame of the creature), the joint of the
  lock icon and its vertical offset, and the pitch limits of the observer of its client facet. `ASwcCreature::GetLockPoint`: the
  feet and the offset (with the scale of the creature) when the offset is not zero (the wolf: 1.0 m), else the joint (the turkey:
  `bind_spine`), else 1.0 m above the feet. The combination of the offset and the joint in the retail client is not known (an
  inference). The read path: `FSwcCreatureTarget::LockPoint` of `GetTargets`, and `GetLockPoint(Interest, FSwcLockPoint&)` (the
  point and the pitch limits). `-SwcLockPoints` draws each lock point (magenta).
- **The spells of the players** (backend stages 3.3b-S2a and S2b): the vortex of Gravity Well (`greataxe_gravitywell_vortex`: a
  sphere of 4 m, active from 0 s to 3.1 s, the stop at 3.5 s), the Pylon (`ice_pylon`: the Spell state at slot 4, health), the trap
  (`trap_bear`: PositionInTheWorld at slot 4, health), the Beacon (`spell_life_staff_beacon`: field 6 of the Spell state is the
  entity that it follows). The table reads field 6 (the actor id and G; none: the zero actor and `00000000FFFFFFFF`). The view:
  a spell whose caster is not a creature of the table (a spell with no Spell state: by the folder of its slice,
  `slices/items/weapons/`) is blue (before its volume is active) and light blue (while it is active), not orange and red; a
  spell with health or with a volume wider than 8 m (the Pylon: its range of 20 m) is a pillar at its place; the Beacon is at the
  shown place of the entity that it follows (a creature, or the local player). A disc lies on the ground (a line of 1 m up and down
  to the static world: the place of the server can be some cm under the surface).
- The route tool: `effect CREATURE STATUS [SECONDS]` (a status row, `knockdown`, `stagger`), `mastery NAME WEAPON LEVEL`, `node NAME
  WEAPON NODE...`, `bar NAME WEAPON SLOT ABILITY`, `place NAME [DX DY [DZ]]` (a remote test character into the world), `cast NAME
  PLACE [--yaw DEGREES]`, and `heal [NAME]`, `ignore on|off [NAME]` for a remote character. A node of row N of a tree needs a point
  in row N−1 of the same tree (backend `Skills::node_condition`): Gravity Well (Mauler tree, row 4) needs Whirlwind, a row-1
  passive, Maelstrom, and a row-3 passive first. A character with no client throws the projectile of Gravity Well to the first
  creature on its line (or 0 m).

Exit check of the creature part of P3.5 (development run, 2026-10-18; `SWC_WORK/evidence/P3.5/exit2.mp4`, 34 s,
`exit2-forced-motion.png`, and the logs; `tests/creatures/vortex.txt`): the remote test character Axewarden (a great axe) casts
Gravity Well 4 m east of the character; the projectile lands at the first wolf 3 m away. The vortex (a spell entity, the blue disc
of 4 m) roots the three wolves ("Rooted" at their plates) and pulls two of them 2.7 m to its center in 0.41 s; the burst at 3.0 s
pushes each one 0.5 m in 0.11 s with the reaction `r_R2_F`; the stop removes the vortex at 3.5 s. Then the route `effect`: a
knockdown (`r_R4_F`), a stagger (`r_R2_F`), and the stun of Shockwave ("Stunned", the action `Stun`, and no status at its end after
4 s). The magenta spheres are the lock points. The tests: `tools/test-creatures.sh` (the forced motion: six states of 50 ms with
one reaction record; the status map: one status, three stacks, a smaller stamp, an empty list; the vortex; the Beacon
attachment: 93, 26, and 94 checks).

## The load of the assets (async)

A load of an asset that is not in memory on the game thread waits for all async loads (`FlushAsyncLoading`): with the World
Partition streaming after a travel that was 489 packages and a frame of 5.5 s at the spawn of a Corrupted Lumberjack (a run of
the player track). The creature view loads with no wait:

1. The spawn: the actor follows the record and its movement, hidden. The animation sets of the slice and of its animated weapons
   that the library does not have are read on a worker thread (`FSwcCreatureLibrary::ParseAnimations`; a set takes up to 0.15 s).
2. The first async request (`FStreamableManager`, high priority): the meshes (the slice, its variants, its weapons) and the clips
   of the fragments Idle and Move. Then the body is made from the assets in memory (`FindObject`) and shows.
3. The second async request: the other clips of the sets. The animation instances find them when they come (`RefreshClips`); a
   clip that is not there yet does not play.
4. A clip that Interchange named STEM + "Null" + NAME: a second request for its second form, when the package is on the disk.

The common assets (the engine cylinder and the translucent material of the shots and the areas, the loot container meshes) load
async at the load of the library; a bag or an effect actor waits for them. The log has the work of each spawn on the game thread
("spawn work ... ms"), the time of the assets ("the assets after ... s"), and the longest frame in the 3 s after a spawn.

Check (development run, 2026-10-18; `asyncspawn`: a travel of 1 km, then three creature kinds that the run never loaded, among
them the Corrupted Lumberjack, while the cells stream): the spawn work 0.1 ms at most; the bodies after 0.25 s to 0.43 s; the
longest frame in the 3 s after the spawns 39 ms; no `FlushAsyncLoading` after the world entry.

## The read path of the other tracks

- `USwcCreatureSubsystem::GetTargets(TArray<FSwcCreatureTarget>&)`: each creature of the table with an actor: its interest
  number, its feet (Unreal cm, the shown place after the interpolation), and the dead mark. The target lock of the camera
  (stage 1.8, docs/CAMERA.md) can take its candidates from it in place of the positions of the snapshot.
- `USwcCreatureSubsystem::GetTable()`: the entity table (the records of the server, with no delay).
- `USwcCreatureSubsystem::FindMeleeContacts(const FSwcMeleeShape&, TArray<FSwcMeleeContact>&)` and `QueryMeleeContacts`: the
  melee contacts of a damage shape (above).
- `USwcCreatureSubsystem::FindNearestLootBag` and `GetLootBags`: the loot bags of the local character (above).
- `USwcCreatureSubsystem::GetLockPoint(Interest, FSwcLockPoint&)` and `FSwcCreatureTarget::LockPoint`: the lock point of the camera
  (above).

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
tools/test-creatures.sh                     # the entity table (93), the contact item and the damage number (26), the shots, areas, bags, spells (94)
export SWC_DEV_DATA=~/Games/soulwarden-client/dev-data-creatures SWC_AGENT_DESKTOP=soulwarden-client-creatures
tools/creature-check.sh creatures-world 80 tests/creatures/fight.txt tests/drive/creatures.txt
# Stage P2.8: the melee contacts with the sword character of docs/GEAR.md, on the street west of the start place.
export SWC_STAGE=P2.8 SWC_DEV_CHARACTER=Swordwarden SWC_DEV_PLACE=8872:4250:138.25
tools/creature-check.sh exit8 50 tests/creatures/p28.txt tests/drive/p28.txt -- -SwcFarContact
```

`tools/creature-check.sh` starts a window run with the development routes (`SWC_SESSION_BACKEND_ARGS=--dev-commands`), runs
the script of `tools/lib/dev_world.py` (`tools/creature-world.sh`: spawn, kill, health, action, heal, level, ignore), plays the
drive script, and records the headless output of the agent desktop (`wf-recorder`). The video and the logs go to
`SWC_WORK/evidence/STAGE` (`SWC_STAGE`, default 1.9). With a drive script, the routes go on the clock of the drive (its lines
`drive: SECONDS s` in the client log; a drive line `SECONDS tick` is a clock mark that does nothing): the drive counts the game
time, which is slower than the wall clock while the world loads. `SWC_DEV_CHARACTER` is the character of the run and of the
routes, and `SWC_DEV_PLACE` (X:Y:Z, engine m) its place at the start of the run (the swings move the character, and the
database keeps its place).

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

Exit check of stage P2.8 (development run, 2026-10-18, after the rebase on stage P2.5; `SWC_WORK/evidence/P2.8/exit8.mp4`,
50 s, with its client log, backend log, and route log). The character Swordwarden (a sword and a shield) faces east on the
street; the camera is behind it, 25 degrees down and 6 m away. Each grey wolf comes 2 m in front of the character and ignores
it (`ignore on`):

| Drive time | Event | Client | Backend |
| --- | --- | --- | --- |
| 7 s | a hit from behind | contact Body; damage number 142 (critical hit, larger and yellow); `Reaction` r_R2_B | accepted; reaction back |
| 11 s | the leave of the interest | the server removes the wolf: a stop; `creature 34: removed` | |
| 12 s | a new entry | a new wolf on the same interest number 34 (a new serial) | |
| 15 s | a hit at the side | contact Body; damage number 109; `Reaction` r_R2_L | accepted; reaction left |
| 21 s | the kill (health 100 before it) | contact Leg_F_L; damage number 109 with the flag 0x0008; `Death` r_Death; no body capsule | accepted |
| 25 s | a miss | no contact (the dead wolf has none) | |
| 31 s | a light chain from the front | contacts Body, Head, Body; damage numbers 109, 109, 149 (critical); `Reaction` r_R2_F three times | accepted |
| 41 s | the range refusal | `-SwcFarContact`: a development contact with a wolf at 6.5 m | refused: "too far" |

The backend counts: 8 attack inputs, 7 contacts, 6 accepted, 1 refused ("too far"); position reports 1,399 accepted, 0
refused. The additive reaction of scope 4 (`Reaction_Additive`, r_R0_F) plays in the runs `melee3` and `exit4`. The codec
tests: `tools/test-creatures.sh` (63 and 26 checks), with the bytes of the backend.

## Differences and open points

- Not shown yet: the procedural clips (sounds, effects, the death material, the dissolve), the effects of the game for the shots
  and the areas (their particles, trails, decals, and sounds: the client shows a simple shaft and a disc), the cloth and the
  bound props of a `CA_BONE` binding, the look poses.
- The melee contact: before 9557c3d the player side keyed one contact by its target and its facet, and a second volume of the
  same strike gave a refused contact ("the same attack", or "dead" after a kill). Now the key is the target (one contact for
  each target for each strike).
- The damage number is a simple number (its rise, its time, its colors are values of this client); the original is the
  damage number canvas of the game (later, through the UI runtime).
- The body capsule that blocks the player is an inference of the orchestrator (in the retail game the creatures block the
  players); the side-by-side check with the retail client is open. The wall line of a contact starts 1 m above the feet of
  the attacker (a value of this client).
- A creature has no reaction that the client starts: each reaction is a record of the server.
- The material precedence, the variant ties (tag priority), the option byte, and the remote motion parameter formula are
  open points of the research (AST-154 O1 to O3, R3 O1, O2, O5). The interpolation delay (0.25 s) is a value of this client.
- The name plate is a simple plate on the canvas of the viewport; the original is the `AiMarker` template (the UI track).
  The 50 m limit of the crosshair rule and the 3 s of the plate of a dead creature are inferences.
- A creature body has the materials of the default glTF pipeline (its own textures), not the shared materials of stage 1.2.
- The side-by-side check with the retail client is open (the feel of the movement, the attacks, and the deaths).
