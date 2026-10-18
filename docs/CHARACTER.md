# The character of the player

The client builds the character of a player as the retail client does: one skeleton for each gender and a set of
skin attachments (the modular body of `player_male.cdf` and `player_female.cdf`). The inputs are the appearance state
(12 index bytes) and the paperdoll state (the equipment) of the own entity (`docs/NETWORK.md`). The sources of the rules
are the Soulwarden research files `appearance.md`, `character-creation.md`, `menu-equipment.md`, and `equipment.md`
(`~/Games/soulwarden/work/research/`, read only).

## The pipeline

```text
game copy --tools/convert-player.sh tables--> content/player/looks.json   (databases, armor rows; game paths only)
          --tools/convert-player.sh locomotion--> content/player/locomotion.json   (fragments, blend spaces, clips)
          --tools/convert-player.sh base----> content/player/base/<gender>.gltf   (skeleton, base mesh, the clips)
client (a run) --writes--> SWC_WORK/player/wanted.json   (the parts that a character needs and that are not imported)
          --tools/convert-player.sh wanted--> content/player/parts/*.gltf  (one skin with one material and the skeleton)
          --tools/import-player.sh----------> /SwcGameContent/Player/... and content/player/imported.json
```

- The player assets go to the shared content plugin of the imported content (`/SwcGameContent/Player`,
  `tools/content-link.sh`): the world level and the client load the same content. `tools/import-player.sh` waits for
  the import lock of `tools/import.sh` (one import at a time).
- A part is one skin attachment (`.skin`) with one material (`.mtl`) and the full skeleton of its gender. The key of a
  part is `skin|material`: a body part with a different skin tone is a different part (the meshes are small).
- `tools/import-player.sh` imports a base with the default glTF pipeline (skeleton, skeletal mesh, physics asset, clip),
  and each part with a copy of the glTF asset pipeline that has the skeleton of the base of its gender
  (`/SwcGameContent/Player/Pipelines/PartAssets_<Gender>`): no new skeleton, no physics asset, no clip.
- The player conversion has its own manifest (`content/player/manifest.json`) and its own import script
  (`Scripts/import_player.py`): the shared import of the world content does not import a part with no skeleton.
- The parts of a character come on demand. Each part of the databases and of the 8,928 armor rows is too much to
  convert at one time (for example about 2,000 facial hair skins for each gender). After the first world entry of a
  new character, run `tools/convert-player.sh wanted` and `tools/import-player.sh`; the next start shows the full body.
  A part with no mesh is not drawn, and the log names it.
- Times of this stage: tables 2 s, base 2 s, 13 parts 22 s, their import 15 s.

## The data

| File | Content |
| --- | --- |
| `<gender>.charcreatedb` | The races (the head skin, the skin tone database, the hair database, the facial hair database, the default tone), the default appearance data (the body slots and their naked skins). |
| `<race>.skintonedb` | For each skin tone: the head material and the body material (a different diffuse texture for each tone). |
| `<gender>.hairstyledb`, `<race>_facial.hairstyledb` | For each style: the skins of its variants (0 base, 1 front, 2 back, 3 brim, 4 hood) and its material; the hair colors (a color). |
| `javelindata_itemappearancedefinitions.datasheet` | `ArmorAppearanceDefinitions`: for each appearance row `Skin1`, `Material1`, `IsSkin1`, `Skin2`, `Material2`, `IsSkin2`, `HideHair`, `HideFacialHair`, `HideHead`, `HideLegs`, `HairChop`, `AppearanceCDF`. |
| `SWC_GAMEDATA_DIR/item-catalog.json` | The item list of the wire of the backend copy (the item with index N is entry N - 1) with `armorAppearanceM` and `armorAppearanceF` of each item (format 5). |

The field ids of the object streams are the CRC-32 of the lower-case field names (the constants of
`converter/swc_convert/player.py` name the meaning of each one).

## The rules of the look (`Character/SwcPlayerLooks`)

1. Gender = appearance byte 0 (0 male, 1 female). Race = entry byte 1 of the races of the gender. Each other byte is
   an index into its list too. An index that a list does not have gives entry 0 (the retail client takes the first
   entry for a name that is not in its database).
2. The skin tone (byte 2) is an entry of the skin tone database of the race: its body material goes to each naked body
   part, its head material to the head of the race (slot `head1`).
3. The default appearance data of the gender gives the naked body parts of the visible slots (`shirt1`, `gloves1`,
   `pants1`, `boots1`).
4. The hair style (byte 3) and the hair color (byte 5) come from the hair database of the race; the facial hair
   (byte 4) and its color (byte 6) from the facial hair database of the race. The variant 0 of a style is the default;
   the color is a tint of the base color of the hair material (`BaseColorFactor` of a material instance at run time).
5. The equipment: each item of the paperdoll slots 0 to 4 (visual slots `hat`, `shirt`, `gloves`, `pants`, `boots`)
   gives its armor appearance row (`armorAppearanceM` for a male body, `armorAppearanceF` for a female body).
   `Skin1` goes to the slot `<visual>1`, `Skin2` to `<visual>2`; a skin with `IsSkin1` or `IsSkin2` has the body
   material of the skin tone, else `Material1` (or `Material2`). The row replaces the naked part of the slot.
6. The head armor (slot 0): `HideHair` removes the hair, else `HairChop` selects the hair variant (`HELMFRONT` 1,
   `HELMBACK` 2, `BRIM` 3, `HOOD` 4; each other value 0; this map is an inference from the variant names);
   `HideFacialHair` removes the facial hair; `HideHead` removes the head.

Not applied yet (a warning line in the log for each one): `HideLegs` (the rule is not known), the cloth parts of an
`AppearanceCDF` (2,205 rows; for example the cape of the Ranger and the skirt of the Magus are `.cloth` files), the eye
color, the face mark, the scar, the tattoo (the layers of the head material), the dye of an item, the weapons.

## The character in Unreal (`Character/SwcPlayerCharacter`)

- `ASwcPlayerCharacter` is an `ACharacter`. Its mesh is the base of the gender: the leader. It has the skeleton and the
  animation (`USwcPlayerAnimInstance` with the locomotion clips of the base; with no locomotion data only the idle clip
  `unarmed_idle`), and it is not drawn (it computes its pose each frame). [MOVEMENT.md](MOVEMENT.md) has the movement
  and the animation.
- Each part is one more `USkeletalMeshComponent` with `SetLeaderPoseComponent(leader)`.
- The weapons, the shield, and the quiver are static meshes on bone attachments of the leader; the cloth parts of an
  armor (capes, skirts) are parts. [GEAR.md](GEAR.md) has the rules, [ACTIONS.md](ACTIONS.md) the actions of the weapons.
- The mesh has a yaw of +90 degrees on the actor: the imported body then faces the forward vector of the actor (the
  facing of the movement state through the space map of `Net/SwcSpace.h`, proved by stage 1.1).
- The capsule of the player slice: radius 0.41 m, half height 0.81 m (stage 1.7).
- The subsystem builds the body again at each change of the appearance state or of the paperdoll state.
- `-SwcView=front` puts the camera 3 m in front of the character at 1.5 m (the pictures of this stage).

## The spawn

The spawn frame reads no table and loads no asset at once:

1. At the character list a worker thread reads the player tables: `looks.json`, `imported.json`, `weapons.json`, the
   item catalog of the backend copy, `locomotion.json` of each gender, and `actions.json` (about 1.2 s to 1.8 s).
2. At the in-game phase (the own entity is there, with its appearance and its paperdoll), the subsystem resolves the
   look and starts an async load of its assets: the base mesh, the parts, the gear, the 49 movement clips, and the
   clips of the actions of the gender (314 assets for `Swordwarden`). While this load runs, the loading screen shows,
   and the async loader of the engine has 50 ms in each frame (5 ms is the default of the engine); after it, the
   default again.
3. The spawn waits for the cells of the place and for these assets (30 s at most for the assets; after that time the
   rest loads at once). The handle of the load keeps the assets in memory.

| Measure (development runs, 2026-10-18) | Before | After |
| --- | --- | --- |
| The spawn frame (the test floor) | 2.3 s (the tables 0.6 s, the look and the actions data 0.6 s, the 739 action clips 1.1 s) | 0.013 s of work, the longest frame of the next 5 s 0.123 s |
| The spawn frame (the world level, 8907.75 4248.35 m) | 3.5 s (measure of the world track) | 0.024 s of work, the longest frame of the next 5 s 0.086 s |
| The spawn after the in-game phase (the test floor) | 3.0 s, then the frame of 2.3 s | 4.1 s (the cells 3.0 s, the assets 4.1 s) |
| The spawn after the in-game phase (the world level) | | 12.5 s (the cells; the assets 3.4 s) |

The log lines: `character: the player tables: ... on a worker thread`, `character: N asset(s) of the look load in the
background`, `world: the spawn: ... of work in its frame`, `world: the longest frame of the first 5 s after the spawn`.
A change of the look after the spawn (a new item) still loads its new meshes at once.

## The development characters

`tools/dev-run.sh --create SPEC` makes a character with the create request of the retail client before the client
starts (`tools/lib/dev_character.py`). The backend gives it the clothing of its archetype. The characters of this stage:

| Name | Creation names | Appearance bytes |
| --- | --- | --- |
| `Devwarden` | the seed character: the default appearance of the backend, no armor | 0 0 3 1 0 0 0 0 0 0 0 0 |
| `Rangerwarden` | Male, face07, skin03, hairstyle05m, facialhair03m, haircolor12, Archetype_Ranger | 0 7 2 5 3 11 11 0 0 0 0 0 |
| `Mysticwarden` | Female, face12, skin08, hairstyle20m, haircolor4, Archetype_Mystic | 1 11 7 20 0 3 0 0 0 0 0 0 |
