# The movement of the player

Stage 1.7 (Linear AST-144), with the armed rates and the aim of stage P2.6 (AST-161), and the postures, autorun, and the
limits of the load and of the statuses of stage P3.3 (AST-176). The client moves the character of the user with the values of the game data, plays the
original clips of the player with the timings of the Mannequin data, and sends the position of the character in the
input report. The sources are the research file `~/Games/soulwarden-client/research/feel-values.md` (R2 the movement,
R3 the animation, R6 the keys) and the Soulwarden protocol notes (4.6, 4.7). Each value in the code has its source
next to it.

## The parts

| Part | Job |
| --- | --- |
| `Character/SwcCharacterMovement` | The movement states, the speeds and the rates of the data, the turn rule, the jumps with the root motion of their clips, the landings. A `UCharacterMovementComponent` with its own `CalcVelocity`. |
| `Character/SwcPlayerAnimInstance` | The animation with no graph asset: the fragment of the movement state, its blend-in time, the blend spaces with the weight grid of the data, the motion parameters with their smoothing. The proxy blends the clips on the worker thread. |
| `Character/SwcMoveRules` | Plain C++ (no engine; stage P3.3): the motion lists and the conditions of the movement actions, the postures and their transitions, autorun, and the limits of the load and of the statuses. `tools/test-movement.sh` tests it. |
| `Character/SwcLocomotion` | Reads `SWC_CONTENT/player/locomotion.json` (the fragments, their markers, the blend spaces, the clips, the root track of the jump clips) and the imported clips of the base of each gender. |
| `Public/SwcPlayerController` | The keys and the mouse look (Enhanced Input, made in code), the pitch limits, and the drive scripts (`-SwcDrive`). |
| `Character/SwcPlayerCharacter` | The capsule, the mesh, the animation. The camera is in [CAMERA.md](CAMERA.md). |
| `Character/SwcTestFloor` | A temporary floor with ramps and steps, until the world level gives the ground (stage 1.3). |
| `USwcBackendSubsystem::UpdatePlayer` | Each frame: the permissions of the server to the movement, the player to the input report. |

## The pipeline of the clips

```text
game copy --tools/convert-player.sh locomotion--> content/player/locomotion.json   (player.adb, .bspace files, clip table)
          --tools/convert-player.sh base --force--> content/player/base/<gender>.gltf (the idle clip and 49 clips)
          --tools/import-player.sh-----------------> /SwcGameContent/Player/Base/... (AnimSequence <gender><clip>)
```

- `locomotion.json` has the default unarmed fragments of `animations/mannequin/adb/player/player.adb`: Idle, Strafe,
  Sprint, Sprint_Start, Fall, Land (Soft, Normal, Hard), Jump (ForwardNav, BackNav, LeftNav, RightNav, no tag), and
  Jump_Standing. For each fragment: the clip or the blend space, the blend-in time, the blend-out time, the loop flag,
  the speed, and the named markers (procedural `CAGE-Named`: `Enable_Gravity`, `GotoFallFromJump`, `CanTransition`).
- The locomotion clips have no root bone track (root lock): the movement data moves the character, the clips play in
  place (research R2.8.5). The jump clips keep the keys of their root bone `Xform` in `locomotion.json`.

## The values

| Value | Data | Source |
| --- | --- | --- |
| Sprint | 5.5 m/s, accel 20 m/s² (100 after 0.275 s in the action), decel 20 m/s², for each weapon; the default with forward or side input (see "The native Sprint and Nav_Stop") | native Sprint 0x142401C40 |
| Sprint_Start | the same rates; to Sprint at the CAGE-TransitionInstant of its fragment (1.4 s; 1.3667 s with a 1H ranged weapon); the Sprint fragment at once at 4.02 m/s or more, in crouch or prone, and after a dodge | native Sprint, ADB |
| Run (Nav) | 4.0 m/s on each axis (backward too), accel 12 m/s² until 1.65 m/s, then 35, decel 35; only with backward input, with no "can sprint", or in a wade (see "The native Nav") | native Nav 0x1423FC970 |
| Walk | Left Ctrl held: max(0.5 S, min(S, 1.65)) of the speed S of the native Nav: 2.0 m/s dry, 1.65 m/s in a wade; no "can run": 3.25 m/s until 1.65 m/s, then 1.65 | native Nav 0x1423FC970 |
| Stop | the native Nav_Stop: 25 m/s² (18 from more than 5.0 m/s); its first frame (1/30 s) has the rate of the action before it | native Nav 0x1423FCC1F |
| Input angles | forward cone 110° (sprint), backward the rest (run); dead zone 0.2 | R2, `playerinput_navigation` |
| ClearMotion | velocity cleared when the input turns more than 80° (Sprint_Start) or 125° (Sprint) from it | R2 |
| Turn rule | strafe mode: alpha = 1 − (1 − r)^(30·dt) to the camera yaw while moving; r = 0.30 run, 1.0 sprint, 0.33 sprint start; no turn at idle or in the air | R2.8.3, C 0x2A919A0 |
| Jump | animation driven: the root bone of the clip until the marker `Enable_Gravity` (0.40 s running, 0.567 s backward, 0.867 s standing), then gravity with the velocity of the clip | R2.4.1, R2.8.4, ADB markers |
| Jump fragment | Jump_Standing with no input; else the tag of the input: ForwardNav within 30°, BackNav within 70° of back, LeftNav or RightNav | R2, ADB Jump |
| Jump clips | `unarmed_jump_sprintleap` (apex 0.714 m at 0.30 s, 5.49 m/s forward), `_left`, `_right`, `unarmed_jump_backward` (1.83 m back), `v2_unarmed_jump_inplace` (wind-up 0.267 s, apex 1.178 m at 0.567 s) | clip root tracks |
| Standing jump air control | 2.5 m/s with 30 m/s², until Fall_From_Jump | R2, `Standing_Jump` |
| Fall_From_Jump | 0.5 s after a running jump; `GotoFallFromJump` 0.833 s after a standing jump | R2.4.2, ADB marker |
| Fall | no air control; drag 0.5 m/s², 80 m/s² above 7 m/s | R2, `playeractions_fall` |
| Landing class | a fall: Soft below 0.25 s in the air, Normal to 0.80 s, then Hard; after a jump: Normal, Hard after 0.3 s of Fall_From_Jump | R2.4.2 |
| Landing hold | `CanTransition` of the Land fragment: 0 s Soft, 0.167 s Normal, 0.5 s Hard (a forced stop with no input) | ADB markers |
| Capsule | radius 0.41 m, half height 0.81 m | R2.6 |
| Ground rules | walkable slope 70°; step-up 0.65 m (the engine step-up in place of the step-up clip of the retail client) | R2.4.3 |
| Gravity | 9.8 m/s² (the engine default 980 cm/s²) | R2.4.2 |
| Permissions | dataBits 0x40 can sprint, 0x80 can run; moreDataBits 0x01 can navigate, 0x02 can dodge (a jump needs both of the last two) | protocol notes 4.6 |

The velocity on the ground is the velocity of the data, with the rules of CameraRelativeMotion of the client code
(0x2AA3830; research `~/Games/soulwarden-client/research/nav-speed`; `swc::move::camera_relative_target` and
`camera_relative_step`, tested by `tools/test-movement.sh`):

- The target is |dir × V| × dir: dir is the unit move input in the yaw frame of the camera, V the speed of the entry on
  each axis (side, forward). The target has the input direction, also when the two axes have different speeds.
- A backward target is multiplied as a whole by 1 − |dir.y| × (1 − BackwardsMultiplier): the sprint S gives 0.55 × the
  speed, the sprint S+D 0.68 × the speed, on both axes.
- One step each frame along the difference (target − current), with the length |rate × unit difference| × dt, to the
  target and not past it. The rate of an axis is the deceleration when |current| > |target| on that axis, else the
  acceleration (no friction).
- The multiplier of the speed table scales the speed, the acceleration, and the deceleration of the entry.

A block (a wall, the face of a step) does not reset the velocity: the next move of the engine has its full length,
which the step-up of the engine needs. The state rules use this speed.

### The native Nav (2026-10-19)

The retail client does not run the motion list of `playeractions_nav.actionlist` for Nav: it runs a compiled copy of the
action (the run function 0x1423FC970, build 1.400.6031.6004151; research `~/Games/soulwarden-client/research/nav-speed-2`).
The XML in the pak is older. The retail read of the requested velocity (`set3-swim/nav-velocity.json`) confirms each value:
the run S 4.0 m/s, the walk 2.0 m/s, the wade W, S, and Space 2.75 m/s, the wade walk 1.65 m/s (the label CamRelMotion in
layer state 26). `swc::move::native_nav` (`SwcMoveRules`, 19 checks of `tools/test-movement.sh`) has the rule for the
states Nav_Start, Nav, and Walk of the client; the first branch that holds gives the speed S and the rates:

| Branch | S (m/s) | Accel, decel (m/s²) |
| --- | --- | --- |
| The start value: the flag "can run" (the constant ctx+0x318 of the action context 0x141924130); no flag | 4.0; 3.25 | 35, 35 |
| The bag over 1.5 × its limit; over its limit | 0.625; 1.65 | 10, 10 |
| The wade (Shallow or deeper, the setting Swimming, not blocking) | 2.75 | 10, 10 |
| Dry: crouch; prone | 1.25; 0.625 | 10, 10; 1, 5 |
| Dry: the flame thrower; the aim or a shot | 2.75; 1.65 | 12, 35 |
| Dry: a combat stance (a weapon in the active weapon slot, drawn or sheathed) | S | 25, 25 |
| Dry: a charged aim, the hip fire walk; the block | 1.65; 2.0 | 25, 25 |
| Dry: not "running" (the context tag: set at the first Nav frame at 1.65 m/s or more, or after 0.5 s in the Nav) | S | 12, 35 |
| Dry: running with "can run"; with no "can run" | S; 1.65 | 35, 35; 25, 25 |
| Then the walk key | max(0.5 S, min(S, 1.65)) | no change |

The target goes to CameraRelativeMotion with the speed S on each axis and BackwardsMultiplier 1.0. A wade has no sprint:
each move in the wade is this Nav (the retail wade W is in layer state 26). The named conditions of the data select the
branches of the weapons and the block [inference of the research: the native checks are functions with no names]. The
labels of the two dry tests alternate around 4.0 and 2.0 by ±0.27 %: an error of the yaw quaternion of the retail client
(polynomial sin and cos), not copied.

Open points of the native Nav: the branch from a sprint (accel 20, the velocity kept up to 5.25 m/s) and a slow crouch
(0.625 m/s) are not in the client (their conditions are not known). The hip fire of the bow and the aim of the hatchet
(2.25 m/s) have no condition in the data. The native code names MoveSpeedMod (key 0x34) as the multiplier; the client
keeps key 0x35 (the live walks of the backend). Both were 1.0 in each retail read. The other native actions (Sprint,
Nav_Start, Nav_Stop, crouch, prone, swim) can also differ from the XML.

### The native Sprint and Nav_Stop (stage P3.G, 2026-10-19)

The retail Sprint_Start and Sprint are one compiled action (0x142401C40), and Nav_Stop runs in the native Nav while its
fragment plays (research `~/Games/soulwarden-client/research/p3g-move/report.md`, a static read of the build 1.400.6031.6004151).
The retail requested velocity of set 6 (`set6-combat/player-move-drawn.json`, `player-move-sheathed-draw-jump.json`) agrees:

| Rule | Value | Retail read (30 input steps each second) |
| --- | --- | --- |
| Sprint start, rapier drawn and sheathed | accel 20 m/s² (`swc::move::native_sprint`; the XML `Stand+Armed` accel 10 is not in the code) | +0.6665 m/s each step to 5.497 |
| Sprint_Start to Sprint | the CAGE-TransitionInstant of the fragment: 1.4 s (the converter writes it as the mark `TransitionInstant`) | action 2553 for 41 steps, then 1566 |
| Stop of a sprint | first frame 20 (the Sprint with no input), then 18 (the NavType tag Sprint: above 5.0 m/s at the request) | 5.497, 4.830, then −0.600 each step |
| Stop of a run back (4.0 m/s) | first frame 25 (the combat stance), then 25 | −0.833 each step |
| Run back, rapier sheathed | the combat stance 25 / 25: the weapon of the active slot, not the drawn state | +0.833 each step to 4.0 |

`USwcPlayerActions` gives the movement the tags of the weapon of the active slot (`SetStanceTags`) and the transition of the
Sprint_Start fragment of the weapon in the hand (`SetSprintStartTransition`). The checks: `native_sprint_stop_tests` of
`tools/test-movement.sh`. Not copied: the retail cap 5.4973 (an `rsqrtps` artefact) and the frame-time excess of 0.1 %.

### Open point: the long travel (2026-10-19)

The world track saw a travel of a long distance wait 24 s for its new place with a cold disk cache: about 1,100 packages of
the world entry were in the queue, and the cells of the new place loaded behind them. The idea: during the cell wait of a
travel of more than 200 m, the client stops the streaming source of the character (the old place asks for no more cells;
the source of `SwcWorldStreaming::IsReadyAt` at the new place has the highest priority) and starts it again at the
arrival. A first version was measured and then removed (not on main): six alternating runs of `tests/drive/travel-long.txt`
(a travel of 426.7 m to shrine_40292, 3 s after the world entry; warm disk cache, the shared machine): the wait from the
travel to the arrival was 6.4, 5.8, and 7.1 s with the source kept, and 5.8, 5.7, and 18.0 s with the source stopped. Two
earlier runs with the source stopped, under a load of 25 to 30, took 63.2 s and more than 44 s. There is no clear gain:
the spread of the shared machine is larger than the difference. A measurement with a cold disk cache (a cache drop needs
root) on a quiet machine decides it.

## The armed rates and the aim (stage P2.6)

With a weapon in the hand (drawn), the conditional rates of the movement action lists replace the unarmed rates. The
tags of the weapon in `actions.json` select them (`USwcPlayerActions::TickComponent` gives the flags to
`USwcCharacterMovement::SetCombat`):

| Phase | Unarmed | Armed | Condition of the data |
| --- | --- | --- | --- |
| Nav_Start, Nav (accel / decel) | 12 / 35, 35 / 35 m/s² | 25 / 25 m/s² | `Combat_Nav_Stand`: also the magic weapons; the weapon of the active slot, drawn or sheathed (stage P3.G) |

Stage P3.G: the XML rates of Sprint_Start (`Stand+Armed` 10 m/s²) and of Nav_Stop (`CombatNav+Melee`, `CombatNav+Magic`
12 m/s²) are not in the retail code: the sprint and the stop have no weapon rule (see "The native Sprint and Nav_Stop").

- The sprint speed (5.5 m/s), the run speed (3.25 m/s), and the walk do not change with a weapon.
- The aim (`AimDownSights`, the right mouse button held, a 1H or 2H ranged weapon or a bow in the hand, no attack):
  the character walks (`Stand_Walk`, 1.65 m/s). Sprint_Start needs "not ADS", thus the aim has no sprint. Each input
  step has the action bit 126 while the aim is on.
- The exit of a dodge sets `SkipSprintStartTimer` (0.85 s, `playeractions_dodge`): in this time a forward input goes
  to Sprint at once, with no Sprint_Start.

## The animation

- One layer for each fragment change. The new layer goes from 0 to 1 in the blend-in time of its fragment; the older
  layers share the rest. A layer below 0.001 goes away.
- A clip fragment plays from its start (loop or hold at its end). A blend space fragment has one phase for all its
  clips: the cycle is the weighted mean of the clip durations divided by their playback scale.
- The weights of a blend space come from its weight grid (VGrid of the `.bspace` file: 4 examples with 4 weights for
  each cell); between the cells the client uses the multilinear share of the corners (an inference).
- The motion parameters: MoveSpeed (m/s) and TravelAngle (rad, positive to the left) with a critically damped
  smoothing of 0.1 s; TurnSpeed (rad/s, positive to the left) with 0.75 s (research R2.3.3, R1 SmoothCD). TravelSlope is
  0 (no slope measure yet).
- The movement states and their fragments:

| State | Fragment |
| --- | --- |
| Idle | `Idle` (`unarmed_idle`; the female base has `female_unarmed_idle`) |
| SprintStart | `Sprint_Start` (blend space, TravelAngle and TravelSlope) |
| Sprint | `Sprint` (blend space, TurnSpeed, TravelAngle, TravelSlope) |
| NavStart, Nav, Walk, Stop | `Strafe` (blend space, MoveSpeed 0 to 4 m/s, TravelAngle, TravelSlope) |
| Jump | `Jump+<tag>` or `Jump_Standing` |
| Fall | `Fall` (`unarmed_fall_flail_loop`) |
| Land | `Land+Soft`, `Land+Normal`, `Land+Hard` |

## The keys and the camera

| Input | Key | Rule |
| --- | --- | --- |
| Move | W A S D | Axis2D (x right, y forward); the sprint is automatic |
| Jump | Space | one press |
| Walk | Left Ctrl | hold |
| Aim | right mouse button | hold, with a ranged weapon or a bow drawn (stage P2.6) |
| Crouch | C | a toggle: crouch, or stand from crouch (stage P3.3) |
| Prone | Z | a toggle: prone, or stand from prone (stage P3.3) |
| Autorun | `=` | a toggle (stage P3.3, "Autorun" below) |
| Look | mouse | 0.025° for each mouse count (0.5 × the option sensitivity 0.05 × the sensitivity of the camera state), no smoothing (`bEnableMouseSmoothing=False`) |

The camera, the wheel (zoom), the target lock (middle mouse), and free look (Left Alt) are in [CAMERA.md](CAMERA.md).

## The position in the input report

Each frame the subsystem gives the connection thread the player: the feet of the character (the bottom of the capsule
less the floor gap of the engine), the camera position, the look (yaw from north to the left, a positive pitch is
down; radians), the forward key, and in the air the distance to the ground. Each input entry then has the flag
`0x8000` and the position field (as the retail client with the game hook of Soulwarden), the camera, the look, and the
action bits 14, 43, 416, 542 while W is down ([NETWORK.md](NETWORK.md), "The input report").

| Run | Ground | Position field |
| --- | --- | --- |
| Development run | any | yes: the dev character moves in the dev data |
| Player run | the world level (stage 1.3) | yes |
| Player run | the test floor | no: the reports have the camera of a character at rest at the server position, thus the saved place of the character of the user does not change |

## The travel (stage P2.10)

A movement state of the server with a new teleport number (protocol notes 4.16: a respawn, a travel command) moves the
character: the client takes the place, the rotation (the identity: north), and the look of the state, with no step between
the two places. The character first waits at its old place with no movement while the cells of the new place load
(`SwcWorldStreaming::IsReadyAt`, as at the spawn; the input reports have no position field in that time), then it goes to
the place, and the camera looks north. A place with no ground (a region that is not in the level) gets the test floor. The
position field then goes to the new place at once (a distance of more than 20 m). The backend used no report from the old
place in the checks of stage P2.10.

## The test floor

When the level has no surface below the spawn, the client puts a floor of 100 × 100 m with its top 0.5 m below the
spawn (the spawn lift of the backend, protocol notes 4.6: the character drops to it as on the real ground), and three
lanes to the right of the facing: lane A at 25 m (a ramp of 30° from 6 m, 10 m long and 5 m high, then a step of
0.5 m at 22 m), lane B at 31 m (a ramp of 75° at 34 m), lane C at 37 m (a step of 0.8 m at 38 m). The character waits
(no movement) until the floor is in the scene queries.

## The drive scripts

`-SwcDrive=FILE` plays a script through the same input actions (Enhanced Input injection; no input tool of the host).
`tests/drive/movement.txt` walks, sprints, turns, and stops, with a running, a standing, a backward, and a side jump.
`tests/drive/rules.txt` runs the three lanes of the test floor. The line `SECONDS ability q|r|f [HOLD]` presses a key of the
bar for HOLD seconds (default 0.1 s; stage P3.5, [ACTIONS.md](ACTIONS.md)). The lines `SECONDS crouch`, `SECONDS prone`,
and `SECONDS autorun` press the keys C, Z, and `=` (stage P3.3).

The clock of a drive script is the game time of the world (`GetTimeSeconds`). Under load it runs slower than the wall
clock and the input steps of 1/30 s (the engine clamps a long frame): in the exit run of stage P3.5, 9 s of drive time
were 15.4 s of input steps. Thus:

- Put a step that must come in a time window of the server (a press in a cooldown, a press in a buffered window) a short
  time after its cause, not at a fixed late time.
- A `turn` with a rate for one second can turn more or less than its rate (9 degrees too far in that run). A test that
  needs a direction gives its target a margin, or it uses the target lock.
- The routes of `tools/creature-check.sh` with a drive script run on the drive clock: each route time needs a drive line
  at or before it (the `tick` lines), else the route waits for the next drive line.

```sh
tools/dev-run.sh --window --enter --quit-when world --stay 36 -- -SwcDrive=$PWD/tests/drive/movement.txt
tools/dev-run.sh --window --enter --quit-when world --stay 33 -- -SwcDrive=$PWD/tests/drive/rules.txt
```

The armed drives of stage P2.6: `tests/drive/combat-move.txt` (the start and the stop with the weapon sheathed, then
drawn, the armed run backward, two jumps with the weapon) and `tests/drive/rules-armed.txt` (rules.txt with the weapon
drawn). The character must have a melee weapon in slot 1:

```sh
tools/dev-run.sh --window --enter Swordwarden --quit-when world --stay 22 -- -SwcDrive=$PWD/tests/drive/combat-move.txt
tools/dev-run.sh --window --enter Swordwarden --quit-when world --stay 35 -- -SwcDrive=$PWD/tests/drive/rules-armed.txt
```

The videos of a drive: record the headless output of the own agent desktop (`wf-recorder`, as `tools/creature-check.sh`
does; `omarchy-agent-desktop screenshot DESKTOP FILE` for one picture). The recorder does not slow the client. Use
`-SwcFrames` (a picture of the view of each N frames, `World/SwcWorldSubsystem`) only for a check that needs each
frame: it slows the client to about 9 frames in a second, and then the cells of the place do not load in 30 s
([WORLD.md](WORLD.md)).

## Exit check (development runs, 2026-10-18)

- Videos in `~/Games/soulwarden-client/evidence/1.7/`: `movement-world.mp4` (walk 1.65 m/s, sprint 5.5 m/s, a turn of
  180°, a running jump, a stop, a standing jump, a run backward at 3.25 m/s with a backward jump, a side sprint with a
  side jump, a side walk) and `slopes-and-steps-world.mp4` (up the 30° ramp, a fall of 4.5 m from its top, up the
  0.5 m step; no walk up the 75° ramp; no step up the 0.8 m step). The full recordings have the login and the loading
  screen before them.
- The backend logs: `rep: position source changed: the position field of the input reports, the exact position (input
  25, flags 0x20084f0)`, then 1,057 position reports accepted and 0 refused in the movement run (967 and 0 in the
  slope run). The client logs have one line for each change of the movement state, with the speed and the feet.

## The world level check (2026-10-18)

The open world level (`/SwcGameContent/World/NewWorld`, stage 1.3): `Swordwarden` (its armor, the sword and the shield,
the greatsword) at 8907.75 4248.35 m in r_+04_+02, `tests/drive/world-walk.txt`, the pictures of the view of
`-SwcFrames` (the real frame rate: about 11 frames in a second with three runs and a region import on the computer).
Evidence in `~/Games/soulwarden-client/evidence/world-level/`: `character-on-ground.png`, `walk-world-level.mp4`.

- The character spawns on the ground of the level with no test floor, lands, walks, runs (the sprint at 5.5 m/s), and
  stops. The backend log: `position reports of the session: 928 accepted, 0 refused`, the largest step 0.334 m.
- Before the speed limit of the position field ([NETWORK.md](NETWORK.md), "The input report") the same walk had 46 of
  925 reports refused as too fast (19.92 m/s between two reports): the move of a long frame came in one step.
- The saved place of a development character moves in each development run, also on the test floor. A place in a
  building of the world level holds the character in the building: `place=X:Y:Z` of `tools/dev-run.sh --create` moves it.

## The armed check of stage P2.6 (development runs, 2026-10-18)

`Swordwarden` (the sword and the shield drawn) on the test floor, `tests/drive/combat-move.txt` and
`tests/drive/rules-armed.txt`; `Mysticwarden` (the bow) with `tests/drive/aim.txt`. About 36 frames in a second.
Evidence in `~/Games/soulwarden-client/evidence/P2.6/` (`armed-start-stop.mp4`, `armed-slope-step.mp4`,
`aim-camera.mp4`, and the client logs):

| Measure (the log of the movement state) | Unarmed | Armed |
| --- | --- | --- |
| Sprint_Start to 4 m/s | 0.22 s | 0.42 s |
| Stop from the sprint | 0.12 s | 0.46 s |
| Run backward to 3.25 m/s | | 0.13 s |

- The slope and step lanes armed: the same rules as unarmed (up the 30° ramp and the 0.5 m step, no walk up the 75°
  ramp, no step up the 0.8 m step).
- The aim with the bow: "actions: aim on (AimDownSights)", then a walk at 1.48 m/s to 1.65 m/s; after "aim off" the
  sprint starts again.

## The movement modes and the limits (stage P3.3)

The sources are the research file `~/Games/soulwarden-client/research/p33-movement.md` and the Soulwarden documents
`docs/systems/position.md`, `status-effects.md`, and `stamina-and-dodge.md` (read only).

### The rules of the data

`tools/convert-player.sh movement` writes `SWC_CONTENT/player/movement.json` (`converter/swc_convert/movement.py`):

- The motion lists of the actions Nav_Start, Nav, Nav_Stop, Sprint_Start, and Sprint (14, 17, 2, 8, and 9 conditionals).
  Each conditional has a condition, the speeds of the three axes, the accelerations, the decelerations, and the
  Backwards multiplier. The first conditional that holds gives the motion (the order of the data).
- The facing lists (the turn rate of each conditional) and the 186 named conditions of the movement actions.
- The movement columns of the 440 status rows: NoNav, NoRun, NoSprint, NoDodge, Stunned, Silenced, MoveSpeedMod,
  SprintSpeedMod, and the categories.

`swc::move::holds` (`SwcMoveRules`) evaluates the condition tree: And, Or, Not, True, False, IfMannequinTag, IfInput,
IfMannequinMarker, IfEncumbered, IfCanRun, IfCanSprint, IfCanNav, IfCanDodge, ActionConditionEquipLoadCategory,
IfPlayerSetting, IfCameraLockActive, IfExternalCondition (Exhausted), MoveSpeed, IfWaterDepth (no water: P4.4), and
IfNamedCondition. A conditional name with no named condition is a Mannequin tag state (`Crouch`, `Wade+Crouch`).

The speeds of the data (m/s; the tests compare each one with `movement.json`):

| Posture | Sprint | Run (Nav) | Walk |
| --- | --- | --- | --- |
| Stand | 5.5 (accel 20; armed start 10) | 3.25 (accel 35; armed 25); the client: the native Nav, 4.0 | 1.65 (Stand_Walk); the client: 2.0 |
| Crouch | 2.5 (start accel 10) | 1.25 (Crouch_Unarmed) | 0.825 (Crouch_Walk: only with a tool or a forced walk) |
| Prone | 1.25 | 0.375 (accel 1, decel 5) | 0.1875 |

- The order of the data decides two cases: an armed walk with Left Ctrl is `Combat_Nav_Stand_Fast` (3.25 m/s), and an
  unarmed crouch walk is `Crouch_Unarmed` (1.25 m/s).
- The turn rates: Nav 0.30, Aiming_Or_Shooting 0.35, Sprint 1.0, Sprint_Start 0.33; the slide 1.0, the dive 0.03.

### The postures

- C toggles `Crouch_Desired` and `Stand_Desired`; Z toggles `Prone_Desired` and `Stand_Desired` (the default key profile).
- At rest the change plays `Idle_PostureTransition+TO+FromFROM` and holds the movement until its `CanTransition` marker
  (0, 0, 0.4, 0.5, 0.8125, and 0.667 s; ClearMotion).
- In a sprint the change plays `Sprint_PostureTransition`: the slide to crouch (2.75 m/s forced, decel 10, then 20 after
  CanTransition 0.5 s), the dive to prone (0.1 m/s, decel 8, then 30, CanTransition 0.733 s), prone to stand (decel 10,
  0.833 s), crouch to prone (0.733 s), prone to crouch (0.8125 s). Crouch to stand in a sprint is at once.
- In a run the change is at once.
- The capsule half height: stand 81 cm, crouch 58 cm, prone 42 cm. The feet stay. A change to a higher posture needs
  room (a capsule overlap test).
- A jump, an attack, and a fall stand the character up first. A draw of the weapon from prone goes to crouch. Prone
  sheathes the weapon (not a rifle).
- The fragments and the camera entries have the posture tag (`Sprint+Crouch`, `Idle+Prone`, `MidCameraCrouch`,
  `MidCameraProne`; [CAMERA.md](CAMERA.md)).

### Autorun

`=` gives a virtual W. It ends at `=`, S, a new press of W, the aim, an attack, or a use of an object (the deactivation
of the action AutoRun). A, D, a dodge, a jump, and the camera do not end it. Thus autorun gives the normal automatic
sprint (5.5 m/s; crouch 2.5, prone 1.25).

### The limits

`swc::move::Limits` collects the limits each frame (`USwcBackendSubsystem::UpdateMovementLimits`):

| Limit | Source | Effect |
| --- | --- | --- |
| Encumbered | the bag weight (the items of the bag × the weight column of the item catalog) above EncumbranceBaseMax (2000 tenths: 200.0) | no dodge, no ability (the rule U3 of the backend) |
| The encumbered movement | the bag weight above 1.5 × the limit (300.0) | the condition IfEncumbered of the motion lists: the walk, no sprint, no dodge, no jump; no attack from rest |
| Overburdened | the equip load above 100 % of 50.0 | no sprint, no dodge, no jump; the run at 3.25 m/s |
| A status | the movement columns of each status of the status map (slot 44) | NoNav, NoRun, NoSprint, NoDodge (also no jump), Stunned (the fragments r_Stun, r_Stun_Loop, r_Stun_End; no movement, no ability, no attack) |
| The speed table | key 0x35 of the speed table (slot 43), clamped to 0 to 1.5 | × the speed, the acceleration, and the deceleration of Nav and Sprint (CameraRelativeMotion 0x2AA3830); the dodge × min(table, 1); a Slow category: the jump × 0.85 |
| The server bits | dataBits and moreDataBits of the movement state | can sprint, can run, can navigate, can dodge |

- The bag limit of 1.5 × is an inference from the live checks: the sprint at 200.2, the walk at 300.2.
- Overburdened is not possible with the items of the game: the heaviest armor set has an equip load of 31.6, and the
  shields have no equip load. The tests cover it.
- A rule of this project: the client reads the movement columns of the statuses itself. The reason: the backend never
  clears the movement permission bits (always 0xC0 0x03). The retail client gets them from the server. A root is
  NoSprint + NoRun + NoNav.
- The backend has no NoDodge check in its dodge (open point below).

### The speed table and the road boost

- The speed table (StatMultiplierTable, slot 43, type 1525; [NETWORK.md](NETWORK.md)): key 0x35 (SprintSpeedMod) scales the
  run and the sprint of the client; key 0x34 (MoveSpeedMod) has no effect (the live checks of the backend). A slow of
  0.5 gives a sprint of 2.75 m/s.
- The road boost of the data: the action Traversal_Road_Sprint_Speed_Boost (IfOnRoad >= 0.1, 3 s of standing sprint)
  applies the status Traversal_RoadSprintBoost_Haste (SprintSpeedMod +0.15, 3 s, DurationMax 3) again each 0.5 s. The
  status is not client predicted (IsClientPredicted 0): the retail client waits for the speed table of the server. Thus
  this client does not predict it: the sprint is 5.5 × 1.15 = 6.325 m/s only when the table of the server has 1.15.
  Until a backend applies the boost, the client has no road boost.

### The water

The depth classes, the wade (the Wade conditionals of the data), and the swim are in [WATER.md](WATER.md) (stage WF5).

### The road maps

`tools/convert-player.sh roads [NAME...]` (`converter/swc_convert/roads.py`; default: the 49 regions of the main world) writes
`SWC_CONTENT/roads` (never in git):

| File | Format |
| --- | --- |
| `NAME.r8` | 2048 × 2048 bytes, 1 m for each texel; row 0 = north (the smallest Unreal Y), column 0 = west. byte = round(255 × weight), weight = min(1, the sum of the road splat layers). IfOnRoad >= 0.1 is byte >= 26. |
| `roads.json` | the format, and for each region its file, its road layers (the splat, the priority, the texels), the texels with a road weight, and the texels on a road |

- The texel of the world point (x, y) m in the region r_+rx_+ry: column floor(x) − 2048 rx, row 2047 − (floor(y) − 2048 ry).
- The source: the region material `jav_newworld_vitaeeterna_X_Y.regionmat` of each region. A road layer has a material in
  `materials/terrain/roads/` (roads_splat, mountainroads_splat, coastroads_splat, damnedroads_splat). Its splat map is
  one channel (BC4), 2048 × 2048. The orientation is the orientation of the macro map: in region 4_2 the road splat
  agrees with the red-green value of the macro map (correlation +0.32; 0.04 or less for each flip or turn).
- The layer priorities and AffectedTiles (the render tiles of a layer) are not applied. A layer with AffectedTiles 0
  adds nothing.
- The result: 28 of the 49 regions have roads; 5.91 km² are on a road. In r_+04_+02: 412,633 texels on a road. (Before the
  fix of the BC4 decoder on 2026-10-18, 1eaed5a: 5.55 km² and 387,754 texels; the values between the two end values of a
  block were too near the second one.)
- An inference: the data does not say if IfOnRoad reads the road weight of the terrain or the cover of a road spline
  (the RoadsAndRivers module). A backend can use the same maps.

### The tests

`tools/test-movement.sh` builds and runs two programs:

- `tests/movement/movement_tests.cpp`: the speeds of each case against `movement.json` and the values of the research,
  the posture keys and transitions, autorun, the limits of the load and of the statuses, and the position check of the
  backend (`world/position_report.cpp`) with the reports of a simulated client: the sprint with the tables 1.0, 1.15, 1.4,
  1.5, and 0.5, the crouch, the prone, and the slide are accepted; a control at 11 m/s is refused (116 checks).
- `tests/net/own_state_tests.cpp`: the speed table and the status map of the encoders of the backend
  (`stat_multiplier_state.cpp`, `status_state.cpp`) into the read path of the client after the golden session of case T2:
  the slow, the haste (6.325 m/s), the key 0x0B, the neutral table; the root and the stun with their ends, stacks, and
  potencies, an older stamp, the empty map (27 checks).

### The exit check (development run, 2026-10-18)

`tests/drive/p33-movement.txt` with the routes `tests/drive/p33-movement-routes.txt`: `Swordwarden` at the road junction
9255 4735 m of r_+04_+02, the sprint along the road to the south-southwest, the turn back, then the other cases. Evidence in
`~/Games/soulwarden-client/evidence/P3.3/`: `p33-movement.mp4`, `p33-movement-sheet.png` (one picture of each case), the logs,
and `roads-r_+04_+02.png` (the road map of the region).

| Case | The limits of the client (`move: limits`) | The speed (`move: sample`) |
| --- | --- | --- |
| Road haste (the status route again each 2.5 s) | speed table 1.15 | 6.325 m/s (7 samples), on the road |
| Crouch sprint | `Sprint+Crouch`, `MidCameraCrouch` | 2.500 m/s |
| Prone sprint | `Sprint+Prone`, `MidCameraProne` | 1.250 m/s |
| Autorun | "autorun on", then "autorun off (the back key)" | 5.500 m/s |
| Slow AI_Snare_6sec | speed table 0.50 | 2.750 m/s |
| Root AI_Root_4sec | nav 0, run 0, sprint 0, jump 0 | W held and a jump press: no movement, no jump |
| Stun AI_Stun_4sec | nav 0, dodge 0, jump 0, abilities 0, attack 0 | `r_Stun`, no movement |
| Bag 263.9 of 200.0 | encumbered: dodge 0, abilities 0 | 5.500 m/s; the client refuses the dodge, the backend too ("the bag of the character is over its weight limit") |
| Bag 363.9 | the movement limit: sprint 0, dodge 0, jump 0, attack 0 | 1.650 m/s (`Stand_Walk`); no jump |
| Bag 58.9 again | each limit 1 | 5.500 m/s; the dodge plays (`Dodge_Roll_Slow`) |

- The backend: 3,298 position reports accepted, 0 refused; the largest distance in one window 7.798 m in 1 s.
- The line `move: sample` (each second while the character moves, only in a development run) gives the speeds.
- Near the end one frame took 5.5 s (`FlushAsyncLoading` at the spawn of a creature). The reports of that frame were
  accepted.

## Differences and open points

- Land_Sprint and Land_Roll (root motion after a landing with input) are not played: a landing with input goes on to
  Sprint_Start (or the run start for a backward input). Close_To_Ground_Sprint at 0.40 s is not used.
- The step-up of the retail client is a clip (0.15 to 0.65 m); the client uses the step-up of the engine.
- A jump from a walk or a run takes the forward speed of its clip (5.49 m/s), as the data drives the jump with the
  clip; check against the retail client.
- The input report has the player of the last game frame at each 1/30 s of the connection thread, not one input for
  each simulation step: a long frame gives a larger step between two reports (the backend accepted each one).
- The ground value of an entry in the air is the half float of the distance (an inference from "0 to 10; 10 in a
  fall" of protocol notes 4.7); on the ground the value of the captures (180).
- TravelSlope is 0.
- The mounts are not in the client. The block plays in the client (stage P3.G, docs/ACTIONS.md "The block"); the backend has
  no block rule. The shield dodge waits for its contract in the backend.
- The backend has no NoDodge check in its dodge, and probably no Silenced check: the client applies both itself.
- The road boost needs a backend that applies Traversal_RoadSprintBoost_Haste (the road maps above).
- Prone with a rifle: the client does not sheathe a rifle, but it has no prone aim. The aim of a magic weapon and
  the shooter stance are not in the client: the aim needs a ranged weapon or a bow.
- In the first second after the world entry the engine prepares the clips (the bind pose).
- The side-by-side check with the retail client (research R2.9) is open, also for the armed rates of stage P2.6.
