# The movement of the player

Stage 1.7 (Linear AST-144), with the armed rates and the aim of stage P2.6 (AST-161). The client moves the character of the user with the values of the game data, plays the
original clips of the player with the timings of the Mannequin data, and sends the position of the character in the
input report. The sources are the research file `~/Games/soulwarden-client/research/feel-values.md` (R2 the movement,
R3 the animation, R6 the keys) and the Soulwarden protocol notes (4.6, 4.7). Each value in the code has its source
next to it.

## The parts

| Part | Job |
| --- | --- |
| `Character/SwcCharacterMovement` | The movement states, the speeds and the rates of the data, the turn rule, the jumps with the root motion of their clips, the landings. A `UCharacterMovementComponent` with its own `CalcVelocity`. |
| `Character/SwcPlayerAnimInstance` | The animation with no graph asset: the fragment of the movement state, its blend-in time, the blend spaces with the weight grid of the data, the motion parameters with their smoothing. The proxy blends the clips on the worker thread. |
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
| Sprint | 5.5 m/s, accel 20 m/s², decel 20 m/s²; the default with forward or side input | R2, `playeractions_sprint` |
| Sprint_Start | accel 20 m/s² unarmed (10 with a weapon, below); to Sprint at 4 m/s or after 1.13 s | R2 |
| Run (Nav) | 3.25 m/s, accel 35 m/s² (Nav_Start 12), decel 35 m/s²; only with backward input or with no "can sprint" | R2, `playeractions_nav` |
| Walk | 1.65 m/s, 25 / 25 m/s²; Left Ctrl held, or no "can run" | R2, `Stand_Walk` |
| Stop | 40 m/s² unarmed (12 with a melee or magic weapon, below) | R2, `Nav_Stop` |
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

The velocity on the ground is the velocity of the data: each axis of the camera frame goes linearly to its target with
the rate of the state (no friction). A block (a wall, the face of a step) does not reset it: the next move of the
engine has its full length, which the step-up of the engine needs. The state rules use this speed.

## The armed rates and the aim (stage P2.6)

With a weapon in the hand (drawn), the conditional rates of the movement action lists replace the unarmed rates. The
tags of the weapon in `actions.json` select them (`USwcPlayerActions::TickComponent` gives the flags to
`USwcCharacterMovement::SetCombat`):

| Phase | Unarmed | Armed | Condition of the data |
| --- | --- | --- | --- |
| Sprint_Start | 20 m/s² | 10 m/s² | `Stand+Armed`: 1H or 2H melee, 1H or 2H ranged, bow |
| Nav_Start, Nav (accel / decel) | 12 / 35, 35 / 35 m/s² | 25 / 25 m/s² | `Combat_Nav_Stand`: also the magic weapons (the first conditional that holds, before NavStart) |
| Nav_Stop | 40 m/s² | 12 m/s² | `CombatNav+Melee`, `CombatNav+Magic` (no change for a ranged weapon) |

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

## The test floor

When the level has no surface below the spawn, the client puts a floor of 100 × 100 m with its top 0.5 m below the
spawn (the spawn lift of the backend, protocol notes 4.6: the character drops to it as on the real ground), and three
lanes to the right of the facing: lane A at 25 m (a ramp of 30° from 6 m, 10 m long and 5 m high, then a step of
0.5 m at 22 m), lane B at 31 m (a ramp of 75° at 34 m), lane C at 37 m (a step of 0.8 m at 38 m). The character waits
(no movement) until the floor is in the scene queries.

## The drive scripts

`-SwcDrive=FILE` plays a script through the same input actions (Enhanced Input injection; no input tool of the host).
`tests/drive/movement.txt` walks, sprints, turns, and stops, with a running, a standing, a backward, and a side jump.
`tests/drive/rules.txt` runs the three lanes of the test floor.

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
- The armed rates of the crouch, the prone, the swim, and the mounts are not in the client. The aim of a magic weapon and
  the shooter stance are not in the client: the aim needs a ranged weapon or a bow.
- A character that runs into a wall changes between Sprint and Sprint_Start each 1.13 s.
- In the first second after the world entry the engine prepares the clips (the bind pose).
- The side-by-side check with the retail client (research R2.9) is open, also for the armed rates of stage P2.6.
