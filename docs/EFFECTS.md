# The effects of the skills on their timeline

Linear AST-230 (MVP-L, Urgent). The request of the user (2026-10-21): "make sure implement the correct skill effects are in the
timeline". Each effect that the retail client plays for a skill plays at the same time of its animation, from the game data: the
abilities and the light, heavy, and charged attacks of the five weapons, the weapon trails, the spell entities of the abilities, the
projectile and hit effects, the creature spells, and the status effects on the body.

The rule is the study `~/Games/soulwarden-client/research/skill-effects.md` (GPT-6.1-Sol, 2026-10-21; scratch `research/skill-effects/`).
The study has four clocks: the fragment clips (the weapon and body effects), the projectiles (the flight effects), the spell slice events
(the child effects of a spell), and the status FX scripts. A hit starts a separate effect. This document follows the ordered plan of the
study (steps 1 to 12) and tells what each step did.

## The parts

| Part | Job |
| --- | --- |
| `converter/swc_convert/skill_effects.py` | The visual clips of each fragment record (into `actions.json`), the weapon effect groups of the weapon effect scripts, the helpers of the weapon files, the particle entities of the slices (into `creatures/effects.json`), and the key list `particles/skill-effects.json`. |
| `converter/swc_convert/luabc.py` `run_tables` | Runs the main function of a table script of the game (the weapon effect scripts: no loop and no branch on data) with Python values for the tables. |
| `converter/swc_convert/particle_systems.py` | The systems of the skill keys (with the fire of stage 2); the mesh particles (Geometry): the glTF of the mesh, its scale, its start angles and rotation rate, and the Illum values of its material. |
| `Scripts/particle_systems.py`, `SoulwardenClientEditor/SwcParticleBuilder.cpp` | The import of the mesh particles (`/SwcGameContent/Particles/Meshes`), the material `M_SwcParticleMesh`, the Niagara mesh renderer of the stateless emitters. |
| `Effects/SwcFragmentEffects` | The fragment effect player: the clips of the fragment of each action channel on its clock. |
| `Effects/SwcParticleEffects` | The effect interface gets `StartAttached` (a bone or a socket, follow or not) and `Keep` (KeepEmitterActive). |
| `Creatures/SwcEffectActor` | The slice effect player of the creature spells and shots. |

```sh
tools/convert-player.sh actions        # actions.json with the "effects" of each fragment, "weaponEffects", "weaponHelpers"
tools/convert-creatures.sh effects     # effects.json with the "particles" of each projectile and spell slice
tools/convert-particles.sh skills      # particles/skill-effects.json: each key, its library node or "missing", its users
tools/convert-particles.sh systems     # the systems of the fire and of the skill keys (FIRST_SLICES of the creatures)
tools/import-particles.sh              # the import (the lock; each call stops new work after SWC_IMPORT_SECONDS, default 480 s)
tools/test-converter.sh                # tests/converter/test_skill_effects.py: the clip times, the Lua table run, the Euler rule
```

## The data

- The visual clips (`effects_of`): ParticleEffect, WeaponEffects, SpawnSlice, RunFxScript, and CAGE-CastSpell of each ProcLayer, with the
  layer and clip index, the start (the sum of the Blend.ExitTime values), the end (the next procedural item of the layer; an open last
  clip ends with the fragment), the blend inputs apart (ExitTime, StartTime, Duration: Duration does not move the start [D]), the fields
  that the client reads, and all the raw parameters. The fragment records of the body database and of the weapon scope (the attachment
  databases `playermaleanims_*attach.adb`) both get them. 423 clips in 263 fragment records.
- The weapon effect groups (`weapon_scripts`): `javelindata_weaponeffects.datasheet` gives the script of each weapon effect id and element
  (Default, Fire, Arcane, Void, Lightning, Ice, Nature). The 94 scripts run with no failure. A group is its list of particle entries
  (particleName, jointName or attachmentName, positionOffset, rotationDeg, scale, followType, killOnStop, playOnOwner); the audio entries
  are dropped (no sound in the MVP). The rule of `WeaponEffectBase:StartParticle` [D]: playOnOwner plays on the character, else on the
  weapon; a jointName is a joint, an attachmentName a helper of the weapon file; the offsets count only when all three are given; the
  entry stops its old emitter first; the exit (`StopParticle`) kills with killOnStop, else it stops the emission.
- The helpers of the weapon files (`weapon_helpers`): the CA_BONE attachments of each weapon `.cdf` (vfx_Base_01, vfx_Mid_01, vfx_Tip_01,
  vfx_Melee_01 ...): the bone (master_root) and the offset. 1,399 files.
- The slice particles (`slice_particles`): the entities of a slice with a particle component: the entity name (the target of the delayed
  events), its IsRuntimeActive flag, its matrix in the slice, the effect, and its settings. 1,779 in the spell slices, 132 in the
  projectile slices.
- The key list (`skill-effects.json`): 884 keys, 868 in the libraries. The 84 keys of the five weapons all resolve. The 16 missing keys
  are of creature slices (none of the five weapons and none of the first creature spells); the client logs a missing key and plays
  nothing (no alias of a similar name [D]).

## The fragment effects (plan steps 2 and 4)

`Effects/SwcFragmentEffects` gets each action fragment of `USwcPlayerAnimInstance::SetAction` (the attack, the abilities, the dodge, the
ranged scope, the draw and the sheathe, the block, the death and the use) with its serial and its clock, and the weapon record of the
fragment (the weapon scope of the stance). The rule of the study ("Runtime order for the client"):

- A clip enters when the clock of its fragment crosses its start: one start for each serial; a late frame enters each clip that it
  crossed; a clip that ends in the same frame exits at once. The start is at the start of the blend (no wait for the blend) [I].
- A clip exits when the clock crosses its end, or when its fragment ends (a new serial, a new fragment, the end of the channel). The exit
  follows the flags of the native exit path (study "Native rules and proof", RVA 0x25774F0) [R]: KillOnExit removes the particles; else
  KeepEmitterActive false stops the emission and the particles finish; else the emitter stays active until its system ends (at most
  10 s, a value of this client). A pooled component with live particles is not taken again (its tail stays).
- A ParticleEffect clip attaches to its joint of the body (EmitterFollows: it follows the bone; else it starts at the place of the bone
  and stays). The offset is in the frame of the joint: the position (m) and the Euler angles (radians, x first, then y, then z) of the
  game through the space map of stage 1.1 (x, -y, z; the rotation axis (-x, y, -z)).
- A WeaponEffects clip (OptionOnEnter Enable) starts the entries of its group in the script of the weapon in the main hand (the weapon
  effect id of its tag set; the element Default). OptionOnExit Disable stops them at the end of the clip.
- Not done in this step: the condition of a clip (a clip with a condition is skipped and logged: the study says that a nonempty
  condition is not true), SpawnSlice, RunFxScript, CAGE-CastSpell (logged), an attachment of the body, the element of the weapon (the
  perks and gems: Default only), the weapon pose for the animated weapon helpers.
- The log: `fragment effects: FRAGMENT TYPE NAME at START (clock C, serial S)` for each entry, and one line for each reason of a skip.

## The slice effects (plan step 7, first part)

`Creatures/SwcEffectActor` plays the particle entities of a creature slice at the root of the actor with their matrix in the slice:

- An entity that is active at the start plays from the start of the spell (a shot: from its launch).
- An activate (2) or deactivate (3) delayed event that targets the entity starts or stops it at its due time on the server clock
  (`swc::creatures::event_due_us`: the remaining time of its state, the completed state, the held activation). A deactivation stops the
  emission; Kill On Deactivate also removes the particles.
- A late observer does not start an activation that is older than 0.5 s (an old burst; a value of this client).
- A hostile area has no disc and no stand-in column; a shot with a particle effect shows no shaft (docs/CREATURES.md).

## The hits (plan step 9, first part)

`tools/convert-particles.sh skills` also writes `particles/impacts.json` (`skill_effects.impact_data`):

- `tables`: ImpactTable_Characters and ImpactTable_Environment: the row key (Sword, Sword+Crit, Bow_Heavy, ...) -> the column -> the hit
  function (189 character rows).
- `functions`: the particle and decal calls of each hit function of `scripts/combatimpact/*.luac` (1,086 functions;
  `luabc.calls_of`: a linear run, the calls of both sides of a test are in the list; `luabc.assigned_functions` names them).
- `damage`: the WeaponCategory and DamageType of each row of `javelindata_damagetable.datasheet` (419 rows).
- `volumes`: the column of each hit volume of the 503 converted creature slices (Flesh, FleshHead, ...): the field 0x01803569 of the
  HitVolume component [D] (its name is not known).

The strike rows of `actions.json` give their damage row (FSwcStrike::FRow::Damage; FSwcMeleeShape::DamageRow). A contact of the character
waits for the damage message of the server (type 2071: it has the critical flag); then `Effects/SwcImpactEffects` plays the hit function:
the row is the WeaponCategory of the damage row, with "+Crit" for a critical hit when the table has that row [R: the critical suffix
first]; the column is the material of the hit volume (else Default); `SpawnParticle(key, point, a3 * N)` plays at the contact point with
the local +Z of the effect along the direction of the strike x N [I]; `SpawnParticleAtAttachment` plays at the helper of the weapon of
the attacker (the blood trail at vfx_Tip_01) and follows it. That the hit plays at the damage message (not at the contact) is [I]: the
selection needs the critical flag. Not done: SpawnDecal, the joint calls, the resist suffix, the elements, the hits of the shots.

Three rules of the sprites, for the hits [I] (a brief to the orchestrator for the retail shader):
- The Diffuse color of a material of the Particles shader tints the sprite (the blood atlas is a gray mask; its .mtl Diffuse is
  0.178, 0.0097, 0.0097). The fire of stage 2 has Diffuse 1 or no material: no change.
- AlphaClip (particle-rules.md 2 [R]): `a = saturate(min(atlas alpha - y, z) x x)`, a <= 0.01 is 0, with x = Scale, y = SourceMin,
  z = SourceWidth each from its Min at the particle alpha 0 to its Max at 1 (the lerp is [I]). The defaults give the plain fade.
- A velocity-aligned sprite with no speed (the blood spurts) aligns to its emit axis: a speed of 0.01 cm/s along the cone.

## The mesh particles (part of plan step 6)

53 emitters of the first systems are mesh particles (the swipes of the weapon groups, the rings, the bowls, the spheres). The converter
writes the glTF of the mesh (`particles/meshes/m1/`) and the record: `meshScale` (SizeX, SizeY, SizeZ: the scale of the mesh, no half
size [I]) with the age curves, `meshQuat` (InitAngles of the game in Unreal), `meshRate` (RotationRateX/Y/Z: all three change sign under
the space map) with the age curve of the Z rate, and `illum`. The builder makes a Niagara mesh renderer (the stateless modules
InitialMeshOrientation, MeshRotationRate with the rate scale, ScaleMeshSize). `M_SwcParticleMesh`: translucent, lit by the translucency
light volume, two-sided; base color = the diffuse map x Diffuse x the vertex color x the particle color; emission = Emittance RGB x
Emittance A x 0.1 x the emittance map x EmissiveScale (the unit rule of the world materials, docs/MATERIALS.md M4); opacity = the diffuse
alpha x Opacity x the vertex alpha x the particle alpha. The UV of each map uses the tiling and offset of its TexMod.

Not done: the dissolve (MaterialDissolve), the TexMod oscillators, the refraction emitters (the Distort children of the swipes: not drawn,
noted), the rotational drag. A stateless emitter simulates in the space of its component: the particles move with the emitter (as
MoveRelEmitter Yes); a world-space trail behind a moving emitter needs another path (open).

## The checks

Development runs with a copy of the user's database (`dev-data-mvpl`; the sword and the shield given by the development routes; drives
`tests/drive/mvpl-chain*.txt`, `mvpl-look.txt`). Evidence in `~/Games/soulwarden-client/evidence/mvp-l/`.

| Run | Result |
| --- | --- |
| mvpl-chain1 | The clips enter at their data times: sword_primary_1 at 0.3125 s (clock 0.3156), sword_primary_2 at 0.15 (0.1520), sword_primary_3 at 0.20 (0.2089), the Dash and thrust_1 of Attack_Heavy_Charged at 0.30 (0.3077). The swipes drew in the default material: `M_SwcParticleMesh` did not compile (a swizzle of a 3-channel pin). |
| mvpl-chain2 | The material compiles (no "Failed to compile" in the log). The shaman strike: the omen and the damage area at 0.00 s, the area stops at 0.86 s (data 0.85), the bolt at 0.91 s (data 0.90). |
| mvpl-look1 | The same steps with `swc.Effects.TimeScale 0.05` (a check value: the particles play 20 times slower, the game time does not change): the swipes are white arcs about the character (sword_primary_1 and 2: half arcs; sword_primary_3: the full circle of Spin_Fast_1); the bolt of the shaman (the arcs, the pink sparks, the tall upright strike). |

| mvpl-hit1 | The hits: Sword FleshHead (the head of the wolf): SwordFleshHead (BLOOD.Impact_Slash_Omni, BLOOD.Impact_Slash_Trail); a critical hit: Sword+Crit Default: SwordDefaultCrit (with CRIT.base_MD_cfx_addon); 0.046 to 0.070 s after the contact. |
| mvpl-hit2, mvpl-look-b | After the three sprite rules: no material compile error; the blood spurt is red (a look drive: each effect 4 m in front, 1.5 m to the side, 20 times slower; `work/mvp-l/look_drive.py`). |

Evidence: `step1-swipes-slow.mp4`, `step1-strike-slow.mp4`, `step1-chain-normal.mp4`, `step1-compare-strike.png` (retail creature-areas
s1-fresh at +1.63 and +1.74 s | client at the bolt time).

Differences to retail (first step):
- The bolt: retail shows a tall bright white and purple column with a white flash on the ground (night, a level camera); the client shows
  the arcs and the sparks, smaller and darker (noon, the camera down 30 degrees). The Light (no light renderer yet) and the Decal (no
  decal renderer yet) of the effect are not drawn; the children of the invisible Bolt parent start at the start of the effect (their
  parent trigger is not used). Check again at night with a level camera.
- The bow: the `shot` group of the default script plays the musket muzzle at the helper vfx_muzzle; the artifact bow file has no such
  helper: the client plays nothing and logs it (retail: open, study open point 5). The bow `tip` and `tip_start` groups are not in the
  default script (logged). The arrow trail: plan step 8.

## Open points

The open points of the study ("Open points" 1 to 10) stay. Of this work:
1. The conditions of the clips (HasTauntGem, the charge tiers, the upgrades): a clip with a condition does not play.
2. The element of the weapon (the perk or gem of the item): Default only.
3. The decal and light renderers, the ribbons, the refraction, the dissolve (plan step 6).
4. The world-space particles behind a moving emitter (the stateless emitters are in the local space).
5. The retail references of the sword, the great axe, and the war hammer: the Soulwarden evidence has no video of them.
