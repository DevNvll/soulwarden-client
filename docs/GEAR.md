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
`/SwcGameContent/Player/Weapons`. A weapon is one static mesh of its skins in the bind pose of its skeleton (no weapon
clip plays yet).

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
- The logical state changes at once. Each part goes to its new place at the marker time of its action (a timer):

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
  old one. The choice of the `From_` tag (the tag that names the most tags of the weapon; a tie goes to the more
  specific tag) is an inference.
- The inactive weapons stay in their sheath places; a weapon with `HideMainWeaponMeshWhileSheathed` is hidden there; an
  accessory with `SheathMode` `ALWAYS_VISIBLE` stays visible.

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

## Open points

- The draw and sheathe body clips (`rswrd_idle_draw_v1_ub`, ...) and the stances of the weapons do not play: a drawn
  weapon is in the hand of the unarmed pose. The weapon clips (the bow string, the flail chain, the moving parts of the
  firearms) do not play.
- The visibility of the inactive weapons, the accessory fallback of a bow with no accessory row, the nocked arrow, the
  side of an asymmetric gauntlet, and the gauntlet body replacement (forearm skins) are open (gear.md, open points).
- The dyes, the eye color, the face marks, the scars, and the tattoos need material parameters and mask textures
  (gear.md "Dyes", "Eyes, face marks, scars, and tattoos"): not in this stage.
- `ShowBaseModelOnly` and `ForceHideForearms` are not applied.
- The side-by-side check with the retail client is open.
