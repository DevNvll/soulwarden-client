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
  roll ends at the end of its animation, or at a move key after CanTransition: the movement starts from the velocity of the
  roll, at most `InheritVelocityMaxSpeed` of the action (6 m/s).
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
- the strikes: the damage key, the window, and the count of the shapes of each strike are the ones of the backend;
- the stamina and the dodge: the live checks LS5 and LS6 and the live proof P5 of the notes (the payments 45, 45, 10, the
  fourth press refused, full again 4.4 s after the third payment), the windows of a dodge that plays, the load classes, and
  the dodge bits of the driver;
- the life (stage P2.10, `Combat/SwcLife`): the death and its lock, the end of a timed use at the death, the hold of the
  death pose under the loading screen, the respawn and its mark CanTransition, a heal with no travel, the loading screen of a
  travel.

```sh
tools/test-actions.sh   # 5834 checks, 0 failed
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

## Open points

- The weapon clips: in the attachment databases (`playermaleanims_1h_meleeattach.adb`, `_2h_`) only the flail has
  clips (18, `rflail_*_prop`: its chain); the sword and the greatsword have none (particle effects, the damage clips,
  and weapon effects only). The clips of the flail, the bow string, and the firearms need the weapons as skeletal meshes.
- The melee contacts need the query of the creature track (stage P2.8); the abilities with strikes are not played.
- The abilities, the block, the sprint and air attacks, the stamina payments of the attacks (0 for the reachable graphs),
  the dodge of the shield (its off-hand fragments), and the overencumbered and encumbered rules (the bag weight: no
  inventory yet) are not played.
- Remote players do not play actions (the Slayer map of other players is open).
- The inferences above (the additive order, the unit of `Decel`, the facing priority, the weapon in the hand at the
  start of an attack).
- The side-by-side check with the retail client waits for the approval of the user.
