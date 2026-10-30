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
| UI materials | `unreal/SoulwardenClient/Scripts/ui_materials.py` | The sprite mask materials in the shared content plugin (`tools/editor-script.sh ui_materials.py`). |
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
tools/convert-ui.sh gamedata               # gamedata/abilities.json, weapons.json, afflictions.json
tools/convert-ui.sh items                  # gamedata/items.tsv, perks.tsv, consumables.tsv, weaponstats.tsv, armorstats.tsv,
                                           # encumbrance.json, itemrules.json (the inventory screens, stage P2.4)
tools/convert-ui.sh uistates               # gamedata/uistates.json: the UI state table (libs/ui/uistates.xml)
tools/convert-ui.sh worldmap               # the world map: gamedata/worldmap.json, the canvas MagicMap, all map tiles and images
tools/convert-ui.sh enums                  # gamedata/enums.json: the enum values of the client program
tools/convert-ui.sh loader                 # gamedata/loader.json: the canvases that the two loaders name
tools/convert-ui.sh actionmaps             # gamedata/actionmaps.json: the action maps of the client profile
tools/convert-ui.sh texture-index          # textures/index.json: sizes, borders, cells (no pixels)
tools/convert-ui.sh options                # the Options screen: its canvas and textures, gamedata/options.json (the defaults
                                           # of the options data of the client program), gamedata/inputprofiles.json
```

- Canvas (`.dynamicuicanvas`) and UI slice (`.dynamicslice`): an AZ object stream. A field id is the CRC-32 of the
  lower-case field name. The output is `canvases/<path>.json` (format 1): the canvas (size, draw order, pixel
  alignment, the positional input `IsPosInputSupported`, the scripts of the canvas entity) and each element with its components (transform, image, text, fader,
  mask, scripts with their properties, flipbook, image sequence, spawner, desaturator, button, progress bar, image
  repeater). An entity id is a u64 in decimal text. A script property value is `{t, v}` with a type (bool, num, str,
  id, vec2, color, array, strs, group).
- `--all` converts each canvas and each slice that a canvas spawns: 244 files. Two slices that canvases name are not
  in the game copy. A slice whose name a canvas also has (one in the game: `lyshineui/notifications/notification`) goes
  to `canvases/<path>.slice.json`; a spawn reads that file first.
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
  The item joins its weapon or rune row (ItemStatsRef to WeaponID) with no letter case: the game data mixes the case
  (AST-270: the item 2hWarhammerT5 has 2HHammerT5, its row 2HhammerT5; with the case the HUD bar had no skills for it).
  `afflictions.json` has the rows of `javelindata_afflictions` (the meters of the HUD and of the equipment screen).
- Item data (`items`): a keyed table (`.tsv`: a head line "key" and the columns, ":n" marks a number column; one line
  for each lower-case key) that the runtime reads by key with the native `swcui.table_row(name, key)` (a key text in
  any letter case, or the CRC-32 of the lower-case key). `items.tsv`: each row of the master item definitions (the
  first row of an id wins), with the icon (the IconPath, else the icon of the armor appearance ArmorAppearanceM, else
  of the weapon appearance). `perks.tsv` (`javelindata_perks*`), `consumables.tsv`. `weaponstats.tsv` and
  `armorstats.tsv`: the stat columns of the weapon and the armor definitions, by WeaponID (the ItemStatsRef of an
  item). `encumbrance.json`: the weight limits of the bag of the player (`javelindata_encumbrancelimits`, row Player).
  `itemrules.json`: the gear score rule and the armor exponent of `playerbaseattributes.pbadb` (object stream, the
  field ids of the names "Base Damage Compound Increase" and the others), the expansions (`javelindata_expansions`:
  MaxEquipGS, MaxDisplayLevel), and the scaling tiers of the weapons (`javelindata_weapontiers`).
- The UI state table (`uistates`): `libs/ui/uistates.xml`, 106 states with their level, their close rules, their
  screens, and the screens that keep their canvas.

## The runtime model (Source/SwcUi)

The model follows the rules of LyShine (the O3DE source is a reference only; no code is copied):

- Rect: parent rect + anchors x parent size + offsets. A flipped rect collapses to one line. The root element has the
  rect of the canvas.
- Transform: T(pivot) R S T(-pivot) for each element that is not the root. Local position = pivot - anchor center.
- Canvas size: the canvas has 1080 lines. The width is viewport width / (viewport height / 1080).
- Image types: Stretched, Sliced, Fixed, Tiled, StretchedToFit, StretchedToFill.
- Fills (O3DE `UiImageComponent`): linear (the far edge moves; the origin edge turns the quad); radial (from the start
  angle, clockwise or not: on one quad the point walks the border, each edge is a quarter of the amount; on a sliced
  image lines from the center clip the 9 slices by the angle); radial corner (two triangles about a corner; sliced:
  one line from the corner clips the slices); radial edge (three triangles about the middle of an edge; sliced: one
  line from that point). A sliced linear fill moves the slice values.
- A change of a text or of an image marks the layout of its own element (a fitter) and the layouts of its parents that use
  its cell (LyShine `UiTextComponent::InvalidateLayout`).
- Fader: multiplies the alpha of its subtree. A disabled element hides its subtree. A canvas with the positional
  input off (`posInput`: 12 canvases of the game, the tooltip among them) takes no pointer: a press goes to the canvas
  below it. A mask clips its children with a
  quad. The mask image itself does not draw (only with "draw behind" or "draw in front"); this is also true for a mask
  component with the masking off (O3DE `UiMaskComponent::RenderDisabledMask`).
- Sprite mask: a mask element with a converted sprite (or its child mask element) also multiplies the alpha of each
  mesh below it by the alpha of its texture: each vertex gets the place in the mask texture (the rect of the mask
  element, the cell of a sprite sheet). With the option UseAlphaTest the mask alpha is 0 or 1 at 0.5. The innermost
  mask applies.
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
- The client (SwcUiSlate.cpp) measures and draws a text with a spacing as a shaped glyph line: it shapes with no Slate
  spacing and adds the spacing to the advances (after each glyph but the last; the part of a pixel goes on to the next
  glyph). Slate itself applies `LetterSpacing` only in its shaper, in points (0.75 of the value) and in whole pixels for
  each glyph, and not in the font measure or in `MakeText` (until stage WF7 the client drew all spacings as 0: the titles
  of the prompt cards were narrow next to the retail pictures). A text with no spacing keeps the font measure and
  `MakeText`.
- The exit hover component of the fork (`exitHover` of the canvas: EventName "self:ExitHover", Delay; the flyout menu of the item
  tooltips has it on FlyoutMenu and PositionContainer): while its element shows and the cursor is outside the rect of the element for
  Delay seconds, the event (a function of the script of the element) runs each frame; a result true keeps it armed (flyoutmenu.luac
  ExitHover: the cursor is in the flyout or in its PositionContainer, or the menu is locked), another result disarms it until the
  cursor enters the element or `UiExitHoverEventBus` Reset (interactables.lua `Swc.TickExitHovers`) [inference: the native component is
  not read]. Before AST-259 the client had no runtime for it: an item tooltip never closed when the cursor left the item. The
  attribute perks of the gear (`PerkID_Stat_*`) have no MOD values in the affix table (nor in the backend copy): their amounts are an
  open point (GetAttributeBonus gives 0 for them).
- The client keeps its shaped lines (at most 4,096). A flush of the Slate font cache (an atlas out of space) frees their font faces:
  such a line draws nothing (AST-258: after some minutes of play the key hints and the health number went empty). A line that is
  dirty (`FShapedGlyphSequence::IsDirty`) is shaped again, and the log has "a cached text line lost its font faces" (at most each
  10 s). Config/DefaultEngine.ini raises the limits of the font atlases (`Slate.MaxFontAtlasPagesBeforeFlush` 4,
  `Slate.MaxFontNonAtlasTexturesBeforeFlush` 8; the default 1 gave the flush of the play test). Check: `tests/drive/ast258-flush.txt`
  (`Slate.FlushFontCache 1` at 12 s and 22 s; the HUD texts stay; evidence AST-258).
- The client measures a text at the scale of the last paint (the DPI scale of the window: 0.61 at a height of 660
  pixels) and divides the width by it, because the font rasterizes at the drawn size and its hinted advances are wider
  than at scale 1. Before (to 2026-10-20) the measure was at scale 1: a piece after a space touched the word before it
  ("deleteDeletewarden" in the delete popup, stage F8), and the lines wrapped later than the drawn widths.
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

### Input and interactive components (swc/input.lua, swc/interactables.lua)

- The converter keeps the interactable part of each interactive component (the handling flag, the hover, pressed, and
  released action names, the state actions of the hover, pressed, and disabled states) and the data of the component:
  button, check box, radio button and group, slider, scroll bar, the fork mouse wheel, scroll box, text input,
  drop-down, draggable, drop target, markup button. A key that ends with "Entity" names an element (a copy maps it).
- The input (the rules of the LyShine canvas): one hover and one active interactable. A move sets the hover (only the
  active one while one is active) and drags the active one; a press of the left button goes to the front interactable
  at the point (`Model::interactable_at`: the front canvas first, then the last child first, children before parents;
  a mask with "mask interaction" hides the points outside it); the release goes to the active one; the wheel goes to
  the scroll box under the point; keys and text go to a text input that edits. A text input stays active after the
  release.
- The state of an interactable: disabled (no handling), pressed (pressed and hovered), hover (hovered or pressed),
  normal. The state actions of the state override the color, the alpha, or the sprite of their target (the overrides
  go away with the state).
- Each action goes to the canvas: `UiCanvasNotificationBus.OnAction(entity, name)`; the fork BaseScreen calls the
  function of its scope. A button click waits for the end of the input event (LyShine queues it).
- The components: a check box toggles on a release inside; a radio button asks its group (one checked button; "allow
  uncheck"); a slider takes the value at the point along its track (step, range; the fill and the manipulator anchors);
  a scroll bar moves its handle (size: the part of the content that shows, with a minimum in pixels) and its scroll
  box; a scroll box puts the pivot of its content at the offset, keeps the content over its parent, follows the size of
  its content each frame while its canvas is enabled (O3DE does not update a disabled canvas: a hidden screen gets the
  size changes at the first frame that shows it), and scrolls by the wheel (the fork step for each notch of 120: an approximation); a drag
  of the content moves it (`OnScrollOffsetChanging`), and each release gives `OnScrollOffsetChanged` (O3DE
  `HandleReleased`); a text input starts the edit on a release inside, takes typed text at its cursor (maximum length, password character), and ends
  with Enter or Escape or a press elsewhere; a drop-down expands (its content goes on top of the canvas at the same
  place) and collapses on a second click, on an option, or on a release outside.
- Typed text (user test 2026-10-26, 8a: "im unable to type name when creating a new character"): the keyboard gives text only as
  character events. The Linux platform layer of the engine (SDL3) starts the text input of each window that accepts input at its
  creation (`LinuxWindow.cpp`, `SDL_StartTextInput`): the client makes no start call. `SDL_EVENT_TEXT_INPUT` gives
  `FSlateApplication::OnKeyChar`; Slate routes the character along the focus path to the game viewport (`FSceneViewport::OnKeyChar`,
  then `InputChar` of the viewport client). An input pre-processor (`IInputProcessor`) sees no character event. The client has its
  own viewport client, `USwcGameViewportClient`. `USwcUiSubsystem::Initialize` sets `GEngine->GameViewportClientClass`: UGameEngine::Init
  makes the game instance (its subsystems initialize) after UEngine::Init and before it makes the viewport client of that class (the
  editor -game run and the cooked build). Not in Config/DefaultEngine.ini: tools/cook.sh hashes Config/Default*.ini, and a change there
  forces a full cook (docs/WORLD.md). The trap: a value in the default object of the engine class does not stay, because UEngine::Init
  loads the config into the engine object (LoadConfig) before InitializeObjectReferences reads it. The viewport client: the console of the engine
  first, then each character to `Swc.HostInput("text", ...)` (`USwcUiSubsystem::UiChar`). The rules: no control character (SDL also
  gives BackSpace and Enter as the characters `\b` and `\r` after their key events; the key route of the text input has them), the two
  halves of a UTF-16 surrogate pair join, and a key of a letter adds no text and goes to no game map while a text input edits. The
  trap: the drive command `text` calls the runtime directly, so the check of AST-267 typed the name and the real keyboard did not. A
  check of typed text uses the drive command `char TEXT` (`FSlateApplication::ProcessKeyCharEvent`, the route after the platform
  layer); the drive `key BackSpace` and `key Enter` also give the platform character. Check (2026-10-26): `tests/drive/ut1026-name-ui.txt`,
  the name "Madevale" (`char Madevalx`, BackSpace, `char e`) in the field, the valid icon, and the create of the new character.
- The drag hand-off (O3DE `InputPositionUpdate`, `OfferDragHandOff`): a press on an interactable in a scroll box (the
  world map of the respawn screen: its tiles) gives the drag to the scroll box when the move along the scroll axes is
  more than the threshold 5 of a contained drag (a squared length, as in O3DE). The scroll box becomes the active
  interactable. Open point: the hand-off to a slider, a scroll bar, or a draggable.
- Tooltips: the tooltip setter of a button (`tooltipsetter.luac`) starts at the hover and asks the Tooltip screen
  (`tooltip.luac` ShowTooltip) after its delay. The dynamic tooltip goes to the cursor (`CursorBus.GetCursorPosition`)
  and takes the size of its sections: a simple tooltip has the flavor section with the text of
  `LocalizeWithDataSheetData` (width: the text and 30 px). The end of the hover hides it.

### Game actions and screen states (swc/actions.lua, swc/manager.lua)

- The action maps of the client profile (`libs/config/defaultprofile.xml` and its includes; `tools/convert-ui.sh
  actionmaps` writes `gamedata/actionmaps.json`, keyboard and mouse only). A map starts with its state in the profile;
  the development maps (debug, flycam, the spectator cameras) start off. `UIInputRequestsBus` turns maps on and off
  and has the input filters of the scripts (an enabled filter holds its actions).
- A key that no text input edits fires each action of an enabled map with that key (onPress at the press, onRelease
  at the release): `CryActionNotificationsBus.OnCryAction(name, value)` to the handlers of the action (1 at a press,
  0 at a release). The maps and the filters are read before the first action of the key.
- Escape fires `ui_cancel` (map default), `toggleMenuComponent` (map ui), and `ui_genpop_cancel` (map ui_genpop),
  among others. When no handler used `toggleMenuComponent` (a handler returns true), the escape rule of the
  emulation runs: the handlers of `DynamicBus.EscapeKeyNotificationBus` get `OnEscapeKeyPressed`; with no handler, a
  state other than HUD with an escapable screen goes back to HUD, and the HUD goes to the state "esc" (1643432462) of
  the escape menu. (The native code of the client does this; the rule comes from the scripts: open point O12.)
- Screen states (stage P2.4): the state table of the game (`gamedata/uistates.json`). The states make a stack by
  their level; the current state is the state of the highest level. A new state closes the states of a lower level
  (`closeLower`) or of a higher level (`closeHigher`) as the table says, and takes the place of a state of its own
  level. A screen shows while a state of the stack lists it (the escape menu shows in "esc", 2907552185, and
  921475099; the respawn screen stays under the escape menu of the respawn state). A screen of both states of a
  transition stays, with no transition (`respawn.luac` toggles 921475099 with Escape and takes any other transition out
  as an error). `ToggleState` and `ExitState` leave the state (its stack entry). A shown screen turns off the action
  maps of its canvas component (`actionMapOverrides`: the escape menu turns off player, movement, camera).
- The inventory: Tab (`toggleInventoryWindow`) leaves the state Inventory or Container when it is current, else it sets
  the native flag "looking through loadout" (`LocalPlayerUIRequestsBus.SetIsLookingThroughLoadout`); the player
  component tells `newinventory.luac`, which queues the state Inventory (2972535350: the screens NewInventory,
  EquipmentV3, and the flyout menu).
  `TryShowById` and `TryHideById` show and hide a screen over the state (the popup) with `OnTransitionIn` and
  `OnTransitionOut`; the state does not change.
- The cursor: the UI needs the pointer while a mouse owner exists (`AddMouseOwner`) or a shown screen has
  `requiresMouse` (`Swc.HostCursor`). The unload of a canvas removes its mouse owner (`Swc.UnloadCanvas`; an inference: the native
  manager is not read). The trap (user test 2026-10-26, 8b): `charactercreation.luac` SetState adds its canvas as a mouse owner on each
  step but the name step, and each quit path (OnExit) sets a step first; the owner stayed after PLAY unloaded the menu screens, and
  the first world entry of the run kept the cursor (no mouse look). A return to the menu makes a new runtime, so the second entry was
  correct. A key goes to the game unless each game map (movement, combat, player, camera)
  that binds it is off, or a text input edits (`Swc.HostKeyToGame`; "maxis_x" for the mouse moves).
- Fixture `escape`: the original EscapeMenu and Popup screens: Escape opens the menu (the cursor, the maps off), exit
  to desktop opens the quit popup over it, Escape closes only the popup, the pointer hovers the menu buttons again,
  Escape goes back to the HUD.
- Buses: the handlers of a bus have an index by address. An event to an address goes only to the handlers of that
  address and to the handlers with no address, in the order of their connection. (Before, each event read each
  handler of the bus: the activation of the 3,992 elements of the respawn screen took 3.9 s, now 0.2 s; the HUD screens
  of the client load in less than 1 s.) A canvas load that takes more than 0.1 s writes a note with its times.

### Loading, error, and respawn screens (swc/host.lua)

- The loading screen of the game (`lyshineui/hud/loadingscreen`; `globals.luac` registers it with `LoadScreenBus`):
  `Swc.HostLoading(on, level, type)` shows it (draw order 1000) with the events of the native code
  (`SetLoadingScreenType`, `OnLoadingLevelDetermined`, `OnCanvasEnabledChanged`, `OnLoadingScreenShown`) and hides it
  (`OnCanvasEnabledChanged(false)`, `OnLoadingScreenDismissed`). The client shows it from its start to the spawn of the
  player (type LEVEL1), and for a respawn (type DEATH) from the respawn request to the end of the loading flag and of
  the cells wait of the new place (at least 1 s: the flag of a respawn can end in the same frame).
- The error UI: a failed login path (a refused connection: `USwcBackendSubsystem::GetLoginError`) gives the popup
  screen of the game with the error of `uierrors.luac` (`UIError_login_unable_to_connect`: "@mm_connection_error_title",
  "@mm_loginservices_UnableToConnectError"); the popup loads when the loader did not load it.
- The respawn screen (`lyshineui/respawn/respawn`, stage P2.10): the vitals write `Hud.LocalPlayer.Vitals.IsDead`, a
  death notice (message 3950) the data callback `Hud.LocalPlayer.OnPlayerDeath` with the time to the screen, the end
  of a death `Hud.LocalPlayer.OnRevived`. The script then queues the state "respawn" (3901667439). With no home point
  (`Hud.LocalPlayer.HomePoints.Count` 0) the list has the one row "Random Location". RESPAWN calls
  `LocalPlayerUIRequestsBus.RequestRespawn(random, homeId)`: a host request (`Swc.TakeHostRequest`) that the client
  gives to `USwcBackendSubsystem::RequestRespawn(bRandom, HomeId)`. The screen holds the world map: the map buses
  give the data of the world map (below; no NPC markers), the group and raid sizes 5 and 10.
- A bus method that the emulation does not define goes to the script handlers of that bus that have it (an EBus with
  a Lua handler: the popup screen has `UiPopupBus`), else a stub.
- The data layer gives a data observer the value also when it is false (the end of a death: `IsDead` false).

### The world map and the travel (swc/worldmap.lua, stage MVP-C)

Linear AST-213. `tools/convert-ui.sh worldmap` writes the data and the textures; the fixture `worldmap` is the test.

- The screen: the original canvas `lyshineui/worldmap/magicmap` (MagicMap: magicmap.luac and its layers) in the state "Map"
  (2477632187) of the state table. The key M (`toggleMapComponent`, map ui) goes to an original script that toggles the state;
  the emulation has no rule of its own. The map data of the script itself (`NewWorld_VitaeEterna`: the bounds 2014, 10496,
  12324 x 10496 m, the zoom levels 1 to 7) gives the tiles: `lyshineui/worldtiles/newworld_vitaeeterna/map_lL_yYYY_xXXX`,
  3,031 tiles of 8 levels (all converted, 2.8 GB).
- The wheel: the canvas and the action maps both get it, as in the native input. A step is 120 (the CryInput unit; the client
  multiplies the Slate step by 120). The map zooms with the actions `ui_scroll_up` and `ui_scroll_down` and the signed value
  (`Swc.FireWheelActions`). Space (`centerMap`) centers the map on the player.
- The data (`gamedata/worldmap.json`, `converter/swc_convert/worldmap.py`):
  - the definitions of 1,357 territories, areas, and points of interest (javelindata_territorydefinitions and
    pointofinterestdefinitions/*): each column in the name of the native class (lower camel case; an icon column also with
    "Path": `mapIconPath`);
  - 1,195 shapes: the polygon prisms of the slices that the territory master slice names (tracts.json
    `territoryMasterSlicePath`: zones/territorybounds, 19 zones/areabounds_*, 23 territories/r_*_pois), in world meters;
  - 1,769 landmarks: TerritoryLandmarks of the region metadata (type, territory ids, position). The 133 spirit shrines
    (type 27) and the 28 inn respawn points (type 20) are the 161 places of the backend place list (each one within 0.5 m).
- The buses (swc/worldmap.lua): MapComponentBus (the territories with their box, the points of interest, the settlements,
  the shrine ids, the landmarks of a territory and a type, the territories that contain a position, the smallest one),
  TerritoryDefinitionsDataManagerBus (the definition, with HasPoiTag), LandClaimRequestBus (the territory of a position; no
  owner, no governance). `Hud.LocalPlayer.IsLandClaimManagerAvailable` true makes the 16 territory overlays. The host
  position (`Swc.HostView`) updates `Hud.LocalPlayer.CurrentAreaTerritory.SmallestContainingId`, `.ClaimKey`,
  `EnteredTerritoryId`, and `ExitedTerritoryId` after each move of 2 m or more.
- The rules of the fixed backend: no discovery (each point of interest is discovered and charted), no wars, races, raids,
  claims, missions, or companies (empty lists).
- The settlements of the map (ut567; static read `work/ut567/qgplan/mapsettle/ANSWERS.md`): `ObjectiveInteractorRequestBus
  GetOutpostDestinations` gives the landmarks of type 1 (Settlement) and 22 (Outpost) with a prism, in their order: the record
  MissionDestinationData (id: the actor id of the landmark, 32 uppercase hex digits; territoryId: the territory shape that contains
  it, for an outpost first its point-of-interest shape [I]; worldPosition x and y; nameLocalizationKey). The world has 13 settlements
  and 9 outposts. `GetClaimDestinations` stays empty (no land claim manager). `Hud.LocalPlayer.LandmarkDataReady` (settlementiconslayer.luac
  waits for it) is true when the map data has landmarks and PlayerMapFiltersHandler InitFilters has run (the order of retail [I]: the
  region data loads long after the map canvas). With the node true earlier, the settlement icons came before the filters, read the
  Stations filter as false, and never showed their stations (the live check msa). The settlement icon (collapsiblesettlementicon.luac) shows the
  landmarks of its territory: the crafting stations by their landmark data ("Smelting5": the station type and its tier), the inn, the
  trading post, the storage, the boards; Windsward has 20. `worldmap.json` has, for each landmark, `actor` and `data` (LandmarkData).
  `ShopDataManagerRequestBus.GetShopDataFromNpcId` gives an invalid record (the shops are a later stage) [I]. Fixture
  `tests/ui/fixtures/mapsettlements.lua` (it skips with the old map data).
- The travel: the original shrine flow (fasttravelicon.luac: a click on a shrine, the flyout with "Fast Travel", the Yes/No
  popup, `PlayerHousingClientRequestBus.RequestFastTravelToTerritory`). The backend has no fast travel of the game (no fee, no
  cooldown, no discovery, no inn recall): the costs are 0 and the check gives Success, or IsDead for a dead character. The
  request goes to the client as `travel shrine POI X Y Z` (the landmark position). The client
  (`USwcBackendSubsystem::RequestTravel`, Private/SwcBackendTravel.cpp) takes the backend place at that position (the place list
  `GET /admin/v1/catalog/places`; the backend ids are not the map ids: the map shrine 40293 is the backend place
  shrine_40292) and sends the admin command `travel` (`POST /admin/v1/commands`). The session (tools/lib/session.sh) gives one
  new random token to the backend (SOULWARDEN_ADMIN_TOKEN, with SOULWARDEN_ADMIN_STEAM_ID of the client) and to the client
  (SWC_ADMIN_TOKEN) in the environment only; the client removes it from its environment, and no log line has it. The backend
  then moves the character as for each travel (3951, the movement state with a new teleport number, 3954), and the client
  keeps its loading gate until the cells of the place are there.
- The drive target `shrine:POI` (Swc.HostPointOf) is the map icon of a shrine.
- The player arrow (AST-289, user test 2026-10-24): the LocalPlayer icon of markerslayer.luac follows `Hud.LocalPlayer.Position`
  (UpdateLocalPlayerPosition) and `Hud.LocalPlayer.PlayerHeading` (UpdateLocalPlayerRotation: positionalicon.luac turns its Image by
  360 - the heading in degrees). The compass and the respawn map read the same two nodes. The client sends `Swc.HostView` at each turn
  of the camera and at each move of 5 cm or more of the feet of the avatar (`USwcUiSubsystem`; before, only at a turn, so the arrow
  stayed at the place of the open while the player walked; the place is the avatar, not the own entity place of the server, which can
  be seconds old). Check: the fixture `worldmap` (the arrow moves 300 m east and 200 m north, and turns 90 degrees, with the map open)
  and a development run (`SWC_WORK/evidence/usertest-1024/ast289-map-arrow.png`, the draw lists of the UI drive: the image at 66.5
  degrees, then 246.5 degrees, as the camera; the software preview of the draw list and the video frame are the same).
- `WarDataServiceBus.GetRaceForTerritory` gives a race that is not valid (IsValid, IsActive, IsUpcoming false): companiesatwarlist.luac
  GetConflicts calls IsValid on it for each territory (before, the stub nil gave "attempt to index a nil value" at each open of the map).

### Dynamic lists (swc/dynamic.lua)

- Dynamic layout: at activation its first child becomes the prototype (the other children go); `SetNumChildElements`
  makes copies to the count (or removes the last ones), then the element takes the size of its layout for them
  (`Model::size_to_fit`: row, column, grid), unless a parent layout controls it.
- Dynamic scroll box: the first refresh prepares the list (the item prototype: the property, else the first child of
  the content; the header prototype with sections; their sizes along the axis; the content becomes empty). A refresh
  asks the scripts (`UiDynamicScrollBoxDataBus`: `GetNumElements`, the sections, the sizes that vary, or the target
  size of a prepared copy), gives the content the total size, and puts copies of the prototypes only at the visible
  places. A copy that becomes visible for an index goes to the scripts (`UiDynamicScrollBoxElementNotificationBus`
  `OnElementBecomingVisible`, `OnSectionHeaderBecomingVisible`, `OnElementInSectionBecomingVisible`); a copy that
  leaves the view goes to a pool. A scroll updates the visible copies.
- The prototypes leave their list: they wait under a hidden element of the canvas ("Prototypes").

## The territory standing (swc/territory.lua, QG1, step 3 of ut567)

The standing of a territory, its tokens, and its cards (Soulwarden protocol notes 4.59; backend `docs/systems/progression-and-skills.md`;
the static read `work/ut567/qgplan/t3points/ANSWERS.md`, all [S] unless marked).

- The data: `tools/convert-territory.sh` writes `ui/gamedata/territory.json` from the backend copy (`progression-tables.json`: the
  standings with their pools, the cards of each rank row of Territory_Standing, the card values) and the UI columns of
  `javelindata_progressionpointdata.datasheet` (UpgradeCardSprite, UpgradeCardIcon, UpgradeCardCategory, UpgradeCardStat,
  UpgradeCardDescription). Windsward: territory 6, progression 1DB87A14, pool TerritoryRankBonus_6 (CCDDF19B).
- The state: the rank and the points come from slot 65 (`HostRanks`). The pools come from slot 67 (`HostPointPools`): "pool free
  category" lines and the cards of the territory pools ("pool card level" lines). The codec keeps the category and the card levels;
  the cards are not learned nodes of the weapon trees. The host sends the pools before the ranks, so that a rank banner reads the new
  tokens.
- `GetUnspentTokens(progression)`: the pool of the territory, and its received free count.
- `GetAvailableProgressionPointsData(progression, filter)`: the cards of the rank row whose index is the spent levels of the pool (the
  levels of the cards with no DoNotSpendPoint), in the row order. With the filter, only with a free token and room below MaxLevel. The
  value is the total bonus of the next level (card_bonus, f32, client 0x5C51100).
- `GetCurrentProgressionPointsData(progression)`: each stored card of the pool with the total bonus of its level. The native order is
  the order of a hash container; the client uses the order of the card CRCs [I].
- `canUpgrade`: a free token, the standing rank is RequiredCategoricalProgressionLevel or more, and the next level is MaxLevel or less.
  The script shows HousePurchase alone while it can upgrade (territorydatahandler.luac ProgressionPointsToRewards).
- The events of a new pools state, only for a territory pool of the earlier state: `OnUnallocatedProgressionPointsChanged(pool, old,
  new)` when its free count changes, then `OnProgressionPointsChanged(card, oldLevel, newLevel)` for each changed card. The first
  state, a new pool, and a removed pool send none. The weapon pools keep their earlier events [I].
- The card choice: `RequestSpendTerritoryStandingPoint` sends "territory spend PROGRESSION CARD", that is 4040 (24 bytes: target,
  entity, the territory CRC, the card CRC) to the ProgressionPoint facet. The client changes nothing before the next points state.
- The claim rows (`MapComponentBus.GetClaims`): one row for each standing territory with a definition and a shape of the world map
  (`gamedata/worldmap.json`; the native map builder walks the world map records). The DevWorld territories 74 and 75 have no shape: no
  row, as in the retail list (qg1 shot 22). The id, monikerId, and settlementId of a row are the territory id; it has the name key. They have no compass icon, so compass.luac does not show them. The claim keys
  (`LandClaimRequestBus.GetClaimKeys`) are the same territories [I: the offline world has no land claim manager]. The standing list,
  the NavBar token count, and the rank banner read them.
- The card popup (TerritoryBonusPopup, POPUP_DRAW_ORDER) loads at the state Map (2477632187), as the retail dynamic canvas loader does.
- The enums: `eTerritoryBonus_*` (0 to 11) and `ePoolCategory_*` (0 to 2) of the binding 0x54DC42D (swc/enums.lua).
- Fixture `tests/ui/fixtures/territory.lua`: the rank-up of Windsward (the banner "Windsward | Standing Rank Up | 1"), the popup with
  HousePurchase alone, the two choices, the events, and the values. With no `territory.json`, it skips the checks with a note.

## The order of the canvases

- The canvases draw in the order of their draw order; equal draw orders keep their load order, the later load on top, and the input goes
  from the top down (`Model::canvases_in_order`). This is the native rule of the retail client ([S], static read
  `~/Games/soulwarden-client/work/ut567/qgplan/canvasorder/ANSWERS.md`: append, a stable sort by draw order, no raise at an enable, a show,
  or a SetDrawOrder with the same value).
- [I] The Popup canvas (`LyShineUI/Popup/Popup`) goes to the top of its draw-order group each time it shows (`Canvas::raiseOnShow`, set by
  `Swc.LoadCanvas`). Decided by the orchestrator on 2026-10-29: two static reads did not find the native cause (the canvas order above,
  and the default load order: `work/ut567/qgplan/bypass/ANSWERS.md`); one retail picture (Soulwarden
  `quests/qg7/implement/shots/53-change-marauders.png`) has the confirmation popup over FactionSelection, which loads later with the same
  draw order 52. Without the rule the popup of Change Faction was under the screen, and its Yes did not reach it (the live check f2a of
  ut567). A third static read (the native show path of a popup) runs; its rule replaces this one when it is proved.
- Checks: the fixtures `canvasorder` (Popup against RewardCelebrationPopup) and `factions` (Popup against FactionSelection, then Yes).

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
| `datalayer.lua` | The data layer: nodes by path, observers (on a change), callbacks (on each set or call). A change also goes to the callbacks of each parent node (stage P3.H: the HUD layout elements wait on `Hud.ActiveLayout.<Name>` and read its table after the layout manager sets each leaf; the retail HUD shows the places of the layout data). |
| `manager.lua` | The screen loader (`ScriptLoadCanvas`), the UI state machine, the keys, the texts with replacements. |
| `tween.lua` | The tween engine (`ScriptedEntityTweenerBus`): virtual properties, the easing functions of Penner, timelines. |
| `game.lua` | The game buses of the HUD: config, abilities, cooldowns, stamina, progression. |
| `owner.lua` | The owner states (stage MVP-A): the attributes, the ranks, the point pools, the rune charge, the achievements and the entitlements, the supported abilities (see "The owner states"). |
| `items.lua` | The items (stage P2.4): the static data, the item stats, the item slots and descriptors of the bag and of the paperdoll, the containers, the item data, the requests of the bag and of the equipment, the weight. |
| `host.lua` | The calls of the client: boot, enter the world, the data of the player, the tick. |
| `frontend.lua` | The front end (stage F2, docs/FRONTEND.md): the native part of the landing screen (the account, the list, the regions, the world CMS data, the selection, the preview requests, the login request, the camera sequence ends) and the value classes WorldMetadata, CharacterMetadata, CharacterNameData, RegionMetadata. |

Rules of the emulation:

- Enums: the values of the client table apply first. An own value of the emulation applies only to a name that the
  table does not have; a difference between an own value and the client value goes into `Swc.enumDifferences` (the
  enum fixture fails on it). A name that the client does not have stays nil.
- Native value classes: the converter lists the other native globals of the scripts (`natives` in
  `gamedata/enums.json`, 121 names); such a name is a stub class whose objects keep the fields that a script sets. A
  missing PascalCase member of an object is a stub method (it gives nil); a missing lowerCamelCase member is nil (the
  naming of the script API of the client).
- The stub layer: a global name that ends with `Bus` is a stub bus. A name like `eUiImageType_Fixed` or
  `SomeType_Value` that no table has is an enum with a number of its own. A `vector_*` name is an engine vector. Each other unknown
  global is a stub. Each stub call goes into the stub log one time with its count and its first caller.
- Activation (the order of `UiCanvasComponent`): the scripts of the canvas entity activate first. The new elements
  activate with no canvas link (`GetCanvas` gives an invalid id). Then the canvas links, then `InGamePostActivate`
  runs from the children up, then `OnCanvasFinishedLoading`.
- Script properties: the converter keeps the property keys as CRC-32. The runtime finds each key of the `Properties`
  table of the script by its CRC-32. An array property starts at index 0. An empty array is an empty table.
- Screen loader: the globals canvas runs `uiloader`, which asks for each screen with `ScriptLoadCanvas`. The runtime
  loads only the screens of the done stages (the class "active" of the census, below). For the other screens it sends
  `OnCanvasLoadingFailed`, and the loader continues. The loader then sets the state HUD (CRC-32 of "hud").
- State machine: a screen with "autohide" has a state of its own (the CRC-32 of its name) and shows only in that state.
  The other screens get `OnTransitionIn` when the new state is HUD, else `OnTransitionOut`.
- Sound calls are stubs (no sound in this stage).

## The items (swc/items.lua, stage P2.4)

- An item slot (`ItemSlot`) and an item descriptor (`ItemDescriptor`) have the values of the backend (the item, the
  quantity, the gear score, the durability, the instance, the flags, the perks) and the static data of `items.tsv`.
  The rarity: the ForceRarity of the data, else the count of the perks (the backend sends no rarity). The weight of a
  slot is the weight of the data times the quantity (the bag sections add it).
- The paperdoll slots that an item takes: the tags of its item class (the rule of the client check 0x679C060, as the
  backend has it). `GetEquipSlots` gives their names (the names of `equipmentcommon.luac`, which are also the names of
  the slot elements of the equipment screen); a slot with no name (8, 18, 19, 20, 26, 29) has no element.
- The item stats of the tooltips: the base damage of a weapon at a gear score (one factor of 1 + 0.0112 for each full
  5 above 100, a smaller factor above 500: client code 0x3691470, Soulwarden `Combat::weapon_base`); the core damage is
  the base times 1 + 0.025 for each level above 1 (client code 0x5BFB530). The armor rating of an item: the gear score
  to the power 1.2 times the scale factor of the set, times its rating factor, each to one decimal (client code
  0x5BFB400). The jewelry lines: gear score x Scale...PerGS x TrinketBonus x 0.01 (client code 0x3636A50). The scaling
  tier of an attribute: the highest tier of `javelindata_weapontiers` whose start the scaling reaches.
- The requests of the scripts (`LocalPlayerUIRequestsBus`) go to the client as host requests: `equip BAG SLOT QTY
  INSTANCE`, `unequip SLOT BAG QTY`, `swap TO FROM INSTANCE INSTANCE`, `use BAG`, `drop BAG QTY INSTANCE`,
  `dropequipped SLOT QTY`. The client sends them to the backend (the request writers of `RepClient`, NETWORK.md). The
  screens change only with the states of the backend (no prediction). A refusal 3626 gives each paperdoll slot to the
  scripts again (`Swc.HostEquipFail`).
- The defaults of the client settings that the item screens read (`Swc.config`, game.lua): the values of the
  registration function of the client (image RVA 0x616C670), with the address of each name. A setting that the
  emulation does not have is false or 0.
- The afflictions: the meters start with no damage and hide (the backend sends no affliction state).
- The equip check of the client (the native ItemSlot `CanEquipItem`; a rule of the emulation from the data and
  `equipmentcommon.luac`): the item has a paperdoll slot, its level requirement (AST-234: the native zero-based rule of
  `Swc.ItemLevelRequirement`, research crafting-ui.md 3: RequiredLevel - 1, else the level of its gear score with the rarity
  modifier; the tooltip shows it plus 1) is not above the level of the wire, and its gear score is not above the MaxEquipGS of the owned expansion
  (Expansion2023, 800; the backend gives each player that expansion and refuses a higher gear score too). The command
  list then has "Equip" off ("Requirements not met"). `PaperdollGetWeaponAutoEquipSlot` (the weapon slot of an equip
  when no weapon slot is empty and no equipped weapon compares) gives the slot of the active weapon, else weapon slot 1
  (the native rule is not known).
- The loot bags (`swc/loot.lua`; protocol notes 4.42): the C++ gives the bags of the creature view in the range of their
  Interact bound with no Looted claim (`Swc.HostLootBags`, with the kind of the claim); a new bag gives the data callback
  `Hud.LocalPlayer.LootDrop.TriggerAreaEntered`, a bag that left gives `TriggerAreaExited` (`newinventory.luac` keeps them).
  Tab near a bag: `OpenNearbyLootContainers` calls `LootDropRequestsBus.OpenLootDrop`: the host request `openloot INTEREST`
  (the interactor sends OpenStorageOption), and the panel opens at once: the emulation clears the open bag, sets the title
  (`Hud.LocalPlayer.InteractName`: "@ui_loot" for the bag of a kill, the live proof of the backend; the name of the slice,
  "@inv_DroppedItems", for a discard), and calls `ContainerScreenRequestBus.RegisterWithLootDropInteractable` of
  `catcontainer.luac`, which sets the loadout flag again: `newinventory.luac` queues the state Container (a set of the flag
  notifies also with no change). The items of the panel are the selected Container (slot 76; `Swc.HostLootSlot`). Take All:
  `takeall` (2939); LSHIFT and a click (the single click of `clickrecognizer.luac` after the time of a double click):
  `TradeBatchAddItem` and `TradeBatchExecute`, the request `take INSTANCE QUANTITY` (2942). The close of the panel
  (`InteractPanelClosed`) ends the use (2473); the end of the use (the Looted claim, the end of the bag, an open with no use
  after 1 s) closes the panel (`UnregisterInteractable`). The weight of the panel: the items of the bag, and the limit of the
  player as the largest value (the live check showed it with no state).
- The cursor notifications of the native code (`CursorNotificationBus OnCursorPressed`, `OnCursorReleased`, the CRC-32 of the
  input channel "mouse_button_left" or "mouse_button_right") come with each button event: the click recognizer reads them.
- The notifications (`lyshineui/notifications/notification`, the screen of `notifications.luac`): the script is the
  handler of `UiNotificationsBus`; `AreNotificationsEnabled` is true. A minor notification shows after the pause of
  the queue (`SecondsBetweenNotifications`, 1.5 s).

## The owner states (swc/owner.lua, stage MVP-A)

`USwcUiSubsystem::FeedOwnerStates` sends the owner states of the read path ([NETWORK.md](NETWORK.md) "The read path of the own
entity") at each change of their revision; `swc/owner.lua` keeps them and gives them to the original scripts:

| Host call | State | What the scripts read |
| --- | --- | --- |
| `HostSupportedAbilities(text)` | the supported-action list ([ACTIONS.md](ACTIONS.md) "The supported-action list") | `weaponquickslots.luac` `GetIsAbilityEnabled` (a script hook): an ability with no rule of the backend shows disabled |
| `HostAttributes(base x5, bonus x5, spent x5, free)` | attribute (27) | `AttributeRequestBus` `GetAttributeBaseLevel`, `GetAttributeBonusLevel` (the rows of the equipment screen, `equipmentstatrow.luac`); `Hud.LocalPlayer.Attributes.<Name>.Spent` tells the rows to read again (a new bonus with the same points sets the path two times) |
| `HostRanks(text)` | categorical progression (65) | `CategoricalProgressionRequestBus` `GetRank`, `GetProgression` (a new state replaces the map) |
| `HostPointPools(text)` | progression points (67) | `Swc.owner.pools` (the skill tree screen is not loaded yet) |
| `HostCharge(percent)` | charge (48) | `Hud.LocalPlayer.Vitals.ChargePercent` (0 to 1; `heartgemslot.luac`) |
| `HostOwnership(achievements, entitlements, received)` | achievement (66), entitlement (19) | `PlayerComponentRequestsBus` `IsExpansionOwnerScript` and `GetOwnedExpansionData` read the achievement Expansion_2023_Unlock (bit 11979; before the first state, the grant policy of the backend: owned); `AchievementRequestBus` `IsAchievementUnlocked` of that achievement |

| `HostStatuses(text)` (stage MVP-E) | status effects (44): "index potency stacks" lines | the armor part of the statuses (see below) |
| `HostConsumeSlot(slot)` (stage MVP-E) | the Use action of the character (not a state) | `Hud.LocalPlayer.Equipment.SetConsumeSlot` (slot 14 to 17) at its start and `CancelConsumeSlot` at its end: `consumablequickslots.luac` dims the four quick slot keys while a consumable is in use (retail 0x6925570 and 0x67A3800) |

The craft state goes to the log only (no craft screen yet). The fixture `ownerstates` checks each call with the original scripts.

### The values of the equipment screen (stage MVP-E; research `mvp-e-static.md`)

`equipmentv3.luac` `UpdateArmorRatings` reads three native values:

- The gear score, `PaperdollRequestBus.GetPowerLevel(paperdoll, 0)` (retail 0x6828F20, a proved rule; `Swc.PowerLevel`):
  A = trunc(sum(weight x GS) + 0.5) over the armor slots 0 to 4 with the fixed weights head 0.20, chest 0.35, hands 0.15, legs 0.20,
  feet 0.10 (an empty slot gives 0); W = the integer mean of the main-hand options 24 and 25 (an empty option counts in the divisor; a
  weapon with `IsShieldCompatible` and a shield counts floor((weapon + shield) / 2)); J = the integer mean of the jewelry slots 5, 6, 7;
  GS = trunc(0.35 W + 0.45 A + 0.20 J). The client uses doubles; the native code float32 (a total within a float32 step of a whole
  number can differ by 1). The current character: 746 with the bow and the rapier.
- PHYS and ELEM, `VitalsComponentRequestBus.GetVitalsArmorModifierValueById(vitals, CRC "physical" / "elemental")`: the retail screen
  reads a native cache whose full equation is not proved ([GEAR.md](GEAR.md) "The armor of the screen"). The client shows the armor of
  the backend rule, the value that changes the damage (`Swc.ArmorRating`).
- The hover lists are made by the script from the buses (`GetDamageTypeKeys`, `GetAbsorptionFromDamageType`,
  `GetWeaknessFromDamageType`, the attributes, the gear score of each item). The absorption and the weakness of the character are not
  in the client yet: the two resistance lists stay empty (open point).
- Live check (run `mvpe-use7`, `use7/screen-values.png`): the gear score 724 with the bow and the great axe (W = 700); PHYS and ELEM
  916.9; after the food and the dust DEX 500 and ELEM 1,237.8 (x 1.35); in the fight 1,100.3 for both (the server status
  `Status_Passive_Greataxe_Mauler_ArmorWhenFoesNear`, +0.2 ArmorFortify).
- The attribute bonus 0 of the current character is correct: its gear has the `StatSwap` Inherent perks (`Global_Perk_StatSwap_Health`,
  `_BaseDamage`: health or damage in place of attributes), and no perk with a MOD attribute column. A food gives a bonus (FoodDEXT52:
  DEX 456 + 44, live run `mvpe-items1`).

## The character window (swc/skills.lua, stage MVP-G)

The original canvas Skills (skills.luac, key K): the attributes, the weapon mastery with the trees of each weapon
(masterytreewindow.luac, masterytree.luac, masterynode.luac), and the trade skills, with the owner states of the character.
`tools/convert-skills.sh` writes `gamedata/skilltrees.json` (converter `swc_convert/skilltrees.py`): the nodes of the 16 weapon
ability tables (tree, row, column, active, the node that a node needs, the icon, the texts), the index of each table in the client
(protocol notes 4.13), the rank tables and the progressions of the backend copy (each node is at the place of the backend tree: 702
nodes, no difference). The textures: `tools/convert-ui.sh textures --canvas lyshineui/skills/skills.dynamicuicanvas --folder
lyshineui/images/skills/` (the node frames, the lines, the panels).

| Part | Rule |
| --- | --- |
| The settings | skills.luac makes its tabs when the data nodes `javelin.pvp-reward-track-enabled` and `javelin.meta-achievements.enabled` have values: the javelin.* settings go into the data layer. The meta achievements are off (no data in the backend); `UpdateTabPoints` for a screen with no tab does nothing (a hook). `UIFeatures.g_uiEnableAttributes`, `...Tradeskills`, `...WeaponMastery` are on (the three tabs of the retail window). |
| The trees | `CharacterAbilityRequestBus` GetAvailableAbilityDataByAbilityTableId, GetAbilityData, IsDefaultUnlocked, GetSpentPointsOnAbility, GetActiveAbilityMoveDataByAbilityTableId (the bar: three places and a fourth empty element: the scripts lock a place whose number is not below the length of the list); `ProgressionPointRequestBus` GetUnallocatedPoolPoints, GetNumAllocatedPoolPoints, GetAllProgressionPointDataFromPoolId, GetLevel; the learned nodes of the ProgressionPoint state (`Swc.HostLearnedNodes`, UI/SwcUiSkills); the final row needs 10 points. |
| The edits | Assign (SpendPointsOnAbility, ApplyAbilityChanges): "assign" for each node (1112), "bar" for an active node to the first empty place (2701), "apply" (2857). The bar (the list of a place: RequestChangeMappedAbility): "bar INDEX PLACE ABILITY" (2701); the client changes its own bar, the next Ability state is the answer. Reset: "respec INDEX" (186) and the answer 2858 (`Swc.HostRespecResult`); with a cost the button is off (the backend resets only a tree with no cost; a hook of UpdateRespecButton). |
| The ranks | `CategoricalProgressionRequestBus` GetRankData (the reset cost, the experience of a rank), GetMaxRank and GetAbsoluteMaxRank (250 for a trade skill: the backend gives each player the expansion), GetMaxPointsForRank. |
| The attributes | `AttributeRequestBus` GetMaxLevelForAttribute and GetMaxPointsForAttribute (500, the backend copy), GetEquipmentAttributeBonusLevel; the pending values are 0 (no attribute change in this stage). |
| The scaling | `WeaponRefDataManagerBus` GetWeaponRefData: the scaling of weaponstats.tsv for the weapon ids of WeaponMasteryData. |
| The drive | "node:ABILITY" (a node of a tree) and "ability:ABILITY" (a button of the list of a place) for the UI drives. |

The check (a copy of the user's database, live run `mvpg-skills5`, `evidence/MVP-G/`): K, the weapon mastery list (the levels and the
bars of the database), the trees of the bow, the great axe, the rapier, the war hammer (19 learned nodes each), and the sword (no
mastery); in the bow tree the list of place 3 gives Penetrating Shot: 2701 (the backend: "is the facet of component c20d99be6902e291
(Stats/...)", "ability bar request ... done"), and the next Ability state shows the bar empty, Splinter Shot, Penetrating Shot; the
trade skills (the ranks of the database, the cap 250). The differences with the retail window (`side-*.png`, `crop-*.png`): no top
navigation bar (since the game menu: the NavBar with JOURNAL, CHARACTER, MAP, and the Game Menu), no tab Achievements, the lines "ENTER Chat" and "ESC Back" since AST-231 (the chat widget and the HelpBar), no blur of the world
behind the window, the Respec button off with a cost, the debug text of the run over the window.

## The census (stage P2.3)

`swc/census.lua` has one row for each of the 164 canvases of the game (`tools/convert-ui.sh loader`: the list
"canvases" of `gamedata/loader.json`). A row has the class, the feature family and the stage of the parity plan
([PARITY.md](PARITY.md); the research map of AST-155, appendix A), and each way that the retail client loads the canvas:

| Way | Source | What decides the load |
| --- | --- | --- |
| `ui` | the UI loader (`uiloader.luac` `AddScreen`, 152 calls) | the load group (1 to 9, one group in each tick) and the gate of the entry |
| `config` | the config table of the UI loader (31 flags, 36 canvases) | after the preload, a flag that turns true (`OnConfigChanged`); an `onDemand` entry only with on-demand loading off |
| `dynamic` | the dynamic canvas loader (`dynamiccanvasloader.luac` `canvasInfos`, 69 entries) | a UI state (it waits for the canvas), a trigger state (no wait), a flag, an interact card, an event |
| `native` | the client program (the path is in the program) | the front end, the video screen, the benchmark |
| `script` | another script | the loading screen (`globals.luac`), the grey box of the virtual cursor (a constant false) |

The gates: the on-demand loading (`UIFeatures.enable-on-demand-canvas-loading`), 36 other flags of the settings, the
console (`sys_DeactivateConsole` 0: never in a retail client), the editor, a tutorial level, and the data node
`UIFeatures.housingImprovements`. The values are the defaults of the build (`Swc.config` in `swc/game.lua`, each with
its image RVA): the backend gives no other configuration document, so the retail client keeps these defaults on this
backend. On-demand loading is on in the build: the UI loader asks for 76 canvases at a world entry (139 with it off),
and the dynamic canvas loader loads the others when the game asks for them.

The classes (2026-10-19):

| Class | Rule | Count |
| --- | --- | --- |
| active | this client loads it in the world (`HudScreens` of `UI/SwcUiSubsystem.cpp`, the globals, the loading screen; stage P3.H adds the consumable quick slots, the XP meter, and the reticles; stage F2 the landing screen of the front end, docs/FRONTEND.md; stage MVP-I the hit indicator and the status tray) | 27 |
| stage | the retail client can load it on this backend with the values of the build; a later stage owns it | 118 |
| service | a public online service (F89): the store (3), the news, the survey, the report, the feedback, the voice chat, the legal screen; no such service on this computer | 9 |
| debug | a diagnostic or editor screen (F90): the logger, the full-screen assert, the style guide, the benchmark (2), the editor tester of the fly-out menu, the grey box of the virtual cursor; their gates are off in a retail client | 7 |
| dormant | no gate opens with the values of the build: the housing tips (`housingImprovements` false), the paperdoll status (`enable-hud-paperdoll-status` false), the first QTE (the build has `enable-ftue-v2` true: `qte_v2`) | 3 |

- Three paths of the UI loader are not canvases (`census.paths`, class "stale"): `hud/readyup/readyupprompt` (an old
  path of the ready-up prompt: group 8, arena 3v3 and on-demand loading off; the dynamic loader owns the canvas
  `readyup/readyupprompt`: research open point O13), `readyup` of the config table (a folder), and
  `inventory/schematicsopeningpopup` of the config table (a misspelled path; `inventory/schematicopeningpopup` has its own
  entries). The retail loaders fail on them too.
- The research map gave `hud/generator` the family F90; its script is a station screen (the fuel, the time to make an
  item, the output): class stage, F57.
- The dynamic canvas loader itself (`dynamiccanvasloader/dynamiccanvasloader`, group 1 with on-demand loading on) is
  class stage: the client loads it with the first stage that has a dynamic screen.

The fixture `census` checks the table (below, "Checks"): the rows against the canvases of the game; each template loads
with its scripts (the element tree of the converted canvas below its root: some files have entities that no element
lists; LyShine does not use them); the requests of the original UI loader at a world entry against the gates of the
table, in the order of the load groups, with the values of the build and with on-demand loading off; the config table
when its flags turn on (both values); the canvas list of the original dynamic loader and the load of the canvas of each of
its 53 UI states.

## The options screen and the keys (stage P2.3)

The original Options screen (`lyshineui/options/options`, `options.luac`, the row scripts `optionslistitem.luac` and
`keybindinglistitem.luac`) opens from the button Settings of the escape menu (the state Options). Its rows build at
`UiMainMenuBus` `OnAuthComplete(true)`: the retail client sends it at the login; the emulation sends it at the end of the
load of the UI loader (`UILoadingNotificationBus` `OnUILoadingComplete`; `Swc.AuthComplete`). Three tabs: Settings (eight
categories: Game, Keyboard & Mouse, Controller, Video, Audio, Social, Accessibility, About; the flags of the build show all
eight), Keyboard & Mouse Bindings, Controller Bindings. This client shows only the categories, the tab, and the rows that it can
apply (section "The game menu").

The values (`swc/options.lua`):

- The data nodes `Hud.LocalPlayer.Options.*` take the defaults of the options data of the client program
  (`tools/convert-ui.sh options`, `converter/swc_convert/optiondefaults.py`, a static read; no code runs): the AZ reflection of
  `SerializedOptionsData` gives the fields (the offset and the type of each: `mov r8d, OFFSET` and the name in either
  order, then `call Field<T>`; 221 fields with a type), its factory gives the constructor (image VA 0x14457BB80), and the
  stores of the constructor give the defaults (221 of 221). The component binds each data node to a member
  (`lea rdx, [rsi+X]`; the struct is at 0x128 from `rsi`): 140 nodes by their binding, 42 by the words of their name.
  Examples: the master volume 100, the camera shake on, the field of view 57, the horizontal look sensitivity 0.05, the
  vertical smoothing 60, the quality 3, the vitals values off (the stamina and mana bars show no number; the health number
  always shows: `healthmeter.luac`).
- A check of the defaults (stage P3.H): the saved options of the retail client (the development prefix of the Soulwarden
  project, `usersettings_*.javsave`, 17 profiles; read only) have the value of the program for 206 of the 221 fields. The
  other 15 are changes of the development runs (the window size, the camera zoom, the master volume 0, the first-launch
  flags, two accessibility toggles, the pad dead zones, the upscaling mode). 30 saved fields have no default in the read:
  16 have a type that the reader does not decode (a color, a string, an unsigned number; for example the reticle colors:
  white, and 1, 0.443, 0.443 on a target), 14 have a registration form that the reader does not find (mostly
  enumerations: the window mode, the voice chat modes, the text size options).
- The values of this client: the resolution of the viewport (a list of one), one monitor, the window mode Windowed, no
  voice device, no cross-play. The text language "Use System Language" (`system-language`) and the list of the languages of
  the game copy, the audio language English; the range of the field of view 40 to 70 (the native code sets these nodes at
  run time: the values come from the retail reference pictures of set 1, `settings-game-camera.png` and
  `settings-video-display.png`: the label 70, and the minimum from the place of the knob at 57).
- The rows of the features of the display device: DLSS frame generation and Reflex do not show (`GetDlssSupported` and the
  others are false). The row Resolution Scaling shows with the temporal upscaler of this client (section "The game menu").
  The retail client shows them on a device with the feature (the picture of set 1).
- A row whose node has no default from the program gets the neutral value of its row at the end of the UI load (a toggle
  off, a slider at its minimum, a drop-down at its first item): 35 nodes; the log line `options: ...` lists them (the
  accessibility and subtitle rows, the voice and stream rows, the chat rows, some control rows).
- `OptionsDataBus`: the reads of the screen; `Reset<Category>Settings` sets the values of the rows of the category again. The
  screen sets the nodes itself; the calls of the display, the frame rate, the render scale, and the quality set their nodes
  too (`SetGraphicsQuality` 1 to 4 sets the eight detail rows). The client saves and applies the values (section "The game
  menu"). The mouse look sensitivities (AST-255): the controller reads `Controls.CameraSensitivityHorizontal`, `...Vertical`,
  `...HorizontalADS`, and `...VerticalADS` each 0.5 s (`USwcUiSubsystem::DataNumber`; [MOVEMENT.md](MOVEMENT.md)). The UI
  drive command `data PATH NUMBER` sets a data node (a check).
- Facts of the scripts that the emulation follows: a text `@label more` localizes the label and keeps the rest (the monitor
  "Monitor 1"); the global `FloatEpsilon` (the float epsilon of the engine) and `Math.Ceil`, `Math.Floor`; with no game pad
  `GetCurrentGamepadIdentifier` is `eGPINone` (the screen shows the default pad picture); the input profiles of the
  controller tab come from `javelindata_inputprofiles.datasheet` (the id is the row number and the current profile is the
  first one: an inference; the pad is outside the device scope).

The keys (`swc/keys.lua`, `UI/SwcKeyBindings`, the player controller):

- A key is a CryInput name ("w", "lshift", "mouse1", "mwheel_up"): the first keyboard or mouse input of an action in the
  action maps of the profile (`gamedata/actionmaps.json`), with the rebinds on top. The scripts get the names
  (`GetKeybind`, `GetKeybindByDevice`, `GetActionInputName`) and make the text themselves (an `@cc_` text, else the name);
  `GetKeybindLocStringByDevice` and `GetKeybindWithoutHold` give the text; `GetActionInMapByKey` gives the actions of a map
  with a key (the conflict check of the key page).
- The key page: a click on a row starts the listen (`LyShineManagerBus` `SetIsKeybinding(true)`; the client then sends the
  mouse buttons and the wheel as keys); each key of the host goes to `KeyInputNotificationBus`; the release of the key ends
  the listen; a key with no conflict rebinds the actions of the row (`GameRequestsBus` `RebindAction`, then
  `SaveRebindData`); a conflict gives the popup of the screen ("Change binding").
- The client (`USwcKeyBindings`, a game instance subsystem) keeps the same table: the defaults from the action maps (345
  actions), the rebinds of the requests `rebind MAP ACTION KEY`, the save at `rebind-save` to
  `SWC_WORK/settings/<the name of the session data folder>/rebindings.json` (no user id; never in the data folder of the
  backend), the reset at `rebind-reset` (the file goes: an inference, the screen calls no save after its reset). At the
  start the UI gets the saved rebinds (`Swc.HostKeyBindings`).
- The player controller maps the keys of its play actions from the table (`ASwcPlayerController::MapPlayKeys`: the four
  moves, jump, walk, target lock, free look, the two weapons, sheathe, attack, dodge, block and aim, interact, the three
  abilities, crouch, prone, autorun), again at each rebind. With no rebind it checks each key against the default of the
  profile (the log line "the key table gives the default keys of the profile (22 actions)"). F8 (a development key), the
  mouse look, and the zoom on the wheel stay fixed.

## The game menu

The user's words (2026-10-25): "cleanup the esc menu removing unused features and add the game menus like settings." The
escape menu and the Options screen are the original canvases and scripts. The client layer (`swc/gamemenu.lua`, the hooks of
the scripts) hides the parts of the features that this offline client does not have, and `swc/options.lua` with
`UI/SwcGameSettings` saves and applies the settings. No canvas, script, or text of the game changes.

The escape menu (`escapemenu.luac`):

- The retail canvas has no Resume button: Escape (`OnEscapeKeyPressed`) or a click outside the menu (`OnEmptySpaceClick`)
  closes it. This stays.
- Shown: GAME: Settings; LEAVE GAME: Exit to Main Menu, Exit to Desktop. The button of a game mode (Skip Tutorial, Leave
  Activity) shows by the rule of the script (no FTUE and no game mode here).
- Hidden (`G.escapeHidden`): Unstuck (the backend has no unstuck), Recall to Inn (the backend has no inn bind, no recall, and
  no recall cooldown: a need of the backend), Submit Feedback, Help, Newsletter, Announcements, Compensations, Free Weekend,
  External Rewards, Connect to Twitch (online services). A section with no shown button hides with its label.
- The store: with `UIFeatures.g_uiEnableEntitlements` false, `OnInit` uses the small frame of the canvas
  (`EscapeMenuHolderNoEntitelments`). The in-game layout of the script then keeps the store holder, the shop label, and the
  wide header, and the small frame of the canvas is shorter than the buttons (the design: the frame ends at 840 px, Exit to
  Desktop at 893 px). The client does what the front-end layout of the script does (`UpdateMainMenuEsc`: the store holder,
  the shop label, and the world name off, the header at the small width).
- The layout: the client wraps `UpdateScreen` and `UpdateMainMenuEsc`. Before the script runs, the client sets back each value
  of its last layout (the script sees the canvas as it made it); after it, the hidden buttons go off, the shown buttons of a
  section take the first places of the buttons that the script showed (one under the other, 50 px), the next section takes
  the place of a section that goes (the gap of the canvas: 70 px), and the small frame and its holders end 47 px below the
  last shown button (the margin of the large frame of the canvas under Exit to Desktop, read at `OnInit`).

The Options screen (`options.luac`):

- Before the script builds its rows (`TryLoadAndBuildScreen`), the client takes out of the tables of the script: the categories
  Controller (no game pad in the device scope), Audio (no sound yet), Social (no chat screen, no voice chat, no invites), About
  (web links); the tab Controller Bindings; and each row that this client cannot apply and that no loaded original script reads
  (`G.optionsHiddenRows`, with the reason of each). A group with no row left goes; a subheader with no row after it goes.
- The key page (Keyboard & Mouse Bindings): the rows of the actions that this client does not handle go too (`G.keyHiddenActions`,
  by the first action of the row, with the reason of each): the screenshot and Hide UI keys, the walk speed toggle, the two
  modifiers, the spectator camera, the emotes and social actions, the target switch keys (the target lock of this client
  switches by the mouse move), the group and ally targets, the gear sets, fishing and music, the mounts, the season rewards,
  company, war scoreboard, social window, army, camp, and augments, the chat, voice, and narration keys, the PvP flag, duels,
  the activity wheel, the pings, and the ready check. A title with no row after it goes. The rows that stay: the keys that the
  player controller maps (`MapPlayKeys`) and the keys that a loaded script or the emulation handles (the zoom, the quick slots
  and their cycle keys, the inventory, the map and its objectives, the character window, the journal, the notification keys,
  the salvage keys). A hidden action keeps its key in the action maps: the conflict popup of the page can still name it.
- Shown: Game (the map auto-zoom, the HUD layout, edit, reset, scale, and margins, the reticle rows, the vitals values, the
  damage numbers, the plate amount, the point reminders, the territory and salvage notices), Keyboard & Mouse (the invert, the
  sensitivities, the ADS multipliers), Video (window mode, monitor, resolution, maximum FPS, resolution scaling and its mode,
  field of view, the quality and its detail rows except water, V-Sync), Accessibility (the subtitles and their rows, the text
  size, the quest pin size and opacity, the dialogue auto progress), and the tab Keyboard & Mouse Bindings.
- The render scale: the retail row Resolution Scaling (`Video.TemporalUpscalingType`: 1 Off, 2 the upscaler) with its Mode
  (`Video.TemporalUpscalingMode`: 1 Native, 2 Quality, 3 Balanced, 4 Performance, 5 Ultra Performance). This client uses TSR,
  the temporal upscaler of Unreal; the item has the client label "Temporal Super Resolution" (the retail text is "AMD FSR 3.1")
  and the mode Native the label "Native" (the retail text is "Native (TAA Only)"). The labels are in `swc/gamemenu.lua`; the
  localization of the game does not change. The default of this client is TSR in the mode Quality (research/opt-gfx.md item
  G1); the retail default is Off. The scale of each mode: the factors of these modes of the retail upscaler (1.5x, 1.7x, 2x,
  3x): 66.7, 58.8, 50, 33.3 %; Off and Native: 100 %.
- `SetTemporalUpscalingModeData` of the script reads the selected item of the Mode drop-down when the type is not Off. With the
  default of this client it runs at the first show, before the drop-down has a selected item (the retail default Off skips
  it; before the guard: a Lua error, and the Escape of the options in the front end did not go back). With no selected item
  the call does nothing: the row keeps the mode list of its row data (the same list).

The NavBar (`navbar.luac`; display only, the decision of 2026-10-25):

- The retail menu bar on top of the screens of the states Esc, Map, Objectives (the journal), Skills, and Options
  (`gamedata/uistates.json`). The client loads its canvas with the HUD screens (`HudScreens`).
- The tabs: JOURNAL (Objectives), CHARACTER (Skills), MAP, and the Game Menu gear (Esc). The other entries of `NavButtonData`
  (Company, Mount, Modes, Activities, the season, Store, the group finder, the social button) go before `SetNavMenu` builds the
  tabs. The bar stays at the left, as retail: the 2023 tab list gives the spacers no width (the retail picture
  `evidence/MVP-G/side-mastery.png`).
- CHARACTER: the script turns the Skills tab on when `UIFeatures.g_uiEnableAttributes` or `g_uiEnableTradeskills` is on (two
  observers of `OnInit`); `swc/skills.lua` sets both (the retail window has these tabs).
- No soft load: with the on-demand loading, a tab of `statesToSoftLoad` goes to the state NavBar, and `OnTick` sets the queued
  state when `DynamicCanvasLoaderBus.PrepareState` says that its canvases are ready. This client loads its screens at the
  start: the client empties `statesToSoftLoad`, and each tab sets its state at once.
- The keys: the NavBar does not take `ui_cancel`, `toggleMenuComponent`, or `toggleJournalComponent`. The escape rule and the
  journal rule of the emulation stay (open point O12). `navbar.luac` has the retail Escape logic for each state, popup, and
  screen (from the HUD it can go to the state NavBar); full parity with it is a later task.
- No season: `SeasonsRewardsSeasonDataManagerBus.GetSeasonData` gives an empty display name (`OnInit` reads it).
- The textures: `tools/convert-ui.sh gamemenu` converts the canvases of the escape menu and the NavBar, their textures, and the
  textures that `navbar.luac` sets by path (`GAMEMENU_TEXTURES`). The cause of the earlier gap: the canvas of the NavBar names
  only the old background `mainbg.dds`; `OnInit` sets the 2023 background by a path in the script (`NAVBAR_BG_PATH_2023`,
  `UiImageBus.SetSpritePathname`), and no converter list had the NavBar screen, so only the wanted list of a run could make
  it. The deploy of 2026-10-25 copied only `navbar_bg_2023.png` (the same bytes as the command makes; the command gives the
  same bytes for the 391 files that the shared content has).
- Fixture `navbar` (35 checks).
- The live check (2026-10-25, run gm3, `evidence/game-menu/gm-navbar.png`): Escape opens the game menu with the NavBar; CHARACTER,
  Journal, Map, and the gear set the states Skills, Objectives, Map, and Esc at once; the bar shows its background and the glow
  of the current tab over each screen; J opens the journal and Escape closes it; the key page shows only the working keys. No
  new Lua error (37, the same set as the run with no NavBar: old faults of the journal and skills screens).
- Seen in the live runs (also before the NavBar): Escape on the Options screen in the world goes to the HUD in one press (the
  screen goes to the state Esc with ui_cancel, and the escape rule of the emulation then closes the escape menu). The retail
  order is part of the later Escape parity task (O12).

The save and the apply (`swc/options.lua`, `UI/SwcGameSettings`):

- Each 0.25 s the UI sends the values of the shown rows that changed (one request "options" with lines `NODE<tab>VALUE`; the
  value is `b1`/`b0`, `n<number>`, or `s<text>`). The client keeps them and applies:
  - `Video.TemporalUpscalingType` and `...Mode`: `r.ScreenPercentage` (set by code);
  - `Video.MaxFps` (frames per second, -1 Uncapped): `t.MaxFPS`;
  - `Video.VsyncEnabled`: `r.VSync`. The default of this client is off, as before (decision of 2026-10-25: V-Sync adds input
    delay, the mouse feel of AST-255 and AST-268, and the compositor does not tear); the retail default is on. Each other
    default keeps what the client did before the game menu; the one change is the render scale (TSR Quality);
  - the detail rows: `Scalability::SetQualityLevels` with no save. The level is the number of the row (1 Low to 4 Very High):
    High (3, the default) keeps the Epic levels of this client, Low is the Unreal level Medium (the Unreal level Low turns off
    Lumen). Effects: `sg.EffectsQuality`; Lighting: `sg.GlobalIlluminationQuality` and `sg.ReflectionQuality`; Objects:
    `sg.ViewDistanceQuality`; Post Processing: `sg.PostProcessQuality`; Shadows: `sg.ShadowQuality`; Terrain:
    `sg.LandscapeQuality` and `sg.FoliageQuality`; Textures: `sg.TextureQuality`;
  - `Video.Fov` (40 to 70): the camera manager blends to it in each state that uses the FOV of the player;
  - `Controls.InvertLook`: the controller turns the pitch of the mouse look the other way;
  - `Video.WindowMode` (0 Fullscreen, 1 Windowed) and `Video.Resolution.Width`/`Height`: a resolution request
    (`FSystemResolution::RequestResolutionChange`) when they differ from the viewport. The windowed fullscreen of Unreal shows
    as Fullscreen.
- `SerializeOptions` (the exit of the screen) asks for the save ("options-save"): the client writes
  `SWC_WORK/settings/<the name of the session data folder>/options.json`, next to `rebindings.json` (`-SwcSettings=DIR` gives
  another folder). A development run has its own data folder and thus its own file; `tools/lib/session.sh` refuses a
  development run on the data folder of the user.
- At the start the client reads the file, applies the values, and gives them to the UI (`Swc.HostOptions`, before the rows
  build). The saved display applies only when the command line has no `-ResX`, `-ResY`, `-windowed`, or `-fullscreen`
  (`tools/dev-run.sh` gives the size of the agent desktop). The display modes of the monitor fill the resolution list
  (`Swc.HostResolutions`); the window mode goes with the size (`Swc.HostDisplay W H MODE`).
- Nothing goes to `Config/Default*.ini` (a change there makes the next cook a full cook) or to the GameUserSettings of Unreal
  (its Saved folder is shared by the cooked build of the user and the development runs with `--cooked`).
- Not applied, the rows hidden: Cap FPS in Background (no frame limit of the engine for an unfocused window), Show FPS (the
  retail counter is a screen of its own), brightness, contrast, sharpness (the look waits for the user), dynamic resolution.

The live check (2026-10-25, evidence `evidence/game-menu/`; runs gm1, gm2b, gmgpu with `-SwcSettings` in the work folder):

- The escape menu and each page (`gm-pages.png`): only Settings, Exit to Main Menu, and Exit to Desktop; the pages Game,
  Keyboard & Mouse, Video, Accessibility, and the key page; the row Resolution Scaling with "Temporal Super Resolution".
- Apply: the render scale 66.7 % at the start, then 50, 66.7, 100, and 50 % at the data changes (the log lines `settings: the
  render scale`); the FOV 65.
- Save and restart: the exit of the options screen wrote 114 values; the next run applied them at the start (the render scale
  50 %, the FOV 65) and showed them on the Video page; the saved horizontal sensitivity 0.1 turned 600 raw counts into 120
  degrees (AST-268: 60 degrees at 0.05). The settings folder of the session data and the data folder of the user did not change.
- The cost of the render scale at the user view 3416 x 1380 (two pairs late in one run, the GPU p50 of the frame windows): 57 %
  9.64 ms, 66.67 % 10.35 ms (+0.7 ms); ProfileGPU 9.42 / 10.12 ms against 11.02 / 10.30 ms. Crops: `gm-render-scale-crops.png`.
- Known point: the engine writes `Saved/Config/<platform>/GameUserSettings.ini` at each exit (`UGameEngine::PreExit`
  SaveSettings, also before the game menu): the window size of the command line and the scalability levels of the run. The
  client does not read that file for its settings; its own file applies at each start.
- The UI drive splits a text with spaces (`point text:Keyboard & Mouse` points at "text:Keyboard"): a drive uses `move` for
  such a label.

## The camera of the screens (swc/camera.lua, ut567)

52 scripts of the game call the camera of the screens (the list: `work/ut567/orbit/camera-calls.txt`). The client loads 9 of them:
the NavBar, the inventory, the conversation, crafting, options, skills, the journal, respawn, and the landing screen. One module
implements the calls for all screens. Before, each call was an empty function in swc/items, swc/frontend, and swc/conversation.

- JavCameraControllerRequestBus. `OverrideCameraState(NAME, BLEND)` puts NAME on top of a stack (one entry for each name).
  `RestoreCameraState(NAME, BLEND)` removes NAME: craftingscreenv4.luac removes UI_CraftingStep3 and comes back to UI_CraftingStep1.
  [I] `RestoreCameraState("", BLEND)` removes all: the full exits of crafting and of the conversation call it.
  `ClearAllStateOverrides` removes all. `SetCameraLookAt(POINT, FLAG)` and `ClearCameraLookAt` set the look-at point ([U] the flag).
  `SetCameraEnableMouseInput` is stored only: the client locks the camera while the cursor shows.
- JavelinCameraRequestBus. `EnableDepthOfField`, `SetDepthOfField(DISTANCE, BLUR, NEAR, FAR, RANGE)`, `ResetDepthOfField`, and the
  two reads of the current values (the scripts read them before a change and tween back to them). The values of the scripts:
  crafting 6 0.4 for the craft roll and 0 0.95 for the result (1.2 s); the inventory 3 0.8; the conversation 5 0.8;
  camerahelperfunctions.luac SetCameraBlur 0 1 1 3 10 (options, skills, the journal, the landing screen).
- The requests go to the client in their own queue (`Swc.TakeCameraRequest`): "camera state NAME BLEND" (the top of the stack, "-" for
  none), "camera lookat X Y Z" (meters), and "camera dof DISTANCE BLUR NEAR FAR RANGE" ("-" for off; one request for each change).
  `USwcUiSubsystem::UpdateCameraRequests` takes all of them each frame.
- The client (`ASwcCameraManager`). `SetUiState` forces the top state with the blend of its request; the drive state wins over it, and
  a name that camera.json does not have is ignored (UI_Tools). `SetUiLookAt` turns the view toward the point from the pivot while a
  screen state is on (the Talk orbit, docs/PERSONS.md "The Talk camera"). `SetUiDepthOfField` sets the depth of field of the view:
  the focal distance is the native distance (at least 0.1 m), f/1, and the sensor width 24.576 mm x `swc.Camera.DofScale` x BLUR.
  The f-stop alone cannot give the retail blur: with the wide view of the client (a focal length of about 12 mm on the default
  sensor), f/0.6 gave about 3 px at 1280 px (the client run s3 of ut567, `swc.Camera.DofFstop` 0.5 to 8: no visible blur). The blur
  radius grows with the sensor width at one FOV. [I] The units of the native blur have no source: `swc.Camera.DofScale` is 5, the
  value of the matched pictures (the client run s4 of ut567 with 2, 5, 10, 20, and 40 against set ut567-screens: at 5 the player of the
  inventory stays sharp and the world blurs as in retail, at 10 the player blurs; skills and the journal blur strongly as in retail).
- [I] The focus and the near blur (ut567, 2026-10-29). With the native distance as the focus, the Talk (5 0.8) blurred the person and
  the dummy (2.5 m from the camera, in front of the focus), and the craft result (0 0.95) blurred all of the world; retail keeps both
  sharp or readable. The rule now: only the world behind the focus blurs (`r.DOF.Kernel.MaxForegroundRadius` 0 while a screen blur is
  on; the value of the engine comes back after it and at EndPlay), and the focus is the native distance, at least `swc.Camera.DofSpanPart`
  (0.25) of the blur span (FAR - NEAR when the script gives them, else RANGE): the inventory 3 m, the Talk 5 m, the craft roll 6 m, the
  craft result 2.5 m, skills and the journal 0.5 m. (The first form of the rule, the distance plus the part, focused the inventory at
  5.5 m: a weaker blur than retail; the window of ut567-linecam w1.)
- [I] The player is hidden in the camera states of the crafting screen (`ASwcCameraManager::HidesPlayer`: UI_CraftingStep1 and
  UI_CraftingStep3), with the other hide reasons of the character (USwcBackendSubsystem). The retail set ut567-screens shows no body
  on the screen, in the craft roll, and on the result; the client showed the body large in the front of the frame (ut567-linecam c1).
  No script call and no key of camera.json names it.
- The body in the inventory (`ASwcCameraManager::UpdateBodyTurn`): in a state UI_Inventory* the body turns to face the camera, and
  at the end it turns to the view again. [I] The retail set ut567-screens: the body yaw 280.9 with the camera yaw 101.1 (180 degrees)
  0.5 s after Tab, and 100.9 after the close. No script call (newinventory.luac BeginPaperdollMode is the skin preview) and no key
  of camera.json or movement.json names the turn. The smooth time is the RotationDampening of the state (0.1 s) [I].
- A world entry clears the stack, the look-at point, and the depth of field (`Swc.CameraReset` in `Swc.EnterWorld`). Retail (set
  ut567-screens): a travel with the loading screen closes the inventory (the HUD and the play camera after it), and a death closes it
  too ("YOU DIED", no screen). The screen closes first, and its OnTransitionOut restores the camera: no other clear is necessary.
- The data layer gives value objects (Vector3, Vector2, Color: the userdata of `Swc.ValueClass`) as copies and stores a copy, as the
  value types of the native data layer. Before (ut567), a script set the z of the shared Hud.LocalPlayer.Position object to 0, and
  GetNpcLookAtPosition read the player 252 m below the person. The person place of the Talk comes from the last focus
  (`TransformBus.GetWorldTranslation`): the client hides the prompt before the script reads it.
- The retail set ut567-screens (2026-10-26, `~/Games/soulwarden/work/evidence/unreal-references/set-ut567-screens`): the inventory
  shows the front of the player and a light blur; the NavBar pulls the camera back with no blur; skills and the journal blur the
  world strongly; the craft roll keeps the world sharp (the focus at 6 m); the result blurs it strongly and darker, with the camera
  1.4 m from the feet and no body in the frame.
- The crafting result keeps its two black layers: BackgroundStep1 (the sprite crafting_background_step3, black, alpha 73 to 236) and
  BlackBg (alpha 0.5), both at fader 1 (the fixture crafting). No script hides the player body on the result; the client hides it in the
  camera states of the screen [I] (below).
- Risks for the screens that worked before: the NavBar, the inventory, and crafting now move the camera, as in retail. UI_Inventory
  has no yaw key, so the client does not turn the view to the front of the player [U: the retail check]. A missing restore would leave
  a state on: the fixtures check that each of the 9 screens leaves no override, no look-at point, and no depth of field at its close.
- Checks: the fixture `camera` (the stack, the look-at point, the depth of field, the reset) and the camera checks of the fixtures
  navbar, escape, inventory, conversation, crafting, options, skills, quests (the journal), respawn, and frontend.

## The Lua errors (the cleanup of 2026-10-25)

The goal: no Lua error at the start of the client, at a travel, and in the journal. `tools/test-ui.sh` runs the start of the client with
no engine (`swc-ui-tests --client-screens`: the loader of the game with the screens of `SwcUi/HudScreens.h`, the same list as the client;
a Lua error fails the run). Before the cleanup that run had 34 errors, the same count as the client log. The causes and the fixes:

| Script (the error) | Cause | Fix in the client layer |
| --- | --- | --- |
| tradeskills_crafting.luac (21: `#self.skillsData`, then each default element) | Call order: the dynamic scroll box refreshes at its post activation (autoRefreshOnPostActivate) before SetVisible fills the list | An empty list at OnInit (a script hook of swc/skills.lua) |
| changetitlewindow.luac (10: `categoryOrderList`) | The same call order (TransitionIn fills the list) | The same hook |
| milestonewindowv2.luac (`#` of nil) | Missing data: PlayerComponentRequestsBus GetRewardMilestoneIds had no answer | An empty list (swc/owner.lua; open point: the level milestones) |
| swc/elements.lua (`module '' not found`) | An element with a script component and no script asset | No instance for an empty script path (O3DE: such a component does nothing) |
| bannertriggers.luac TrySeasonsRewardsSeasonPassBanner (compare nil) | Call order: the observer of PlayerEntityId reads the level node before the client set it | The client sends the level before PlayerSpawned (UI/SwcUiSubsystem.cpp FeedLocalPlayer); the config value javelin.seasons-rewards.min-level-for-seasonpass is 25 (registration 0x61736E2) |
| bannertriggers.luac OnProgressionPointsChanged (index nil) | Missing stub: ProgressionPointRequestBus GetStaticProgressionPointData | A record with a category that is not a territory (swc/skills.lua) |
| documentstab.luac (journal: `#` of nil, `GetCurrentBusId`) | Missing data (JournalDataManagerBus) and a missing bus method | Empty document lists (swc/quests.lua; open point: the lore documents); each bus has GetCurrentBusId: the address of the handler of the event that runs (Swc.CallHandler, swc/core.lua) |
| loadingscreenmanager.luac UpdateTipPoolPriorityLevels (AST-280, at each travel) | Missing native data calls: 355238560 (the show count of a tip, -1 for none) and 1214891646 (save it) | Both calls, with a store for the session (swc/host.lua). The tip screen was off: `UIFeatures.g_uiEnableLoadingScreenTips` is true in retail (registration 0x4A3AC9F, the value byte 1) and the client did not have it; with it a travel shows the destination image, the mode icon, and a tip (live lc3) |
| conversationoptionv2.luac OnPress (user test 2026-10-26: `stack overflow`, then the Talk ended) | Call order: OnInit sets the callback of the option to OnPress itself; the screen sets up its options one tick after OnConversationStateChange. A key E in that frame (OnCryAction: ui_interact goes to dialogOption1 while the center hint is off) pressed option 1. The first Talk loads the canvas, and that frame took 0.6 s. In the same frame E also acted on the center hint of the Talk before and ended the new Talk at once | swc/conversation.lua: the option keys (ui_interact, ui_nav_select, dialogOptionN) do nothing until the screen updates its options (at most 1 s); a press of an option with the OnInit callback does nothing. Fixture `conversationpress` |
| bio.luac SetupBonuses (user test 2026-10-26: `#` of nil at the BIO tab of the Skills screen) | Missing data: PlayerHousingClientRequestBus GetOwnedHouseData was a stub (nil) | An empty list: the backend has no housing (swc/worldmap.lua). The fixture `skills` opens the BIO tab |
| hudcanvasdefault.luac and lootticker2024canvas.luac SetIsVisible (AST-228) | An empty ElementsWithActionListeners list | Fixed before (commit 54c0c77, the hook of swc/elements.lua); no client log after 2026-10-23 has the error |

Fixture `luaerrors` (22 checks) runs each path and checks `Swc.errorCount` after each step; with the Lua of main it fails 9 checks
(the 34 errors of the load, the spawn, the points event, the journal, GetCurrentBusId, and the AST-280 error of a travel).

## Absent features (the cleanup rule of the user, 2026-10-25)

A screen shows no hint and no category of a feature that this client does not have (swc/gamemenu.lua, each with its reason in the code):
- The help bar takes no hint whose action is a hidden action of the key page (`G.keyHiddenActions`): for example the ping wheel hint of
  the map (mapmenu.luac SetHelpBar, target_tag_selection).
- The journal hides the tabs and the content lists of the objective types that the backend cannot give (`G.journalHiddenTabs`):
  Location, Favor, Season Story, Epic Equipment, Faction Missions, Event Quests, Town Projects, Recipes, Artifacts. It keeps Main Story,
  Faction Story, Skill Progression, Mount Quest, and Side Stories. Mount Quest stays (the decision of the orchestrator, 2026-10-25: the
  keep rule wins): backend 1.8f supports and offers 9 MountUnlock rows, so the player can see and finish them. The rules: the TabbedList
  rule for hidden buttons (isVisible false, EnableHiddenButtons), and for each hidden list and its header row SetIsEnabled false with the
  height 0 (a disabled child keeps its place in a LyShine layout: the rule of the script for its MSQMessage).
- The loading screen tips of absent features go (`G.tipHidden`, the hook of loadingscreenmanager.luac CreateTipQueue; AST-244 item 5):
  housing, company, the trading post and the trade between players, the social window, the PvP flag, duels, the PvP rewards track and
  the PvP and faction missions, war, invasions, Outpost Rush, pings, fishing, and camps. A list keeps its tips when none would stay
  (the script stops on an empty queue). The tips of the places that the client cannot load (dungeons, trials, arenas) stay: they show
  only at a travel to such a place. 242 of the 712 tips go.
- Fixture `unusedfeatures` (27 checks; picture unusedfeatures-journal).

## The world views (stage WF7)

The UI in the world views uses the original canvases (`tools/convert-ui.sh world`: the canvases, their textures, the status
rows `gamedata/statuseffects.json`, the damage types `gamedata/damagetypes.json`, the option labels
`gamedata/interactoptions.json`). The native parts of the game (not in the scripts) are emulation code of this project.

The creature plates (`swc/markers.lua`; the canvas `lyshineui/markers/markermanager`):

- At the world entry the data node `Hud.Markers.LocalPlayerMarkerSet` is true: `markermanager.luac` registers the marker
  types (`SetMarkerParams`: the place offset, the fade, the scale, the occlusion of each type of `markerdata.luac`) and the
  pools (`AddMarkerBucketGroup`; the emulation copies the vectors), and clones the elements of each pool
  (`Hud.LocalPlayer.Markers.a0` to `a9` for the AI pool).
- The feature flags and the count of the plates come from the registration of the client settings (image RVA 0x4A3A805 to
  0x4A3A92C; each entry: the name, the type, a bool, the value; an int is the second DWORD of the value):
  `UIFeatures.nameplateStatusEffects` true (live: the icons at the plates), `namePlateQuantityPc` 15, the option
  `Video.NameplateQuantity` 15: 10 elements in the AI pool.
- Each frame the C++ gives the view projection of the camera (`Swc.HostViewProjection`: the Unreal matrix, the size of the
  viewport of the UI, the camera) and the creatures of `USwcCreatureSubsystem::GetPlates` (`Swc.HostPlates`, one line for
  each creature with vitals and a marker within 50 m). The creatures with a marker type of the table of
  `markermanager.luac` ("Wolf", "Damned", ...; the slice data, `tools/convert-creatures.sh markers`) get the elements of
  the pool: the shown ones first, then the nearest. A creature keeps its element while it is in the pool.
- The data nodes of an element: `MarkerComponentId` (a marker id for each entity), `Type`, `EntityName`, `Level`,
  `HealthPercent`, `HealthMax`, `IsDead`, `EnemyDifficulty` and `CreatureTypeOverride` (the CRC-32 of the lower-case creature
  type: the frame and the level mark of `marker.luac`), `IsTeamAlignmentEnemy` true (the red bar), `StaminaBaseMax` 0 and
  `StaminaPercent` 1 (no creature stamina: the bar hides), `IsTargetingPlayer` false (no threat data), the status rows
  (`StatusEffectStackSize.N` first, then `StatusEffectImage.N` and `StatusEffectIsNegative.N`, N = 1 to 5: the statuses with
  ShowInNameplates by UiNameplatePriority; one with HidesOtherStatusEffectIcons shows alone), and `IsVisible`. A value goes
  only when it changes (a data callback of the scripts gets each set).
- The place: the feet plus the offset of the type (wZ: 1.5 m for a wolf), projected; the pivot of the element (the bottom
  middle) goes there. The scale goes from maxScale to minScale between scaleStart and scaleDistance, the alpha (the fader of
  the element) from maxAlpha to minAlpha between fadeStart and fadeDistance (AI: 1 to 0.6 between 1 m and 15 m; 1 to 0
  between 10 m and 15 m). The ramps are a rule of this client (the native rule is not known).
- `IsVisible`: the show rule of the creature view (the near range, the crosshair angle; `bShown`), the point is in front
  of the camera, the alpha is above 0, and the world does not hide the point: each 0.33 s (occlusionInterval) the C++
  traces from the camera to the point of each marker with allowWorldOcclusion (`Swc.HostMarkerPoints`,
  `Swc.HostMarkerOcclusion`; a hit on a creature or on the pawn does not hide it; the hide is an inference).
- When the UI runs, the creature view draws no simple plates and numbers (`SetUiPlates`).

The prompt cards (`swc/interact.lua`; the canvas `lyshineui/markers/unifiedinteract`, `markermanager.luac` in the interact mode,
`unifiedinteractcard.luac`, `unifiedinteractoption.luac`, `gatheringtooladditionalinfo.luac`, `hint.luac`):

- Each frame the C++ gives the prompt of the focus (`FSwcInteractor::GetPrompt`; `Swc.HostPrompt`): the kind (a bag, an object of
  the level data, a fixture), the name, the place, the phase and the progress of the use, the tool refusal, and the options. A new
  focus (or a new refusal state) gives `OnInteractUnfocus` and `OnInteractFocus(data, no marker, the marker type)` on
  `UiInteractorComponentNotificationsBus`. The marker type selects the card of `markermanager.luac`: a chest
  "Master_LootContainer", a node by its gathering type ("Master_Tree", "Master_Minerals", "Master_Plants"), a body "Wolf" (an AI
  type: the Gatherable card, as the retail picture of a skinning), a lore page "LoreItems", a bag and a fixture none (the
  Structure card). The table is an inference: the marker types of the object slices are not converted.
- The name: the name of the row of an object; for each bag "@inv_DroppedItems" (the Interact component of the bag slice; the live
  check L5 of the backend shows "DROPPED ITEMS" for both kinds of bag).
- The rows (`PopulateInteractOptions`): one copy of the option template of the card for each option of the bound, with the label of
  the option record of the UI database (`gamedata/interactoptions.json`: TakeAllOption "@interact_takeAll", OpenStorageOption
  "@ui_storage_interact", GatherOption "@ui_gather", Trigger "@ui_ignite_fuse"). The first option has the key of `ui_interact`
  (E), the second of `ui_interact_sec` (T); a hold time gives the hold hint. The key names: the table of the emulation, else the
  first key of the action in the action maps of the data.
- The hold: the fill of the ring of the row (`PressandHoldProgressBar`) and `CryActionHoldProgress` of the hints, from the progress
  of the use of the client.
- The tool refusal of a gather: the key hides, the crossed ring of the card shows (`DontHave_Tool`), and the gather info of the row
  (a copy of `GatherAdditionalInfo` in `AdditionalInfoRoot`) gets `PopulateGatheringToolAdditionalInfo` with the gathering type and
  an empty tool list: the script shows the icon of the gathering type and "No equipped Axe" (the retail pictures F-03 and L6-02 of
  the backend gathering stage show this form). A bag shows no container info (live check L5).
- The place: `Hud.LocalPlayer.UnifiedInteract.ScreenPosition.<entity>`, the place of the bound projected, in canvas units.
- The key T: the action `ui_interact_sec` of the map "ui" (actionmaps.json) at a bag opens its panel (`OpenLootDrop`: the request
  OpenStorageOption, as Tab near a bag). The player controller does not change.

The notices (`swc/notices.lua`, the bag feed of `swc/items.lua`, `Swc.HostProgression`):

- The damage numbers (the canvas `lyshineui/hud/damagenumbers/damagenumbers`, `damagenumber.luac`): a pool of copies of the template
  (`DamageNumberContainerV2`: the option UseNewDamageNumbers is on, the floating numbers: the default of the program and the
  saved retail options); a
  copy goes to the screen point of its target, gets `OnCombinedDamageDisplayed` (the entries with the name of the damage type of
  `gamedata/damagetypes.json`, the special number for a critical hit or a ranged headshot that can crit, the weakness and the
  absorption of the vitals row of the creature of `gamedata/defenses.json`), and comes back to the pool at `OnAnimDone`. The
  events: the damage numbers of the player (2071; the point at the creature, updated for 10 frames), the damage of the own
  character (3601; the red number), the heals of the own character (the green "+N"); the numbers of the own character 1.6 m above
  the feet and 0.5 m to the right of the view (the Player settings of the script).
- The tween rule: a tween with a delay waits in a queue and replaces the running tween of its property at the end of its delay,
  with the value of that moment as its start; a tween with no delay removes the queued tweens of its property from an earlier frame
  (the fade in and out of a damage number, the pulse of a button hover). A queued tween of the same frame stays (user test
  2026-10-26, 8c): `progressiontickeritem.luac` QueueFadeOut queues the fade out (a delay of 2 s, a time of 3 s; its OnComplete
  hides the row), and in the same call the new row fades in with no delay. With the removal each new progression row of the loot
  ticker (the coin, the experience) stayed on the screen ("+5.40" coin); a row that took a merge (UpdateValue) went. The rules are an
  inference from these scripts (the code of the gem is not in the game files).
- The loot ticker (`lyshineui/hud2024/lootticker/lootticker2024`): a bag slot with the sync cause 1, 6, 20, 25, 37, or 40 (34 for a
  slot that the vector had) and a larger quantity gives `OnLootReceived(slot, the new quantity less the prior quantity)`
  (Soulwarden research loot.md: the gates 0x48DA750 and 0x48E4130); the load of the bag (0x11) and the cause 0 give none. The
  icon of the item shows the count of the item in the bag (`ContainerRequestBus` `GetItemCount` with the item descriptor). The
  experience: `Hud.LocalPlayer.Progression.Glory` (the total), `PrevRequiredGlory` and `RequiredGlory` (the level table
  `gamedata/xplevels.json`); the ticker takes the first total of the world entry with no line. The coin: the first value of
  `Hud.LocalPlayer.Currency.Amount` is the start total (lootticker2024.luac: a nil total takes the value; 0 is a value in Lua); each
  later increase is a row "+N.NN" (hundredths), a second gain while the row shows adds to it, and the row goes 5 s after the last
  gain. The client gives the wallet before PlayerSpawned (`FeedLocalPlayer`), and PlayerSpawned writes no 0 before the first wallet
  (`Swc.player.coinKnown`): before, the wallet of the world entry showed as a gain. Fixture `tickercoin` (10 checks). The flag
  `g_enableNewProgressionTicker` (0x4A3A1F7) and `javelin.xp-multipliers.character` 1.0 come from the settings registration.
- When the UI runs, the simple heal numbers (`SetUiHealNumbers`) and the loot lines of the object layer (`SetUiLootNotice`) go off.

## The fight HUD (stage P3.H)

The HUD of a fight against the retail client (Linear AST-194): the vitals, the ability bar, the weapon slots, the crosshair, the
damage numbers, the creature plates, the compass, the death and respawn screens. The retail references: the pictures of the
Soulwarden evidence (`~/Games/soulwarden/work/evidence/`: fight, hit, abilities, abilities-crit; many at 1256 x 660, the size of
the client window) and the retail combat capture of set 6 (the list of the UI track).

- The screens: the client also loads the consumable quick slots (`hud2024/consumablequickslots`: the slots 3 to 6, the
  paperdoll slots QuickSlot1 to QuickSlot4, 14 to 17, with the item counts and the keys) and the level and XP meter
  (`hud2024/xp`: the level under the vitals; `xpmeter.luac` shows an XP popup at a change and hides it after 1.9 s).
- The level mark at the maximum level: `xpmeter.luac` hides the meter when the level is at the maximum of the expansion data
  (`GetOwnedExpansionData().maxCharacterLevel`: `MaxDisplayLevel` of `javelindata_expansions`, 70) and
  `Hud.LocalPlayer.ProgressionRequestBus.IsConnected` is true (the player spawn sets it). It shows the level up for 6 s, then
  fades out in 2 s. The retail pictures of set 6 (level 70) show no level mark.
- The quick slot keys 3 to 6 (`consumablequickslots.luac`): `ItemCommon:GetFullDescriptorFromId` reads the item data (the
  emulated item data has the gear score override and range and the fixed perks Perk1 to Perk5), then
  `LocalPlayerUIRequestsBus.SendVirtualInput("Use_Quickslot_1")` to `_4`. `swc/items.lua` gives the host request
  `press 404`, `117`, `140`, `513` (the action bits of the consumables, protocol notes section 4). The client puts the bits into
  the next input entry (`ASwcPlayerController::PressVirtualBits`, K3: one press for each input entry; of two presses in one
  input step only the last one reaches the server).
- The cooldowns of the consumables: the cooldown state of the read path (field 3: the CRC-32 of the lower-case item id and the
  end) goes to `Swc.HostItemCooldown(crc, seconds)`: `Hud.LocalPlayer.Cooldowns.<crc>.ItemId` and `.TimeRemaining`
  (`itemlayout.luac`), and `CooldownTimersComponentBus.IsConsumableOnCooldown` and `GetConsumableCooldown` answer for the
  consumable id (the item CRC). The slot shows the orange fill and the count.
- The bow reticle (`reticlebow.luac`): `WeaponAccuracyRequestBus.GetEstimatedInaccuracy` is 1 at rest and 0 in the aim (the
  client sends the aim from the camera state: `Swc.HostAim`). The bars are at Lerp(18, 26, t) and the dots at Lerp(11, 18, t)
  from the center: 26 and 18 pixels at rest, 18 and 11 in the aim, as in `ui-crosshair-bow.png` and
  `ui-crosshair-bow-aim.png` of set 6. The spread model of the native code (the draw time, the move) is not known.
- The background of an ability icon (stage MVP-E; the HUD slots Q, R, F, and also the trees and the tooltips):
  `weaponabilityslot.luac` `SetIcon` sets the image `Bg` behind the icon to `AbilitiesCommon:GetBackgroundPath(uiCategory)`
  (`_common/abilitiescommon.luac`). Its table has the keys 2838003374 (`abilities_bg1.dds`, red-brown), 1203776827
  (`abilities_bg2.dds`), and 3685086581 (`abilities_bg3.dds`, teal); each other value gives `icons/misc/empty.dds`. The keys are
  the CRC-32 of the lower-case `UICategory` of the ability data: `heal`, `attack`, `defense` (`Passive` and an empty field: no
  background). Before, the emulation (`swc/game.lua` ability data) gave the text of the field: no key agreed, and the icons stood
  bare on the dark slot. Now it gives the CRC-32. The retail bow slots (P3.H `strip-abilities-1.png`) have the red background of
  Attack; Riposte (Defense) has the teal background. The dim of an ability with no backend rule stays (fixture `ownerstates`).
- The vitals: only the health bar shows its number. The stamina and mana numbers follow the option
  `Video.HudShowVitalsValues`: off (the program and the saved retail options).
- The reticle (`hud2024/reticles/reticlescanvas`, `reticlesmanager.luac`; the native part: `swc/reticle.lua`):
  `Hud.LocalPlayer.ReticleToShow` is the CRC-32 of the lower-case `ReticleName` of the active weapon (the weapon data,
  `gamedata/weapons.json`: melee, spear, bow, pistol, rifle, blunderbuss; the manager maps melee 3488054177, bow 2551600555,
  rifle 3151297237, spear 2677968547, and the names `...HaveTarget` of `ReticleTargetName` to its reticles; an other name gives
  the melee reticle). The option `Controls.AlwaysShowReticle` is on (the program default): the reticle also shows while the
  weapon is sheathed, as in the retail pictures (four white ticks at the screen center). The colors
  `Controls.ReticleColor` (white) and `Controls.ReticleTargetColor` (1, 0.443, 0.443) come from the saved retail options. The
  manager keeps the drawn slot from the events `OnSlotUnsheathed` and `OnSlotSheathed` of `PaperdollEventBus`
  (`Swc.HostActiveWeapon` sends them).
- The HUD layout (`hudlayoutmanager.luac` of the HUD edit mode canvas): the default layout `hudlayoutdatadefault.luac` sets
  `Hud.ActiveLayout` (the place of each HUD part, the opacity of the full bars: 0.4). Its places differ from the canvases for
  some parts: the XP meter is at y -41, the canvas at -49 (5 pixels lower at 660 lines, as in the retail pictures).
- The fixture `fighthud` takes a picture at 1256 x 660 (`swcui.set_viewport`): the canvas is 2055 x 1080, as in the client
  window of that size (the engine viewport is in Slate units, 2057 x 1081). Against the retail picture of the same size
  (abilities-crit `l6-crit-1.png`): the quick slots and the level mark are at the same place (0, 0 pixels), the weapon slots
  have the same frames.
- The place of the HUD parts (`tools/lib/hud_compare.py RETAIL.png CLIENT.png FOLDER`): two pictures of one window size; for each
  region the shift and the scale of the client picture where the edges agree best, and a picture of the two regions and their
  edges (retail red, client green). With different content (another build, another heading) the score is low: the edge
  picture is then the check.
- The cut of a running ability cooldown: the server gives a new, earlier end (the cooldowns of the weapon after some critical
  hits). `Swc.HostCooldown` keeps the full time of the running cooldown, thus the fill of the slot drops, as in retail.
  Before, the fill started again at full with the new time left.
- The hit indicator (stage MVP-I; `lyshineui/hitindicator/hitindicator`, `hitindicatormanager.luac`, `hitindicatoredge.luac`,
  `hitindicatorarrow.luac`; the native part: `swc/hitindicator.lua`): at each hit on the own character (message 3601) a red edge
  of the screen in the direction of the source and a red arrow near the center that points to it, as in the retail take set 6
  `ui-fight-hud.mp4`. The native call `VitalsComponentNotificationBus.OnDamage` on the root entity of the HUD has 8 arguments (the
  use in the bytecode of the manager): 2 the damage as a part of the maximum health (no hit below the epsilon; the edge sprite and
  its opacity by thresholds; the edge shows 0.9 to 1.6 s), 3 the place of the source (nil or x + y + z < 0.5: the edge at 90
  degrees, no arrow), 4 the angle of the edge, 5 true: no arrow, 7 true: no indicator. The client sends 5 and 7 false.
  - The angle: the screen convention of the arrow script (`math.atan2` of the canvas points; 0 right, 90 down, 180 left, 270 up):
    the source in the frame of the camera heading from the feet of the avatar (the right part on x, the back part on y). The native
    rule is not known (an inference). The edge script gives no place at some exact angles (0 and 225 in the fixture): an exact
    multiple of 45 degrees gets 0.01 degrees more.
  - The place of the own character: the feet of the avatar. The own entity place of the server and `Hud.LocalPlayer.Position` of
    `Swc.HostView` (sent only with a new heading) were seconds old in the live check, and a wolf bites from 1.5 m: the edge showed on
    the wrong side. `Swc.HostHit` also sets `Hud.LocalPlayer.Position` (the arrow projects it).
  - `LyShineManagerBus.ProjectToScreen(position, bool, bool)`: the canvas point (1080 lines) of a world point; a point behind the
    camera gets a far point in the direction of its place in the frame of the camera heading (the native rule is not known).
  - Retail shows no screen effect for the low health state itself (set 6, from 813 to 676 of 5,330): the health bar gets its warning
    (the hatched background, `healthmeter.luac`), which the client already shows (`strip-health-trail-1.png` of P3.H).
- The status tray of the own character (stage MVP-I; `lyshineui/statuseffects/statuseffectsv2`, `statuseffects.luac`,
  `statuseffect.luac`; the native part: `swc/statustray.lua`): the icons above the health bar. The native code fills the data nodes
  of the eight elements, `Hud.LocalPlayer.StatusEffects.N` (N = 1 to 8): `.Potency`, `.EndTime` (a wall clock time point; the script
  shows the time left, or "—" when it is past), `.StackSize` ("×N" from 2), and `.EffectId` (the status id; an empty id ends the
  element). `StatusEffectsRequestBus.GetStatusEffectDataByCrc(player, the CRC-32 of the lower-case id)` gives the icon, the name,
  the description, and isNegative (the dark red frame).
  - The data: `gamedata/statustray.json` (`tools/convert-ui.sh world`): the status rows with `ShowInUITray` (2,131 rows:
    PlaceholderIcon, IsNegative, DisplayName, Description, UIPriority, HideHudDurationNumbers) and the index of the StatusEffects
    state of the owner (the backend copy, `ability-data.json`) to the status id.
  - The host (`Swc.HostStatusTray`): the status map of the owner (slot 44) as "index potency stacks end" lines, the end in Unix
    seconds on the wall clock of the server (0: no end). A status keeps its element while it stays; a new status takes the first
    free element; with more than eight, the higher UIPriority first. The order rule of the native code is not known (an
    inference). HideHudDurationNumbers is not used (the scripts do not read it).
  - Retail (set 6 `ui-fight-hud.mp4`): no icon at full health; at low health the stamina regen icon of the chest perk
    (`Status_Perk_Chest_HealthLowStamRegen`, no end) with "—".
- The localization of a label inside a text (`Model::localize`, stage MVP-I): the engine replaces each `@label` (letters, digits, `_`)
  that it knows, also after other text ("30@ui_seconds_short" of `TimeHelperFunctions:ConvertToShorthandString`, the time left of
  a status: "30s"). Before, only a label at the start of the text was localized.
- A label at the start with more text after it (2026-10-26, the user test: the BIO tab showed "@ui_world_select_world: Soulwarden"): the
  label runs over letters, digits, `_`, and `-` (1,435 keys have a `-`); a known label is localized and the rest is localized by the
  same rules ("@ui_world_select_world:" is "World:"). Before, only a label followed by a space was localized. 104 canvas texts have a
  label followed by other text (the map "@ui_governed_by:", the social screen "@ui_cooldown:"). Checks: fixtures `text`, `luaerrors`.

The check against the retail set 6 (2026-10-19, `~/Games/soulwarden-client/evidence/P3.H/compare`; the client runs
`p3h-views`, `p3h-world`, `p3h-fight`, `p3h-fight2` at 1280 x 720 with the build bow-boltcaster on the dry sand of set 3; the
times are from the line of the backend):

| Item | Retail | Client |
| --- | --- | --- |
| The reticle (sheathed, rapier, bow, bow aim) | four white ticks; the bow bars 26 and 18 px, 18 and 11 px in the aim | the same (`compare-*.png`) |
| The HUD places (vitals, weapon slots, abilities, quick slots) | | the same places (`hud_compare.py`) |
| The level mark at level 70 | no mark | no mark after 047009b (`compare-level-mark.png`) |
| The cooldown fill of R, F after the cast | 0.29 s (R), 0.28 s (F) | 0.35 s (R), 0.32 s (F) |
| The cooldown count | R 19, 18, 16 at 0.2, 1, 3 s | the same (`strip-abilities-*.png`) |
| A cut of the cooldowns at a critical hit | R 16 to 14, the fill drops | the count drops (`strip-cooldown-cut.png`: the fill started again at full); the fix of `HostCooldown` (the fixture `fighthud`, not yet in a run) |
| A press in the cooldown | refused ("the cooldown runs") | refused; the slot shows the count |
| The potion (key 3) | heal 2500, the cooldown fill 0.45 s after the use line | heal 2500 (K3, bit 404), the fill after the cooldown state of the server: the count is 1 lower at the same time (`sbs-potion.mp4`) |
| The health loss of 4400 | the number 0.1 s after the line; the trail stays 0.6 s after the last change, then 0.3 s | the number 0.2 s after the line; the trail the same (`strip-health-trail-*.png`: `multifillmeter.luac` decreaseDelay 0.6, fillAnimTime 0.3) |
| The damage numbers of the rapier | the two parts of a split hit with the type icons, the lightning number of the perk | the same; a critical hit is orange (`strip-damage-heavy.png`, `sbs-damage.mp4`) |
| The plates | a plate at 5 m, none at 15 m and 30 m | the same; the plate is about 30 percent wider (open point) |
| The compass (N, E, S, W) | | the same marks and places (`compare-compass.png`) |
| The death: the start of the darkening | 1.54 s after the death | 0.35 s after the death |
| The death: the darkening to the respawn screen | 6.2 s | 6.2 s (`strip-death-*.png`, `sbs-death.mp4`) |
| The respawn screen | "YOU DIED!", the map, "Random Location", RESPAWN | the same |

`respawn.luac` queues the respawn screen only when both `Hud.LocalPlayer.Vitals.IsDead` and `Hud.LocalPlayer.OnPlayerDeath`
are set. The retail health bar still showed the health before the death bite 1.4 s after the death line: the native vitals
of the retail client gave the death late in that take (one take, with still video frames at that time). The client sets
the two values in the same frame.

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
| `ContainerRequestBus`, `ContainerEventBus` (the bag) | the bag (Container, slot 61): each slot with its item (index in the item catalog, quantity, gear score, durability, instance, perks, flags) |
| `PaperdollRequestBus.GetSlot` (full items) | the paperdoll items of the owner view (`paperdollItems`) |
| `Hud.LocalPlayer.Currency.Amount` | the coin (Currency, slot 60) |
| `Hud.LocalPlayer.Progression.Level` | the level of the Progression state (slot 70) plus 1 |
| `Hud.LocalPlayer.Inventory.CurrentEncumbrance` and the limits | the sum of the weight of the bag items (the weight of the item data times the quantity); the limits of `encumbrance.json` |
| `Hud.LocalPlayer.PlayerHeading` | camera yaw (`GetCameraYawDegrees`, radians, west positive) |

## The client

- `USwcUiSubsystem` starts in a game run (not in the editor). `-SwcUi=off` stops it. `-SwcUiScreens=all` loads each
  screen of the game (a probe; most textures are not converted).
- The widget fills the game viewport (Z order 10).
- Input (`UI/SwcUiInput.cpp`): a Slate input pre-processor sees each key and mouse event before the game viewport.
  Each key goes to the runtime (its CryInput name: `escape`, `w`, `lshift`, `tilde`); the UI keeps a key from the game
  when it used it or when `Swc.HostKeyToGame` says no. During a harness drive, each key press writes a log line
  (`ui: key w: it goes to the game`, `the UI used it`, `the UI keeps it from the game`). While the UI needs the pointer, the cursor shows (input mode
  game and UI, no capture) and each mouse event goes to the UI only; a key release and a button release always go to
  the game too (a held key or attack ends). The player controller is not changed: the pre-processor holds its input.
- Harness input (`-SwcUiDrive=FILE`, `tests/drive/ui-*.txt`): a script of UI input events in the process (no input
  tool of the host): key, keydown, keyup, point ELEMENT (an element of a shown canvas by its name), waitfor ELEMENT
  (the clock stays until it shows), move, click, press, release, drag, wheel, log, draw NAME (the draw list of the frame:
  `REPORTS/draw-NAME.json`), end (releases each held key and button). An element name `text:LABEL` is a shown element with that text (after the localization; "_" for a space);
  `in:LABEL:NAME` is the shown element NAME in the nearest parent of the text LABEL (a button of one row of a list); a plain
  NAME whose first element is hidden gives the first shown element of that name;
  `item:ITEMID` is a shown item cell of the item (the canvas of the highest draw order first); `loot:N` is the item cell N of
  the panel of an open bag; `lootbag` is shown while a loot bag is near the character (a waitfor mark); `world` is shown while no
  loading screen shows (stage P2.3: the HUD elements exist under the loading screen). A key goes through Slate as a key of the keyboard (`FSlateApplication::ProcessKeyDownEvent`): the UI first,
  then the game viewport and the player controller when the UI does not keep it. The pointer events go to the UI
  entry of the pre-processor. The clock starts at the spawn of the player, as the drive of the player controller
  (`tools/creature-check.sh` runs the development routes on the same clock), or at a failed login path. The widget
  draws a marker at the harness pointer. `tools/ui-check.sh LABEL STAY DRIVE` makes a window run with a drive and
  records the agent desktop (wf-recorder) into `SWC_WORK/evidence/P2.2`; with `--refused` the login path goes to a
  closed port of the loopback (`-SwcHttp=127.0.0.1:1`) and the client stops after STAY seconds.
- Font size: the draw list gives the glyph size in canvas pixels (0.8 of the LyShine size); a Slate size is in points,
  thus Slate gets 0.75 of the glyph size.
- Additive blend (LyShine blend mode Add: the target plus color x alpha): the premultiplied blend of Slate with a
  premultiplied copy of the texture (color x alpha) and the vertex color x alpha with an alpha of 0. (Before, the texture
  was not premultiplied: a white particle with alpha 0 drew as a white square.)
- Sprite mask: the client draws an image below a sprite mask with the quad clip of the mask (as in 1.10). The
  option `-SwcUiMaskMaterial` draws it with the UI materials `/SwcGameContent/SwcUi/M_SwcUiMasked` and
  `M_SwcUiMaskedAdditive` (made by `tools/editor-script.sh ui_materials.py`, no game content): one dynamic instance
  for each texture, mask texture, and alpha test; the custom vertices of Slate have the mesh UVs in TexCoord 0 and the
  mask UVs in TexCoord 1. This path is not complete (open point O11). When the shader map of a mask material is
  incomplete (the cache server is not in the network namespace of a run), the client compiles it again once.
- Draw list: with the reports, the client writes the draw list of the last frame to `draw.json` of the reports
  (`python3 -B -m swc_convert.uipreview draw.json OUT.png` makes a software picture of it). A mesh has the id of its
  element (`e`; the ids of the elements of a canvas file are the ids of that file).
- Logs: errors in the client log; each line in `ui-log.txt` of the reports. Every 30 s and at the end, the reports
  go to `SWC_WORK/logs/ui` (`-SwcUiReports=DIR`: a folder of its own; the default folder is common to the runs of all
  tracks): `ui-stubs.txt` and `.json` (the stub log), `ui-errors.txt` (each Lua error one time with
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
`screen(path, drawOrder, hud)` (a screen of the loader; `hud`: no autohide), `allow(path)` (a screen that the loader
may load), `element(canvas, "A/B")`, the input helpers (`move_to`, `click`, `drag`, `wheel`, `key`), and
`picture(name)` (a draw list; `--preview` makes the PNG in `reports/fixtures/FIXTURE/draw/`). `swcui.mesh_area(id)`
gives the area, the triangle count, and the mask texture of the mesh of an element in the draw list. The fixtures:

| Fixture | Checks |
| --- | --- |
| `enums` | The client table against the own values, the paperdoll slots of the protocol notes, the LyShine values, a name that the client does not have. |
| `controls` | The original InfoModal: the hover of its button (`button.luac`) and its press, which closes the modal (the button of the game acts on the press). Then original slices spawned into the modal with their scripts: a slider (a drag sets 50; typed 25 in its text input with Enter sets 25), a toggle (two radio buttons in a group), a check box, a drop-down (its list of 12 items cloned from a prototype by `dropdown.luac`, the expand, the wheel scroll). Pictures `controls-button-normal`, `-hover`, `controls-slider`, `controls-textinput`, `controls-dropdown-open`, `-scrolled`. |
| `lists` | The original ReusableScoreboard screen shows the test data of its own script (`GetDummyColumnData`, `GetDummyRowsData`, 60 rows) in its dynamic scroll box: the content has the size of all rows, only the visible rows have copies, the script fills each copy; the wheel scrolls (rows 7 to 18 show). Pictures `lists-top`, `lists-scrolled`. |
| `visuals` | The HUD screens WeaponQuickslots, ReticlesCanvas, Vitals2024, and Compass with their scripts in the HUD state. The fixture shows the parts that need game events and sets the fill amounts through `UiImageBus` (0, 0.25, 0.5, 0.75, 1): the area of each mesh against the O3DE geometry for a radial fill (the combo points, also from 60 degrees), a counter-clockwise radial fill (the channel ring), a sliced radial fill (the hint ring of the heart rune), a counter-clockwise corner fill (the heat meter), a linear fill (the turret health), an edge fill (the reload ring, set through the bus). The heart rune hexagon and the compass give their sprite mask to the meshes below them. Pictures `visuals-fills-0` to `-100`, `visuals-masks`. |
| `census` | The census table (`swc/census.lua`, 164 rows) against the canvases of the game and the two loaders: each template loads with its scripts (the Lua errors of the templates of later stages go to the notes); the original UI loader at a world entry asks for exactly the canvases whose gate is open (76 with the values of the build, 139 with on-demand loading off) in the order of the load groups; the config table asks for exactly its open entries when the flags turn on; the canvas list of the original dynamic loader is the table, and each of its 53 UI states asks for its canvas. |
| `options` | The original Options screen from the button Settings of the escape menu: the defaults of the client program (the camera shake on, the master volume 100), a label with more text ("Monitor 1"), a picture of each category of this client (Game, Keyboard & Mouse, Video, Accessibility) and of the binding tab; the rows that show and the rows, categories, and tab that do not show (the game menu); the render scale TSR Quality with its client label; the save and the apply (the "options" lines of the shown rows, a change goes again, the preset sets the detail rows, `SetResolution`, `Swc.HostOptions`, "options-save"); the key page: a click on the jump row starts the listen (the client gets `keybinding 1`), a key that no action has binds the jump (the UI actions use it; the client gets the rebind and the save; the hints get the key), the key of the journal (J) gives the conflict popup, the reset of the page sets the defaults again (`rebind-reset`). Pictures `options-*`. |
| `fighthud` | The fight HUD screens (Vitals2024, WeaponQuickslots, ConsumableQuickslots, XP, Compass, ReticlesCanvas) with a player of the host calls: two weapons with ability bars, the heart rune, three consumables in the quick slots. The quick slot 1 shows the count 9 and the key 3; the empty slot 3 shows only the key 5; the level 65 shows under the vitals and the XP popup of the spawn is out after 3 s; the weapon slots show the other weapon (key 2) and the heart rune (key G); the reticle of the sword is the melee reticle
(`ReticleToShow` 3488054177), it shows with the weapon sheathed; a drawn bow gives the bow reticle (2551600555). The HUD edit mode sets the
default HUD layout. A cut of a running cooldown keeps its full time. Pictures `fighthud`, `fighthud-bow`, `fighthud-1256` (the window size of the retail pictures). |
| `frontend` | The original landing screen (LandingScreenV2) with the list of the host: no list request before the account is ready (the end of the UI load is not the account event, FE9); no rows before the three ready flags in three completion orders; five characters and an empty slot (the row template clones); the row levels (the document value + 1); the default selection (the last played); a click on a row (the id, the radio state, the preview hide and show, the world); a refresh with a new order keeps the selection by id (FE10); the return from the options asks for the list again; Options, Escape, and the exit state; Enter and a click on PLAY give one login request; a failed login (the error popup, its OK, the refresh, PLAY again); the world type FTUE; no login for a character that must be renamed; no character, no world; the camera sequences (the transition plays `IntroToCharacter`, a queued sequence, the level load plays it again, the end after its duration; stage F4); the delete of a character (stage F8: the options of the row, Delete, the hold-to-accept popup; No and a hold of 0.3 s send nothing; a hold of 3 s sends one "menu delete ID"; the result refreshes the list and selects another character; a failed delete; the last character); no Lua error (94 checks). Pictures `frontend-list`, `frontend-select`, `frontend-empty`. |
| `loader` | The screen loader of the game (`uiloader.luac`) at a world entry, with two allowed screens (Vitals2024, InfoModal): it asks only for canvases of its list (`gamedata/loader.json`, 153 paths), one time each, with the ending `.uicanvas` (105 for the world); it counts each load and failure and sets the state HUD; the HUD screen has no autohide and shows, the menu has autohide and shows only in its state. Each canvas that the game has for the two lists (the UI loader and the 69 canvases of `dynamiccanvasloader.luac`) is converted; 2 paths of the UI loader have no canvas in the game. |
| `escape` | The original EscapeMenu and Popup screens driven by keys and clicks (`Swc.HostInput`): Escape in the HUD fires `toggleMenuComponent`, the state goes to "esc", the menu shows with the cursor and the maps player, movement, camera off; exit to desktop opens the quit popup over the menu (the state stays; the map ui off); Escape fires `ui_genpop_cancel` and closes only the popup; the pointer hovers a menu button again; Escape gives `OnEscapeKeyPressed` to the menu and the HUD comes back with its maps; the exit to the main menu opens the quit popup (quitgamepopup.luac), and Yes gives one request "menu quit 1" (stage F6, docs/FRONTEND.md); the game menu: only Settings, Exit to Main Menu, and Exit to Desktop show, no store and no support section, the sections one under the other, the small frame 47 px below the last button. Pictures `escape-menu`, `escape-popup`, `escape-focus`, `escape-quit-main`. |
| `navbar` | The original NavBar in display-only mode with the escape menu (the game menu): Escape in the HUD opens the escape menu as before, the NavBar shows over it with only JOURNAL, CHARACTER, MAP, and the Game Menu gear, the CHARACTER tab is on, no soft load; a click on each tab sets its state at once (Skills, Objectives, Map, Esc: the gear by its sprite); J opens the journal; Escape opens and closes the escape menu; the NavBar hides in the HUD; no Lua error (35 checks). Pictures `navbar-esc`, `navbar-skills`. |
| `loading` | The loading screen of the game (`loadingscreen.luac`) shows and hides with the events of the native code; a refused connection gives the original error popup (`popup.luac` ShowError, `UIError_login_unable_to_connect`) with the cursor, and its OK button closes it. Pictures `loading-start`, `loading-later`, `loading-error`. |
| `respawn` | The original Respawn screen: the dead mark and a death notice of 5 s queue the state "respawn", the list has "Random Location"; a drag on the world map (a press on a tile, the hand-off to the `MagicMap` scroll box) moves its content by the drag vector, `magicmap.luac` sees the drag and its end; Escape opens the escape menu of the respawn state over the screen and Escape goes back to it; the RESPAWN button (on 2 s after the map) gives the host request "respawn 1", the loading screen of the death shows, the revive and its end give the HUD. Pictures `respawn-list`, `respawn-drag`, `respawn-loading`. |
| `tooltip` | The original EscapeMenu and Tooltip screens: with the dead mark the menu buttons have the tooltip "@ui_button_disabled_dead"; the hover of a button shows the dynamic tooltip after the delay of its setter, at the cursor, with the English text and the width of the text and its padding; the end of the hover hides it; with the character alive the button has no tooltip. Picture `tooltip-dead`. |
| `transitions` | The open of the original InfoModal (`OnTransitionIn`, its tweens) and the hover of its button (`button.luac`): each animation that a script starts has the eased value at each frame (60 a second), the final value, and ends after its delay and duration (15 animations; 4 replaced by a new one on the same property, the rule of the gem). The close hides the modal at once (its script has no animation). The ease functions against values of the Penner equations. Pictures `transitions-open-00` to `-24`, `transitions-hover-00` to `-24`, `transitions-close-00`. |
| `inventory` | The original NewInventory, EquipmentV3, Tooltip, Encumbrance, and FlyoutMenu screens with a bag of the host calls (a sword, a shirt, 3 potions, 25 ore), the equipped sword, and the coin. The weight of the bag (14.0 of 200.0); Tab opens the state Inventory with the two screens and the cursor; the bag sections have their items and weights; the hover of the sword gives the item tooltip (gear score 100, core damage 70); a click gives the command list (Equip, Lock, Drop); Equip gives the host request `equip 0 24 1 INSTANCE`; the answer of the server (the swords change places) changes the screens and the weight (14.6); a sword that needs level 21 has "Equip" off ("Requirements not met") and no request; a refusal of the server (3626) keeps the paperdoll; Use of the potions gives `use 2`, the answer changes the weight (18.3); Unequip of the equipped sword gives `unequip 24 -1 1`, the answer empties the slot (21.3); Tab closes. Pictures `inventory-open`, `-tooltip`, `-commands`, `-equipped`, `-refused`, `-unequipped`. |
| `itemtooltips` | AST-259: the hover of each item (a fixed list of the item classes and, when the local file `testdata/ast259-items.json` of the UI content is there, the 44 items of a copy of the user's database: weapons, armor, jewelry, consumables, resources, patterns, runes, the artifacts) opens its tooltip with no script error, and the move away closes it. The fixes of the play test: CharacterAttributeRequestBus GetAttributeBonus (the MOD values of the affixes of the perk x its potency; nil gave "compare number with nil" in IsAttributePerk), the perk type of GetStaticPerkData as the enum ePerkType (perkdatahandler.luac GetSortedPerks dropped a perk with the text type), the perk methods GetLocalizedDescription, IsCharmType, HasEquipAbilities, IsSeasonalPerk, ObjectivesComponentRequestBus GetObjectivesCountForType and GetMaxObjectivesForType (javelin.objectives-max-artifact-quests 10), and the exit hover component (below). Picture `itemtooltips-amulet`. |
| `itemclicks` | AST-260 (the user test "right click I think it should equip/unequip"): the rule of the original scripts. A double click (newinventory.luac ItemDoubleClickAction) equips a bag item (`equip 0 24 1 INSTANCE`) and unequips an equipped item (`unequip 24 -1 1`); the right button held over an item (OnCursorPressed, the CRC-32 of "mouse_button_right") shows its tooltip and, when it can compare, the compare tooltip, with no request; itemdraggable.luac OnReleased ends after a right button press and OpenContextMenu is empty: a right click equips nothing in retail either. Pictures `itemclicks-open`, `itemclicks-right-held`. |
| `loot` | The original CatContainer screen next to NewInventory and EquipmentV3: a bag near the character and Tab give the host request `openloot 57` and the state Container ("LOOT", the loot bag mode); the items of the server show ("2 items", the weight 5.4); LSHIFT and a click on the ore give `take INSTANCE 9`; the answer of the server leaves "1 item"; Take All gives `takeall`; the end of the use closes the panel (the state Inventory). Pictures `loot-panel`, `loot-taken`. |
| `plates` | The original AiMarker plates (`markermanager.luac`, `marker.luac`, `markerstatuseffect.luac`): the scripts register the types and the pools; two grey wolves of the host get the elements a0 and a1: the near one (5 m) with its name, its level 18, the red bar, and the stun icon on a red square at the middle of the view 1.5 m above its feet; the far one (12.4 m, one half of its health) with the alpha 0.52; at its death the bar empties, and a creature out of the pool hides. Pictures `plates`, `plates-dead`. |
| `prompt` | The original prompt cards: a loot bag 2 m ahead gives the card "DROPPED ITEMS" with "E Take All" (the hold form) and "T Open" at the screen place of the bag; a hold of one half fills the ring of the key to one half; T gives the request `openloot 57`; a young tree with no axe gives the gatherable card "YOUNG TREE" with the crossed ring, no key, and "No equipped Axe" with the icon of the gathering type; no focus hides the card. Pictures `prompt-bag`, `prompt-hold`, `prompt-tree`. |
| `notices` | The original damage numbers and loot ticker: 64 slash (white, the slash icon), 132 critical (orange), 85 thrust at a wolf (yellow with the arrow: the weakness of the wolf), 151 strike to the own character (red), a heal "+120" (green); the numbers go back to the pool after their animation; 9 iron ore with the cause 1 give "+9 Iron Ore" (the icon: the count in the bag, 9); 5 rawhide while that row shows give "+5"; 27 experience give "+27 XP"; the first total gives no line; a load of the bag gives no notice. Pictures `notices-damage`, `notices-loot`, `notices-xp`. |
| `tickercoin` | The coin rows of the loot ticker (user test 2026-10-26, 8c): the wallet of the world entry gives no row; a gain of 5.40 shows "+5.40" and goes after 5 s (2 s delay, 3 s fade); two gains merge (+2.25) and go 5 s after the second; a gain while the HUD is hidden goes too. With the tween rule before, 2 checks fail (and 1 of `notices`: the progression line goes). |
| `conversationpress` | E in the frame of the open of a Talk (user test 2026-10-26, 8d): Cecilia Polly, the first Talk of the runtime (option 1 has the OnInit callback): no stack overflow, no request, the Talk stays, a later E ends it; Saira Yangandul (two lines): E at the open does not end the new Talk, E shows the second line, E at the last line ends it. |
| `text` | The original InfoModal screen (`infomodal.luac`, `modalrow_text.luac`) shows four text rows by its own scripts: wrap, `<font color face>` pieces, an inline image, a link with a line end; the rows do not overlap. Then the gesture tag, raw text for markup that is not valid, ellipsis, shrink to fit, clip. Pictures `text-modal`, `text-overflow`. |

The layout check (C++, in the test program): each converted canvas of 1920 x 1080 loads into a model with no scripts,
and each UI slice spawns into one; the layouts compute; each child of a layout must have the offsets that the editor
saved. Result: 959 layouts, 1,450 children, 0 differ. Apart: the layouts with the default cells of texts compare with
the font metrics of the tests (`tests/ui/font_measure.cpp`, stb_truetype, `tests/ui/vendor/stb`): 806 of 907 children
agree within 1 px (the rest: the integer advances of the CryFont size cache and wraps at the edge); a guard needs 85%.
Not compared: 2 layouts whose saved child offsets are older than the last edit of their canvas.

Exit check of stage P2.2 (development runs, 2026-10-18; `SWC_WORK/evidence/P2.2`):

```sh
export SWC_STAGE=P2.2 SWC_DEV_CHARACTER=Devwarden SWC_DEV_PLACE=8872:4250:138.25
tools/creature-check.sh ui-exit 70 tests/drive/ui-exit-routes.txt tests/drive/ui-exit-player.txt -- -SwcUiDrive=$PWD/tests/drive/ui-exit.txt
tools/creature-check.sh ui-tooltip 50 tests/drive/ui-tooltip-routes.txt tests/drive/ui-tooltip-player.txt -- -SwcUiDrive=$PWD/tests/drive/ui-tooltip.txt
tools/ui-check.sh ui-refused 45 tests/drive/ui-refused.txt --refused
```

`ui-exit.mp4`: W moves the character; Escape opens the escape menu (the cursor shows; W stays out of the game);
exit to desktop opens the nested quit popup; Escape closes only the popup; the pointer hovers the menu buttons again;
Escape closes the menu (the cursor hides) and W moves the character again; the death gives the respawn screen; two
drags move its world map; RESPAWN gives the loading screen of the death, then the HUD; `end` releases the held keys W
and D. `ui-tooltip.mp4`: the escape menu of the respawn state shows the tooltip of a dead character at the cursor;
Escape goes back to the respawn screen. `ui-refused.mp4`: the error popup of the game with the cursor; OK closes it.
The UI lines of each run are in `LABEL-ui.txt`.

Exit check of stage P2.4 (development runs, 2026-10-18; `SWC_WORK/evidence/P2.4`):

```sh
export SWC_STAGE=P2.4 SWC_DEV_CHARACTER=Devwarden SWC_DEV_PLACE=8872:4250:138.25
tools/creature-check.sh ui-equip 74 tests/drive/ui-equip-routes.txt tests/drive/ui-exit-player.txt -- -SwcUiDrive=$PWD/tests/drive/ui-equip.txt
tools/creature-check.sh ui-loot 200 tests/drive/ui-loot-routes.txt tests/drive/ui-loot-player.txt -- -SwcUiDrive=$PWD/tests/drive/ui-loot.txt
tools/test-items.sh
```

`ui-equip.mp4` (`ui-equip-sheet.png`): Tab opens the inventory with the bag of the server; the hover of the sword gives its
item tooltip and a click the command list; Equip puts it into weapon slot 24 (the server: "equip ... done"; the weight 76.8
to 73.8); the command list of the equipped sword has Unequip (the slot is empty again, 76.8); the potions have Use (3 to 2,
76.3); the sword of tier 3 (level 21; the routes set level 10) has Equip off with "Requirements not met", and no request goes
out. `ui-loot.mp4` (`ui-loot-sheet.png`): five kills of grey wolves give bags; Tab near the nearest bag opens the panel "LOOT"
(OpenStorageOption); LSHIFT and a click take its item (2942: "1 moved"); the Looted claim ends the use (2473) and closes the
panel; Tab twice opens the next bag; Take All takes its item (2939). The clocks: the routes start after the world entry of
the log, which can come up to 45 s after the clock of the drives starts; the UI drives wait for the items of the routes
(`waitfor item:...`, `waitfor lootbag`). `tools/test-items.sh`: the bag, the paperdoll items, the coin, and the level of
the encoders of the backend into the read path, and the requests against the readers of the backend (31 checks).

`tools/dev-build.sh` gives a preset of the backend (`--list` shows them) to the dev character. It uses the
development routes of the backend (`--dev-commands`): only in a development session, never with the data folder of
the user ([AGENTS.md](../AGENTS.md), rules).

Exit check of stage WF7 (development runs, 2026-10-18; `SWC_WORK/evidence/WF7`; no debug text: `SWC_DEBUG_LINES=0`):

```sh
export SWC_STAGE=WF7 SWC_DEV_CHARACTER=Devwarden SWC_DEV_PLACE=8872:4250:138.25 SWC_DEBUG_LINES=0
tools/creature-check.sh wf7-plates 95 tests/drive/wf7-plates-routes.txt tests/drive/wf7-plates-player.txt
tools/creature-check.sh wf7-fight 110 tests/drive/wf7-fight-routes.txt tests/drive/wf7-fight-player.txt
tools/creature-check.sh wf7-prompt 160 tests/drive/wf7-prompt-routes.txt tests/drive/wf7-prompt-player.txt -- \
    -SwcUiDrive=$PWD/tests/drive/wf7-prompt.txt -SwcDrop=90:OreT1
unset SWC_DEV_PLACE
tools/creature-check.sh wf7-gathering 170 tests/drive/wf7-gathering-routes.txt tests/creatures/gathering.txt
tools/match-view.sh set1-oxboro noon-n noon-e dusk-w night-s --out $SWC_WORK/evidence/WF7/matchview --label wf7-matchview
```

`wf7-plates.mp4`: three grey wolves of level 18 (14 m, 9 m at one half of their health, 4 m stunned): the AiMarker plates (the
name, the red bar, the level mark, the stun icon on its red square); the far plate fades; the near wolf dies. `wf7-fight.mp4`: the
swings of the player give the damage numbers of the original canvas at the wolf (white, the yellow numbers with the arrow of the
weakness to thrust, the orange critical hit of the kill, the lightning part); the bites give the red numbers of the own character;
the body has the prompt "E GREY WOLF". `wf7-prompt.mp4`: the dropped ore gives the bag card "DROPPED ITEMS", "E Take All", "T Open";
T opens the panel (OpenStorageOption), Escape closes it (2473), E held takes the items (the ring of the key; TakeAllOption), the
loot ticker shows the ore. `wf7-gathering.mp4`: the cards of the gathering check of P5.2: "YOUNG TREE" with the crossed ring and
"No equipped Axe", the tree with the axe, the ore vein, the cranberries, the rabbit body ("E CHALKY RABBIT"), the powder keg
("Ignite Fuse"). `matchview/`: four views of set 1 with no debug text. The side-by-side pictures `compare-*.png`: retail (the
Soulwarden evidence of creatures, abilities-crit, fight, loot, loot-bags, gathering) | client.

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
- The heart rune slot shows the icon of the rune and its charge (stage MVP-A). The key G (input bit 450) is not in the input map
  yet: the slot plays no activation, also for the supported rune Detonate (order 11 of the MVP plan).
- A sliced radial fill clips with lines in canvas space; O3DE clips in viewport space. The angles are the same unless
  the element has a scale that is not uniform.
- A sprite mask in a sprite mask: only the innermost one applies (LyShine combines the two in the stencil).
- O13: the simple tooltip works (the text, the size, the place). `LocalizeWithDataSheetData` gives the localized text;
  the expressions of the data sheets in a text (`{[...]}`) stay as text. The item tooltip sections (stats, perks,
  compare) come with the inventory screens (P2.4).
- O14: after the error popup of a refused connection, the client has no front end (no login screen, no new try): it
  stays in the empty level. Stage F6 (docs/FRONTEND.md) closes the paths after the account: a refused login token
  (F5) and a world connection that ends go back to the landing screen with the error popup, and PLAY can try again.
  A refused authentication at the start (before the account) still has no new try (this client has no login screen).
- The escape menu over the respawn screen: the draw order of the game (the respawn screen 20, the escape menu 41)
  puts the menu on top. Its buttons are off while the character is dead (the script).
- O12: the escape rule and the states of the escape menu are rules of the emulation from the scripts (the native
  screen manager of the client has them). The NavBar script (`navbar.luac` OnCryAction) has the retail Escape logic; the
  NavBar of this client does not take the escape and journal actions (display only, the game menu). Full parity with that
  logic is a later task: the NavBar takes the actions, the rules of the emulation step back, and each escape flow is checked
  again. The popup grid list (`simplegriditemlist.luac`) finds no row table in
  `OnInit`: the quit popup works; the split stack popup is not checked.
- O11: the sprite mask in the client window. With `-SwcUiMaskMaterial` the mask material compiles and its instances
  have the textures of the item (the game thread and the render thread show them, the texture reference points at
  each texture), but the window shows the default (white) textures of the material for them. An engine texture
  asset as the image parameter draws. Thus the client uses the quad clip; the offline runtime and the preview apply
  the sprite masks.
- Without a frame limit (`-nullrhi`), the runtime ticks with each frame.
- The items (stage P2.4): the attribute part of the core damage waits for the attribute state of the read path (the
  equipment screen shows the place-holder values of its canvas); `isMagic` of a weapon (the label "core power"); the
  ammunition stats; the equipment sets (their perks and names: no bonus lines); the defaults of the client settings
  that the item screens do not read (183 settings of the scripts have a default that is not false or 0); the key hints
  of the command list (Lock, Drop) show no key; the largest weight of the
  panel after the state of the bag (the live check showed 1000.0; the rule is not known); the objective data (the quests: `GetObjectiveData` gives no title); the
  expressions of the data sheets in the item texts (O13: the potion text shows `{[400.000000]}`).
- The world views (stage WF7):
  - The marker types of the objects of the level data and of the bodies are not converted: the card of a prompt comes from a
    table of this client (a chest, a gathering type, a lore page; a body has the AI type "Wolf").
  - The rules of the native marker code that the scripts do not show are rules of this client: the ramps of the scale and the
    alpha between the distances of the marker params, the hide of an occluded marker, the order of the pool (the shown
    creatures first, then the nearest). The marker data `VerticalOffset` (m_verticalOffset of the slice) is not written.
  - The read path has no threat of a creature (`IsTargetingPlayer` is false: no threat mark) and no creature stamina (the
    stamina bar of a plate hides).
  - The status texts of the damage numbers (`OnStatusEffectDisplayed`, ShowTextInDamageNumbers) and the
    generic numbers are not fed.
  - The gather work bar of the HUD ("29.3 remaining") is a different element; the circle timer of a gatherable card is not fed.
  - The loot ticker: the mastery line (+N War Hammer Mastery) needs the categorical progression state (not in the read path).
  - `lootticker2024canvas.luac` gives one error at its load (the loop of an empty array property from index 0), after the
    visibility is set; `hudcanvasdefault.luac` has the same error form.
  - The rule of the queued tweens comes from the scripts (damagenumber.luac, button.luac); the code of the gem is not in the game
    files.
- The census and the options (stage P2.3):
  - The dynamic canvas loader does not load in the client (no screen of its list is in a done stage); the fixture runs it.
  - 35 option nodes have no default from the program (their neutral value shows; the log lists them); the options other
    than the keys are not saved and do not change the client (P6.9). The version of the About page is the text of this
    client.
  - The pad: the action maps of the converter keep only the keyboard and mouse inputs (the controller tab shows no button
    labels); the id of an input profile is the row number (an inference). The zoom keys stay on the wheel.
  - `javelin.enable-exit-survey` has no reference in the code of the program: not set (its row does not show).
  - A rebind of the row Jump / Traverse also moves sprint, jump_hold, and moveup to the new key: the key page calls
    RebindAction for the four actions of the movement map that have Space in the profile data. The retail behavior is not
    checked yet.
- The conversation screen (the Talk with a person): not started. The focus of the user (2026-10-19, docs/ROADMAP.md) puts
  the interactions after the world, the graphics, and the gameplay. The retail references are set 5
  (`~/Games/soulwarden/work/evidence/unreal-references/set5-talk/`: the idle and talk views of three persons, the
  conversation screen, a refused talk; its README).
- The fight HUD (stage P3.H):
  - The name of the place under the compass (`PoiName` of `compass.luac`: "OXBORO" in the retail pictures) needs the territory
    of the player (`TerritoryDefinitionsDataManagerBus`, the area events `OnUiTriggerAreaEventEntered`). The backend sends no
    territory; the retail client finds it from the area data of the world, and no track converts the territory or POI area
    shapes yet. The plan: a converter item writes the trigger areas of the slices with their shapes and their territory, area,
    and POI ids (the slice walk of `converter/swc_convert/decals.py`); the client fires the area events from the position of the
    character by the rule of the game data (not by the nearest landmark).
  - `Hud.LocalPlayer.HasReticleTarget` is false: the target rule of the native code is not known (`ReticleRayCastDistance` is
    3.0 for the weapons). The `ReticleTargetName` of the sword is `spearHaveTarget` (the throw reticle of the manager); whether
    the native code sets the target name in `ReticleToShow` is not known.
  - The action bar (`hud2024/actionbar/actionbar`: the social and chat buttons at the bottom left) is not loaded: not a part of
    the fight HUD.
  - Not checked against retail: the bites on the character and their numbers (in the two client runs the wolves died before
    they attacked), the respawn (the RESPAWN click of the UI drive came too early; `tests/drive/p3h-fight2-ui.txt` now clicks
    later; the retail respawn has no loading screen, the client shows the loading screen of a respawn), the stamina and mana
    bars in use (`player-dodge-stamina-2`, `ui-abilities-lifestaff`, `ui-mana-empty`), the option pages.
  - The retail screen gets dark and red at low health and at a hit (a screen effect of the native code, not a canvas): the
    client has no such effect.
  - The retail client dims the keys of all quick slots for about 0.5 s at a use (the drink state); the client has no drink
    state (the drink animation is an item of the player track).
  - The plate of a creature is about 30 percent wider than in retail at the same distance (the cause is not known: the
    camera, or the scale of the world view canvas).
  - The client shows the prompt "E GREY WOLF" over a dead wolf at 3 to 9 m; the retail takes of set 6 show no prompt (their
    kills gave no loot: "the creatures ignore it").

## The key hints (AST-231)

- The HelpBar (lyshineui/helpbar/helpbar: the loader group 8, no autohide) and the chat widget (lyshineui/hud/chat/chatwidget) are in
  the screens of the client (`UI/SwcUiSubsystem.cpp` HudScreens). The screens push their actions to the help bar
  (DynamicBus.HelpBar PushToStack, SetGlobalActionsToDefault: "ESC Back"); the conversation screen pushes "E Next"; the crafting
  screen pushes "SPACE Max Quantity" only for a recipe that can be crafted with a max quantity above 1 (craftingscreenv4.luac
  OnCanCraftUpdated).
- The chat widget shows "ENTER Chat" with the flag UIFeatures.g_chatWidget true (swc/game.lua; the retail pictures show it). Its own
  list hides it in the HUD state and the Talk state (screenStatesToHide), as retail. The retail HUD line "ENTER Chat" is the Chat
  screen (LyShineUI/HUD/Chat/Chat, group 2): not loaded (no chat service).
- The key of an action: the names compare with no case (swc/keys.lua): the screen asks "trySkip", the map crafting has "tryskip".
- Check (evidence AST-231: ast231-help4.mp4, ast231-max2.mp4, ast231-help-crops.png retail | client): the Talk screen "E Next
  ESC Back", the character window "ENTER Chat ... ESC Back", the crafting screen "ENTER Chat ... SPACE Max Quantity ESC Back".

## The mission boards (C-12 of ut567)

The town board of a settlement (QG6a). The source is the static read `~/Games/soulwarden-client/work/ut567/qgplan/boards/ANSWERS.md`
and the backend encoders (states 3829, 1746, 3086; requests 3819, 2461).

- The open: the option of action 51 (OpenMissionBoardOption) of a board object shows again (the hide rule of AST-287 is removed). The
  interactor (Interact/SwcInteractor) keeps the board use open after the execution. UI/SwcUiBoards calls `Swc.HostBoard("open", ACTOR)`,
  and swc/boards.lua sets `Hud.TerritoryGovernance.EntityId` and starts the state 640726528 (TownProjects). With no screen 2 s after the
  open, the use ends.
- The offers: the client computes them (Boards/SwcTownOffers, the same rules as the backend generator: the parity test
  `tests/net/board_offer_tests.cpp`, 1,872 runs). The input is `ui/gamedata/missiontown.json` (tools/convert-missions.sh), the
  characterId, the server clock, the projects of the board (1746), the town entry of the provider state (3829 field 3), the trade skill
  ranks, and the level of the screen (the wire level plus 1). The territory is `Hud.LocalPlayer.CurrentAreaTerritory.ClaimKey`.
- The data of the screen: `ui/gamedata/missions.json` (the mission rows with their cells, the projects, the events).
  ObjectivesDataManagerBus GetMissionData, TerritoryGovernanceRequestBus, LandClaimRequestBus GetTerritoryProgressionData, and
  TerritoryInterfaceComponentRequestBus come from these files and from the board states.
- The provider identity is the Claim landmark actor of the territory (landmark type 4). The id of a claim row of the map
  (MapComponentBus GetClaims) is that actor: the screen finds an accepted card through the destinationOverride of the mission.
- The requests: a card accept is "board accept MISSION PROJECT" (3819). A hand-in is "board complete INSTANCE" (2461). The close of the
  screen (RequestCancelCommittedInteraction) is "board end".
- CanCompleteMission: the root container has the progress of its child count minus 1, its last child is not complete, and the
  destinationOverride is the provider of the current territory.
- The board use of a level data object (kind missionBoard): the focus keeps the board flag of action 51. The board objects and the world
  entity have no place state: the snapshot keeps them in their own list (`FSnapshot::Boards`, `OtherEntity::has_board_state`). Window 1
  (2026-10-29): the screen opened, the seed was the seed of the backend, and the provider identity was the Claim landmark actor; the
  offers were empty before this list.
- The checks: the fixture `tests/ui/fixtures/boards.lua`, the claim row check of `tests/ui/fixtures/territory.lua`.
- The project grid (simplegriditemlist.luac, 2 columns of 744 px in each row): the dynamic scroll box makes its first copies at its own
  InGamePostActivate, children before parents, before the OnInit of the grid script (swc/elements.lua; static read
  `~/Games/soulwarden-client/work/ut567/qgplan/gridclone/ANSWERS.md`). Its first refresh fills the pool to the default count (5). An item
  takes the size settings of an item, not of a header (swc/dynamic.lua entry_size). Before, each row had 4 columns, 2 with the template.
- The hand-in (retail qg6a I5): the banner "You have N completed mission(s)" with Complete All Missions, its popup (the items, the total
  reward), Accept: RequestMissionCompletion of each ready mission (2461). After it the town entry makes the mission unavailable in the
  seed: a change of the available flag of an offer refreshes the open screen (Hud.Objective.OnActiveObjectiveUpdate). Window 2b
  (2026-10-30): 30 Stone Block given, the card COMPLETED, the popup 130 xp / 1.08 coin / 350 standing, the backend hand-in with the
  same reward as the success event of the mission row (CMission_Success_Gather_RefinedT2_Small), the banner Standing Rank Up.
- The pictures of the cards: `imagePath` (an item picture) or `detailImagePath` (a full card picture of
  `lyshineui/images/missionimages`, the kill and explore cards). The mission images come with
  `tools/convert-ui.sh textures --folder lyshineui/images/missionimages` (49 files; the shared content had 1).
- The hover of a card fades the picture (townprojects_availabletask.luac OnFocus: the fade 0.5 of Icon; the image alpha 0.4): measured in
  the fixture. Open point: the plus sign of a potion card has no circle (retail: a circle with a plus).
- The pin rule: a town mission is not pinned at the accept (the auto-pin flag of CommunityGoal is off; retail qg6a I3 shows no tracker
  entry after the accept).
- Open point: the faction board (option 8, state 2609973752, 3816) is phase 2.
