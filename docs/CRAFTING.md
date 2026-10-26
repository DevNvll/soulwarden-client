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
- The camp stations of the world places stay in (AST-244, 2026-10-26): the templates int_watchtowercampt1 ("Urda's Campfire",
  `@ui_watcherscampfire`), int_hunterscampt1 and int_hunterscampt1_small ("Camp", `@CookingKitT1_Name`); kind craftingStation, key Camp1,
  19 placements in `spawned` (watchtowers, quest camps, road campfires). The retail client shows "Urda's Campfire, E Craft" and its
  crafting screen (Soulwarden evidence quests/18d I0, 18e F-19 to F-22). The converter keeps only the bound with OpenCraftingOption (the
  plan bound of the buildable camp, ApplyAllResources and CancelStructurePlan, stays out: the client takes the first option of the bound
  in focus); a camp slice has no MasterName, so its name is its one text key. The player camps (permanentcamp_master, campT5, the
  underworld camp: kind camp) stay out by their kind: the retail client hides OpenCraftingOption of a player camp (Soulwarden
  interactions.md, live check L5). Test: `tests/converter/test_interactions.py`.

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

## The batch, the stop, and the cooldown (part 3)

- The count: the quantity widget (SPACE: setCraftMax, the largest count of the materials, the bag, and the cooldown) and one 2997 with
  the whole count (javelin.crafting-enable-craft-all-override true). The server makes 20 jobs in each world tick and sends a progress
  2993 for each tick.
- The retail screen has no client cancel of such a batch: its cancel button (OnStopCraft) only stops the job loop of the other mode (no
  craft-all override). A batch stops on the server (the end of the use: Escape after the craft, 3746, a travel, the death; a command);
  the completed jobs stay, and the server sends a final progress (the completed count of the total). The screen then shows "Crafting
  attempt failed." after its 10 s timer, as the retail client with this backend (Soulwarden evidence crafting/37b RESULT: "A stop of a
  craft-all batch before its last job ... shows 'Crafting attempt failed.' after 10 s; the completed jobs stay").
- The cooldown of a recipe: the windows of state 205 (`C.CooldownLeft`): the screen shows "Crafting Cooldown: 23h 59m per N items",
  limits the count, and disables CRAFT at 0 left.
- The trade skill points of the screen: GetIngredientCount of a recipe is the units of the ingredients that are not a currency (client
  0x5C47AC0; the backend Crafting::experience_of), not the count of the entries.
- NotificationData (a native class) starts with maximumDuration 2.0, the defaultDuration of notifications.luac: a minor notice ends after
  the duration of its type (before, "Crafting attempt failed." stayed on the screen).
- Development routes (tools/lib/dev_world.py): `craft-stop` (the server stops the batch), `craft-cooldown RECIPE USED SECONDS`.

## The list, refining, and the refusals (parts 4 to 6)

- The list (AST-231, research crafting-ui.md 1): GetRecipes lists a recipe with RequiredWorldTags only when one tag is a tag of the
  world (javelin.world-tags; a match test, code 0x5C66960). This world has no tag: the retail woodshop with this backend lists no
  "Ethereal Dust Refining" group (evidence 37a D1-02).
- IsRecipeKnown(id, second) (code 0x2E5E720): a recipe with RequiredAchievementID needs that achievement (the bit of the Achievement
  state; with the second argument true the call returns that result at once); a recipe of javelin.red-button-disable-crafting-recipe-list
  is not known; a recipe with no trade skill is known; else the recipe level of the rank must reach the RecipeLevel (the retail woodshop
  at Woodworking 0 lists Lumber, level 50, under "Unlearned"). bKnownByDefault is not read.
- The recipe set (AST-234, research recipe-known.md): all 8,294 recipes; there is no default-known filter. The station script
  (craftingtreeview.luac IsItemVisible) hides a recipe whose RequiredAchievementID is not unlocked, and a recipe with
  DisplayIngredients when none of those items has a positive count in the inventory cache; a shown recipe goes under Learned or
  Unlearned by IsRecipeKnown(id, true). Learning is the achievement bit of the Achievement state 3210 (bit n & 7 of byte n >> 3,
  n the AchievementIndex of javelindata_achievements: gamedata/achievements.json, 17,005 ids; Swc.AchievementIndex,
  AchievementRequestBus IsAchievementUnlocked). The backend keeps its refusal of a recipe with no bKnownByDefault ("recipe unlock or
  achievement rule"): a shown recipe can stay unavailable; the requirement row shows that cause.
- The name of a recipe: RecipeNameOverride, else the name of the output item (the rows and the search read it).
- A recipe with no trade skill: GetRecipeTradeskill gives "None" (the value that UpdateRequirements tests).
- A recipe with RequiredExpansion: IsRecipeExpansionUnlocked is the owned expansion (the backend gives Expansion2023), so the tree lists
  it; the backend refuses it, and the refusal row shows that.
- Refining (part 4): the refining recipes of the smelter (Iron Ingot) and of the woodshop (Timber) with the bonus item chance of the
  backend rule (`C.BonusChance`); the server made bonus units in the stopped batches (7 and 8 of 100 to 120 jobs).
- The refusals (part 6, decision C3): `C.Refusal` gives the first cause of the backend data, an expansion, or an Assembly key with no row.
  A hook of craftingstatspanel.luac UpdateRequirements puts "Not available on this server: CAUSE" into the original expansion row of
  the requirements (the X icon, red), and CanCraftRecipe is false (CRAFT disabled: no request). A bag with no place and a cooldown use
  the rows of the original screen (GetSpaceToCraft, the cooldown line).
- A request that the backend refuses for a reason that the client does not know ends as in retail: the stop form, and "Crafting attempt
  failed." after the 10 s timer of the screen.

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
- `mvpj-batch2` (part 3): at the smelter SPACE and CRAFT: one 2997 of 2,000 Iron Ingots, the server stop after 100 jobs (100 Iron Ingots,
  fee 100 paid), the final progress, 2999; at the stonecutter the Runestone with 9 of 10 crafts used: CRAFT of the last one, and at the
  next open "per 0 items" and CRAFT disabled.
- `mvpj-exit4` (the exit check; video `MVP-J/mvpj-exit4.mp4`): the smelter: one Iron Ingot (Smelting 0), then a batch of 1,999 that the
  server stops after 120 jobs (127 Iron Ingots with 7 bonus units, fee 120); the forge: the Orichalcum Great Axe (Weaponsmithing 150, gear
  score 520); the woodshop: the Apprentice Logging Research Notes with "Not available on this server: no granted item (SkipGrantItems: a
  later stage)" (CRAFT sends nothing), then Timber (Woodworking 0). The comparison pictures `MVP-J/mvpj-compare-*.png` and the crops
  `mvpj-crop-*.png` (retail: the Soulwarden evidence 37b I1-01, 37c I2-01, 37a D1-02).
- The UI drives use the drive target "recipe:ID" (the shown row of a recipe) and the drive command "text" (the search box).

## The differences from retail (the comparison pictures)

- The hint bar of the screen: closed by AST-231 (the HelpBar and the chat widget; "ENTER Chat", "SPACE Max Quantity" for a recipe that can be
  crafted with a max quantity above 1, "ESC Back"; evidence AST-231/ast231-help-crops.png).
- The refusal row is a client addition (the retail screen has no such row); its long text shrinks to the row.
- The craft mods (AST-231, research crafting-ui.md 2): GetCompatiblePerkResourceIds(output, others) gives the resources with SocketType
  None and a perk bucket with one leaf perk for the classes of the output whose ExclusiveLabels do not meet the labels of the fixed perks
  of the output and of the other selected resources (crafting.json craftMods: 308 resources; the great axe T5 has 51, the skinning
  knife T2 7, as the study). GetDisplayPerkIdFromResource: the first perk of the bucket. The selector (perkitemselector.luac) adds the
  None row, the bag counts, and the order (evidence AST-231/ast231-mod-selector.png). The perk tooltip text of a row: below.
- The preview lines (AST-231, research crafting-ui.md 3): weaponAttributes.primaryAttack (the row of TooltipPrimaryAttackData:
  the base damage at the gear score x DmgCoef + AddDmg, toward zero; itemrules.json attackRows) and GetLevelRequirement (zero-based:
  RequiredLevel - 1, else the first level whose MaxEquippableGearScore reaches the gear score, plus the rarity modifier when the level
  has IgnoreItemRarity false). The great axe: 223, 27%, 48 at 520 (run ast231-mods2; evidence AST-231/ast231-preview-crops.png), 251,
  27%, 60 at 600 (the fixture craftrules).
- The tree order of the forge (AST-249, research forge-order.md 1): GetCraftingCategoryData returns the group text from the shared
  string table of the client, a table of the CRC of the lower case that keeps the first spelling. The TradingGroup texts of the
  item definitions come first (crafting.json groupSpellings; the order of a retail start is an inference), then the recipe groups:
  the axe recipe (cell "Greataxes") returns "GreatAxes". craftingsortinfo.luac has no index for "GreatAxes" (Greataxes 101,
  Greatswords 102), thus Greatswords comes first, as in retail (I2-01-axe-preview-600.png; fixture crafting, picture
  crafting-forge-order). GetCategoryData finds a row by the lower case too ("GreatAxes": the row Greataxes, "Great Axes").
- The craft mod tooltip (AST-234, AST-249, research forge-order.md 2): GetGenericLocalizedDescription fills each formula {[...]} of
  the perk text at the perk multiplier 1 and at the potency of the display score (javelin.max-display-gear-score 800 plus the
  largest ItemClassGSBonus of the perk; Swc.PerkPotency: 1 plus each span of ScalingPerGearScore from the minimum weapon gear
  score 100, at most MaxGearScoreForScaling); "LOW-HIGH" when the two texts differ, a constant part single. A perk with a class
  bonus gets the line @perk_weapon_bonus_info. "+4.5-16.28% damage against targets with active Grit."; the Maelstrom charm
  "by 3-12.82% for 8s." (score 1300). Open point: the factor of ScalingPerPlayerLevel (no perk row has a value).
- The gear score of the axe: retail 600 (Prismatic materials, Weaponsmithing 200, a food and a trophy), the client 520 (Asmodeum,
  Weaponsmithing 150): the same rule with other inputs; the server accepted the claim.
- The screen size: the client runs at 1280 x 720, the retail pictures at 1256 x 660.

- The camera and the depth of field of the screen (OverrideCameraState UI_CraftingStep1) are not done: the world behind the screen is
  sharp.
- The forge itself (the template child `craftingstation_t5_blacksmith_complete`, a second server child) has no body in the client:
  the client shows no server object body (a gap of the world view).
- The storage search (7044) is not sent: the backend answers it with no result, and the screen shows the bag only.
