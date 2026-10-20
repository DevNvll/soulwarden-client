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
  `requiresMouse` (`Swc.HostCursor`). A key goes to the game unless each game map (movement, combat, player, camera)
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
  give empty data (no points of interest, no territories, no NPC markers), the group and raid sizes 5 and 10.
- A bus method that the emulation does not define goes to the script handlers of that bus that have it (an EBus with
  a Lua handler: the popup screen has `UiPopupBus`), else a stub.
- The data layer gives a data observer the value also when it is false (the end of a death: `IsDead` false).

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
  `equipmentcommon.luac`): the item has a paperdoll slot, its RequiredLevel (the level of the wire; the tooltip shows it
  plus 1) is not above the level of the wire, and its gear score is not above the MaxEquipGS of the owned expansion
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
| active | this client loads it in the world (`HudScreens` of `UI/SwcUiSubsystem.cpp`, the globals, the loading screen; stage P3.H adds the consumable quick slots, the XP meter, and the reticles; stage F2 the landing screen of the front end, docs/FRONTEND.md) | 25 |
| stage | the retail client can load it on this backend with the values of the build; a later stage owns it | 120 |
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
eight), Keyboard & Mouse Bindings, Controller Bindings.

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
- The rows of the features of the display device (the resolution scaling, DLSS frame generation, Reflex) do not show: this
  client has no such feature (`GetFsrSupported` and the others are false). The retail client shows them on a device with
  the feature (the picture of set 1).
- A row whose node has no default from the program gets the neutral value of its row at the end of the UI load (a toggle
  off, a slider at its minimum, a drop-down at its first item): 35 nodes; the log line `options: ...` lists them (the
  accessibility and subtitle rows, the voice and stream rows, the chat rows, some control rows).
- `OptionsDataBus`: the reads of the screen; `Reset<Category>Settings` sets the values of the rows of the category again.
  `Set*` and `SerializeOptions` are stubs: the screen sets the nodes itself; the options other than the keys are not saved and
  do not change the client in this stage (P6.9).
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
  with the value of that moment as its start; a tween with no delay removes the queued tweens of its property (the fade in and out
  of a damage number, the pulse of a button hover).
- The loot ticker (`lyshineui/hud2024/lootticker/lootticker2024`): a bag slot with the sync cause 1, 6, 20, 25, 37, or 40 (34 for a
  slot that the vector had) and a larger quantity gives `OnLootReceived(slot, the new quantity less the prior quantity)`
  (Soulwarden research loot.md: the gates 0x48DA750 and 0x48E4130); the load of the bag (0x11) and the cause 0 give none. The
  icon of the item shows the count of the item in the bag (`ContainerRequestBus` `GetItemCount` with the item descriptor). The
  experience: `Hud.LocalPlayer.Progression.Glory` (the total), `PrevRequiredGlory` and `RequiredGlory` (the level table
  `gamedata/xplevels.json`); the ticker takes the first total of the world entry with no line. The flag
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
| `options` | The original Options screen from the button Settings of the escape menu: the defaults of the client program (the camera shake on, the master volume 100), a label with more text ("Monitor 1"), a picture of each category (Game, Keyboard & Mouse, Controller, Video, Audio, Social, Accessibility, About) and of the two binding tabs; the key page: a click on the jump row starts the listen (the client gets `keybinding 1`), a key that no action has binds the jump (the UI actions use it; the client gets the rebind and the save; the hints get the key), the key of the journal (J) gives the conflict popup, the reset of the page sets the defaults again (`rebind-reset`). Pictures `options-*`. |
| `fighthud` | The fight HUD screens (Vitals2024, WeaponQuickslots, ConsumableQuickslots, XP, Compass, ReticlesCanvas) with a player of the host calls: two weapons with ability bars, the heart rune, three consumables in the quick slots. The quick slot 1 shows the count 9 and the key 3; the empty slot 3 shows only the key 5; the level 65 shows under the vitals and the XP popup of the spawn is out after 3 s; the weapon slots show the other weapon (key 2) and the heart rune (key G); the reticle of the sword is the melee reticle
(`ReticleToShow` 3488054177), it shows with the weapon sheathed; a drawn bow gives the bow reticle (2551600555). The HUD edit mode sets the
default HUD layout. A cut of a running cooldown keeps its full time. Pictures `fighthud`, `fighthud-bow`, `fighthud-1256` (the window size of the retail pictures). |
| `frontend` | The original landing screen (LandingScreenV2) with the list of the host: no list request before the account is ready (the end of the UI load is not the account event, FE9); no rows before the three ready flags in three completion orders; five characters and an empty slot (the row template clones); the row levels (the document value + 1); the default selection (the last played); a click on a row (the id, the radio state, the preview hide and show, the world); a refresh with a new order keeps the selection by id (FE10); the return from the options asks for the list again; Options, Escape, and the exit state; Enter and a click on PLAY give one login request; a failed login (the error popup, its OK, the refresh, PLAY again); the world type FTUE; no login for a character that must be renamed; no character, no world; no Lua error. Pictures `frontend-list`, `frontend-select`, `frontend-empty`. |
| `loader` | The screen loader of the game (`uiloader.luac`) at a world entry, with two allowed screens (Vitals2024, InfoModal): it asks only for canvases of its list (`gamedata/loader.json`, 153 paths), one time each, with the ending `.uicanvas` (105 for the world); it counts each load and failure and sets the state HUD; the HUD screen has no autohide and shows, the menu has autohide and shows only in its state. Each canvas that the game has for the two lists (the UI loader and the 69 canvases of `dynamiccanvasloader.luac`) is converted; 2 paths of the UI loader have no canvas in the game. |
| `escape` | The original EscapeMenu and Popup screens driven by keys and clicks (`Swc.HostInput`): Escape in the HUD fires `toggleMenuComponent`, the state goes to "esc", the menu shows with the cursor and the maps player, movement, camera off; exit to desktop opens the quit popup over the menu (the state stays; the map ui off); Escape fires `ui_genpop_cancel` and closes only the popup; the pointer hovers a menu button again; Escape gives `OnEscapeKeyPressed` to the menu and the HUD comes back with its maps. Pictures `escape-menu`, `escape-popup`, `escape-focus`. |
| `loading` | The loading screen of the game (`loadingscreen.luac`) shows and hides with the events of the native code; a refused connection gives the original error popup (`popup.luac` ShowError, `UIError_login_unable_to_connect`) with the cursor, and its OK button closes it. Pictures `loading-start`, `loading-later`, `loading-error`. |
| `respawn` | The original Respawn screen: the dead mark and a death notice of 5 s queue the state "respawn", the list has "Random Location"; a drag on the world map (a press on a tile, the hand-off to the `MagicMap` scroll box) moves its content by the drag vector, `magicmap.luac` sees the drag and its end; Escape opens the escape menu of the respawn state over the screen and Escape goes back to it; the RESPAWN button (on 2 s after the map) gives the host request "respawn 1", the loading screen of the death shows, the revive and its end give the HUD. Pictures `respawn-list`, `respawn-drag`, `respawn-loading`. |
| `tooltip` | The original EscapeMenu and Tooltip screens: with the dead mark the menu buttons have the tooltip "@ui_button_disabled_dead"; the hover of a button shows the dynamic tooltip after the delay of its setter, at the cursor, with the English text and the width of the text and its padding; the end of the hover hides it; with the character alive the button has no tooltip. Picture `tooltip-dead`. |
| `transitions` | The open of the original InfoModal (`OnTransitionIn`, its tweens) and the hover of its button (`button.luac`): each animation that a script starts has the eased value at each frame (60 a second), the final value, and ends after its delay and duration (15 animations; 4 replaced by a new one on the same property, the rule of the gem). The close hides the modal at once (its script has no animation). The ease functions against values of the Penner equations. Pictures `transitions-open-00` to `-24`, `transitions-hover-00` to `-24`, `transitions-close-00`. |
| `inventory` | The original NewInventory, EquipmentV3, Tooltip, Encumbrance, and FlyoutMenu screens with a bag of the host calls (a sword, a shirt, 3 potions, 25 ore), the equipped sword, and the coin. The weight of the bag (14.0 of 200.0); Tab opens the state Inventory with the two screens and the cursor; the bag sections have their items and weights; the hover of the sword gives the item tooltip (gear score 100, core damage 70); a click gives the command list (Equip, Lock, Drop); Equip gives the host request `equip 0 24 1 INSTANCE`; the answer of the server (the swords change places) changes the screens and the weight (14.6); a sword that needs level 21 has "Equip" off ("Requirements not met") and no request; a refusal of the server (3626) keeps the paperdoll; Use of the potions gives `use 2`, the answer changes the weight (18.3); Unequip of the equipped sword gives `unequip 24 -1 1`, the answer empties the slot (21.3); Tab closes. Pictures `inventory-open`, `-tooltip`, `-commands`, `-equipped`, `-refused`, `-unequipped`. |
| `loot` | The original CatContainer screen next to NewInventory and EquipmentV3: a bag near the character and Tab give the host request `openloot 57` and the state Container ("LOOT", the loot bag mode); the items of the server show ("2 items", the weight 5.4); LSHIFT and a click on the ore give `take INSTANCE 9`; the answer of the server leaves "1 item"; Take All gives `takeall`; the end of the use closes the panel (the state Inventory). Pictures `loot-panel`, `loot-taken`. |
| `plates` | The original AiMarker plates (`markermanager.luac`, `marker.luac`, `markerstatuseffect.luac`): the scripts register the types and the pools; two grey wolves of the host get the elements a0 and a1: the near one (5 m) with its name, its level 18, the red bar, and the stun icon on a red square at the middle of the view 1.5 m above its feet; the far one (12.4 m, one half of its health) with the alpha 0.52; at its death the bar empties, and a creature out of the pool hides. Pictures `plates`, `plates-dead`. |
| `prompt` | The original prompt cards: a loot bag 2 m ahead gives the card "DROPPED ITEMS" with "E Take All" (the hold form) and "T Open" at the screen place of the bag; a hold of one half fills the ring of the key to one half; T gives the request `openloot 57`; a young tree with no axe gives the gatherable card "YOUNG TREE" with the crossed ring, no key, and "No equipped Axe" with the icon of the gathering type; no focus hides the card. Pictures `prompt-bag`, `prompt-hold`, `prompt-tree`. |
| `notices` | The original damage numbers and loot ticker: 64 slash (white, the slash icon), 132 critical (orange), 85 thrust at a wolf (yellow with the arrow: the weakness of the wolf), 151 strike to the own character (red), a heal "+120" (green); the numbers go back to the pool after their animation; 9 iron ore with the cause 1 give "+9 Iron Ore" (the icon: the count in the bag, 9); 5 rawhide while that row shows give "+5"; 27 experience give "+27 XP"; the first total gives no line; a load of the bag gives no notice. Pictures `notices-damage`, `notices-loot`, `notices-xp`. |
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
- The heart rune slot shows the icon of the rune. Its charge (`Hud.LocalPlayer.Vitals.ChargePercent`) has no source in
  the read path of the backend yet, thus the slot does not show the charge.
- A sliced radial fill clips with lines in canvas space; O3DE clips in viewport space. The angles are the same unless
  the element has a scale that is not uniform.
- A sprite mask in a sprite mask: only the innermost one applies (LyShine combines the two in the stencil).
- O13: the simple tooltip works (the text, the size, the place). `LocalizeWithDataSheetData` gives the localized text;
  the expressions of the data sheets in a text (`{[...]}`) stay as text. The item tooltip sections (stats, perks,
  compare) come with the inventory screens (P2.4).
- O14: after the error popup of a refused connection, the client has no front end (no login screen, no new try): it
  stays in the empty level.
- The escape menu over the respawn screen: the draw order of the game (the respawn screen 20, the escape menu 41)
  puts the menu on top. Its buttons are off while the character is dead (the script).
- O12: the escape rule and the states of the escape menu are rules of the emulation from the scripts (the native
  screen manager of the client has them). The popup grid list (`simplegriditemlist.luac`) finds no row table in
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
