# The crafting

Stage MVP-J (Linear AST-226). The player crafts at the stations of the settlements with the original crafting screens of the game:
the canvases CraftingScreenV4 and StationProperties with their scripts (`lyshineui/crafting/*.luac`,
`lyshineui/stationproperties/stationproperties.luac`) in the Lua runtime of the UI. The client gives these scripts the native part:
the recipes, the station of the use, the checks before a request, and the request. The server owns each result. The sources are the
Soulwarden documents `docs/systems/crafting.md` and `interactions.md` (read only), the protocol notes 4.36 and 4.50 to 4.52, the
backend copy (`crafting-data.json`, `interaction-data.json`), and the evidence of the backend stages 3.7a to 3.7c
(`~/Games/soulwarden/work/evidence/crafting/`).

## The parts

| Part | Job |
| --- | --- |
| `converter/swc_convert/crafting.py`, `tools/convert-ui.sh crafting` | `SWC_CONTENT/ui/gamedata/crafting.json`, the canvases, and their textures (below). |
| `converter/swc_convert/actions.py interactions` | The station templates of the server children in `SWC_CONTENT/player/interactions.json`: the slice, the bounds, the Assembly key, the fee flag, and the name key of each station. |
| `Interact/SwcInteractor` | The focus of a station, the use of OpenCraftingOption (action 20), the open of the screen at the request, the end of the use. |
| `UI/SwcUiCrafting` | The station use of the client to the screen (`Swc.HostStation`), and the request "station end" of the screen. |
| `Lua/swc/crafting.lua` | RecipeDataManagerBus, CraftingRequestBus, CraftingCategoryDataManagerBus, and the parts of other buses that the screens read. |
| `Net/Codec/SwcRepMessages`, `SwcRepClient` | The craft request 2997, the stop 2999, the progress 2993 (`tools/test-net.sh`: the readers and the writers of the backend). |

```sh
tools/convert-player.sh interactions   # the station templates (the backend copy)
tools/convert-ui.sh crafting           # crafting.json, the canvases, the textures
tools/test-ui.sh --fixture crafting    # the fixture tests/ui/fixtures/crafting.lua
```

## The data

`crafting.json` has the recipes with bKnownByDefault (2,340 of 8,294). The retail client with this backend lists only "Learned"
recipes (the evidence 37a to 37c: the tree has the one root "Learned"), and the backend refuses a recipe that is not known by default
(its cause "recipe unlock or achievement rule"). Each recipe has the values of the backend file (the rules, and the causes
`unsupported` of the backend) and the UI columns of the recipe tables of the game (RecipeNameOverride, AdditionalFilterText,
DisplayIngredients, ...). The file also has the crafting categories (javelindata_craftingcategories), the station rows, all 617
ingredient categories, the reward events, the progressions and the rank tables of the trade skills, and the values of the backend for
the gear score and the perks.

The ids of the native data are CRC-32 values of the lower-case text: the ingredient id of an item, a category, or a currency, the
recipe id, the progression id. An ingredient type is an eIngredientType value (Item 1, Currency 2, CategoryOnly 4).

## The station use

- The stations are the server children of the settlements (`spawned` of the interaction data; decision D1 of the backend: a child
  has the template of its runtime variant). The server sends a record of a child near a character (150 m); the client finds the
  template by the slice asset of the record and the focus by the bound shapes (capsules of 1.5 to 2 m). The prompt has the name key
  of the slice ("@BlacksmithT5_MasterName": "FORGE TIER 5") and "E Craft".
- E (the hold 0.5 s of OpenCraftingOption) sends the request; the client opens the screen at its request (the station facts of the
  client: protocol notes 4.36) and keeps the request while the screen shows. A refusal (3743 result 2), the end by the server (3746),
  a death, and a loading screen close the screen.
- The open: the state CraftingStation, `Hud.LocalPlayer.InteractName` (the name key: stationproperties.luac reads the header and the
  trade skills of the station from it), and the Assembly key of the use (CraftingRequestBus GetCraftingStationName: "Blacksmith5").
- Escape (or the back button) leaves the state; OnCraftingScreenHidden gives the request "station end", and the client sends 2473.
- The canvas CraftingScreenV4: the retail dynamic canvas loader loads it at the first station. That loader does not run in the client
  (as the conversation screen of docs/PERSONS.md), so `swc/crafting.lua` loads it at the first open.
- The camps stay out: the retail client hides OpenCraftingOption of a camp (Soulwarden interactions.md, live check L5).

## The rules of the screen (copies of the backend rules)

- The recipe list: the recipes whose station type the station row permits (craftingtreeview.luac groups them by the station types).
- The fee (`Crafting::fee_of`): no fee when the Assembly does not charge one; else count x CraftingFee; with UseCraftingTax at least 1
  (hundredths of a coin: "Everfall Fee: 0.01").
- The craft gear score (`Crafting::gear_score_of`), the bonus item chance (`Crafting::yield_of`), the cooldown left
  (`Crafting::cooldown_left`), the Azoth of an upgrade level (`Crafting::azoth_of`): `C.CraftGearScore`, `C.BonusChance`,
  `C.CooldownLeft`, GetPerkCostForLocalPlayer.
- The craft check (CanCraftRecipe): the station permits the recipe, the rank, no cause of the backend (`C.Refusal`: a cause of the
  data, an expansion, an Assembly key with no row), the materials of the selection in the bag, the cooldown, the coin of the fee, a
  place in the bag (500 slots).
- A recipe with RequiredExpansion: IsRecipeExpansionUnlocked is false (the backend refuses it: CraftVerdict::expansion); the screen
  shows its expansion row red.

## The craft (part 2)

- CRAFT (craftingscreenv4.luac OnCraftClicked) calls CraftingRequestBus CraftRecipe(recipe, count, perk items, upgrade, the selected
  items, the gear score roll). `swc/crafting.lua` writes the host request "craft RECIPE COUNT FEE GEARSCORE UPGRADE PERKS ITEMS", and
  the client sends 2997: the fee claim of the client rule (`C.Fee`; the native adds it), the gear score claim of the quality bar, one
  entry for each ingredient (type Item, quantity 1, the CRC-32 of the selected item; 0 for a currency ingredient).
- The settings of the client that the screens read (their registration defaults: research `scratch/melee-mode/raw-all-defaults.txt`):
  javelin.crafting-enable-craft-all-override true (the whole count in one request), crafting-max-num-perk-items 4,
  crafting-enable-first-craft-bonus true, crafting-first-craft-bonus-exp-multiplier 3, max-crafting-jobs 10,000, always-skip-craft-roll
  false, enable-post-cap-trade-skills true; javelin.enable-objectives true (the button Pin Recipe of the retail screen).
- The result: the bag slot updates of the server with the cause ItemCreated (3) go to ContainerEventBus OnSlotUpdate (the screen shows
  the result 2 s after the last one); each progress 2993 goes to CraftingEventBus OnCraftingCountUpdate; a progress whose completed
  count reaches its total (also the stop form 0 of 0) makes the client send 2999 (the handler of the client, protocol notes 4.50).
- The state 205 (slot 63): the cooldown windows and the gear score bonus of the statuses go to `Swc.HostCraftCooldowns` and
  `Swc.HostCraftBonus` (UI/SwcUiCrafting) at each change.
- The perk rows of a procedural result: the bucket type and the chance of each PerkBucket column of the output (the backend copy
  perk-data.json `buckets` in crafting.json); an upgrade place gets the modifier 1.0.

## The checks

- `tools/test-ui.sh --fixture crafting`: the open with a level 70 character, the station header, the recipe list, Escape (one request
  "station end"), the close of the client (no request).
- `tools/test-net.sh`: the knife request of live check L3 byte for byte, a batch with a perk item, an upgrade, and a currency entry
  read by the backend, the stop, the progress and its stop form of the backend.
- Development runs (evidence `MVP-J/`): `mvpj-stations1` (the forge, the smelter, the woodshop of the settlement near the first test
  place: each E opens the screen and the server accepts the use), `mvpj-esc3` (E, the screen "FORGE TIER 5" with the recipe list, the
  Azoth and the coin of the copy of the user's character, Escape: 2473, the use ends, the prompt shows again).
- `mvpj-craft3` (part 2; a copy of the user's character with a kit: 8,000 Iron Ore, T5 materials, Weaponsmithing 150): at the smelter the
  search "Iron Ingot", its row, CRAFT: one 2997 (count 1, fee 1), the progress 1 of 1, 2999, the result screen "IRON INGOT"; at the
  forge the Orichalcum Great Axe: one 2997 (fee 449, gear score 520), the server makes the axe with the gear score 520 and three perks.
- The UI drives use the drive target "recipe:ID" (the shown row of a recipe) and the drive command "text" (the search box).

## The differences from retail (part 1)

- The camera and the depth of field of the screen (OverrideCameraState UI_CraftingStep1) are not done: the world behind the screen is
  sharp.
- The forge itself (the template child `craftingstation_t5_blacksmith_complete`, a second server child) has no body in the client:
  the client shows no server object body (a gap of the world view).
- The craft mods (perk items) of a procedural result are not listed yet (GetCompatiblePerkResourceIds: part 5).
- The storage search (7044) is not sent: the backend answers it with no result, and the screen shows the bag only.
