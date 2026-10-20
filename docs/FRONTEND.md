# The front end and the character selection

Static research, 2026-10-19. Retail client build `1.400.6031.6004151`.
The client source of this pass is `main` at `c7e668e`.
No game, editor, build, or development run started in this pass.

The first implementation should run `LandingScreenV2` with the original scripts.
It needs a native menu adapter and a local preview character. The HTTP list already arrives.
The list alone is not enough to run the screen.

## Evidence and scope

- D: a game file read in this pass. A script reference names the original `.luac` and its function.
  The `.lua.txt` files are reading aids, not source code. Fresh bytecode listings also check
  `SelectCharacter` and `OnLoginInfoListResult` with `tools/lib/lua_listing.py`.
- C: the Unreal client source at the base commit above. These facts describe the current code.
- R: earlier research. Native addresses below come from that research and its saved decompiler output.
  Each address is image-relative for the retail build above. This pass did not open Ghidra.
  The project at `~/Games/soulwarden/ghidra` has lock files.
- P: a proposed implementation or test. It has no live proof.

Sources used throughout this document:

| Key | Source |
| --- | --- |
| D1 | `lyshineui/main menu/landingscreenv2.luac`, with the functions named below. |
| D2 | `lyshineui/main menu/landingscreenv2.dynamicuicanvas`. The converted file is `ui/canvases/lyshineui/main menu/landingscreenv2.json`. |
| D3 | `lyshineui/_common/worldlistcommon.luac`, `CharacterDataVectorToTable`, `CharacterNameVectorToTable`, `RegionVectorToTable`, `WorldVectorToTable`, `UpdateWorldDataWithCMS`. |
| D4 | `libs/ui/uistates.xml`. |
| D5 | `levels/nw_frontend_nighthaven/level.pak`, `mission0.entities_xml`, `mission_mission0.xml`, and `leveldata.xml`. |
| D6 | `lyshineui/_common/cinematicutils.luac`, `PlayCinematic`, `StopSequence`, `OnCinematicStateChanged`. |
| R1 | `~/Games/soulwarden/work/research/menu-equipment.md`, sections 1 to 3 and Unknowns. |
| R2 | `~/Games/soulwarden/work/research/character-creation.md`, sections 1 to 4 and the address index. |
| R3 | `~/Games/soulwarden/work/research/appearance.md`, sections 2, 3, and 7. R2 corrects its old XML-only interpretation. |
| R4 | `~/Games/soulwarden-client/research/ui.md`, sections 2 and 3. |

Scratch files are in `~/Games/soulwarden-client/research/frontend/`.
They include the asset list, script call lists, bytecode listings, canvas summary, scene XML,
sequence records, and light records. No game content or decoded document goes into git.

## Rules

- Keep the original Lua scripts and canvases. Add their missing native contracts to the emulation.
- Keep the menu model separate from the replicated world model. No game-server connection is needed
  to select or preview a character. [D1 `SelectCharacter`; R3 7.1]
- Select by character id. A display name or an array index is not an identity. Recheck the selected
  id against the current account list before Play or delete. [P; `NETWORK.md` account contract]
- Never log a menu document, an account identity, a persona name, or a player character name.
  Fixture identities and names are made up. Keep reports outside the repository.
- Keep the loopback checks, the backend release copy, and the run namespace of `NETWORK.md`.
- A value with no static proof stays an open point. An offline fixture proves the adapter behavior.
  It does not prove retail timing or appearance.

## The retail screens

There is no separate character-selection canvas in this flow.
`LandingScreenV2` contains the character list and its surrounding panels. [D1; D2]
The canvas has 3,383 serialized elements and 44 distinct attached script paths. These counts include
all panels, also those that are hidden. Its canvas size is 1920 x 1080. It requests the mouse,
is not escapable, is replaceable, and has draw layer `EscapeMenu`. It has no action-map override.
The canvas file has draw order 0. The host still needs to register it with the screen manager. [D2]

| Canvas, relative to `lyshineui/` | Scripts and entry points | Place in the flow |
| --- | --- | --- |
| `main menu/landingscreenv2.dynamicuicanvas` | `main menu/landingscreenv2.luac`: `OnInit`, `OnTransitionIn`, `BeginConnection`, `RefreshWorldList`, `OnTransitionOut`, `OnShutdown`. Root script element `LandingMaster`. | Character selection, region and world selection, first settings, and the landing panels. [D1; D2] |
| The character rows inside that canvas | `main menu/charactercreatebutton.luac`: `SetSlotStatus`, `SetCharacterInfo`, `OnSelected`, `OnUnselected`, `OnChangeCharacter`, `OnPress`. Also `_common/charinfocard.luac`. | Row name, level, world, crest, selected state, create, delete, and transfer controls. The active row template is in the canvas. [D2; those scripts] |
| The world list inside that canvas | `worldlist/worldlist.luac`, `worldlist/worldlistinfocard.luac`, `worldlist/worldlistrefreshbutton.luac`, `main menu/worldselectlistitem.luac`, `worldselectrecommendeditem.luac`. | World rows, sort, refresh, and region controls. [D2] |
| `options/options.dynamicuicanvas` | `options/options.luac`: `OnAuthComplete`, screen transitions. | Options from the menu. Existing emulation and fixture. [C `docs/UI.md`, options section] |
| `popup/popup.dynamicuicanvas` | `popup/popup.luac` and `popup/popuprequestwrapper.luac`. | Quit, delete confirmation, create confirmation, and errors. [D1 `LandingExit`, `OnCharacterDeletePressed`, `HandleCreateCharacter`, `OnPopupResult`] |
| `main menu/renamepopup.dynamicuicanvas` | `main menu/renamepopup.luac`. | Required rename before Play. Native load, outside the first selection stage. [D1 `OnSelectCharacterPlayPressed`; C `swc/census.lua`] |
| `main menu/charactercreation.dynamicuicanvas` | `main menu/charactercreation.luac`, `cacarchetypestabv2.luac`, `cacappearancetab.luac`, `cacidentitytab.luac`. | Later creation flow in a different local level. [R2 1.3 to 1.4; D2 counterpart canvas] |
| `worldlist/worldselectpopup.dynamicuicanvas` | `worldlist/worldselectpopup.luac`, `worldlist/worldlistdatahandler.luac`. | World step of creation. It is not the world panel inside LandingScreenV2. [R2 1.4 and 2.1] |
| `main menu/legalscreen.dynamicuicanvas` | `main menu/legalscreen.luac`. | Legal screen. The census classes it as a service screen. No new public service is needed for selection. [C `swc/census.lua`; D4] |
| `main menu/newsmodal/newsscreen.dynamicuicanvas` | `main menu/newsmodal/newsscreen.luac`, `main menu/newssources.luac`. | News modal. Marketing and news also have panels inside LandingScreenV2. [D2; D1 `UpdateNewsScreen`; `newssources:RefreshNewsSources`] |
| `hud/loadingscreen.dynamicuicanvas` | `hud/loadingscreen.luac`, `globals.luac`. | Native loading events at level changes. The client already runs these scripts. [C `docs/UI.md`, loading section] |

The census marks landing, creation, and rename as native loads.
They are not ordinary HUD load groups of `uiloader.luac`. Merely adding the landing canvas to
`HudScreens` does not supply its native menu data or change the start state. [C `swc/census.lua`,
`UI/SwcUiSubsystem.cpp`; R4 3.5]

### The two state machines

The screen manager reads the following rows from D4. All these rows have stack level 5.
`false/false` means that neither lower nor higher levels close when the state enters.
A new state still replaces a state at the same level. [D4; C `swc/manager.lua`]

| UI stack state | Screens | Close lower / higher |
| --- | --- | --- |
| `LandingScreen` | `LandingScreenV2`, `VirtualCursor` | false / false |
| `CharacterCreationScreen` | `CharacterCreation` | false / false |
| `Legal` | `LegalScreen` | false / false |
| `MainMenuOptions` | `Options`, `VirtualCursor` | false / false |
| `MainMenuEsc` | `EscapeMenu` | true / false |
| `MainMenuStore` | `DynamicCanvasLoader`, `StoreScreen`, `StoreScreenV2` | false / false |

`VirtualCursor` keeps its canvas on transition out. Its entries have `Feature_Controller`.
`NewsScreen` is a level 7 state with `NewsScreen` and `DynamicCanvasLoader`; its close flags are
false/false. `PurchasesAndDLC` is a level 5 state that also lists LandingScreenV2, but closes higher
levels and keeps many HUD canvases. It is not the normal initial menu state. [D4]

D1 has a second state machine inside LandingScreenV2:

| Local screen state | Value |
| --- | ---: |
| `SCREEN_STATE_LANDING` | 1 |
| `SCREEN_STATE_CHARACTER_SELECT` | 2 |
| `SCREEN_STATE_WORLD_SELECT` | 3 |
| `SCREEN_STATE_BASIC_SETTINGS` | 4 |
| `SCREEN_STATE_DISPLAY_SETTINGS` | 5 |
| `SCREEN_STATE_NEWS_PANEL` | 6 |
| `SCREEN_STATE_ATTRACT_SCREEN` | 7 |

These values are not stack levels or stack state CRCs. `SetScreenState` changes panels within one
canvas. `OnTransitionIn` normally enters character selection on a fresh transition in this build.
Thus the old assumption that every start first needs a press on a separate title screen is wrong.
`OnLandingPlayPressed` remains a path for the landing panel. [D1 main chunk, `OnTransitionIn`,
`SetScreenState`, `OnLandingPlayPressed`]

There is a third small state value in this script: `STATE_NONE` 0,
`STATE_CONNECTING_TO_SERVICE` 1, and `STATE_CONNECTING_TO_GAME` 2. It drives waits and errors.
It is not a UI stack state. [D1 main chunk, `BeginConnection`, `OnSelectCharacterPlayPressed`]

### Native requests and completion events

D1 `OnInit` connects `UiMainMenuBus`, `UiCharacterServiceNotificationBus`,
`UiLoginScreenNotificationBus`, `CrySystemNotificationsBus`, `GameInviteNotificationBus`,
`LoadScreenNotificationBus`, and `AuthBackendNotificationBus`.
It also connects the Lua bus `DynamicBus.LandingScreenBus` at its entity id.
`OnTransitionIn` adds `EntitlementNotificationBus` and `DynamicBus.UITickBus`.
The input events are `CryActionNotificationsBus` at `frontEndContinue`, `frontEndExit`, and
`ui_cancel`. These are action names, not host keyboard keys. [D1 named functions]

| Request or query | Result consumed by the original script | Evidence |
| --- | --- | --- |
| `GameRequestsBus.IsFirstRun`, `IsAuthorized`, `IsMenuReset`, `ClearFirstRun`, `GetMainMenuLevelName` | Initial state and menu behavior. | D1 `OnInit`, `OnTransitionIn`, `SetCustomizableCharacterEntityId`. |
| `UiMainMenuRequestBus.RequestCustomizableCharacterEntityId()` | `SetCustomizableCharacterEntityId(entityId)` stores the preview entity and calls `CustomizableCharacterRequestBus.SetAlwaysUpdate(entityId, true)`. | D1 `OnInit`, `SetCustomizableCharacterEntityId`. |
| Native auth completion | `UiMainMenuBus.OnAuthComplete(success)` sets `authSuccess`; a shown screen then refreshes. | D1 `OnAuthComplete`. |
| `EntitlementRequestBus.EntitlementsReceived()` | `EntitlementNotificationBus.OnEntitlementsReady`; after a five-second wait the script can ignore a missing completion. | D1 `OnTransitionIn`, `OnEntitlementsReady`, `RefreshWorldList`. |
| `UiLoginScreenRequestBus.GetLoginInfoLists(true)` | `OnLoginInfoListResult(worlds, characters, names, maxCharacters, pendingMerges)`. | D1 `RefreshWorldList`, `OnLoginInfoListResult`; R2 native request 0x64356E0. |
| `UiLoginScreenRequestBus.GetRegionList()` | `OnClusterListResult(regions)`, despite the method name used for the request. | D1 `RefreshWorldList`, `OnClusterListResult`. |
| `DynamicContentBus.RetrieveCMSData(eCMSDataType_Worlds, true)` | `OnWorldCMSDataSet(data)` sets the third ready flag. | D1 `OnLoginInfoListResult`, `OnWorldCMSDataSet`. The callback bus mapping still needs a native check. |
| `EntitlementRequestBus.IsEntitledForWorld(crc(worldId), isBattleWorld)` | A world passes the account entitlement gate. | D1 `OnWorldListResult`. |
| `UiLoginScreenRequestBus.SetVisibleCharacter(characterId)` | The native menu applies the menu save data to its character. This is not a world spawn. | D1 `SelectCharacter`; R3 native selection 0x41F9E20. |
| `MainMenuSystemRequestBus.SetSelectedWorldId(id)`, `UiLoginScreenRequestBus.SetSelectedWorldId(id)` | The selected world is cached for subsequent menu work. | D1 `SelectServer`, `ForceSelectServerBasedOnCharacter`. |
| `GameRequestsBus.RequestLogin(characterId, worldType)` | The login queue and connection states. | D1 `OnSelectCharacterPlayPressed`; R2 0x6462B20. |
| `GameRequestsBus.GetConnectionStatus()` | `SetLoginState` chooses Play, authenticating, configuring, or connecting text and a spinner. | D1 `OnTick`, `SetLoginState`. The script gives the numeric connection-state table below. |
| `UiMainMenuRequestBus.ShowLoadingScreen()` | Loading during level unload. | D1 `OnLevelAsyncUnloadStart`. |
| `CrySystemNotificationsBus` level notifications | `OnLevelLoadComplete` starts a queued camera sequence. | D1 `OnLevelLoadComplete`, `PlayOnTransitionInCinematic`. |
| `CinematicRequestBus.PlaySequenceByName`, `StopSequence` | `CinematicEventBus.OnCinematicStateChanged(name, event)`. Stopped, Aborted, or BeyondEnd completes the callback. | D6. |
| `TransformBus.SetWorldZ` | Show the character at menu height or hide it at Z -1000. | D1 `ShowCharacter`. |
| `JavelinCameraRequestBus.EnableDepthOfField`, `SetDepthOfField`, `ResetDepthOfField` | Blur while another menu screen forces the preview hidden. | D1 `SetCharacterVisibilityOverride`, `SetCameraDof`. |
| `GameRequestsBus.RequestDisconnect(eExitGameDestination_Desktop)` | Quit after the quit popup. | D1 `OnPopupResult`. |

`GetConnectionStatus` must use the numbers in D1's local table, not the ordinal of a new C++ enum.
The table is `Disconnected` 0, `QueryGameUpdateCheck` 1, `WaitingForGameUpdateCheck` 2,
`QueueGameLogin` 3, `WaitingForQueuedLogin` 4, `QueryForRemoteConfigClass` 5,
`WaitingForRemoteConfigClass` 6, `ObtainREPRequirements` 7, `waitingForREPRequirements` 8,
`StartREPConnection` 9, `WaitingForREPConnection` 10, `WaitingForActorGameConnection` 11,
`WaitingForSpawnPoint` 12, `WaitingForPlayerSpawn` 13, and `InGame` 14.
`SetLoginState` uses ranges 3 to 4, 5 to 6, and 9 to 13 for its three busy labels. [D1 main chunk,
`SetLoginState`]

Other calls include progression multipliers, account ownership and trial queries, Steam store links,
benchmark, transfer, activity, invites, Twitch, rewards, accessibility, analytics, and platform
privileges. These are not permission to connect to their public services. Return explicit local
results where the local backend has no service. Keep the retail branch conditions. Do not make all
unknown calls return successful values. The per-function call list is in the scratch folder. [D1;
P adapter rule; C `swc/census.lua` service classes]

### Data read by the menu

`CharacterDataVectorToTable` copies the following fields into a `CharacterMetadata` value. [D3]

| Fields | Use or limit |
| --- | --- |
| `characterId`, `name`, `worldId`, `prevWorldId`, `personaId` | Selection identity, visible name, world and merge matching. Account identity stays in memory. |
| `currentLevel`, `currency`, `position` | `charactercreatebutton:SetCharacterInfo` displays `currentLevel + 1`. `SelectCharacter` writes `position` to `Hud.LocalPlayer.Position`. A visible use of currency was not proved. |
| `guildId`, `crestData`, `portraitData` | Crest and portrait data. A guild id needs an `IsValid()` value, not nil. |
| `publishedSource`, `publishedElapsedSeconds` | `GetLastPlayedCharacterIndex` chooses the smallest nonzero elapsed time. Zero does not win that test. |
| `ftueCompleted`, `mustRename`, `mustRenameReason`, `needsTransfer`, `hasTransfered`, `transferReason` | Play branch, rename, transfer, and status messages. Preserve the spelling `hasTransfered`. |
| `modifiedDate`, `nameModifiedDate`, `needsTransferDate`, `nameLatentDate`, `isNameLatent`, `isFreshStart` | Latent-name warnings and fresh-start status. Preserve the native time value behavior used by the helpers. |

`NameReservations` supplies a separate list. `CharacterNameVectorToTable` reads `name`, `personaId`,
`worldId`, `region`, `creationDate`, `modifiedDate`, and `channel`. This list supports region counts.
It is not a second list of preview bodies. [D3; D1 `OnLoginInfoListResult`, `PlayerHasCharacterInAnyRegion`]

A world value has `status`, `type`, `publicStatusCode`, `publicName`, `worldId`, `worldSet`,
`maxAccountCharacters`, `name`, `worldMetrics`, `version`, `transferToRegion`, `isFull`, and
`isRecommended`. Region values have `regionId`, `name`, `awsRegion`, and `latencyMs`.
Lua builds its own fields such as `isSelectable`, `characterCount`, and `serverMotd` around them.
Do not feed the raw HTTP member names directly to these scripts. [D3]

The required world CMS result has `globalMotd.title`, `globalMotd.description`, and
`worldDescriptions`. D1 reads the first two without a nil guard. `UpdateWorldDataWithCMS` joins
description records to world ids. A local empty CMS result must still have this shape.
Its full description schema is an open point. [D1 `OnWorldCMSDataSet`; D3; P local result]

Data-layer paths include `MainMenu.LastError`, `MainMenu.IsBanned`,
`MainMenu.ErrorPopups.ErrorSeen`, `LastErrorId`, `LastErrorTimepoint`, `LastErrorAdditionalInfo`,
and `LastErrorEventId` under `MainMenu.ErrorPopups`.
The script also uses `MainMenu.NewCharacterId`, `MainMenu.HasLevel60Character`,
`MainMenu.FreeSessionExpired`, `Game.TransferredCharacterName`, `NewsScreen.IsInitialized`,
`Hud.LocalPlayer.Position`, language and accessibility options, and `UIFeatures` gates.
`NewCharacterId` is consumed and cleared when the default selection runs. [D1 `OnInit`,
`SelectDefaultCharacter`, `ApplySelectedRegionOverride`, `SetFreeWeekendPopupVisible`]

## The 3D menu scene

The game copy has several menu levels: `main_menu`, `frontend`, `frontendv2`, seasonal variants,
`nw_frontend_settlement`, `nw_frontend_winterconv`, and `nw_frontend_nighthaven`.
Their presence does not prove which one a configuration selects. D5 gives a complete NightHaven
scene. Earlier research reports NightHaven in an earlier retail run, `world-time.md` 5.1.
This pass uses it as the reference scene, with the level-selection policy still open.

The scene is a local level, not `slices/player.dynamicslice` from a replicated world record.
D5 has 6,828 serialized entities, 6,468 `MeshComponent` entries, 61 `LightComponent` entries,
three cameras, and six sequence components. These are file counts, not active draw counts.
A full menu level import is a separate cost from running the 2D screen. [D5]

### Preview character and camera

`Player Entity` in D5 has `MainMenuComponent`, `LocalPlayerSaveDataComponent`,
`CustomizableCharacterComponent`, `SkinnedMeshComponent`, `SimpleAnimationComponent`,
`SequenceAgentComponent`, `PopupComponent`, and `CameraEffectsComponent`.
Its creation meshes are `objects/characters/player/male/player_male.cdf` and
`objects/characters/player/female/player_female.cdf`. Its initial mesh is the male CDF.
The customizable-character index is 1. These fields identify a menu actor, not a network player.

The following positions are serialized world coordinates in engine meters. [D5]

| Entity | X | Y | Z | Other fields |
| --- | ---: | ---: | ---: | --- |
| `Player Entity` | 3031.3986816 | 2047.3601074 | 84.0721970 | Full transform includes a small tilt. Use its basis, not a guessed facing. |
| `MainCamera` | 3032.5168457 | 2045.4516602 | 85.1500015 | FOV 55 degrees; enabled on activation. |
| `CharacterCameraPlacement` | 3032.5168457 | 2045.4516602 | 85.1500015 | FOV 55; disabled on activation. |
| `EstablishingCameraPlacement` | 3039.7954102 | 2045.4716797 | 85.7021713 | FOV 55; disabled on activation. |

The camera clip planes are 0.2 m and 8000 m. Apply the camera-axis and FOV conversion of
`ARCHITECTURE.md`; a serialized FOV is not permission to use 55 as an Unreal horizontal FOV.
The exact fork FOV convention needs a check before the scene stage. [D5; P]

`MainCamera` has parent `Main_layer`. The parent is a translation of `(1020, -1700, -38)` m.
The camera local position is `(2012.5168457, 3745.4516602, 123.1500015)` m.
The sequence records also keep local values. Do not apply the world translation twice or treat
all sequence values as world coordinates. The complete matrices stay in the scratch records. [D5]

`SetCustomizableCharacterEntityId` uses Z `84.0722` when the main menu level name contains
`nw_frontend`. Otherwise it uses `122.07218170166`. `ShowCharacter(false)` uses Z -1000.
The script forces `SetAlwaysUpdate(true)` so the hidden mesh can still update. [D1]

### Animation and sequences

The preview has `Hide Until Animated=true`. Its simple animation plays
`unarmed_idle_loadingscreen`, layer 0, loop on, speed 1, weight 1, and animation-driven motion off.
The CAF is `animations/gameplay/character/player/male/unarmed/navigation/idle/unarmed_idle_loadingscreen.caf`.
Both gender parameter files include `objects/characters/player/player_shared.chrparams`.
That shared file includes the male Unarmed animation folder. The existing character converter
can read this CAF. Import and playback on both skeletons still need a check. [D5; those game files;
C `converter/swc_convert/character.py`]

| Sequence in D5 | Serialized time range | Contents or caller |
| --- | --- | --- |
| `IntroStart` | 0 to 3 s | MainCamera transform tracks; named by the landing script. |
| `IntroToCharacter` | 0 to 1 s | MainCamera position and rotation tracks. D1 uses it on initial character selection. |
| `CharacterToIntro` | 0 to 1 s | MainCamera tracks. D1 `ReturnToAttractScreen` plays it. |
| `EstablishingToCharacter` | 0 to 1 s | MainCamera tracks. A Lua caller was not proved. |
| `Idle` | 0 to 36 s | Player Entity and `unarmed_idle_loadingscreen` animation track. Sequence start policy was not proved. |
| `OnPlay` | 0 to 2 s | MainCamera tracks and a two-second black ScreenFader key. Exact native start point is open. |

The script names and completion events are enough for a fixture adapter. The scene stage must
read the saved spline keys and their tangents. Replacing these tracks with a guessed camera lerp
would change the menu motion. [D5; D6; P]

### Lights and environment

D5 has four named character projector lights. All four are visible and on initially.
Their projector FOV is 90 degrees. The colors below are the serialized values;
Unreal photometric units are not the same as the retail multipliers.

| Light | World position in m | RGB | Diffuse / specular multiplier | Projector distance |
| --- | --- | --- | --- | ---: |
| `characterLight_RimLeftSC` | 3030.7698, 2047.3765, 86.0456 | 1, 0.7804, 0.4667 | 1.5 / 1 | 2.3 m |
| `characterLight_FillLeft` | 3031.0618, 2046.0662, 85.6029 | 1, 0.9765, 0.9529 | 0.8 / 0.5 | 3 m |
| `characterLight_RimRightSC` | 3031.7834, 2048.0557, 86.4604 | 0.5216, 0.8196, 1 | 0.8 / 1 | 2 m |
| `characterLight_FillRight` | 3032.6965, 2047.0951, 86.0164 | 0.6510, 0.9098, 1 | 0.5 / 0.2 | 2.8 m |

`EnvironmentProbe_Character` is at `(3031.3896, 2047.3362, 85.2569)` m. It uses
`textures/cubemaps/frontendv2/frontendv2_cm.dds` and color `(0.8667, 1, 0.9490)`.
`EnvironmentProbe` uses the same cubemap. `GenerateCubemapEntity` is off and hidden;
its presence must not create another active light. Other lights serve the tent, bottles,
cactus, and distant scene. Full transforms and fields are in `lights.json` outside git. [D5]

The `TOD` entity has a cylinder of radius 100 m and height 100 m. Its `TimeOfDayPOIComponent`
uses `Libs/TimeOfDay/Frontend/NW_Frontend_NightHaven.xml`, priority 2, blend time 0,
blend distance 1 m, and an hour override of about 16.7. Its sky slice is
`slices/sky/skydome_tropicalstorm_post_a.dynamicslice`.
The preset itself has `Time=16.733334` and `TimeStart=0`.
The mission has `Time=16.75` and `TimeStart=0`. The preset override and the mission start are
different inputs. Reuse the environment readers before choosing an hour in Unreal. [D5; preset file;
R `world-time.md` 5.1; C `docs/ENVIRONMENT.md`]

### Equipment in the preview

The menu reads `PublishedData`, not a live Paperdoll replicated state. The decode is
base64, then zlib, then a JSON object. Its members include `currentLevel`, `currency`, `position`,
`worldId`, and `visualsBlob`. The last member contains bytes of a binary AZ object stream,
`PlayerSaveData`, class version 3. Keep its length and bytes, including zero and high bytes.
A conversion of the whole inflated JSON through a normal Unicode string can damage this blob.
Use the byte-preserving rules of R2 5.3 as the reference for the decoder. [R2 3 and 5.3; R3 7.1]

The native chain is character model reader 0x74E1F60, login-info conversion 0x64278C0,
base64/zlib helper 0x171A500, JSON reader 0x16B3850, then object-stream loader 0x14236F0.
Selection 0x41F9E20 applies the result to the menu customizable character.
With a gender change it waits for the mesh-created path. [R3 7.1]

The save data gives the 12 appearance names: `gender`, `race`, `skinTone`, `hairstyle`,
`facialHair`, `hairColor`, `facialHairColor`, `eyeColor`, `faceMark`, `scar`, `tattoo`,
and `tattooColor`. Convert names through the same creation databases that supply the world
appearance indexes. The existing resolver takes indexes, so it needs a menu input adapter.
Do not treat `race` as a numeric index. [R2 3.4 to 3.5; R3; C `Character/SwcPlayerLooks.h`]

`equipment` maps visual names `hat`, `shirt`, `gloves`, `pants`, and `boots` to entries with
`appearanceId`, `itemName`, and four dye bytes. `appearanceId` is the lower-case CRC-32 of the
armor appearance row, normally the item definition's `ArmorAppearanceM` or `ArmorAppearanceF`.
It is not an item-catalog index or a row number. All known native writers leave `itemName` empty.
The original server writer omits slots above 4. The proved menu contract therefore has no weapon,
shield, jewelry, tool, or bag entry. [R1 1.1 to 2.3: reflection 0x36FC490, publish lambda
0x673EE40, publish function 0x67F0600, create writer 0x2EB0FE0]

Resolve these appearance CRCs directly against the armor rows in `player/looks.json`.
Reuse the body slots, skin-tone materials, head hide flags, and hair variants of `SwcPlayerLooks`.
Do not invent an item id for a CRC to pass it through `ResolveGear`.
The low-level apply functions are 0x6956C40 and 0x6957C00. The native reader loop for the equipment
map remains unfound in R1. Nonempty `itemName`, bad CRCs, zero CRCs, unknown slots, menu transmog,
and dye precedence remain open. [R1 Unknowns; C `Character/SwcPlayerLooks.{h,cpp}`; P reuse]

R1 is an old fault diagnosis. It must not be read as a statement that the current backend still
returns creation-time gear. `CHARACTER.md` already specifies item-catalog format 5 and appearance
columns. This pass did not query a backend or inspect a user database. Check the actual release
reply with a made-up fixture in the implementation stage. [C `docs/CHARACTER.md`; P]

## The flow and the present client

The retail path for an existing character is as follows. [D1 unless another source is named]

1. Register the native menu screens and enter UI state `LandingScreen`.
   `OnInit` requests the preview entity; `OnTransitionIn` chooses the character-select panel.
2. Complete auth and entitlements. `BeginConnection` hides the preview and disables unsafe controls.
   `RefreshWorldList` requests the login info and regions.
3. `OnLoginInfoListResult` converts worlds, characters, reserved names, and limits.
   It requests world CMS data. `OnClusterListResult` supplies regions.
   `TryOnWorldListRefreshed` requires `worldListReady`, `characterDataReady`, and `regionDataReady`.
4. Build the rows. Capacity is `maxCharactersPerRegion + purchasedCharacterSlots`.
   With no maintenance bypass, empty slots can create only when there is a selectable world.
   The script can clone its row template. Four visible rows is a layout limit, not a hard list limit.
5. `SelectDefaultCharacter` first uses `MainMenu.NewCharacterId`, then an existing valid selected
   index, then the least nonzero published elapsed time, else row 1.
   With no character it hides the preview. Do not send a login request for an empty list.
6. A row change goes through the radio group. `SelectCharacter` hides the preview, schedules
   `CheckCharacterVisibility` after 0.25 s, sets the selected id, calls `SetVisibleCharacter`,
   writes the position data node, and selects the character's world.
7. Play checks selection and ready data. It handles required rename and transfer before login.
   Then it disables the controls, shows the spinner, caches world information, and calls
   `RequestLogin(id, "OpenWorld")`, or `"FTUE"` when `ftueCompleted` is false.
8. Native `RequestLogin` 0x6462B20 normally sends an empty world id in the login queue request.
   A world-type selection is conditional on `g_loginWithWorldTypes`; its default is 0.
   Continue through the existing token, DTLS, carrier, registration, level, and own-entity path.
   [R2 4.1 to 4.3; C `docs/NETWORK.md`]
9. Show the world loading screen until the world, character assets, and spawn are ready.
   Remove the menu preview and menu input ownership. Enter HUD. A later leave to the menu must
   refresh the account list and preview from new data. [P integration; C `docs/NETWORK.md`, `docs/UI.md`]

| Part | Present at the base commit | Missing for the menu |
| --- | --- | --- |
| HTTP login | `Net/SwcLoginClient` has channel, token, credentials, entitlement sync/list, configuration, and login info. | A refresh API with completion state for Lua; full menu metadata. [C `.h`, `.cpp`] |
| Character list | `FSwcCharacterInfo` keeps `CharacterId`, `Name`, `WorldId`, `bFtueCompleted`, and the raw `PublishedData` string. | No menu-document decoder, reserved-name list, limits, elapsed time, status fields, or Lua `CharacterMetadata` projection. [C same files] |
| World list | `FSwcWorldInfo` keeps id, name, status, type, and version. The subsystem logs these values. | Region data, world flags and metrics, and the menu's world gate inputs. A log is not a menu eligibility check. [C same files; D3] |
| Entry | `USwcBackendSubsystem::OnCharacterList` calls `StartTables`; `-SwcPlay` finds the requested row and calls public `EnterWorld(id)`. The latter obtains the login token. | Lua request dispatch, selected-id validation, pending-request protection, and a retry path. Current token failure quits. [C `Private/SwcBackendSubsystem.cpp`] |
| UI boot | Original HUD, options, loading, errors, screen stack, lists, radio buttons, data layer, tweens, and input run today. | Native front-end load and lifecycle. `SwcUiSubsystem` starts LEVEL1 loading and hides it only after world entry and possession. Without `-SwcPlay`, a successful list does not finish that wait. [C `UI/SwcUiSubsystem.cpp`] |
| Auth event | `swc/options.lua` sends `OnAuthComplete(true)` at `OnUILoadingComplete` for Options. | Separate content readiness from real account success. Do not let this synthetic event start menu requests before auth. [C named file] |
| Game queries | `swc/game.lua` returns `IsInMainMenu=false` and `GetMainMenuLevelName="frontend"`. | A real menu phase and the chosen menu level name, auth state, entitlement results, regions, connection states, and menu buses. [C named file] |
| Preview | `SwcPlayerLooks`, `SwcPlayerCharacter`, Leader Pose, async player tables, wanted parts, and gear resolution exist for the world. | A menu input form for names and appearance CRCs, an independent actor, the menu idle, and camera ownership. [C `docs/CHARACTER.md`, `Character/`] |
| Errors and leave | `HostConnectionError` runs the retail popup. Existing world shutdown can send carrier disconnect. | O14 recovery, account-list refresh, cleanup for a second entry, and a normal return to selection. [C `swc/host.lua`, backend subsystem; D1 `ResetScreenAfterError`] |

### Creation and delete, later stages

Creation starts from an empty slot. `OnCreateCharacterPressed` can open the regular/seasonal popup
inside the landing canvas, then `HandleCreateCharacter` asks for confirmation. `StartIntro` calls
`UiMainMenuRequestBus.StartIntro`. Native 0x41FAD90 loads `FTUE_V2_CC`, unless the creation-level
override is set. This is not the menu preview scene. [D1; R2 1.3]

The later screen has archetype, appearance, identity, and world-select states.
The name request is `POST /prod/game/worlds/{worldId}/characters/validator`.
Creation is `POST /prod/game/worlds/{worldId}/characters`.
`CreateCharacterResult` supplies the new id. In the intro level, the controller and the
`Character_Create_End_Trackview_Sequence` continue to `RequestLogin(newId, "FTUE")`.
Outside that level, `cacidentitytab` sets `MainMenu.NewCharacterId` and returns to `LandingScreen`.
Do not force the normal intro path back through the list. [R2 1.4, 2.2, 2.3, 4.1]

The current delete button is `OnCharacterDeletePressed`. It asks for a hold-to-accept popup.
Yes invokes `UiCharacterServiceRequestBus.DeactivateCharacter(selectedCharacterId)` after the spinner
callback. `DeactivateCharacterResult` refreshes the list. Do not infer a text-entry confirmation
from the placeholder localization key. The read script requests a hold. [D1 named functions]
The native chain is 0x41ED2E0, 0x642C420, 0x642C970, then job 0x64148E0,
`DELETE /prod/game/characters/{characterId}`. [R2 2, address index]

## Plan in stages

These are proposed stages, not completed features. S is a narrow data or adapter change.
M crosses one subsystem boundary. L crosses several boundaries or needs scene and animation work.
Each stage keeps its own change small enough to review. No backend edit is part of this plan.

| Stage | Size | Work and reuse | Fixture exit checks |
| --- | --- | --- | --- |
| F1, menu data | M | Add bounded, byte-preserving `PublishedData` decoding. Extend the HTTP result for the menu fields, limits, reserved names, worlds, and regions. Reuse `SwcLoginClient`, the converter object-stream field rules, and the appearance databases. Keep the codec plain C++ where possible. | Made-up list with zero, one, and several characters; display level 1 and a higher level; every appearance name; empty/missing/bad document; embedded zero and high bytes; malformed base64, zlib, and object lengths; output and inflate limits; all five armor slots; no weapons; account A cannot select B. No user capture as a fixture. |
| F2, offline landing | L | Load the original landing, popup, tooltip, and Options canvases through `swc/manager.lua`. Add menu value classes and request/completion buses. Reuse enums, input, clone, radio, list, data-layer, and tween code. Supply a shaped empty local CMS result and explicit local service results. Reconcile auth completion in `swc/options.lua`. | New `frontend` fixture runs original scripts. No list before all three ready flags. Test all completion orders, auth false, entitlements, no world, no character, four rows and an overflow row. Click a nonfirst row and check selected id, world, row level, and radio state. Preview hide/reveal at 0.25 s. Options/back, refresh, popup/cancel, keyboard actions. Fail on Lua errors and unexpected required stubs. Save draw lists and software pictures. |
| F3, selected preview look | M | Add a menu look input to the existing resolver. Reuse naked defaults, head/hair rules, parts, Leader Pose, async loads, and wanted-file handling. Read armor by appearance CRC. Keep the actor independent of the world pawn and its paperdoll. | Plain look tests for male/female, default names, armor replacement and removal, head hide and hair chop, unknown CRC, empty map, and unsupported dyes. Change A to B during an async load; A must not replace B. Clear stale parts after an invalid document. |
| F4, preview scene | L | Convert a bounded NightHaven scene subset and the menu CAF. Reuse `source.py`, slice/placement and environment readers, `convert.sh character --clip`, player import, shared content, and the space map. Add the menu camera and sequence adapter. Keep full scene import as a later extension. | Reader fixtures assert parent/world transforms, camera clips, sequence endpoints and tangent data, light on/off and fields, and TOD override. A Lua fixture gets sequence completion after its actual duration and tests stop/abort. A pure playback check asserts idle speed and loop. Later, a development video must show the moving menu, not only a still image. |
| F5, Play and world entry | M | Route `GameRequestsBus.RequestLogin` through the host request queue to `EnterWorld`. Reuse token, transport, registration, world streaming, preloads, and `HostLoading`. Add an explicit menu/loading/world phase and map the connection labels used by D1. Keep `-SwcPlay` as an automated bypass. | Original script emits the selected id and world type once; selection alone emits no login. Double Play makes one pending request. Test stale id, FTUE flag, required rename/transfer refusal, token failure, retry, loading start/end, and HUD after spawn. Network tests retain the existing bytes. |
| F6, recovery and return | M | Close O14. Let the original popup and landing error logic restore selection. On world leave, stop the connection, dispose world-owned view state, load the menu, and refresh the list. Preserve selection by id when it still exists. | Refused auth, token, and REP connection paths can retry. Out-of-order callbacks from a previous attempt cannot replace the new list. Enter, leave, change character, enter again. Cursor and action maps recover. No old preview or world actor remains. |
| F7, creation | L | Separate design and implementation for `FTUE_V2_CC`, original creation scripts, validation, serialization, archetypes, and intro sequences. Reuse F1 names and blob rules. | Name valid/invalid/unavailable and timeout; world unavailable; create success/failure; exact new id; intro completion to login; fallback `NewCharacterId` selection. |
| F8, delete | M | Add DeactivateCharacter HTTP and notification adapter. Keep the original hold-to-confirm behavior and list refresh. | Cancel and incomplete hold send nothing. Confirm sends the selected id once. Test failed deletion, removal of selected row, last character removed, and account ownership refusal. |

F1 and F2 give the selection foundation. F3 and F4 give the preview. F5 and F6 complete the first
usable front end for existing characters. F7 and F8 follow it. This order keeps new character
creation from blocking selection of characters that already exist. [P]

`tools/convert-ui.sh canvases PATH...`, `scripts`, `textures --canvas PATH`, `fonts`, `text`,
`enums`, `uistates`, `loader`, and `texture-index` already exist. Use those commands with an explicit
front-end canvas set before adding a bundle command. Scripts are already converted in the shared
content. A text dump is not a replacement for its `.luac`. [C `converter/swc_convert/ui.py`, `docs/UI.md`]

The UI fixtures are `tests/ui/fixtures/*.lua`. Reuse `prelude.lua` and the existing `census`,
`transitions`, `lists`, `options`, `loading`, and `escape` fixtures.
They can load original canvases, deliver events, click elements, run frames, and check the model
without the engine. `tools/test-ui.sh --preview` also writes software pictures.
That command builds the test program, so it was not run during this static-only pass. [C `docs/UI.md`]

The implementation exit checks will need the offline UI and network tests, then a development run
with made-up characters and a retail side-by-side check. Keep the screenshots and videos outside
git. Do not claim retail parity from these static findings. [P; project rules]

## Traps and open points

| Id | Open point or trap | How to close it |
| --- | --- | --- |
| FE1 | Which menu-level configuration chooses NightHaven, and when it changes. Level files and an old observation do not prove the present selector. | Read the native menu-level/config registration when Ghidra is free. Keep the reference choice explicit in F4. |
| FE2 | Full native bootstrap order and the exact callback bus for world CMS. The script handler names and connection-state numbers are known. | Read the registrations and main-menu activation code. F2 tests event permutations; later compare with a retail trace. |
| FE3 | Current release completeness for metadata, reserved names, regions, and CMS. This pass read no runtime reply. | Use synthetic HTTP fixtures first. Then read a development reply from the backend copy. No user database. |
| FE4 | `visualsBlob` bytes can be damaged by Unicode or NUL-terminated string handling. Large zlib output can also exhaust memory. | F1 uses byte lengths and decode limits. Test high bytes, zero bytes, and oversized data. |
| FE5 | Equipment-map reader loop, nonempty itemName, bad/zero CRC, transmog and dye priority. R1 did not find the loop. | Find the caller around native selection 0x41F9E20 and apply functions 0x6956C40/0x6957C00. Keep unsupported cases explicit. |
| FE6 | Scene sequence defaults are local under Main_layer. Some placement entities serialize local and world values that do not agree with that parent. | Prefer the active MainCamera and its sequence agent; resolve parent chains. Do not use a placement marker's local value as the camera. F4 asserts world endpoints. |
| FE7 | Fork camera FOV convention, spline evaluation, sequence start policy for Idle/OnPlay, and final lighting units. | Read native camera/sequence code and reuse existing readers. Later use a matched video for motion and a matched picture for framing. |
| FE8 | The female menu idle, missing imported parts, dye, face layers, and some cloth rules. The world resolver already has limits. | Convert only requested content and both menu skeleton clips; expose existing resolver limits. Check both bodies in F3/F4. |
| FE9 | Auth true currently means UI loader complete in Options. Loading currently waits for a world pawn. | Split content-ready and account-ready events. Add explicit front-end phases in F2/F5. Keep the existing Options fixture passing. |
| FE10 | Default selection retains an index on refresh. A reordered list can make that index name another character. | Preserve the intended id in the host and test reorders. Keep the original Lua default rules visible in the fixture. |
| FE11 | Rename/transfer flags can block Play. Creation, delete, online news, store, invites, and legal acceptance are larger than the first stage. | Provide explicit supported/refused paths for the existing local account. Do not silently bypass a required rename. F7/F8 own create/delete. |
| FE12 | Return and retry cleanup are not complete in the present client. A successful initial login does not prove a second entry. | F6 tests attempt generations, pending requests, actor cleanup, input, and a second selected character. |

## Result of this pass

The foundation is specified. No client feature was implemented or tested live.
Only this document changes in the research branch. The scene and script records remain outside git.

## Implementation

### F1, the menu data (2026-10-20)

- `Menu/SwcMenuDocument.{h,cpp}` reads PublishedData: base64, zlib (the zlib of the engine), a JSON reader that keeps the bytes
  of each text, and the PlayerSaveData object stream. It gives the 12 appearance names, the player, company, entity class,
  crest, and backstory texts, the map `equipment` (key, appearance CRC, item name, four dye bytes), `currentLevel`,
  `currency`, `position`, and `worldId`. An unknown key and a nonempty item name stay in the result: the look decides (FE5).
- The limits (FE4): text 512 KiB, inflated document 256 KiB, visualsBlob and one value 64 KiB, depth 16, 4096 elements, 32
  equipment entries. A status tells the fault (no text, bad base64, bad zlib, too large, bad JSON, no visualsBlob, bad stream).
- `tools/test-menu.sh` (no engine, with the address and undefined-behavior sanitizers): 85 checks with the documents of the
  backend writer (`make_menu_document`, read only): male and female, levels 1 and 60, every appearance name, all five armor
  slots, appearance ids with zero and high bytes, a name with high bytes, a quote, and a backslash, no weapon entry (the
  writer drops slot 7), real deflate blocks, an other member order with spaces, the agreement with the backend reader, and the
  faults: empty, too large, bad base64, cut zlib, a zlib bomb, cut and bad JSON, no visualsBlob, an escape above 0xFF, each
  cut of the object stream, a byte after the end, a large size field, too deep elements, an element with no mark.
- `FSwcLoginClient` keeps the menu values of each character (persona in memory only, the menu flags and dates, the decoded
  document), the world values of the menu, the regions of the channel document, the reserved names, and
  `MaxChannelCharacters`. `RefreshLoginInfo` asks for the list again (one refresh at a time).
- A run with the backend copy (`dev-run.sh --quit-when characters`, the development character of the UI track): the log line
  of the character gives "menu document ok (level 70, male, 5 clothing entries)".

### F2, the offline landing (2026-10-20)

- `Lua/swc/frontend.lua` is the native part of LandingScreenV2 (the original canvas and scripts), with the rules of the
  native study `~/Games/soulwarden-client/research/frontend-native.md`:
  - The account (FE9): `Swc.HostMenuAccount(authorized, entitlements)` sends `UiMainMenuBus.OnAuthComplete`, then
    `EntitlementNotificationBus.OnEntitlementsReady` (the order of the login path). In the front end, the end of the UI load
    (`swc/options.lua`) goes only to the options screen.
  - The list: `GetLoginInfoLists` gives the host request `menu list`; the host sends the worlds, the characters, the reserved
    names, and `OnLoginInfoListResult` with the native classes `WorldMetadata`, `CharacterMetadata`, `CharacterNameData`.
    `GetRegionList` gives `OnClusterListResult` (`RegionMetadata`, latency 0 ms on loopback) in the next frame.
  - The world CMS data (FE2): `RetrieveCMSData(eCMSDataType_Worlds, true)` gives `UiMainMenuBus.OnWorldCMSDataSet` with an
    empty local result of the shape that the script reads, in the next frame.
  - The menu level (FE1): `GetMainMenuLevelName` gives the explicit override `NW_Frontend_NightHaven` (the schedule row
    `EventNightHaven_Frontend`; the setting `javelin.frontend-level-name` has the native default `NW_Frontend_Settlement`).
    `Swc.HostMenuLevelLoaded` sends `OnLevelLoadComplete`: the landing screen plays its queued `IntroToCharacter` then.
  - The selection: `SetVisibleCharacter(id)` gives `menu preview ID`; `TransformBus.SetWorldZ` of the preview entity gives
    `menu show 1` or `menu show 0`. A refresh keeps the selection by its id (FE10: the selected id goes to
    `MainMenu.NewCharacterId` before the list result, the first rule of `SelectDefaultCharacter`).
  - Play: `RequestLogin(id, type)` gives `menu login ID TYPE` (stage F5). `RequestDisconnect` gives `menu quit`.
  - The settings of the registration: `UIFeatures.mm-server-refresh-time-s` 60 (the auto refresh of the list),
    `javelin.schedule-frontend-level-enable`, `javelin.enable-remote-pending-world-merges`, and
    `javelin.set-initial-region-id-from-landing-screen` true. The pending world merges (the data call 225285239) are empty.
  - No public service: the store, invites, Twitch, rewards, news, and surveys give explicit local results.
- The emulation also changed: a radio group gives no destroyed element as its state, the Escape fallback of the HUD does not act
  in the front end (the screens handle Escape with their `ui_cancel` handlers), and `Swc.errorCount` counts the Lua errors.
- The behavior of the original script: `SelectCharacter` hides the preview and gives the id to the host; the selection of the
  world of the character (`SelectServer`) shows the preview again in the same call. The check of 0.25 s later keeps it.
  Escape on the options in the main menu goes to `MainMenuEsc` (options.luac `ExitScreen`), Escape there to the landing.
  The landing screen asks for the list again at each transition in, each 30 s for the regions, and each 60 s for the list.
- The fixture `frontend` (52 checks) has the exit checks of the plan. All 1552 checks of `tools/test-ui.sh` pass.
- The client: with no `-SwcPlay`, `USwcBackendSubsystem::IsFrontEnd()` is true after the list; `USwcUiSubsystem` ends the
  loading screen of the start, loads the menu screens (`Swc.HostMenuStart`), sends the regions of the channel document and the
  account, and answers each `menu list` with a refresh of getlogininfo (one at a time). The UI drive clock starts at the
  landing screen. `tools/menu-check.sh LABEL SECONDS` records a window run of the front end.
- The run `f2-landing` (the two development characters of the UI track, 1256 x 660): the landing screen, a click on each row,
  the refreshes of the regions (30 s) and of the list (60 s). The side-by-side picture with the retail picture B6-04 of the
  Soulwarden evidence has the same panels, places, and sizes. The background is the empty default level (stage F4).

### F5, Play and the world entry (2026-10-20)

- `menu login ID TYPE` (the `RequestLogin` of PLAY): `USwcUiSubsystem` checks that the id is a character of the current
  account list and that no login runs, then calls `USwcBackendSubsystem::EnterWorld(id)`. A selection alone sends no login.
  The world type (OpenWorld or FTUE) is not in the login queue request: the native `RequestLogin` sends an empty world id.
- The connection states of the table of the script: 3 (QueueGameLogin: "@mm_authenticating") while the login queue request
  runs, 10 (WaitingForREPConnection: "@mm_connecting") after the token. At the token the menu ends (`Swc.HostMenuEnd`: the menu
  screens unload) and the loading screen of the world shows (LEVEL1); it ends at the possession of the player character
  (`ASwcPlayerCharacter`), not of the default pawn of the map: the spawn waits for the cells of the place. Then the screen
  loader of the world and the HUD start as with `-SwcPlay` (which stays the automated bypass).
- A failed login token in the front end does not stop the client: `USwcBackendSubsystem` keeps the error
  (`GetEnterError`), and `Swc.HostMenuLoginFailed` writes `MainMenu.LastError`. The data callback of the script shows the popup
  "@mm_connectionfailed" (more than 5 s after its start or its last error popup), and its OK resets the screen and asks for the
  list again; PLAY can try again. A character id that is not in the list gets the same error.
- The fixture `frontend` (62 checks) adds: the failed login, the popup and its OK, the list refresh, PLAY again; the world type
  FTUE; no login for a character that must be renamed (the rename popup of the script). The wall clock of the emulation follows
  the fixture time there (the 5 s rule of the popup).
- The run `f5-play` (`tools/menu-check.sh`, the place of the development character at the dry sand): a click on a row, PLAY,
  the connection label, the token after 3.8 s, the loading screen for 12.6 s (the cells of the place), the spawn, and the HUD.
  `evidence/F5/compare/f5-flow.png` has six frames of the video.
- Not done in F5 (F6): the return to the menu, a second entry, the retry of a failed connection after the token.

### F3, the look of the selected character (2026-10-20)

- `Menu/SwcMenuPreview` (`ASwcMenuPreview`) is the character of the main menu: an actor of its own, with no world pawn and no
  paperdoll. The base of the gender is the leader (not drawn; the idle clip); each part follows its pose (Leader Pose), as the
  world character. The look comes from `FSwcPlayerLooks::ResolveMenu` (the menu input of the resolver, docs/CHARACTER.md).
- `menu preview ID` makes the look of that character when the look tables are there (`USwcBackendSubsystem::GetPlayerLooks`,
  no wait in a frame): the 12 names and the armor ids of its menu document. An invalid document or an id that is not in the
  list clears the body (no stale parts). A map key that is not hat, shirt, gloves, pants, or boots, a nonempty item name, and a
  nonzero dye are not applied yet (a log line each). The missing parts go to the wanted list (`tools/convert-player.sh wanted`,
  `tools/import-player.sh`).
- The assets load asynchronously. A new selection cancels the load before it, and a load that ends after a newer request is
  not shown: an old body never replaces a new one (the run `f3-switch`: the click on the second row while the first body
  loads; only the second body shows).
- `menu show 1|0` shows or hides the parts (the landing screen moves the body to the menu height or to Z -1000).
- The place and the camera: the actor stands at the place of the Player Entity of the NightHaven scene (game 3031.3987,
  2047.3601, 84.0722 m) with its yaw (-147.88 degrees); its camera is the end key of `IntroToCharacter` relative to the feet
  (1.118, -1.908, 1.078 m; pitch -3.5, yaw 31.5 degrees), and it is the view of the player controller in the menu. The FOV is
  the vertical 55 degrees of MainCamera (the fork axis is open, FE7). The token of PLAY removes the actor.
- The run `f3-preview` (the two development characters of the UI track): the female body (6 of 6 parts, 4 armor rows of the
  ranger start) and the male body (6 of 6 parts, 5 armor rows). The made-up spec of the female character has the hairstyle
  name `hairstyle12f`, which the database does not have: the first entry ("None") and a note. `evidence/F3/compare/sbs-preview*.png`:
  the retail picture B6-04 and the two client bodies; the place and the size of the body agree. The scene, its lights, and its
  idle clip are stage F4.

### F4, the NightHaven scene and its camera (2026-10-20)

- The converter (`converter/swc_convert/frontend.py`, `tools/convert-frontend.sh`):
  - `clips` adds the menu idle CAF to the player pipeline: the clip pack `clips|GENDER|menu` of both genders (the female
    uses the male CAF, as `player_shared.chrparams` gives). Then `tools/import-player.sh`.
  - `scene [--radius M]` reads `mission0.entities_xml` of the level pack and writes `SWC_CONTENT/frontend/nighthaven.json`
    (outside git): the named entities (Player Entity, TOD, Main_layer, MainCamera, the two camera placements) with their
    world and local matrices and their parents; the camera fields; the six sequences (the tracks of each entity node, the
    spline keys with their flags and tangents); the TOD entity; the static meshes within 60 m of the Player Entity; the
    lights within 40 m (the source fields as text); a ground patch.
  - The meshes: 522 placements of 184 meshes. The fog volumes, the effect meshes, and the sky dome are left out (47). Each
    placement has the object path of the imported asset (`objects.imported_mesh`: the import shortens long names and
    changes some characters; the stem rule found only 349 of 522). The meshes are shared world content (the world tools
    convert and import them; the wanted list is empty).
  - The ground patch: 121 x 121 heights at 1 m from the terrain of `coatlicue/nw_frontend_base`, with one color from its
    macro map.
- `Menu/SwcMenuCurves` (plain C++) is the tangent update and the evaluation of the native study (FE7): the tangent modes,
  the reach limit, and the Bezier time solve. `tools/test-menu.sh` checks them with the keys of `IntroToCharacter`.
- `ASwcMenuPreview` loads the record after its spawn:
  - The scene meshes load asynchronously. Each one is a movable component of the actor. A static part cannot attach to the
    movable root; then its world place is lost (the first run had all parts near the world origin).
  - `menu sequence NAME` plays a sequence of the record on the camera (MainCamera under Main_layer; the game rotation
    Rz Ry Rx; a camera looks along its Y axis). `menu sequence-stop NAME` stops it. `swc/frontend.lua` sends them at
    `PlaySequenceByName` and `StopSequence` of the script. The fixture `frontend`: the transition plays
    `IntroToCharacter`, a queued sequence, the level load plays it again, and it ends after its 1 s (67 checks).
  - The body plays the menu idle clip of its gender (the pack of `content/player/imported.json`; else the world idle).
- The lights (`Menu/SwcMenuLights.h`): four lights of the rig have bulbs of 0.5 m to 30 m within 2 m to 15 m of the body. The
  world rule (r^2 / D^2 from the center) is exact only far from the bulb; at the body it gave up to 9 times the source
  light. The menu reads the source attenuation 1 / (1 + d / r)^2 with d from the center of the light (it stays below 1 in
  the bulb) and matches the candela at the body (1 m above the Player Entity) when the light reaches it, else at the middle
  of its range. The probes, the hidden cubemap entity, and the area light are not made (27 of 31 records). The lights are
  on (`swc.MenuLights` 1; `-SwcNoMenuLights` turns them off for a compare).
- The environment of the menu (K4, 2026-10-20; docs/ENVIRONMENT.md, "The menu mode"): the client environment runs only after
  the first record of the world clock, and the front end has no world session. Before K4 the menu had the environment of the
  level (the engine sun and a fixed exposure), and the physical units of the lights and of the emittance burned the image
  white (the runs `f4-scene2` and `f4-scene3`). Now the preview sets the menu mode of the environment when it reads the
  scene record ("environment": the preset and the hour 16.7 of the TOD entity, its sky slice, the sun path and the moon of
  the menu mission) and clears it when it ends (the token of PLAY). The run `f4-menu-mode`
  (`tests/drive/f4-scene-play-ui.txt`): the menu mode with the sun at 18 degrees, the lights on, both bodies with their
  colors, PLAY, the end of the menu mode at the token, and the world environment at the first record (the beach as
  before). `evidence/F4/compare/sbs-menu-mode.png`: the exposure is correct; the image is brighter and colder than B6-04,
  with less fog (a look point).
- The runs (`tests/drive/f4-scene-ui.txt`): `f4-scene2` (the lights on) and `f4-scene3` (the lights off). The record loads
  with the landing screen; `IntroToCharacter` plays two times (as the script calls it); the 522 meshes show about 10 s after
  the landing; the female body and the male body have 6 of 6 parts. `evidence/F4/compare/sbs-scene.png`: the framing agrees
  with B6-04 (the tent at the left, the body in the middle with the same size, the house at the upper right).
- Open points:
  - The look of the menu next to B6-04: brighter, colder, and with less fog. The probes of the menu level are not
    converted (the ambient is the world probe), and the material overrides of the placements are not applied (some plants
    have other colors).
  - The ground is a flat-color patch at the terrain heights. The terrain of `nw_frontend_base` with its materials is world
    work.
  - The subset is 60 m: the dome at the upper left of B6-04 is outside it. The campfire effect, the probes, and the area
    light are not made. The material overrides of the placements are not applied.
  - FE7: the axis of the FOV; the callers of `Idle` and `OnPlay`.

### F6, the return to the character selection (2026-10-20)

- The path of the scripts: the escape menu button "Exit to Main Menu" (the element `ButtonExitMainMenu`; the script field is
  `ButtonExitMainNemu`) shows the quit popup of the popup screen (its module `quitgamepopup.luac`, with its countdown in
  combat). Yes calls `GameRequestsBus.RequestDisconnect(eExitGameDestination_MainMenu)`. `swc/frontend.lua` gives
  "menu quit DESTINATION": 0 quits; 1, and 2 (the attract screen, which this client does not have), go back to the
  character selection from the world.
- `USwcBackendSubsystem::ReturnToMenu`: the carrier disconnect. When the connection ends, `ResetWorldSession` sets each
  member of one world entry back to its start value, and `OpenMenuLevel` loads the level of the world again: its actors and
  its world subsystems start new, as the retail client loads its menu level. In the front end, a world connection that ends
  with no leave goes back too. Its cause is the entry error: the landing screen shows `@mm_connectionfailed` 5.5 s after
  the menu start (the script shows `MainMenu.LastError` only more than 5 s after the init of the screen).
- `USwcUiSubsystem`: after the level load of a return (`PostLoadMapWithWorld`), the Lua runtime starts again by the start
  path of the client (the start loading screen), and `ResetSession` sets the state of one run of the screens back. The
  widget, the resources, the input processor, the item catalog, and the drive stay. The reports of the runtime that ends
  are kept with the number of the return (`ui-errors-N.txt`, `ui-log-N.txt`, `ui-stubs-N.txt`).
- The selection by id: the host keeps the last selected character over the return and gives it to the new runtime before
  its first list (`Swc.HostMenuSelected`; the FE10 path writes it to `MainMenu.NewCharacterId` when the list has it). The
  landing screen selects the same character again each 30 s and after each list: the body of that character stays (before,
  the client built it again each time).
- The menu shows when its scene is ready. The preview actor, its scene record, and the menu mode of the environment come
  first, under the start loading screen. The landing screen starts when the meshes of the scene are placed and after 0.5 s
  of frames shorter than 0.25 s, at most 30 s after the spawn. Before this rule, after a return, one frame of 10 s (the sky
  mesh and the local lights of the environment of the new level) froze the landing screen, and the landing screen showed
  over an empty view while the meshes loaded.
- The UI drive: when a "waitfor" ends, the clock goes on from the time of that step (after a long pause, the next steps
  ran all at once).
- The run `f6-return7` (`tests/drive/f6-return-ui.txt`): the first development character enters the world; Escape,
  Exit to Main Menu, the quit popup, Yes; the carrier disconnect and the end of the connection in 0.14 s; the level load
  (39 s) and the scene (13.6 s); the landing screen with the played character selected; the second development character,
  PLAY, and the world at the place of that character. `evidence/F6/compare/f6-flow.png` and `f6-return7.mp4`.
- Tests: the fixture `escape` (30 checks: the exit to the main menu, the quit popup, Yes, one request "menu quit 1"); the
  fixture `frontend` (69 checks: the host selection).
- Open points:
  - The return takes about 1 min: the level load (36 s to 105 s on the shared computer), then the scene. The retail time
    is not measured.
  - A world connection that ends with no leave: the path is in the code, but no run tested it.
  - Each world entry has 3 Lua errors of the HUD 2024 scripts (`hudcanvasdefault` SetIsVisible, `lootticker2024canvas`;
    also in F5). The state changes of the escape menu give more of the same kind.
  - A refused authentication at the start (before the account) still has no new try (O14 in docs/UI.md).

### F4b, the look of the menu scene against B6-04 (2026-10-20)

The measure: the color report (`tools/lib/color_report.py`: CIEDE2000, L*, saturation, and hue of three bands) of a client
picture with the second row selected against B6-04, with a mask of the UI panels and the character (the retail character
has another look). Before F4b (K4, the menu mode): ΔE 24.3 (top 16.2, middle 30.6, bottom 30.6); the client is brighter
(L* 46.7 against 20.5), greener (hue +63 degrees), and less saturated (0.275 against 0.393).

- Step 1, the material overrides of the placements (the world rule, docs/WORLD.md "Material overrides of the placements"):
  238 of the 522 placements have an override (`Material Override Asset` of the mesh render node; 68 pairs). The scene
  converter makes their slot maps with `overrides.add_pairs` (the pair part of `overrides.convert`; the world uses the same
  function) and writes the slots of each placement; the preview loads the material instances with the meshes and sets
  each slot by its name (`/SwcGameContent/Materials/KEY`, as the cells of the world). The run `f4b-1`: 484 slots set,
  14 not imported (2 `n_` instance keys of the materials track). ΔE 21.8 (top 9.4, middle 28.1, bottom 35.7): the maple
  at the right is red as in retail, the house is darker. The dye flowers with the override "blackwhite" render white and
  bright (in retail they are dark): a point of their material instance (`m_8bdd69912d`, the materials track). The 68 pairs
  are at the slot map version 2 (A2/A2b) in the shared `pairs.json`; the world pairs are still at version 1.
