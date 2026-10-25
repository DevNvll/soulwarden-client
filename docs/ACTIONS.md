# The actions of the player with a weapon

Stage P2.7 (Linear AST-162). The character plays the stances of its weapon, the draw and the sheathe, the light chain,
the partial and the full heavy attack, and the weapon switch from the original action graph: the Mannequin fragments,
their layers and marks, and the transitions of the action lists. The server follows the same graph with the input steps
of the client (Soulwarden protocol notes 4.32), so the client plays the fragments that the server expects.
[GEAR.md](GEAR.md) has the gear and its places. [MOVEMENT.md](MOVEMENT.md) has the unarmed movement.

## The data

`tools/convert-player.sh actions` writes `SWC_CONTENT/player/actions.json` (`converter/swc_convert/actions.py`):

| Block | Content | Source |
| --- | --- | --- |
| `weapons` | For each weapon tag set: the stance fragments (`Idle`, `Strafe`, `Sprint`, `Sprint_Start`, `Fall`, `Land`, `Jump`), the fragments of a shield (tag `Offhand`), the draw and the sheathe fragments, and for a melee weapon its attack graph | The SubADB chain of `player.adb` (the first database whose tags the weapon has); the graph of the backend copy (`SWC_GAMEDATA_DIR/weapon-actions.json`, the same graph as the server) |
| `fragments` | The layers of each fragment (each item: its exit time, start time, blend-in time, speed, weight, loop, and clip or blend space), its blend-out time, and its marks (`CAGE-Named`: start and end) | The animation databases |
| `clips` | The duration, the flags (1: additive), the animated bones, and the root track of the attack clips | The `.caf` files |
| `blendSpaces` | The blend spaces of the layers, with the weight grid | The `.bspace` files |
| `activities`, `conditions` | `AnimationDrivenMotion`, `CameraRelativeFacing`, and `InputRelativeFacing` of each attack action, with their conditionals, and the named conditions | `playeractions_attack.actionlist` |

The clips come on demand in packs, one for each gender and weapon tag set: the client writes the packs that it does
not have into the wanted list (`"clips": [["Male", "1H_Melee,Sword"]]`); `tools/convert-player.sh wanted` (or
`clips GENDER TAGS`) writes `clips/<gender>/<tags>/clips.gltf` (the skeleton and each clip of the set, the root bone
locked), and `tools/import-player.sh` imports the animations onto the skeleton of the base. The sword set has 157 clips,
the greatsword set 88.

## The attack graph

`Combat/SwcAttackGraph` is plain C++ with the rules of the backend (`world/attack_sequence.cpp`,
`world/combat.cpp` `Combat::step_switch`):

- The clock of a fragment that started at the input step s is (k − s + 1) / 30 s at the step k.
- At rest a press starts the first fragment of the graph (`Attack_Start_Charge`).
- An instant transition starts its fragment at the first step in its window with its input. A buffered transition takes
  a press in its window and starts its fragment at the end of the window; a press before the window is lost. The full
  charge needs the hold at its step. A fragment ends at its length; a press that no window takes, now or later, starts
  a new attack.
- The weapon switch: a key of the other weapon switches at once at rest; in a fragment at its switch mark
  (`CanUnsheathe`, `CanTransition`) when a switch window (`CanUnsheathe_Buffer`) starts in the 20 steps of the key, else
  the key is lost. In the first 3 steps after the switch a press is kept; the kept press starts the first attack of the
  new weapon 3 steps after the switch.

The attack input of the input report (protocol notes 4.15): the press bit 555 with 203 and 408 at the step where the
left mouse button goes down, the hold bit 191 at each step after it while the button is down, and the release bit 567
at the step where it goes up. Each step takes one button event (a click in one frame is a press, then a release at the
next step). The driver (`swc::combat::Driver`) runs on the thread of the session at each input entry
(`RepClient::Options::inputStep`, [NETWORK.md](NETWORK.md)): it adds the bits, reads the weapon keys of the entry, and
steps the switch and the graph. Thus the client follows the graph at the same input steps as the server. The game
thread reads a snapshot: the visits, the last step, and the switches.

## The playback

`Character/SwcPlayerActions` (a component of the character) and `Character/SwcPlayerAnimInstance`:

- The base: the stance fragment of the weapon in the hand for the movement fragment (layer 0), else the unarmed
  fragment of `locomotion.json`. The other layers of the stance fragment and the fragment of the shield are overlays.
- Two action channels over the base: the draw or the sheathe (`UpperBody`), then the attack (`FullBody`). Each item of
  a layer with a weight above 0 is an overlay on the bones of its clip: an override with its weight, or an additive clip
  (its rotation is multiplied before the rotation below it; the order is an inference). An item with weight 0 keeps the
  pose below it (layer 0 of the attacks: the base plays on). An item blends in over its blend-in time; a fragment at the
  end of its channel blends out over its blend-out time.
- The clock of the attack fragment is the clock of its visit at the last input step, plus the time since that step
  (at most 1/30 s).
- The motion of an attack: the root track of its clip times `MotionMultiplier` of the first conditional of
  `AnimationDrivenMotion` that holds, in the frame of the character. `InheritVelocity` keeps the velocity at the start
  (`InheritVelocityMaxSpeed` at most), and `Decel` takes it to 0 (the unit m/s² is an inference). The move keys do
  not move the character while an attack plays.
- The homing of an attack (stage P3.G; research `~/Games/soulwarden-client/research/p3g-collision/report.md`): the
  `Procedural type="Homing"` clips of the fragment (`actions.json` `homing`; the rapier and the sword: from 0.067 s to
  0.40 or 0.43 s, MoveToDistance 1, MoveToDuration 0.2, MaxMoveDistance 4.25, Radius 3.75, MaxAngle 45, Height 5,
  TurnRate 0.4). At the start of a clip the client takes the creature in Radius, MaxAngle of the facing, and Height (the
  score distance / Radius + angle / MaxAngle: an inference) and its point T, the nearest point of the homing segment of
  the creature (the axis of the body capsule of the slice, the offset ± its length; the wolf: 1.0 m in front of its
  center). While the clip runs the root motion stops: the character moves on a smoothstep to 1 m before T in
  MoveToDuration (at most MaxMoveDistance, no upward motion) and turns to T with TurnRate. With no target the root motion
  plays on (an inference). The retail rapier chain on a wolf 2.6 m away (`set6-combat/player-rapier-light-heavy.json`)
  stops 2.00 to 2.02 m from the wolf; the client stops at 2.00 m (`USwcPlayerActions::MoveHoming`).
- The facing of an attack: `CameraRelativeFacing` (the first conditional that holds) turns the character to the camera
  yaw; else `InputRelativeFacing` turns it to the move keys. An exponential rate is the share of the remaining angle in
  each step of 1/30 s; else degrees in each second. The priority of the two is an inference.
- The conditions: `And`, `Or`, `Not`, `True`, `IfMannequinMarker` (the marks of the fragment at its clock),
  `IfMannequinTag` (the tags of the weapon), `IfCameraLockActive`, `IfInput` (the attack input and its hold),
  `IfFacingVelocityAngleDiff`. The other types are false on the client.
- The draw and the sheathe: each part moves at the mark of its fragment (`unsheathe_weapon`, `Unsheathe_Offhand`,
  `sheathe_weapon`). A switch sheathes the old weapon and draws the new one at the sheathe mark. The stance of a weapon
  starts at its draw mark and ends at its sheathe mark. An attack puts the weapon of its graph into the hand at once
  (an attack with a sheathed weapon, or the kept press of a switch; an inference).

## The melee contacts (stage P2.8, the part of the player track)

The split with the creature track (Linear AST-163): the player track makes the shapes of each strike and puts the contacts
into list 3 of the input entry; the creature track tests a shape against the hit volumes of the creatures and gives the
contact fields (`USwcCreatureSubsystem::FindMeleeContacts`).

- The strikes: for each graph fragment, the damage keys, the windows, and the shapes of the backend file (the same as
  the server), with the rotation offsets and the flags of the CAGE-Damage items of the animation databases (the
  backend file has no rotation; `actions.json` `graph[].strikes`). `tools/test-actions.sh` compares each strike with the
  backend: 243 strikes with 302 shapes.
- The window: in each input step whose clock (of the next entry) is in the window of a strike, each shape of the strike
  goes to the query. The shapes use the final pose of the frame (the delegate of the bone transforms of the mesh).
- A shape (`Combat/SwcMeleeShape.h`, `FSwcMeleeShape`, Unreal world space): a sphere, a capsule (its axis on local Z;
  `HalfLength` is half the segment, without the caps), or a box (the half extents: the values of the data are full
  dimensions, an inference from the box of the greatsword: 2.0 m on its blade of 1.76 m). The frame is a bone of the
  player at its pose, or the weapon in the hand (the hand place of docs/GEAR.md on the hand bone) with a joint of the
  weapon skeleton (`actions.json` `weaponJoints`; an empty joint is the frame of the weapon model, its blade on +Z). The
  offset and the rotation (about x, then y, then z) go through the space map of stage 1.1. The cast direction is the move
  of the shape since the step before. `UseEndAsCenter` and the scale of the clips are not applied.
- One contact for each target for each strike of a fragment visit: the key is the visit, the strike, and the entity group
  of the target, not its hit volume (`Combat/SwcContactFilter.h`; a second volume of the same creature in a later step of
  the strike gave a second contact, which the server refused as the same attack, or as dead after a kill). The contacts of a frame go into list 3 of the next
  input entry of the session (`RepClient::queue_contacts`), with the position field.
- The query is a hook (`USwcPlayerActions::SetContactQuery`): with no query the strikes give no contact.
- Command line: `-SwcDrawStrikes` draws the shapes; `-SwcContactProbe` sends one made-up contact for each strike (a
  receiver that does not exist) as a check of list 3.

Check (development run, `-SwcDrawStrikes -SwcContactProbe`, evidence in `~/Games/soulwarden-client/evidence/P2.8-player/`):
`strike-shapes.png` and `strike-shapes.mp4` (the capsule on the sword blade in the light swings, the forward capsule
of the heavy lunge, the box on the greatsword blade). The client: 9 strike windows, 25 shapes to the query, 9 contacts
in list 3. The backend: `hit claims of the session: 10 attack input(s), 9 melee contact(s): 0 accepted, 9 refused (9 not
a creature, ...)`.

## The bend of the upper body to the aim (usertest-1024 item 4)

The retail client bends the spine of the player toward the aim during an attack (the research `usertest-hitvolume.md`
section 4; the update function 0x6865830, the aim direction 0x6781C60). Thus a swing reaches a low target (a rabbit) when
the camera looks down. With no bend, the strike shapes of the great axe went above a low rabbit (2 contacts in 24 casts).

- The data: the Mannequin clip `AttackHeightRetargeting` (layer of a fragment; `actions.json` `fragments[].effects`, with
  `lockDir`, and `minAngle`/`maxAngle` or `blendIn`/`blendOut` when the clip overrides them) and the defaults of the
  `AttackHeightRetargetComponent` of the player slice (`actions.json` `retarget`: blend 0.35 s in and out, the limits −60°
  and +90°, the upper offset 0°, the lower offset 15°, the dead zone 5°, the joints `Spine`, `Spine1`, `Spine2`). 122
  fragments (all tag sets) have the clip: the attacks and abilities of the melee weapons, the bow, the staffs, the block;
  30 of them override the limits (for example −20° to +90°).
- The rule (`Character/SwcAttackRetarget.h`, tested by `tools/test-actions.sh`): the angle between the aim and its
  horizontal part, signed by the height of the aim, times max(0, the cosine of the facing of the character to the aim);
  no bend inside the dead zone; beyond it the offset is added; a smoothstep blend; the clamp to the limits. With
  `LockTargetDirOnStart` the aim of the first frame of the clip stays. The aim is the camera direction.
- The pose (`USwcPlayerAnimInstance::UpdateRetarget`, `FSwcPlayerAnimProxy::Evaluate`): each of the three joints turns by
  the angle / 3 about the horizontal right axis of the aim, in component space, after the overlays. The weapon in the
  hand and the strike shapes read this final pose.
- The log: `retarget: AttackHeightRetargeting starts (aim pitch ...)` and `retarget: the clip ends: the largest bend ...`.

Open points [I] (inferences, not proved by the decompile; the check with the retail take decides):

- [I] The sign of the lower offset: the stored value 15 is added as it is to a downward angle (a negative angle), thus it
  reduces the bend (an aim 30° down gives a bend of −15°). The other reading (the offset increases the bend) gives −45°.
- [I] The space of the turn: the client turns the joints in component space about the right axis of the aim, after the
  overlays (operator mode 3 of the retail pose code). A turn in the local space of each joint gives a different bend when
  the clip leans the spine.
- [I] The restart of the blend at the start and the end of the clip: the update 0x6865830 restarts the blend only at a
  change of the side of the dead zone. The client restarts it also at the start and the end of the clip, else the bend
  stays after the clip.
- [I] The aim: the camera direction of the controller. The retail function 0x6781C60 uses the target of the input when it
  has one.

## The dodge and the stamina (stage P2.5)

The retail client predicts its own stamina, and the Stamina state of the server does not change the predicted value
(Soulwarden protocol notes 4.38; `docs/systems/stamina-and-dodge.md`). The server decides each dodge at its input step
with its own stamina. Thus the client predicts at the same input steps with the same rules: `Combat/SwcStamina` (plain
C++, the rules of `world/stamina.cpp`) in the driver of the session thread, next to the attack graph.

- The data: `stamina-data.json` and the encumbrance row and the item loads of `perk-data.json` of the backend copy
  (`actions.json` `stamina`): the maximum 100, the regeneration 33.33 in one second, the delays 1 s, the winded time 2 s,
  the dodge of each load class (light `Dodge_Roll_Fast` 45, medium `Dodge_Roll` 45, heavy `Dodge_Roll_Slow` 50; the marks:
  the payment 0.1 s, CanTransition 0.5 s, the windows 0.267 s to 0.5 s buffered and from 0.5 s instant).
- The load class: the sum of the whole loads (tenths) of the armor items in the paperdoll slots 0 to 4 against the
  encumbrance row (the maximum 500; light below 26 %, medium below 46 %, heavy to 100 %, else overburdened: no dodge).
- The rules: a payment stops at 0 and sets the delay of the regeneration; at 0 the character is winded for 2 s and exhausted
  until the stamina is full; a dodge needs the stamina above 0, no exhausted state, and a load class with a dodge; it pays
  at its mark 0.1 s; there is no regeneration from its start to its mark CanTransition, then the delay of the exit (1 s).
  A press while a dodge plays: before the windows a duplicate; in the buffered window the next dodge at the end of the
  window (one for each window); in the instant window at once.
- In an attack the attack graph decides the dodge (the dodge windows of the fragment: CanDodgeCancel at once,
  CanDodgeBuffer at the end of the window, no dodge in a cancel window that starts with the fragment), and each step asks
  the stamina whether a dodge can start (a refused dodge keeps the attack). The parity test has dodge presses and refused
  dodges.
- The input: left Shift, one step with the bit 322 for each press, also for a press that the stamina refuses (live check
  LS9). The drive command `dodge`.
- The playback: the dodge fragment of the load class and of the direction of the move keys in the camera frame (no key: to
  the rear; forward; `LeftNav`, `RightNav`, `BackNav`), of the weapon in the hand or of the unarmed set (the clip pack
  `unarmed`), on the FullBody channel. At the start the character faces the camera (ApplyAlignRotation). The root track
  moves it with the motion of the action Dodge of `playeractions_dodge.actionlist`; the facing of that action turns it. The
  roll ends at its mark CanTransition (0.5 s; stage P3.G: the retail rolls of `set6-combat/player-dodge-stamina-2.json` end
  there with or without a move key, 0.297 m each step, then the next state): with a move key the movement starts from the
  velocity of the roll, at most `InheritVelocityMaxSpeed` of the action (6 m/s); with no move key the character stops at once.
- The HUD: the read path (`USwcBackendSubsystem::GetLocalPlayer`) gives the predicted stamina, thus the stamina bar of the
  UI track shows the prediction.
- The correction (a rule of this project; the retail client takes the server copy at a rewind of its movement, which this
  client does not have): the driver moves the last Stamina state forward with its regeneration (field 3, the delay that was
  left) and takes it when the prediction differs by more than 10 for 2 s with no payment in that time. The multiplier of the
  regeneration of the state (field 5) goes into the prediction.
- The movement capsule of the creatures (the creature track) stops a roll as it stops a walk: no special rule.

Check (development runs, `tests/drive/dodge.txt`, recorded from the window with the HUD and the debug text; evidence in
`~/Games/soulwarden-client/evidence/P2.5/`): `dodge-light.mp4` (the heavy chest only, 11.0), `dodge-medium.mp4` (with the
heavy legs, 17.3), `dodge-heavy.mp4` (with the head and the hands, 26.7): four rolls in the four directions, then four
presses 0.73 s apart. In each run the client and the backend had the same verdicts: light and medium 7 dodges at 45 and
one press refused (exhausted), heavy 6 dodges at 50 and two presses refused; the stamina full again after the empty
sequence; no correction; 0 refused position reports.

## The block (stage P3.G)

The right mouse button with a melee weapon (the tags 1H_Melee, 2H_Melee, MagicStaff, Ice of `playeractions_block`) raises the
weapon: the action Block_UpperBody plays `Block_UpperBody_Raise` (the rapier: `rswrd_block_start` of `player_1h_melee.adb`, speed
2.0), then Block_Continue_UpperBody holds `Block_UpperBody` while the button is down, on the UpperBody channel. Both set the tag
Blocking: the stand and the move use the Blocking variants of the stance (`Idle+Blocking` `rswrd_block_idle`, `Strafe+Blocking`
`rswrd_block_strafe_blend`; the converter takes them from the next database of the chain), the native Nav takes the branch of the
block (2.0 m/s, 25 / 25 m/s²), and the condition SprintDisabled holds (no sprint). The start clears the motion (ClearMotion). The
block lasts at least Block_ForceBlock_Timer (0.3 s, the status table of the backend copy). A sheathed melee weapon of the active
slot goes to the hand at once (Trigger_From_Sheathed). No block in an attack, a dodge, prone, with the stamina at 0 or exhausted,
or in the life lock. `USwcPlayerActions::UpdateBlock`.

The input: the retail client sends the same bits for the right button with each weapon (the bow aim and the rapier block of set
6): at the press `aim_draw_press_virtual`, `aim_draw`, `aim_draw_active_virtual`, `ui_back_radial`, `aim_draw_press`,
`aim_draw_release_virtual`, `block`, `block_active_virtual`, `fishing_reset`, `block_depressed`, `rotate_character_start`,
`block_press_virtual` (484), `rotate_character_start_hold`, `musicnote_chord_2`; in each next step while it is down `aim_draw`,
`aim_draw_active_virtual`, `block_active_virtual`, `block_depressed`, `mount_stop`, `rotate_character_start_hold`,
`mount_stop_virtual`; at the release `unbind_key`, `rotate_character_end`, `aim_draw_release`. The driver of the client sends
them (`swc::combat::kBitsRightPress`, `kBitsRightHeld`, `kBitsRightRelease`; the press is the block literal of the transitions).

The backend has no block rule: a blocked bite makes its full damage (the retail take shows the same). The retail character has
no reaction to a bite (see "Open point: the hit reaction of the own character").

## The death and the respawn (stage P2.10)

The client plays the death of its own character itself (protocol notes 4.18): the server sends the vitals state with the dead
mark (bit 0 of `vitalsData`) and the death notice 3950 with the time to the respawn screen (5 s), and no animation state for a
player. `Combat/SwcLife` (plain C++) has the state and the lock of the input; `USwcPlayerActions` plays it:

| State | Data | The client |
| --- | --- | --- |
| Death | the action Death of `playeractions_death.actionlist` (IfDead): SheatheWeapon of the main and the off hand (the tag Unarmed), DisableInteractor, DisableCameraLock, then `r_Death` with the tag Unarmed (`unarmed_death_droptokneesfaceplant`, 4.23 s) | the attack, the aim, and a timed use end; the weapons go to their sheaths at once; the fragment plays on the FullBody channel and holds its last pose; the camera lock ends; the camera state DeathCamera |
| The give-up | the button "Respawn" of the respawn screen after the time of the notice: the request 1938 with the row "Random Location"; the client shows its loading screen before it sends | the original respawn screen of the UI track (`lyshineui/respawn/respawn`: `LocalPlayerUIRequestsBus.RequestRespawn(random, homeId)` calls `USwcBackendSubsystem::RequestRespawn(bool, FString)`); also the development key F8 (the drive command `respawn`); each refused before the time of the notice |
| The respawn | the server: 3746, 6709, the movement state with the next teleport number (the nearest travel place), the vitals state with the mark clear, 3954 | the pose of the death holds while the loading screen shows and while the cells of the new place load; then the character goes to the place and the action Respawn plays `Respawn` (`unarmed_crch_idle_to_idle`, 0.733 s); the input is free at its mark CanTransition (0.533 s); the full stamina |

- The lock of the input: the move keys, the jump, the attack, the dodge, the aim, the weapon keys, and the sheathe key do
  nothing while the character is dead, while a loading screen shows, while the respawn plays before its mark, and while a
  timed use plays (`USwcPlayerActions::IsInputLocked`; the controller gives the movement no keys).
- A heal of a dead character (the development route `heal`) has no travel: the respawn plays at once at its place.
- The development key F8 is in no action map: it does nothing while the UI has the cursor (`USwcUiSubsystem::UiCursor`, the
  cursor owner of stage P2.2; checked with the escape menu open, `tests/drive/menu-hold.txt`).
- The timed use of an object (the action DelayedInteraction of `playeractions_interact.actionlist`: the options Trigger,
  PickupBuildable, RevivePlayer) sheathes the weapons at once and plays `Delayed_Interact` (the kneel:
  `unarmed_idle_to_crch_idle`, then the loop `unarmed_crch_bag_idle_field_craft`) while its request lasts
  ([INTERACTIONS.md](INTERACTIONS.md)).
- The clips are in the clip pack `unarmed` of each gender (`tools/convert-player.sh actions`, `clips Male unarmed`,
  `clips Female unarmed`, `tools/import-player.sh`).

Check (development runs on the test floor, 2026-10-18; evidence in `~/Games/soulwarden-client/evidence/P2.10/`):
`death.mp4` with `tests/drive/death.txt` and the routes `tests/drive/death-routes.txt` (`Swordwarden`, the sword in the
hand, the route `hurt 100000` at 4 s): the death fragment and the weapons in their sheaths at the notice; the move keys, a
jump, a click, and a dodge while dead gave no movement, no attack, and no dodge bit; the give-up at 8 s (3.9 s after the
notice) was refused, the give-up at 10 s went out; the travel of 4,080.3 m (teleport number 2) with 3746, 6709, and 3954; the
cells of the new place in 2.3 s to 3.0 s; the respawn fragment free at 0.56 s to 0.60 s; then a sprint, the draw, and an
attack. The backend accepted each position report (533, 0 refused) and used no report from the old place.

## Exit check of stage P2.10 (development run on the world level, 2026-10-18)

The complete fight loop: `tests/drive/fight-loop.txt` with the routes `tests/drive/fight-loop-routes.txt` and the UI drive
`tests/drive/fight-loop-ui.txt` (`tools/creature-check.sh fight-loop 70 ...`, `Swordwarden` at level 60 at 9255 4735 m, 12 m
from the Dustmeadow Shrine). Evidence in `~/Games/soulwarden-client/evidence/P2.10/`: `fight-loop.mp4`, `fight-loop-client.log`,
`fight-loop-backend.log`, `fight-loop-routes.txt`.

- The fight: six grey wolves, two swings each: six kills (6 melee contacts, 6 accepted), five items in five bags.
- The loot: E at two bags (0.7 m and 1.4 m): after the hold of 0.5 s the take; the bag of the character from 9 to 10 and 11
  items; each claim Looted; the end of each use (2473).
- The death: the dead mark and the notice 3950 (5 s); `r_Death`; the original respawn screen with the world map and the row
  "Random Location"; the button RESPAWN (it shows 2 s after the map); the request 1938; the travel of 10 m to the shrine; the
  respawn fragment free at 0.54 s.
- The continued play: a sprint and an attack chain at the shrine. The backend accepted each position report of the session
  (1,837, 0 refused).
- The nearest travel place of the first place of the check (8872 4250 m) is the Inn of Oxboro: the respawn there is in the inn,
  and the simple building collision of the world level keeps the character in the state Fall (a known defect of the world
  track). The place of the check has a shrine as its nearest travel place.
- Note for a route script: a creature with 1 health gives no credit for its kill (no loot), and a creature that ignores the
  character takes no death from a hit; the check uses 100 health and fighting creatures.

The bow, the fire staff, and the life staff have no attack graph in the backend: their basic attacks and abilities come from the
action lists of the data ([RANGED.md](RANGED.md), stage P3.6).

## The abilities (stage P3.5, the player part)

The keys of the bar play the abilities of the weapon in the hand from the same graph as the server (Soulwarden protocol
notes 4.22 "The keys", 4.27, 4.32; backend `world/attack_sequence.cpp` `AttackSequences::start_ability`, `world/abilities.cpp`).
The build of the check is the build sheet of the user (great axe: Charge, Maelstrom, Gravity Well; war hammer: Wrecking Ball,
Shockwave, Path of Destiny; 19 nodes of each tree).

The data (`tools/convert-player.sh actions`, the graph of the backend copy):

| Field | Content | Source |
| --- | --- | --- |
| `abilities` of a weapon | The ability id and its CRC-32 (the id on the bar), the action, the fragment, the selections of a fragment by the nodes and the perks (`ChangeActionFragment`), the casts of the action, the cooldown (`Time`, `MinTime`), and the shots | `weapon-actions.json`, `ability-data.json` (`cooldowns`), the spell tables (`javelindata_spelltable_*`: a cast of a spell with `CastingTypes` Projectile has a shot with the CRC-32 of its `RangedAttackName`) |
| `transitions` of a graph fragment | Also the starts of the abilities (`ability`, row 6: the ability id) and the transitions inside an ability (`terms`, row 6: an OR of ANDs of the literals `input` attack, block, collided; `node`; `perk`; `playing`; `hits`) | `weapon-actions.json` (format 2) |
| `markers`, `casts` of a graph fragment | The named marks (the throw of Gravity Well: `CastGavityWellProjectile` at 0.533 s) and the spell casts | `weapon-actions.json` |
| `rows` of a strike | The damage row of each row of the strike, the nodes that it needs and forbids, and its shape | `weapon-actions.json` |

The rules (`Combat/SwcAttackGraph`, plain C++; `Driver` on the thread of the session):

- The keys Q, R, F (the places 0 to 2; the input numbers 402, 115, 139): the first entry of a press has the press bit and
  the virtual bit (402 and 479, 115 and 571, 139 and 523), each step while the key is down the hold bit (39, 332, 207), the
  step of the release the release bit (211, 196, 172). One press in each step. The bits go out also for a press that the
  client does not play (the backend reads only the press bit).
- The order of a step: the attack step of the sequence, then the press (`RepSession::handle_attacks` of the backend). The
  ability of the press: the bar of the weapon in the hand (the bar of the Ability state whose abilities the graph of the
  weapon has). The fragment: the last selection whose learned nodes and perks hold, else the fragment of the action.
- The start (`Sequence::start_ability`, the rules of the backend): at rest at the press; in a fragment only the step of the
  press counts: an instant window `CanAttack_<ability>` that holds it starts the ability at the press, a buffered window at
  its end (`start + max(round(end * 30) - 1, 0)`; the step loop starts a pending ability first), the mark `CanTransition`
  and the end of the fragment at the press. With none, the client does not play the press. The fragments of one ability
  keep its cast (not a fragment `Attack_*`).
- The transitions inside an ability (`terms`): the learned nodes (the ProgressionPoint state, slot 67), the attack and the
  block input of the step, the fragment that plays, and the collision of the character. The client sees its collision: a
  block of the movement by the body of a creature in a fragment (an inference: only a creature; the retail input
  `character_collided` is not described). The server does not see it, and it takes the transition at the match of the
  contact. The hits of a cast (`IfAbilityNumHits`; only Whirlwind with Unending Winds reads them) are the hit count of the
  ability in the Ability state (fields 1 and 2, stage MVP-A: `Driver::set_ability_hits`); 0 before the server sends one.
- The cooldown: the client starts its own cooldown at a press that it plays, with the time of the data (the setter of the
  retail client runs at the press), and it does not play a press in its own cooldown. The countdown of the HUD comes only
  from the cooldown state of the server (slot 45; the UI track). The own cooldown only blocks a press. The correction: the
  end of an entry of the server is the end of the client, one time for each end (the backend keeps the end of the client
  equal to its end, also after a reduction and with the cooldown factor of the character); an ability with no entry keeps
  its own end (a reset of the server: the client keeps its cooldown, live check I8-05 of the notes).
- The shot of a cast: at the step of the named mark of its spell in the fragment of the cast (Gravity Well: 15 steps after
  the press), one item in list 2 of the input entry with the attack of the data (`8E14366B`, "gravitywell"); the origin
  1.5 m above the character, the direction of the look ([NETWORK.md](NETWORK.md)).
- The strikes of an ability: only a strike with a row for the learned nodes (`Combat::row_for` of the backend: Maelstrom has
  two strikes in each window, one with the node Gravity and one without it), with only the shapes of those rows.
- The fragments play as the attacks: their clips, their root motion and facing (the actions of
  `playeractions_ability_<weapon>.actionlist`), and their strikes. Charge goes in the direction of the body: a short walk
  turns the body to the camera (protocol notes 4.22).

The creature motion (the pull of the vortex, the push of Path of Destiny, the impacts) and the spell entities come from the
server; the creature track shows them ([CREATURES.md](CREATURES.md)).

## Exit check of the player part of stage P3.5 (development run on the world level, 2026-10-18)

`tests/drive/p35-abilities.txt` with the routes `tests/drive/p35-abilities-routes.txt` (`tools/creature-check.sh p35-abilities 45
...`, `Swordwarden` at 9255 4735 m with the great axe in slot 24 and the war hammer in slot 25, the build of
`tests/drive/p35-build-routes.txt`). Evidence in `~/Games/soulwarden-client/evidence/P3.5/`: `p35-abilities.mp4`,
`p35-abilities-sheet.png`, `p35-abilities-client.log`, `p35-abilities-backend.log`, `p35-abilities-routes.txt`.

- The bars and the nodes: the client took the bars of the two weapons and 38 learned nodes from the states of the server.
- Charge: the collision with the wolf 11 steps after the press, the uppercut at the next step, its hit (row
  `GreatAxe_Engage1`) and its impact.
- Maelstrom (`_Upgrade`: No Reprieve): the two spins, the hits of the pull rows (`_Reach_RP2`, `_NoReprieve_RP2`: the node
  Gravity), the travel of 2 m of each wolf.
- Gravity Well: the shot 15 steps after the press; the projectile of the server at a creature; the vortex (4 creatures, the
  pull to its center at 7 m/s) and the burst after 3 s (4 hits).
- The switch to the war hammer; Wrecking Ball: the knockdown of 4 wolves (the reaction of power 5, 2.8 s); Q 2 s later: the
  own cooldown (the bits went out; the server refused it too: "the cooldown runs"); Shockwave: the stun of 4 wolves; Path of
  Destiny: 3 hits and the push of 2 m; its end on the server 3.3 s earlier after the hits (Stimulated Reduction), which the
  client took.
- The reset of the server at 29.5 s: the bar has no countdown, and F in the own cooldown of Path of Destiny played nothing.
- The counts: 8 presses (6 played, 2 in the own cooldown), 24 hits, 10 melee contacts (10 accepted), 1 shot, 1,349 position
  reports accepted (0 refused).

## The supported-action list (stage MVP-A, Linear AST-214)

The client plays, sends, and shows an action only when the fixed backend has a rule for it. An action with no rule gives no false
success, heal, cooldown, or damage result. The list is a copy of the backend source (read only) in `Combat/SwcBackendSupport.h`:
`world/abilities.cpp` `kRules` (the abilities with a cast plan) and `world/runes.h` `kDetonateAbility`. `tools/test-actions.sh`
reads the two backend files and compares them with the copy: a new rule of the backend fails the test until the copy has it.

The active abilities of the five weapons of the MVP (16 of 30 have a rule):

| Weapon | Supported (a cast plan of the backend) | Unavailable (no cast plan) |
| --- | --- | --- |
| Sword and shield | none | Reverse Stab, Whirling Blade, Leaping Strike, Shield Bash, Shield Rush, Defiant Stance (`Ability_Sword_FinalStand`) |
| Rapier | Riposte | Tondo (`Ability_Rapier_Slash`), Flurry, Flourish and Finish, Fleche (`Ability_Rapier_Aerial_Lunge_Intro`), Evade (`Ability_Rapier_Evasion`) |
| Bow | Penetrating Shot, Explosive Arrow (`Ability_Bow_SplinterShot`), Rapid Shot | Poison Shot, Evade Shot, Rain of Arrows |
| Great axe | Charge, Reap, Execute, Whirlwind, Maelstrom, Gravity Well | none |
| War hammer | Wrecking Ball, Shockwave, Path of Destiny, Mighty Gavel, Armor Breaker, Clear Out | none |

The rule of the client for an unavailable ability:

- The driver (`Driver::set_supported_abilities` with the set of `supported_abilities()`; `USwcBackendSubsystem` sets it at each
  world entry) keeps the key up: no press, hold, or release bit goes to the server, no fragment plays, and no own cooldown
  starts. The verdict of the press is `PressVerdict::unsupported`; the log line is `ability: key Q (place 0) ... no rule of the
  backend (not sent)`, and the counts at the end of a session have "N with no rule of the backend". The keys of the bow go
  through the same gate before the ranged program.
- The HUD shows the slot disabled with the original rule of `weaponquickslots.luac`: its `GetIsAbilityEnabled` (a silenced or
  mounted character, no mana, no shield) gets one more condition through a script hook (`swc/owner.lua`, `Swc.scriptHooks`):
  an ability that is not in the list of the host (`Swc.HostSupportedAbilities`) is not enabled. The slot then dims (the Dimmer
  of `weaponabilityslot.luac`, opacity 70 percent), and a press of its key plays the disabled flash of the slot
  (`OnTryUseDisabledAbility`). The retail client has no text for this case: the client shows none.
- The cooldown state of the server is the only source of a cooldown on the HUD: an ability with no rule never gets one.

The other actions of the list:

| Action | Backend rule | Client |
| --- | --- | --- |
| Heart rune | Only `Ability_Rune_Detonate` activates (the key G, input bit 450, with a full charge). Bile Bomb and each other rune ability have no rule (`docs/systems/passive-rules.md` of the backend). | The charge (slot 48) fills the rune icon of the HUD. G (the action `rune_ability` of the action maps) goes to `heartgemslot.luac` OnCryAction; a script hook (`swc/owner.lua`, stage MVP-E) sends bit 450 only for Detonate with the state CHARGED (a full charge) and no silence. A rune with an other equip ability never shows the state CHARGED (no ready glow; the fill shows the charge of the server), and G plays the red flash of the script and sends nothing. The icon empties at the next charge state of the server, not at the press. The equipped rune stays: the client does not change it to hide the limit. |
| Block | No block rule, no shield damage reduction, no grit, no hit reaction of the own character. | The right button plays the block pose only (see "The block"); the server damage is unchanged. No HUD value shows a block result. |
| Travel | The admin travel command of the backend and the nearest-place respawn. No retail shrine travel, discovery, fee, inn binding, or inn recall. | Incoming travel and respawn work. The travel selector is the work of the UI track (order 7). |
| Recipes | Crafting state 205 and the requests 2997 / 2999. The backend refuses the recipes with a RequiredExpansion field and the recipes with conditions that it does not support: of the 8,294 rows of the copied crafting data, 1,917 have no export cause, 149 of them with an expansion field, thus at most 1,768 are candidates before the rank, station, material, and fee checks (research `mvp-gaps.md`, E11). | No craft screen yet (order 14). The client reads the craft state (the cooldown windows and the gear score bonuses) and makes no item locally. |
| Consumables | Use from a quick slot (bits 404, 117, 140, 513) and Use of the bag (2917); the server owns the item loss, the heal, the statuses, and the shared cooldowns. | The quick slots show the server cooldowns (stage P3.H). |

Exit check of stage MVP-A (2026-10-20): `tools/test-actions.sh` (the copy against kRules and kDetonateAbility, 16 of 30, the gate of
the driver), `tools/test-movement.sh` (the owner states of the backend encoders into the client: 64 checks), `tools/test-ui.sh`
(fixture `ownerstates`: 25 checks). A development run with a copy of the user's database in its own data folder (the level 70
character: a bow and a rapier with Evade, Flourish, Fleche; drive `tests/drive/mvpa-owner-ui.txt`): with the rapier in the hand the
three slots dim, each key logs "no rule of the backend (not sent)", and the backend counts one ability press in the session (the
bow); the equipment screen shows STR 50, DEX 456, INT 5, FOC 5, CON 19 (the points of the database plus the minimum 5) and the Azoth
2,500 / 2,500. Evidence: `~/Games/soulwarden-client/evidence/mvp-a/` (`mvpa-owner2.mp4`, `hud-compare.png`, `full2-34.png`).

## The consumables (stage MVP-E, Linear AST-217)

The quick slot keys 3 to 6 (`consumablequickslots.luac`, `SendVirtualInput Use_Quickslot_1` to `_4`) send the bits 404, 117,
140, 513 for the paperdoll slots 14 to 17 (stage P3.H). The server owns the use: the item loss, the heal, the statuses, and the
shared cooldowns (protocol notes 4.23). The client plays the action `Use_QuickSlot_N` of `playeractions_use.actionlist`:

- The fragment `Use` of `player.adb` for the `MannequinTag` of the consumable (`javelindata_itemdefinitions_consumables`) and the
  posture: Potion `unarmed_potion_drink_ub`, Eat `unarmed_eatfromhand_ub` (speed 1.5), WeaponOil `rswrd_imbue`; a tag with no
  variant (Boost: the dusts) takes the variant with no tag, `unarmed_drink_ub` (speed 1.3). Crouch has its own variants. The clips
  are upper-body clips: the fragment plays on the UpperBody channel, and the legs keep the locomotion.
- The conditions of the activation: an item in the slot, not prone, not in deep water (the swim), not mounted. The client does not
  check the consumable cooldown timer of the condition: the retail client plays the drink also in a cooldown of the server (live
  proof I7 and I12 of the protocol notes).
- The end: the end of the fragment or its mark `Exit_Use` (the deactivation condition).
- `tools/convert-player.sh actions` writes the fragments (`use|POSTURE|TAG` of the use map of the unarmed set) and the tags of the
  consumables (`consumables` of `actions.json`). Code: `USwcPlayerActions::StartConsumableUse`, called from
  `ASwcPlayerController::PressVirtualBits`.
- The inputs while the fragment plays (`player.grid`, the rules from Use; research `mvp-e-static.md` 4): an attack, an ability key, or
  a dodge before the mark `CanBuffer_UpperBody` (B) does nothing; from B it waits, and at the mark `CanTransition_UpperBody` (T) the use
  ends and the waiting input starts (`USwcPlayerActions::GateByUse`). A block and a jump do not start (Never). The movement goes on.
  The weapon swap keeps its own path. No move speed of the use is proved: the client sets none. The marks: Potion B 0.67 s, T 1.0 s,
  `Exit_Use` 1.2 s; Drink B 1.67 s, T 2.0 s, `Exit_Use` 2.67 s; Eat B 1.67 s, T 2.0 s, `Exit_Use` 2.0 s.
- The dim of the four quick slot keys: the in-use state of the paperdoll (retail `SetInUsePaperdollItem` 0x6925570, the clear
  0x67A3800), not a timer. The client sends the slot of the use to the HUD (`HostConsumeSlot`: `SetConsumeSlot` and
  `CancelConsumeSlot`), and `consumablequickslots.luac` dims the keys.
- Live check (2026-10-21, a copy of the user's database in the own data folder; `tests/drive/mvpe-use*.txt`, run `mvpe-use7`;
  evidence `~/Games/soulwarden-client/evidence/mvp-e/`: `mvpe-use7.mp4`, `use7/use-sheet.png`, `use7/screen-values.png`): key 3 plays
  `Use|Potion` (1.23 s), the server heals 2,830 to 5,330, and the stack goes 49 to 48 on the HUD; a click at 0.61 s of the use does
  nothing, a click at 1.07 s starts the attack; key 6 plays `Use|Eat` (2.51 s) and the jump in it does not start; key 5 (the dust) plays
  the variant with no tag. G with Bile Bomb sends nothing (run `mvpe-items1`); with Detonate, the charge rises with the hits (100
  percent after about 40 contacts of the great axe on wolves of level 70), G sends bit 450, the backend gives
  `Rune_Detonate_Dominator`, and after 3 s the explosion hits the wolves near the character.
- Open: the item mesh in the hand, and the time of the consume (the retail client consumes at `UsePaperdollItem`; this client sends
  the bit at the press, as before, and the server applies the use at once).

## The five weapons (stage MVP-E part 9, Linear AST-217)

- The perks of the items for the action selection: `USwcBackendSubsystem` reads the perk list of the wire (`perk-data.json` of the
  backend copy: the perk with index K + 1 is row K) with the ability rows of each perk, and gives the driver the ability rows of the
  perks of the equipped items (`Driver::set_facts`; the backend rule `Gear::perks`: the armor and the jewelry 0 to 7, the heart rune 45,
  and the active weapon with its off-hand item). The selections and the terms with a perk (`IfAbilityActive` External) then hold: of
  the five weapons only the artifact Whirlwind (`GlobalPerk_Artifact_Set2_GreataxePerk`) has one. The current character: 26 ability rows.
- The hit counts of the abilities: stage MVP-A (`Driver::set_ability_hits`).
- Live check (2026-10-21, run `mvpe-five1`, `tests/drive/mvpe-five*.txt`, a copy of the user's database; the sword and the shield are
  given in the copy by the development routes): the bow (a hip shot and an aimed shot hit; Penetrating Shot pierces, 6,547; Explosive
  Arrow 1,547 and the explosion 5,584; Rapid Shot: three arrows after the death of the wolf); the great axe (Charge with the uppercut
  on two wolves, light attacks, Maelstrom with six hits, Gravity Well: the vortex where no wolf stood); the rapier (a light chain of
  three, a heavy attack; Evade and Fleche send nothing); the war hammer (Wrecking Ball on two wolves, Shockwave three hits, Path of
  Destiny three hits and the push of 2 m, light and heavy attacks); the sword and shield (a light chain, the block pose of 1.6 s, Q with
  no ability on the bar, a heavy attack on two wolves). The backend counts 12 presses: 6 played, 4 with no ability, 0 refused.
- Open: the own arrow view of the bow abilities stops at the ground before the target (the server hits are right); the aim pose of
  the bow does not follow the camera pitch (the fragment `Aim` uses the aim-pose clips `bow_*_aim_poses_15x`: 15 poses of a yaw and
  pitch grid, which the client does not play); the block has no server rule (a pose only); the homing of Wrecking Ball and the pull
  of Reap are made by the server only.

## The tests

`tools/test-actions.sh` (no engine) builds `tests/actions/actions_tests.cpp` with the attack sequence of the backend
(its sources, read only):

- the action order of the sword: a click, the light chain, a press before a window, a press after a window, the
  partial heavy attack, the full charge, the repeat of a long hold, a new attack after the end;
- the marker order: the marks of each fragment with marks come one time, in the order of their times, for frames of
  1/30, 1/60, 1/144, and 0.1 s; the draw marks of the sword;
- the weapon switch (live checks SW-L1 to SW-L3 of the notes) for the war hammer to the great axe and for the sword to
  the greatsword;
- the driver: the bits of a click, a hold, and a release; a weapon key at rest;
- the parity: 3,672 input scripts (the live check inputs and random clicks and holds) for each of the 12 melee graphs:
  the client and the backend start the same fragments at the same steps;
- the strikes: the damage key, the window, the count of the shapes, and the rows of each strike are the ones of the backend;
- the abilities (stage P3.5): the data of each ability and each transition is the data of the backend (the ids, the
  fragments, the selections, the terms); the cooldown and the shots of Gravity Well; the fragment of Maelstrom with No
  Reprieve; the parity of the ability presses: 2,400 random scripts of clicks, block presses, and presses of the abilities of
  each melee weapon, with random learned nodes, give the same verdicts (at once, later, refused) and the same fragments at the
  same steps as `AttackSequences::start_ability` and `step` of the backend;
- the keys of the bar in the driver: the bits of a press, a hold, and a release; Charge at rest and its own cooldown of 20 s;
  the uppercut at 0.967 s with no collision and at the step of a collision; a press in the own cooldown (its bits go out);
  an end of the server that comes earlier (a press after it plays), one time for each end; a reset of the server (the own
  cooldown holds); a press with no window (the first steps of Gravity Well); the shot of Gravity Well 15 steps after its
  press; an empty place of the bar;
- the stamina and the dodge: the live checks LS5 and LS6 and the live proof P5 of the notes (the payments 45, 45, 10, the
  fourth press refused, full again 4.4 s after the third payment), the windows of a dodge that plays, the load classes, and
  the dodge bits of the driver;
- the life (stage P2.10, `Combat/SwcLife`): the death and its lock, the end of a timed use at the death, the hold of the
  death pose under the loading screen, the respawn and its mark CanTransition, a heal with no travel, the loading screen of a
  travel.

```sh
tools/test-actions.sh   # 8690 checks, 0 failed
```

A video of the actions: the recording of the agent desktop, not `-SwcFrames` (the rule of the videos in
[MOVEMENT.md](MOVEMENT.md), "The drive scripts").

## Exit check (development runs, 2026-10-18)

`Swordwarden` (the sword in slot 24, the shield in 27, the greatsword in 25) on the test floor of the world level (a
place outside the built regions: the frame rate of the world level was about 8 frames in a second under the load of the
computer, too low for the windows of a chain), `tests/drive/actions.txt`, the pictures of `-SwcFrames` with
`r.MotionBlurQuality 0`. Evidence in `~/Games/soulwarden-client/evidence/P2.7/`:

- `chain-heavy-charge.mp4`: the draw; the light chain `Attack_Primary`, `Attack_Primary_Strike2`,
  `Attack_Primary_Strike3`; the partial heavy attack `Attack_Heavy` (the release 0.4 s after the press); the full
  charge `Attack_Heavy_Charged`; the greatsword chain; the sheathe.
- `buffered-swap.mp4`: the key 2 in a sword chain. The switch came at input 624, the switch mark of
  `Attack_Primary_Strike2` (its start 601 + 23); the backend log has `Swordwarden switches to slot 25 at input 624 (the
  key at input 592, 32 steps before); a kept press starts the first attack 3 steps later`. The greatsword swing started
  at input 627. The backend counts (`/v1/dev/world`, `basicAttacks`): 16 visits, 1 end by a change of the weapon; the
  client had 16 visits too. 654 position reports accepted, 0 refused.

## The fight check of stage P3.G (set 6, 2026-10-19)

The retail takes of set 6 (`~/Games/soulwarden/work/evidence/unreal-references/set6-combat/player-*.json`, the preset
bow-boltcaster, level 70) go into the client as replays (development runs, the same preset, the own agent desktop):

- `tools/lib/retail_replay.py TAKE.json tests/drive/p3g-NAME`: a drive script and a route script from a take: the keys at the
  input step of the retail input report that shows them (the harness time can be late), the look of the harness with the same
  angles in the same steps (0.0984 degrees for each unit of the harness), the zoom option of the take, a travel to the retail feet
  before each part, the creatures 0.4 m above the floor at their retail offsets, the ignore commands of the take at their times.
- `tools/lib/retail_compare.py TAKE.json CLIENT_LOG --backend BACKEND_LOG`: the move segments (the steady speed, the time to
  90 %, the stop distance from the key up), the backend events of both sides on one clock (hits, dodges, stamina payments,
  refused dodges, shots, arrows, bites, the death), the dodge distance in 0.6 s, the camera pitch limits.
- `tools/lib/side_by_side.py`: the retail video and the client video side by side, the same time from the first key, a line of
  text with the values of each part. The videos: `~/Games/soulwarden-client/evidence/P3.G/sbs-*.mp4`.

| Item | Retail | Client |
| --- | --- | --- |
| W, rapier drawn: speed, to 90 %, stop distance | 5.49 m/s, 0.27 s, 0.73 m | 5.48 m/s, 0.27 s, 0.74 m |
| S drawn | 3.99 m/s, 0.17 s, 0.26 m | 3.99 m/s, 0.17 s, 0.26 m |
| D, A drawn: stop distance | 0.73, 0.72 m | 0.75, 0.71 m |
| W sheathed | 5.47 m/s, 0.27 s, 0.72 m | 5.45 m/s, 0.24 s, 0.70 m |
| S sheathed (the combat stance of the active slot) | 3.99 m/s, 0.26 m | 3.96 m/s, 0.24 m |
| Standing jump: apex | 1.18 m at 0.57 s | 1.18 m at 0.55 s |
| Run jump: apex | 1.04 m at 0.47 s | 0.73 m at 0.25 s (open point) |
| Rapier light chain: the stop of the homing | 2.00 to 2.02 m from the wolf | 2.00 m |
| Rapier hits (Strike2, Strike3, the late clicks, the full heavy) | 2.51, 2.98, 10.13, 13.07, 22.01 s | 2.72, 2.99, 10.22, 13.12, 22.12 s |
| Dodges: time, stamina 45 at 0.10 s, distance in 0.6 s | 4.15, 4.48, 4.48 m | within 0.05 s; 4.11, 4.25, 4.43 m |
| Exhausted: refused dodges | 5 and 2 | the same, within 0.07 s |
| Block: the move in the block | 1.91 m/s (2.0) | 1.96 m/s |
| Bow: the full draw, the arrow | 18.3 m in 188 ms, a hit | 17.9 m in 182 ms, a hit |
| Bow: the aim walk W, S, A | 1.59, 1.63, 1.62 m/s | 1.59, 1.59, 1.63 m/s |
| Camera: the pitch limits; the distance at −70° in the aim and at +60° | −70 to 60°; 2.70, 1.08 m | −69.9 to 60°; 2.73, 1.10 m |
| Death: from the first bite to the death | 3.32 s | 3.27 s |

## Open points

- Stage P3.G (set 6): the run jump of the retail client is higher and longer (apex 1.04 m at 0.47 s, 0.8 s in the air) than the
  root track of `unarmed_jump_sprintleap` (0.71 m at 0.30 s); its rule is not known (a research stopped). The first strike of
  the rapier chain hits at 2.0 m in the client, not in retail (the shape of the strike or the hit volumes of the wolf). The
  retail wolf blocks the character 0.1 m farther than its capsule (1.77 m, no source in the data). The aim pose of the bow does
  not follow the camera pitch. The interaction prompt (a FLINT node) shows in the bow aim; retail shows none. The body of a
  sheathed character turns back to the camera at a stop; retail keeps the turn. The respawn shows a loading screen in the client
  until the cells of the new place load (about 24 s on the shared machine); retail shows none with this backend. The retail
  death video shows the fall 1 to 2 s later than the client (the clock of that video is not sure). A press and a release in one
  long frame of a replay lost the press (the drive keeps the second step of a button for the next frame).
- The weapon clips (AST-202): the bow and the flail play their weapon clips on skeletal weapons, on the clock of the body
  fragment ([GEAR.md](GEAR.md) "The weapon clips"). The sword and the greatsword have no weapon clip (particle effects, the damage
  clips, and weapon effects only); the musket has none either. Open: the flail strike shapes use the bind pose of its joints.
- The abilities (stage P3.5): the homing of Wrecking Ball and the pull strike of Reap that only the server makes (later); the
  perks of the items for
  the selections and the terms (the client has no perk rows: the artifact of Whirlwind); the cooldown factor of the character
  before the first state of the server (the own end comes from the data until then); the block press (bit 484) has no key.
- The block, the sprint and air attacks, the stamina payments of the attacks (0 for the reachable graphs), and
  the dodge of the shield (its off-hand fragments) are not played. The limits of the bag weight and of the statuses are in
  [MOVEMENT.md](MOVEMENT.md) "The limits" (stage P3.3).
- Remote players do not play actions (the Slayer map of other players is open).
- The inferences above (the additive order, the unit of `Decel`, the facing priority, the weapon in the hand at the
  start of an attack).
- The side-by-side check with the retail client waits for the approval of the user.
- The hit reaction of the own character (stage P3.G; research `~/Games/soulwarden-client/research/p3g-reaction/RESULT.md`,
  a static analysis of the retail client 1.400.6031.6004151). The decision is with the user; until then the client is as the
  retail client on this backend: no reaction of the own character.
  - The retail rule: the message 3601 (DamageTaken) does not start the reaction; its handler (0x686C6A0) drives only the HUD
    (the red mark, the red edge, the number). The trigger is the state ReactionTracking (slot 31, type 1927) that the server
    sends to the owner: a counter, the attacker, the damage table and row, the direction, the impact, the stun breakout, the
    flags, and the attacker level. Its apply function (0x683D910) writes the power (`PowerLevel`), the hit stun (`HitStun`,
    frames of 1/30 s), and the decay into the damage context. The reaction actions have `ServerStarted="true"`: a new counter
    lets the client start the reaction locally (0x2A66E50, 0x48731C0), with the first row of `PlayerReactionTableHarness`
    whose power and angle hold (front −45° to 45°, right 45° to 135°, left −135° to −45°, back the rest). A creature 10 or
    more levels above the character makes a power-0 hit a power-1 hit with a hit stun of 20 frames.
  - The Soulwarden backend sends only 3601 and the vitals state (no ReactionTracking, no action, no stun status). Thus the
    retail client on this backend shows no reaction of its own character either. A client run (a grey wolf of level 70,
    `tests/drive/p3g-rapier.txt`): 7 damage marks, no reaction.
  - The reactions of the data: a wolf bite (`Attack_01_Bite`, power 0, HitStun 20) plays `UpperBodyReaction` `r_R0_F`
    (`unarmed_r0_f`, an additive upper-body clip of 0.567 s, no input block, a push of 1.0 m from the wolf, no sprint for
    0.5 to 0.85 s by the load); a lunge (power 2) plays `Reaction` `r_R2_*` (0.667 s, no move until the end, a push of 2.5 m);
    a knockdown (power 4) plays `Reaction_Knockdown` `r_R4_*` (about 2.27 s, a push of 5 m). The pushes come from
    `ReactionMotion`, not from the root motion of the clips.
  - The options: (a) a backend change: the backend sends ReactionTracking to the hit character with the row of the attack;
    then the retail client and this client play the reaction by the same rule. (b) A client rule with no backend change: the
    client makes the damage context itself from 3601 and the attack record of the source creature (the damage row of the
    data: `PowerLevel`, `HitStun`, `ImpactDistance*`, `ImpactDecayRate`, `NoReaction`), then plays the table; the retail
    client on this backend does not do this, and a push of 4 to 5 m (power 4) is more than the position check of the backend
    allows in one tick (0.667 m). (c) No reaction, as now.
