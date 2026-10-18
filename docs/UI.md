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
tools/convert-ui.sh enums                  # gamedata/enums.json: the enum values of the client program
tools/convert-ui.sh texture-index          # textures/index.json: sizes, borders, cells (no pixels)
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
- Enum values (`enums`): `converter/swc_convert/luabc.py` reads the bytecode of each script and gives the global
  names that the scripts read and do not write. For each enum-like name, `uienums.py` reads the client program
  (`Bin64/NewWorld.exe`, read only, no code runs): the client registers each value with a LEA of the name text and a
  call of a template function for that value. Down that call chain (depth 4 or less) a LEA points to a getter of a
  few bytes (`mov eax, 2; ret`): its constant is the value. 1,674 of 1,681 names have a value. 7 names are not in the
  client (they are nil in the client). One name text has 5 registrations (`ePaperDollSlotTypes_QuickSlot6`, values 19
  to 23): the first one applies, as in the script context of the client.
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
- Text: wrap, shrink to fit (uniform or width only), ellipsis, alignment, case. The font effects (the passes of a
  `.font` effect) draw in reverse order. The text part is below.
- Draw list: textured triangles, text lines, and clip quads, in viewport pixels. The Slate widget and the preview draw
  the same list.

### Text (Text.cpp)

- Markup (an element with markup on): the text is XML for LyShine. The tags: `b` and `i` (no change: a `.font` file is
  a family of one font), `a action data` (a link piece), `font face color` (a piece in another `.font` and color),
  `img src height scale vAlign yOffset xPadding lPadding rPadding` (an inline image; height "fontAscent" by default),
  and the fork tag `g` (the gestures of the conversation lines: no text). The XML entities decode; an escaped line end
  is a line end. A text that is not valid markup (an unknown tag or attribute, a `&` with no entity, a tag that does
  not close) shows raw, as in LyShine. With markup off, the text shows raw, less the `g` tags.
- Layout: the spans become words, spaces, line ends, and images. Wrap at spaces; the fork WrapSplitWords breaks a word
  that does not fit in one line. Shrink to fit (uniform: a smaller font size; width only: a horizontal scale), then
  ellipsis (the lines that fit in the height; the last one and each line that is too wide end with "..."). Clip: the
  draw clips at the rect.
- Glyph size: CryFont draws a glyph at 0.8 of the font size (its default size ratio; no `.font` file of the game has
  another one), and the line is the font size. The character spacing is in 1/1000 of the font size. The saved widths
  of the canvases give these rules: the widths of 1,878 texts with a horizontal fitter are 0.79 to 0.84 of the em
  widths at the font size; with the 0.8 rule the spacing in 1/1000 of the font size fits (ratio 1.007), in 1/1000 of
  the glyph em not (1.041). The saved line heights are the size or the size + 1 (the rounding of the size cache).
- The draw list has one text item for each piece (its font, its color, the glyph size, the spacing in 1/1000 of the
  glyph size) and one mesh for each inline image (in the first pass of the font effect only).
- The default cell of a text: the widest line with no wrap (+ 0.01 with wrap) and the height of the wrapped lines.

### Layouts (Layout.cpp)

- A row, a column, or a grid sets the anchors of each child to 0 and sets its offsets. A fitter gives its element the
  target size of its own layout cell, with no minimum (the saved canvases show it; O3DE applies the minimum). The pivot
  keeps its place; stretched anchors join at their middle first.
- A layout cell gives min, target, and extra ratio for each axis. A value that the element does not override comes from
  the default cells of its components (with "ignore default cells" off): an image (Fixed: the texture size; Sliced: the
  borders; StretchedToFit: the height for its width), a text (the widest line with no wrap; the height of the wrapped
  lines), or a layout (row and column: padding, spacing, and the cells of the children; grid: the cells for its width).
- Along a row or column: the targets when they fit; else the minimums, then the rest for the cells under their targets
  (by the amount that each needs); the space that is left goes to the cells with an extra ratio. Across: the whole
  space for a cell with an extra ratio, else its target.
- The fork forms (fixed row and column): the children keep their sizes; the spacing is the step from one child start to
  the next. "Visible only" (grid and fixed forms): only the enabled children.
- The layout manager: a canvas load computes all layouts before the scripts activate. A child that comes or goes, a
  change of a cell, of a text, of an image, or of a layout value, and a change of the size of a layout element mark the
  layout. The marks compute at the end of the frame (after the tick of the scripts) or at
  `UiCanvasBus.RecomputeChangedLayouts`: all widths first, then all heights, parents before children. A hidden canvas
  keeps its marks.
- `textures/index.json` (`tools/convert-ui.sh texture-index`) gives the size, borders, and cells of a texture that is not
  converted yet: the default cells of images need them. Such a texture does not draw.

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
| `enums.lua` | The enum values: the client table (`gamedata/enums.json`) first, then the own values for the other names. |
| `elements.lua` | The script instances, the activation of the elements, clone, destroy, the spawner, the flipbooks. |
| `lyshine.lua` | The LyShine buses: element, transform, image, text, fader, mask, canvas, spawner, flipbook, image sequence. |
| `datalayer.lua` | The data layer: nodes by path, observers (on a change), callbacks (on each set or call). |
| `manager.lua` | The screen loader (`ScriptLoadCanvas`), the UI state machine, the keys, the texts with replacements. |
| `tween.lua` | The tween engine (`ScriptedEntityTweenerBus`): virtual properties, the easing functions of Penner, timelines. |
| `game.lua` | The game buses of the HUD: config, paperdoll, abilities, cooldowns, item data, stamina. |
| `host.lua` | The calls of the client: boot, enter the world, the data of the player, the tick. |

Rules of the emulation:

- Enums: the values of the client table apply first. An own value of the emulation applies only to a name that the
  table does not have; a difference between an own value and the client value goes into `Swc.enumDifferences` (the
  enum fixture fails on it). A name that the client does not have stays nil.
- The stub layer: a global name that ends with `Bus` is a stub bus. A name like `eUiImageType_Fixed` or
  `SomeType_Value` that no table has is an enum with a number of its own. A `vector_*` name is an engine vector. Each other unknown
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
- Font size: the draw list gives the glyph size in canvas pixels (0.8 of the LyShine size); a Slate size is in points,
  thus Slate gets 0.75 of the glyph size.
- Additive blend: premultiplied alpha with an alpha of 0 (an approximation).
- Logs: errors in the client log; each line in `SWC_WORK/logs/ui/ui-log.txt`. Every 30 s and at the end, the reports
  go to `SWC_WORK/logs/ui`: `ui-stubs.txt` and `.json` (the stub log), `ui-errors.txt` (each Lua error one time with
  its count), `ui-surface.txt` (each emulated call that the scripts used, with its count). The missing textures go to
  `SWC_WORK/ui/wanted.json` (then `tools/convert-ui.sh wanted`).

## Checks

```sh
tools/test-ui.sh --preview                         # core rules, 3 s of HUD scripts, the fixtures, pictures
tools/test-ui.sh --no-hud --fixture enums          # one fixture only
tools/test-ui.sh --all-screens --seconds 5         # the probe: each screen of the game, no engine
tools/build.sh editor
SWC_DEV_DATA=... SWC_AGENT_DESKTOP=... tools/dev-build.sh --preset bow-boltcaster
SWC_DEV_DATA=... SWC_AGENT_DESKTOP=... tools/dev-run.sh --window --enter --quit-when world --stay 60
```

The offline test uses a made-up player: a sword, a rapier, a heart rune, three abilities on each bar, one cooldown.
Its reports go to `SWC_WORK/build/ui-tests/reports`.

Fixtures (`tests/ui/fixtures/*.lua`): each fixture runs in a runtime of its own after the boot of the emulation (the
globals canvas, no screen of the loader). It loads original canvases with their original scripts, sends input, runs
frames, and checks values. The helpers are in `prelude.lua`: `check`, `equal`, `near`, `frames(seconds)`, `load(path)`,
`element(canvas, "A/B")`, and `picture(name)` (a draw list; `--preview` makes the PNG in
`reports/fixtures/FIXTURE/draw/`). The fixtures:

| Fixture | Checks |
| --- | --- |
| `enums` | The client table against the own values, the paperdoll slots of the protocol notes, the LyShine values, a name that the client does not have. |
| `text` | The original InfoModal screen (`infomodal.luac`, `modalrow_text.luac`) shows four text rows by its own scripts: wrap, `<font color face>` pieces, an inline image, a link with a line end; the rows do not overlap. Then the gesture tag, raw text for markup that is not valid, ellipsis, shrink to fit, clip. Pictures `text-modal`, `text-overflow`. |

The layout check (C++, in the test program): each converted canvas of 1920 x 1080 loads into a model with no scripts,
and each UI slice spawns into one; the layouts compute; each child of a layout must have the offsets that the editor
saved. Result: 959 layouts, 1,450 children, 0 differ. Apart: the layouts with the default cells of texts compare with
the font metrics of the tests (`tests/ui/font_measure.cpp`, stb_truetype, `tests/ui/vendor/stb`): 806 of 907 children
agree within 1 px (the rest: the integer advances of the CryFont size cache and wraps at the edge); a guard needs 85%.
Not compared: 2 layouts whose saved child offsets are older than the last edit of their canvas.

`tools/dev-build.sh` gives a preset of the backend (`--list` shows them) to the dev character. It uses the
development routes of the backend (`--dev-commands`): only in a development session, never with the data folder of
the user ([AGENTS.md](../AGENTS.md), rules).

## Open points

- O3: the glyph size (0.8 of the font size) comes from the saved canvases; the place of the glyphs in the line (the
  baseline) is an approximation (the Slate line box in the middle of the line). O8: the rule of 1080 lines. A
  side-by-side picture with the retail client checks them.
- O10 (the fork tag `g`): the gestures of the conversation lines; the text shows no gesture.
- O4: the order of the state transitions is a rule of the emulation, not a fact of the client.
- O9: the run-time surface. The reports of a run give it (`ui-stubs.txt`, `ui-surface.txt`). Each later screen adds
  its buses and its value classes.
- `hudcanvasdefault` gives one error on the compass (an empty array property). The retail client probably has the
  same error.
- The mana meter divides by the maximum of the client (field 1 plus the MaxMana parts). The value can be more than
  the maximum.
- The mana cost of an ability comes from the mana cost table by AbilityID (an approximation).
- The heart rune slot shows the icon of the rune. Its charge (`Hud.LocalPlayer.Vitals.ChargePercent`) has no source in
  the read path of the backend yet, thus the slot does not show the charge.
- A sprite mask clips with its quad, not with its alpha.
- Without a frame limit (`-nullrhi`), the runtime ticks with each frame.
