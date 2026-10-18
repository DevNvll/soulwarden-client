# The UI of the Soulwarden client

The client runs the original UI scripts of the game (Lua 5.1 bytecode) on the original canvases (LyShine). Stage 1.10
(Linear AST-147) runs the HUD: the vitals, the weapon bar with the abilities and the heart rune, and the compass. The
research is `~/Games/soulwarden-client/research/ui.md`.

## Decisions of the user (stage 1.10)

- D1: the original Lua 5.1 scripts run in an embedded, unchanged Lua 5.1.5 (MIT). An emulation of the buses of the
  game engine is behind a stub layer. The stub layer logs each call that the emulation does not have. Stage 1.10 is
  the go/no-go of this way.
- D2: the converter writes the canvases as data. One runtime widget draws them with the rules of LyShine. The scripts
  clone, reparent, and spawn elements by their entity ids, as in the game.
- D3: the HUD data comes from the states of the backend (the read path of the player track, [NETWORK.md](NETWORK.md),
  section "The read path of the own entity (the HUD)").

## Parts

| Part | Path | Job |
| --- | --- | --- |
| UI converter | `converter/swc_convert/ui.py`, `tools/convert-ui.sh` | Canvases, UI slices, scripts, textures, fonts, texts, and game data of the HUD -> `SWC_CONTENT/ui`. |
| Lua 5.1.5 | `Source/ThirdParty/Lua51` | An unchanged copy of the release (MIT). `NOTICE.md` has the checksum. |
| UI runtime | `Source/SwcUi` | Plain C++ with no engine header: the JSON reader, the element model with the LyShine rules, the draw list, the Lua host. The client and the offline tests build the same files. |
| Emulation | `unreal/SoulwardenClient/Lua/swc` | Our Lua code: the buses of LyShine and of the game, the data layer, the tween engine, the screen manager, the stub layer. |
| Client UI | `Source/SoulwardenClient/UI` | `USwcUiSubsystem` (the runtime in the game, the data of the backend) and `SSwcUiCanvas` (a Slate widget that draws the draw list). |
| Offline tests | `tests/ui/ui_tests.cpp`, `tools/test-ui.sh` | The core rules with made-up data, then the HUD scripts on the converted content with no engine. |
| Preview | `converter/swc_convert/uipreview.py` | A software picture of a draw list (numpy and Pillow). |
| Dev build | `tools/dev-build.sh`, `tools/lib/dev_build.py` | Gives a build (gear, abilities) to the dev character of a development run. |

## The content (tools/convert-ui.sh)

No converted file goes into git. The converter writes to `SWC_CONTENT/ui`:

```sh
tools/convert-ui.sh hud [--force]          # all the content of the HUD (the commands below, for the HUD)
tools/convert-ui.sh canvases [PATH...|--all]
tools/convert-ui.sh scripts                # each script of the game: scripts/<path>.luac (unchanged bytes)
tools/convert-ui.sh textures PATH...|--canvas PATH|--folder PREFIX
tools/convert-ui.sh wanted [FILE]          # the textures that a run asked for (SWC_WORK/ui/wanted.json)
tools/convert-ui.sh fonts                  # fonts/fonts.json, fonts/effects.json, fonts/files/
tools/convert-ui.sh text                   # localization/en-us.json
tools/convert-ui.sh gamedata               # gamedata/abilities.json, gamedata/weapons.json
```

- Canvas (`.dynamicuicanvas`) and UI slice (`.dynamicslice`): an AZ object stream. A field id is the CRC-32 of the
  lower-case field name. The output is `canvases/<path>.json` (format 1): the canvas (size, draw order, pixel
  alignment, the scripts of the canvas entity) and each element with its components (transform, image, text, fader,
  mask, scripts with their properties, flipbook, image sequence, spawner, desaturator, button, progress bar, image
  repeater). An entity id is a u64 in decimal text. A script property value is `{t, v}` with a type (bool, num, str,
  id, vec2, color, array, strs, group).
- `--all` converts each canvas and each slice that a canvas spawns: 244 files. Two slices that canvases name are not
  in the game copy.
- Texture: a PNG with an optional `.sprite.json` (the borders of a sliced image, or the cells of a sprite sheet).
- Game data: `abilities.json` has each ability of the weapon ability tables (key: the CRC-32 of the lower-case
  AbilityID) with its icon, its category, its cooldown time, and its mana cost. `weapons.json` has each weapon and heart
  rune item (key: the lower-case item id) with its ability table, its mannequin tags, its tier, and its equip ability.

## The runtime model (Source/SwcUi)

The model follows the rules of LyShine (the O3DE source is a reference only; no code is copied):

- Rect: parent rect + anchors x parent size + offsets. A flipped rect collapses to one line. The root element has the
  rect of the canvas.
- Transform: T(pivot) R S T(-pivot) for each element that is not the root. Local position = pivot - anchor center.
- Canvas size: the canvas has 1080 lines. The width is viewport width / (viewport height / 1080).
- Image types: Stretched, Sliced, Fixed, Tiled, StretchedToFit, StretchedToFill. Fills: linear and radial (the radial
  fill walks the perimeter from the top middle).
- Fader: multiplies the alpha of its subtree. A disabled element hides its subtree. A mask clips its children with a
  quad. The mask image itself does not draw (only with "draw behind" or "draw in front").
- Text: wrap, shrink to fit (uniform or width only), ellipsis, alignment, case. The markup tags are removed. The font
  effects (the passes of a `.font` effect) draw in reverse order.
- Draw list: textured triangles, text lines, and clip quads, in viewport pixels. The Slate widget and the preview draw
  the same list.

## The Lua host

- Lua 5.1.5 compiles as C++ (`SwcLua51.cpp`, `SwcLua51Os.cpp`). Thus a Lua error is a C++ exception and unwinds the
  C++ frames of a native function.
- A script file of the game is 2 bytes (`04 00`), then stock Lua 5.1 bytecode. The bytecode has no source names. The
  loader removes the 2 bytes and gives each function the path of the script (the stack traces show the script).
- `require` and `RequireScript` load from `SWC_CONTENT/ui/scripts`, with the module name in lower case. A module runs
  one time (the cache of `require`).
- The host removes the `io` library and the `os` functions that touch files or processes.
- `swcui` is the table of the native functions (the element tree, the components, the canvases, the textures).
  `EntityId` is a userdata with one object for each id: `==`, `tostring`, and table keys work as in the game.

## The emulation (Lua/swc)

| File | Job |
| --- | --- |
| `boot.lua` | Loads the files below. |
| `core.lua` | The bus framework (`Connect`, `Broadcast`, `Event`, handlers by id), `DynamicBus`, the stub layer, the loader of the scripts. |
| `values.lua` | The value classes: `Vector2`, `Vector3`, `Color`, `Uuid`, `UiOffsets`, `UiAnchors`, the time points, `Math`. |
| `enums.lua` | The enum values that the emulation knows. |
| `elements.lua` | The script instances, the activation of the elements, clone, destroy, the spawner, the flipbooks. |
| `lyshine.lua` | The LyShine buses: element, transform, image, text, fader, mask, canvas, spawner, flipbook, image sequence. |
| `datalayer.lua` | The data layer: nodes by path, observers (on a change), callbacks (on each set or call). |
| `manager.lua` | The screen loader (`ScriptLoadCanvas`), the UI state machine, the keys, the texts with replacements. |
| `tween.lua` | The tween engine (`ScriptedEntityTweenerBus`): virtual properties, the easing functions of Penner, timelines. |
| `game.lua` | The game buses of the HUD: config, paperdoll, abilities, cooldowns, item data, stamina. |
| `host.lua` | The calls of the client: boot, enter the world, the data of the player, the tick. |

Rules of the emulation:

- The stub layer: a global name that ends with `Bus` is a stub bus. A name like `eUiImageType_Fixed` or
  `SomeType_Value` is an enum with a number of its own. A `vector_*` name is an engine vector. Each other unknown
  global is a stub. Each stub call goes into the stub log one time with its count and its first caller.
- Activation (the order of `UiCanvasComponent`): the scripts of the canvas entity activate first. The new elements
  activate with no canvas link (`GetCanvas` gives an invalid id). Then the canvas links, then `InGamePostActivate`
  runs from the children up, then `OnCanvasFinishedLoading`.
- Script properties: the converter keeps the property keys as CRC-32. The runtime finds each key of the `Properties`
  table of the script by its CRC-32. An array property starts at index 0. An empty array is an empty table.
- Screen loader: the globals canvas runs `uiloader`, which asks for each screen with `ScriptLoadCanvas`. The runtime
  loads only the screens of the stage (the four HUD screens). For the other screens it sends
  `OnCanvasLoadingFailed`, and the loader continues. The loader then sets the state HUD (CRC-32 of "hud").
- State machine: a screen with "autohide" has a state of its own (the CRC-32 of its name) and shows only in that state.
  The other screens get `OnTransitionIn` when the new state is HUD, else `OnTransitionOut`.
- Sound calls are stubs (no sound in this stage).

## The data of the backend

`USwcUiSubsystem` reads `USwcBackendSubsystem::GetLocalPlayer` each frame and calls the host functions. A value goes
to the data layer only when it changes.

| Data path or bus | Source |
| --- | --- |
| `Hud.LocalPlayer.Vitals.HealthPercent`, `HealthMax` | vitals: health, healthMax |
| `Hud.LocalPlayer.Vitals.StaminaPercent`, `StaminaMax`; `UiStaminaRequestsBus` | stamina (slot 42), or the vitals values |
| `Hud.LocalPlayer.Vitals.ManaPercent`, `ManaValue`, `ManaMax` | mana (slot 47), or the vitals values |
| `PaperdollRequestBus.GetSlot`, `PaperdollEventBus.OnPaperdollSlotUpdate` | the paperdoll state (item id from the item catalog of the backend) |
| `Hud.LocalPlayer.Equipment.ActiveWeaponSlot`, `IsSheathed` | activeMap bits 24 to 26, sheatheMap |
| `CharacterAbilityRequestBus`, `Abilities.OnDataUpdate` | abilityBars (table CRC-32 and three ability keys) |
| `CooldownTimersComponentBus` | cooldowns (end time in server microseconds; an indefinite end shows as 3600 s) |
| `Hud.LocalPlayer.PlayerHeading` | camera yaw (`GetCameraYawDegrees`, radians, west positive) |

## The client

- `USwcUiSubsystem` starts in a game run (not in the editor). `-SwcUi=off` stops it. `-SwcUiScreens=all` loads each
  screen of the game (a probe; most textures are not converted).
- The widget fills the game viewport (Z order 10). It has no input in this stage.
- Font size: a LyShine size is in canvas pixels; a Slate size is in points. The factor is 0.75.
- Additive blend: premultiplied alpha with an alpha of 0 (an approximation).
- Logs: errors in the client log; each line in `SWC_WORK/logs/ui/ui-log.txt`. Every 30 s and at the end, the reports
  go to `SWC_WORK/logs/ui`: `ui-stubs.txt` and `.json` (the stub log), `ui-errors.txt` (each Lua error one time with
  its count), `ui-surface.txt` (each emulated call that the scripts used, with its count). The missing textures go to
  `SWC_WORK/ui/wanted.json` (then `tools/convert-ui.sh wanted`).

## Checks

```sh
tools/test-ui.sh --preview                         # core rules + 3 s of HUD scripts, picture hud.png
tools/test-ui.sh --all-screens --seconds 5         # the probe: each screen of the game, no engine
tools/build.sh editor
SWC_DEV_DATA=... SWC_AGENT_DESKTOP=... tools/dev-build.sh --preset bow-boltcaster
SWC_DEV_DATA=... SWC_AGENT_DESKTOP=... tools/dev-run.sh --window --enter --quit-when world --stay 60
```

The offline test uses a made-up player: a sword, a rapier, a heart rune, three abilities on each bar, one cooldown.
Its reports go to `SWC_WORK/build/ui-tests/reports`.

`tools/dev-build.sh` gives a preset of the backend (`--list` shows them) to the dev character. It uses the
development routes of the backend (`--dev-commands`): only in a development session, never with the data folder of
the user ([AGENTS.md](../AGENTS.md), rules).

## Open points

- O1: the enum values of the client are in its compiled code. The emulation uses its own values. The paperdoll slots
  come from the protocol notes (section 4.11).
- O3: the font size factor 0.75. O8: the rule of 1080 lines. A side-by-side picture with the retail client checks
  them.
- O4: the order of the state transitions is a rule of the emulation, not a fact of the client.
- O9: the run-time surface. The reports of a run give it (`ui-stubs.txt`, `ui-surface.txt`). Each later screen adds
  its buses and its value classes.
- `hudcanvasdefault` gives one error on the compass (an empty array property). The retail client probably has the
  same error.
- The mana meter divides by the maximum of the client (field 1 plus the MaxMana parts). The value can be more than
  the maximum.
- The mana cost of an ability comes from the mana cost table by AbilityID (an approximation).
- No layout components (the HUD uses none). A sprite mask clips with its quad, not with its alpha.
- Without a frame limit (`-nullrhi`), the runtime ticks with each frame.
