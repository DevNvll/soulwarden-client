# The gear of the character

Stage 1.6b (Linear AST-152). The character shows the weapons of its paperdoll, the shield, the quiver of a bow, and the
cloth parts of its armor (capes, skirts) with no cloth simulation. The source is the gear research
`~/Games/soulwarden-client/research/gear.md` (AST-153) and the Soulwarden research `equipment.md` and `appearance.md`.
[CHARACTER.md](CHARACTER.md) has the body and the armor.

## The data (`tools/convert-player.sh weapons`)

`SWC_CONTENT/player/weapons.json` has:

| Block | Content | Source |
| --- | --- | --- |
| `items` | For each weapon item of the master item tables: the look (the character file `MeshOverride`, its model skeleton, its skins with the overrides of the appearance row), the stats (`EquipType`, `MannequinTag`, `OffHandMannequinTag`, `AnimDbPath`, `IsShieldCompatible`, `HideMainWeaponMeshWhileSheathed`), and the accessory (the quiver of a bow). | `ItemStatsRef` → `WeaponItemDefinitions`; `WeaponAppearanceOverride` → `WeaponAppearanceDefinitions`; `WeaponAccessoryDefinitions` (its `WeaponID` names the master item) |
| `equipTypes` | For each equip type: the attachment in the hand and in the sheath, for the main hand and for the off hand, with the offsets (position m, rotation in degrees). | `sharedassets/genericassets/items/equiptypesdatabase.equipdb` |
| `attachments` | The bone attachments (`CA_BONE`) of `player_male.cdf` and `player_female.cdf`: bone, position, rotation (w, x, y, z). | the base character files |
| `timings` | For each tag set of the weapons: the draw fragment `Unsheathe_UpperBody` and the sheathe fragment `Sheathe_UpperBody` with their first clip and their markers. | `player.adb` and its weapon databases (`SubADBs`) |

Rules of the export (gear.md, "From a paperdoll entry to a weapon"):

- The appearance and the stats are two different keys. The ids compare without case (`2hGreatSwordT2` → the row
  `2hGreatswordT2`).
- `SkinOverrideN` replaces skin N of the character file and `MaterialOverrideN` its material; an unset override keeps
  the file (the bow `2hBowT2` gets its string as skin 2).
- A model on the player skeleton (the gauntlets) is a part of the body, not a rigid weapon: its skins go to the parts.

The weapon meshes come on demand, as the parts: the client writes the items with no mesh into the wanted list
(`"weapons"`), `tools/convert-player.sh wanted` converts them, and `tools/import-player.sh` imports them into
`/SwcGameContent/Player/Weapons`. A weapon with weapon clips (the bow, the flail) is a skeletal mesh with its clips; each other
weapon is one static mesh of its skins in the bind pose of its skeleton ("The weapon clips" below).

## The places

| Weapon | Equip type | In the hand | In the sheath | Offset of the equip database |
| --- | --- | --- | --- | --- |
| Sword, rapier, hatchet, flail | `RSword_LeftHolster` | `righthandattach` | `left_sword` (Hips) | none |
| Great axe, war hammer | `Hammer_Axe` | `righthandattach` | `hammer_axe` (Spine1) | none |
| Greatsword | `Greatsword` | `righthandattach` | `grtswrd_sheathed_onback` (Spine1) | none |
| Spear | `Spear` | `righthandattach` | `spear` (Spine1) | none |
| Bow, fire staff, life staff | `Bow` | `lefthandattach` | `bow` (Spine1) | none |
| Musket, blunderbuss | `Rifle` | `righthandattach` | `rifle` (Spine1) | none |
| Shield (slots 27 to 29) | `Shield`, off-hand fields | `lefthandattach` | `shieldattach` (Spine2) | sheath rotation 90° about x |
| Quiver (accessory) | `Quiver` | `quiver` (Spine2) | `quiver` | none |

The transform of a part relative to its bone: the socket of the base character file of the gender, then the offset
of the equip database (rotation about x, then y, then z). Both go through the space map of stage 1.1: position
(x, −y, z) × 100, quaternion (w, x, y, z) → (−x, y, −z, w).

## The weapon state

- The paperdoll state (group 2): `activeMap` selects the active weapon pair (slots 24 to 26 and the off hand 27 to 29),
  `sheatheMap` has the sheathe bit of each weapon. At the world entry the client takes them: the active weapon in its
  sheath or in the hand. The backend has no draw and no sheathe (protocol notes 4.11): each weapon has its sheathe bit.
- The keys: 1 and 2 draw the weapon of slot 24 and 25 (the input report has their bits 9, 40 and 355, 306 for one
  entry); the sheathe key (`~`) sheathes or draws the active weapon (its bit is not known: none). The client draws and
  sheathes itself, as the retail client.
- The logical state changes at once. Each part goes to its new place at the mark of its action: the draw and the sheathe
  fragments play on the UpperBody channel and their marks move the parts (stage P2.7, [ACTIONS.md](ACTIONS.md)):

| Weapon tags | Draw: `unsheathe_weapon` | Sheathe: `sheathe_weapon` (`From_` tag) |
| --- | ---: | ---: |
| `1H_Melee,Sword` | 0.133 s (`player_sword.adb`) | 0.400 s (`From_1H_Melee_Sword`) |
| `2H_Melee,GreatSword` | 0.533 s (`player_greatsword.adb`) | 0.833 s (`From_Greatsword`) |
| `2H_Melee,GreatAxe` | 0.367 s (`player_greataxe.adb`) | 0.733 s (`From_2H_Melee`) |
| `Bow,LongBow` | 0.200 s (`player_bow.adb`) | 0.533 s (`From_Bow`) |
| `MagicStaff,Fire` | 0.200 s (`player_magicstaff_fire.adb`) | 0.733 s (`From_MagicStaff`) |
| `2H_Ranged,Rifle` | 0.167 s (`player_2h_ranged.adb`) | 0.733 s (`From_2H_Ranged`) |

  An off-hand part moves at `Unsheathe_Offhand` of the draw fragment of its main weapon (else at its
  `unsheathe_weapon`). A switch from a drawn weapon to another one draws the new one after the sheathe marker of the
  old one. A key of the other weapon switches at the step of the attack graph (at once at rest; in an attack at its
  switch mark, [ACTIONS.md](ACTIONS.md)). The choice of the `From_` tag (the tag that names the most tags of the weapon; a tie goes to the more
  specific tag) is an inference.
- The inactive weapons stay in their sheath places; a weapon with `HideMainWeaponMeshWhileSheathed` is hidden there; an
  accessory with `SheathMode` `ALWAYS_VISIBLE` stays visible.

## The weapon clips

AST-202 (research `~/Games/soulwarden-client/research/weapon-props.md`, "The rule for the client"). The bow string, the bow
limbs, and the flail chain move with clips of the weapon. These clips play on the weapon, not on the player.

The data (`tools/convert-player.sh actions`, `actions.json`):

- The controller of the player (`playermalecontrollerdefs.xml`) gives the scopes of each fragment, with the tag overrides (the
  first override whose tags the state has; the order rule is an inference). The scopes `Weapon` and `WeaponTransition` are in the
  context `WeaponContext`. This context reads the attachment database of the weapon: `AnimDbPath` of its weapon row
  (`playermaleanims_bowattach.adb`, `playermaleanims_1h_meleeattach.adb`).
- For each fragment record of the main hand (stance, dodge, block, draw, sheathe, attack graph, ranged program) that owns one of
  these scopes for its tags, the converter selects the variant of the same fragment in the attachment database: the tags of the
  weapon, of the body record, and `Stand`; the variant with the most tags (the first one of equal variants). The body record and
  the weapon record are separate: their clips, start times, speeds, and blend times differ.
- `weapons[].weaponScope`: `adb`, `records` (body record key -> `{record, scopes}`), and `clips` (the weapon clips of the
  records). The weapon clips are in `clips` with `"prop": true` (the prop folder `animations/gameplay/prop/weapons/`; the bones
  are the bones of the weapon skeletons).

| Fragment (bow, tags `Bow`) | Weapon clip | Start / speed / blend-in |
| --- | --- | --- |
| `Bow_ReadyWeapon_UpperBody` | `bow_idle_to_aim_halfdrw_prop` | 0 / 1 / 0.067 s |
| `Bow_Charge_UpperBody` | `bow_aim_halfdrw_to_fulldrw_prop` | 0 / 2 / 0.2 s |
| `Bow_Aim_UpperBody` (`ChargedAim`) | `bow_aim_halfdrw_idle_prop` (`bow_aim_fulldrw_idle_prop`), a loop | 0 / 1 / 0.5 s |
| `Bow_Shoot_UpperBody`, `Bow_Shoot_Charged` | `bow_aim_halfdrw_release_prop`, `bow_aim_fulldrw_release_prop` | 0 / 1 / 0.034 s |
| `Sheathe_UpperBody` `From_Bow` (`WeaponTransition`) | `bow_aim_halfdrw_to_idle_prop` | 0.5 / 1 / 0.1 s |
| `Unsheathe_UpperBody`; `Idle`, `Strafe` with `Aim` or `ChargedAim` | none | the aim action keeps the string pose |

The counts: the bow 48 records (24 clips), the flail 47 records (15 clips: the movement owns `Weapon` for `1H_Melee+Flail`). The
sword, the great weapons, the spear, the staffs, and the firearms have weapon records with no clip (damage clips, effects); the
musket has no weapon clip in the data, and the client does not invent one.

The mesh (`tools/convert-player.sh wanted`, `tools/import-player.sh`): a weapon whose tags have weapon clips and whose model is a
`.chr` is a skeletal glTF. Its skeleton has the bones of the model and each bone of its skins that the model does not have (the
string bone `bowStringPull2_jnt` is only in the bow skins; the clips animate it); the bind pose of a bone is the bind of the first
skin that has it. All skins are one mesh (the string skin of `SkinOverride2` is a second section). The clips are animations of
the same file, with no root lock. The import makes a new skeleton for each weapon (no automatic skeleton: never the player
skeleton), no physics asset, and gives the materials their usage flags ("The weapon materials" below). A static mesh of an
earlier import of the weapon is deleted first. A weapon that was converted as a static mesh before becomes a skeletal mesh when
`tools/convert-player.sh wanted` gets its item again (the manifest has the flag `skeletal`).

The client:

- `FSwcActions` reads `weaponScope` into `FSwcWeaponActions::WeaponRecords` (body fragment -> weapon record).
- `USwcPlayerAnimInstance` writes the weapon visits of each frame: for each fragment of the stance and of the two action
  channels whose weapon record exists, the record with the time and the weight of the body fragment. Thus the weapon clip starts
  at the same fragment visit and runs on the same clock (`clipTime = start + elapsed x speed`; a loop wraps). A fragment covers
  the fragments before it on its channel only after the blend-in of its body record and of its weapon record (the half draw loop
  blends in for 0.5 s).
- The weapon is a `USkeletalMeshComponent` with `USwcWeaponAnimInstance`. Its update comes after the update of the base (a tick
  prerequisite). The weapon of the slot of the stance follows the visits of the player animation: the clip of each item of each
  layer, with its blend-in from the item before it, overrides the animated bones of the clip over the bind pose. Other weapons
  stay in their bind pose. The hand and sheath places do not change.

The check: `tools/test-actions.sh` (`weapon_scope_tests`: the bow rows above, the sheathe scope, no record for the draw and the
aim walk, the string bone in the full hold clip, the same clip time for body and weapon at 0.5 s of the charge, the flail idle and
first attack, no clip for the sword and the musket), and the side-by-side video with the retail client.

The video check (development run, 2026-10-20; `Devwarden` with the preset `bow-boltcaster`, the replay `tests/drive/p3g-bow.txt` of
the set 6 take `player-bow`; `SWC_STAGE=AST-202 tools/creature-check.sh ast202-bow 32 tests/drive/p3g-bow-routes.txt
tests/drive/p3g-bow.txt`). Evidence in `~/Games/soulwarden-client/evidence/AST-202/`: `sbs-ast202-bow.mp4` (retail and client from
the draw key to the end of the second release), `frames/zoom-sheet.png`, `frames/release-sheet.png`. The client log has "weapon:
Artifact_Set1_2hBow (slot 24) is a skeletal mesh with 24 of 24 weapon clips". In both clients the string goes to the hand in the
ready fragment (the half draw), stays at the hand in the charge and the full hold, and goes back to the bow in the first frames of
the release. The opinion of the user is open.

## The weapon materials (AST-206)

The glTF pipeline of `tools/import-player.sh` makes each material of a weapon or a tool an instance of the Interchange master
`/InterchangeAssets/gltf/M_Default`. That master is a content of the installed engine, and the editor does not save it. Thus its
usage flags cannot be set: the weapon meshes are Nanite meshes (the bow and the flail are also skeletal meshes), the client
compiled the shaders of these instances at each start, and the client log had "missing usage flag Nanite! Default Material will
be used in game" for each weapon (`tools/fix-usage-flags.sh` found 22 of them; the spear of the development character too).

The rule (`import_player.py`, `weapon_usage`): each material instance of a weapon whose base material is outside the content plugin
gets the project copy of that base, `/SwcGameContent/Player/Materials/NAME_Weapon` (now only `M_Default_Weapon`), with the usage
flags of `WEAPON_USAGES` (Nanite, skeletal mesh). The copy has the same graph and parameters, and the instances keep their
parameters: the look does not change. A base material in the content plugin gets the flags. Each new import of a weapon gets this
pass; a weapon with no mark `_usage` (`WEAPON_USAGE`) in `imported.json` gets it one time with no new import.

The check (2026-10-20): `tools/import-player.sh` gave the usage pass to the 25 weapons of the manifest and made
`M_Default_Weapon`. The offscreen picture of all weapons (`work/ast-206/weapon_sheet.py`) before and after the change is the same
to the pixel (`~/Games/soulwarden-client/evidence/AST-206/before/weapons-sheet.png` and `after/weapons-sheet.png`); after the change
all 25 weapons have the base `M_Default_Weapon` with both flags (`after/weapons.json`). The old static mesh of the bow `wep_ran2h_bowt2`
(the manifest key with `|03e69eb3`, before the skeletal bow) has no material (the grid material of the engine): an open point.
The client log of a world entry with the preset `bow-boltcaster` (`ast206-gear-c1`: the bow and the rapier) has no line "missing
usage flag" (before: the bow and the rapier had one each in each run).

## The cloth parts

An armor row with its own character file (`AppearanceCDF`) can have `CA_CLOTH` attachments: a cape or a skirt. The
`.cloth` file names its render skin; the client draws that skin as a part on the player skeleton with the material of
the attachment, with no cloth simulation (gear.md "Cloth without a solver"). The other attachments of that character
file do not replace the selected armor. A weighted bone of the skin that the base skeleton does not have (the
simulation bones `bind_lower_body_sim_*` of the Ranger cape) gives its weight to its nearest parent in the skin file
(`Hips`): the chain stays in its bind pose. 1,842 armor rows have cloth parts, 1,524 of them a cape.

`HideLegs` of the chest hides the two pants parts (`pants1`, `pants2`); the flag of a pants row does not hide it.

## The development characters

`tools/lib/dev_character.py` gives and equips items with the development routes of the backend
(`SWC_SESSION_BACKEND_ARGS=--dev-commands`): `gear=ITEM@SLOT+ITEM@SLOT` in the spec of `--create` (also for a character
that is there). The characters of the check:

| Name | Gear |
| --- | --- |
| `Swordwarden` (male) | `HeavyChestBT5` (a metal chest with a cape) in slot 1, `1hSwordT2` in 24, `1hShieldAT2` in 27, `2hGreatSwordT2` in 25 |
| `Mysticwarden` (female) | `MediumChestAT5` (a leather chest with a cape and a skirt) in slot 1, `2hBowT2` (with its quiver) in 24, `2hElementalStaff_FireT2` in 25 |
| `Rangerwarden` (male) | the Ranger archetype chest (the Ranger cape with the six frozen bones) and its start sword `1hSwordT2_FTUE` |

`tests/drive/gear.txt` turns the camera round the standing character: back and front views, weapon 1 in the hand,
weapon 2 in the hand, the sheathe key.

```sh
SWC_SESSION_BACKEND_ARGS=--dev-commands tools/dev-run.sh --create "name=Swordwarden,gear=HeavyChestBT5@1+1hSwordT2@24+1hShieldAT2@27+2hGreatSwordT2@25" --quit-when characters
tools/dev-run.sh --window --enter Swordwarden --quit-when world --stay 27 -- -SwcDrive=$PWD/tests/drive/gear.txt
```

## The armor of the screen (stage MVP-E, Linear AST-217)

The retail equipment screen reads PHYS and ELEM from a native StatModifiers cache of the vitals (`GetVitalsArmorModifierValueById`,
0x6833FC0 and lookup 0x5C5A900; research `~/Games/soulwarden-client/research/mvp-e-static.md` 2). Its slot filter and the order of
its additive and multiplicative stages are not proved. The decision of the orchestrator: the client shows the armor of the backend
rule, because the backend is the authority of this project and that value changes the damage:

- The item sum: for each armor item of the slots 0 to 4, round1(ArmorRatingScaleFactor x round1(GS ^ 1.2 x set scale factor))
  (`Combat::armor_of` of the backend; the item rule of the retail client 0x5BFB400; `Swc.ItemArmorRating`).
- Times max(1 + part, 0). The part of the statuses: for each status of the owner (the state 44, fed by `HostStatuses`) its
  `PhysicalArmor` or `ElementalArmor` x potency x stacks; each value goes to the first category of the status with a limit for the
  column (ArmorFortify +2.0, ArmorRend -0.7), else it is free (`Statuses::values` of the backend). The data: `gamedata/ownerstats.json`
  (`tools/convert-ui.sh gamedata`, from the backend copy `ability-data.json`).
- Not in the client (gaps): the armor parts of the perks of the gear (`Gear::values` of the backend; the gear of the current
  character has none), the attribute thresholds (CON 50, 200, 300 rows), and the absorption and the weakness of the hover lists. The
  value is the backend value, not a proved copy of the retail cache.

## Open points

- The draw and sheathe body clips and the stances of the weapons play from stage P2.7 ([ACTIONS.md](ACTIONS.md)). The
  weapon clips of the bow and the flail play from AST-202. Open: the strike shapes of the flail use the bind pose of its
  joints (`weaponJoints`), not the shown chain; the equal-tag options of a weapon record take the first one (the retail option
  rule is not known); the other bows and flails become skeletal when they are converted again; a native correction of the string
  (a hand follow) is not proved (weapon-props.md [U]).
- The visibility of the inactive weapons, the accessory fallback of a bow with no accessory row, the nocked arrow, the
  side of an asymmetric gauntlet, and the gauntlet body replacement (forearm skins) are open (gear.md, open points).
- The dyes, the eye color, the face marks, the scars, and the tattoos need material parameters and mask textures
  (gear.md "Dyes", "Eyes, face marks, scars, and tattoos"): not in this stage.
- `ShowBaseModelOnly` and `ForceHideForearms` are not applied.
- The side-by-side check with the retail client is open.
