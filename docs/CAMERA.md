# The camera of the player

Stage 1.8 (Linear AST-145), with the combat cameras of stage P2.6 (AST-161). The camera uses the camera data of the client: the camera states of `gamecamera.xml`, the
`ActivateCamera` entries of the camera action list, and the `CameraLockComponent` of the player slice. The source is the
research file `~/Games/soulwarden-client/research/feel-values.md`, part R1. Each value in the code has its source next
to it.

## The parts

| Part | Job |
| --- | --- |
| `converter/swc_convert/player.py camera` | Writes `SWC_CONTENT/player/camera.json`: the settings of `libs/camera/gamecamera.xml`, each of its 171 camera states with the include chain resolved (a child value replaces the value of its parent) and its own `FromTransition` times, and the 100 `ActivateCamera` entries of `playeractions_camera.actionlist`. |
| `Character/SwcCameraData` | Reads `camera.json`. With no file the camera has the values of MidCamera. |
| `Public/SwcCameraManager` | The player camera manager: the state, the blends, the pivot, the zoom, the collision, the FOV, the pitch limits, the target lock. It makes the view of the character (the character has no camera component). |
| `Public/SwcPlayerController` | The keys of the camera (mouse, wheel, middle mouse, Left Alt) and the drive commands of the camera. |
| `Character/SwcSmooth.h` | SmoothCD (C 0x104A460) and the curves of (x, y) pairs. |

```sh
tools/convert-player.sh camera      # camera.json from the game copy
```

## The camera state

Each frame the manager takes the entry of the camera action list (the first true condition wins):

| Condition | Entry | State | SmoothTime |
| --- | --- | --- | --- |
| the character is dead (the dead mark of the vitals state) | `DeathCam` | DeathCamera | 0.10 s |
| the target lock is on | `Locked_Cam+POSTURE` | LockedCamera | 0.20 s |
| the aim (`AimDownSights`: the right mouse button, a ranged weapon or a bow drawn; [MOVEMENT.md](MOVEMENT.md)) | `Aim+POSTURE` | AimCamera | 0 s |
| fragment Sprint or Sprint_Start | `Dynamic+Sprint+POSTURE` | DynamicCamera | 0.30 s |
| fragment Idle | `Dynamic+Idle+POSTURE` | MidCamera | 0.30 s |
| fragment Strafe (run, walk) | `Dynamic+Nav+POSTURE` | MidCamera | 0.30 s |
| each other fragment (jump, fall, land) | `Dynamic+POSTURE` | MidCamera | 0.30 s |

POSTURE is `Stand`, `Crouch`, or `Prone` (stage P3.3, [MOVEMENT.md](MOVEMENT.md) "The postures"): the entries of the data
give MidCameraCrouch, MidCameraProne, and the crouch states of the lock and of the aim. The shooter stance and the siege
weapons are not in the client. The drive command
`camera STATE` selects a state for the checks.

A change of state blends each value with SmoothCD: the distance, the offset, the FOV, the pitch limits, and the look
sensitivity. The time is the `FromTransition` of the new state for the old state when it has one, else the SmoothTime of
the entry (R1.4.2; the order is an inference). Examples: MidCamera to DynamicCamera 0.50 s (DynamicCamera has
`MidCamera 0.5`), DynamicCamera to MidCamera 0.30 s, MidCamera to LockedCamera 0.25 s, MidCamera to AimCamera and back
0.09 s.

| State | Distance | Offset (right, forward, up) | Pitch | Zoom | DistanceDampening |
| --- | --- | --- | --- | --- | --- |
| MidCamera | 2.0 m | 0.5, 0, 1.7 m | −70° to 60° | 0 to 6 m | 0 |
| DynamicCamera | 2.0 m | 0.5, 0, 1.7 m | −70° to 60° | 0.2 to 6 m | 0 |
| LockedCamera | 2.0 m | 0.4, 0, 1.8 m | −35° to 35° | 0.2 to 6 m | 0.1 s |
| AimCamera | 1.0 m | 0.5, 0, 1.7 m | −70° to 60° | none | 0.05 s |
| DeathCamera | 2.0 m | 0.5, 0, 1.7 m | −70° to 60° | 0 to 6 m | 0 |

## The view

1. The pivot: the feet of the character, the up and forward offset of the state, the Z smoothing (below), then the side
   offset in the yaw frame of the camera (R1.5).
2. The zoom: the zoom option (2.0 m at the start) clamped to the zoom range of the state, smoothed with `zoomLag` 0.1 s
   (or `ZoomLagOverride`). One wheel notch is `zoomAmount` 1.0 m (wheel up: nearer). The arm is the distance of the
   state and the zoom: 4.0 m at the start, 2.0 m to 8.0 m in MidCamera.
3. The collision: a sphere cast of 0.084 m (`collisionSphereCastRadius`) from the pivot less 0.049 m to the arm and
   0.04 m more (`focusPointOffsetCastExtension`), on the camera channel (the static world blocks it, the characters do
   not). A hit pulls the camera in with the DistanceDampening of the state (0: at once). After the hit the camera goes
   back out with `offCollisionSmoothTime` 0.35 s (SmoothCD).
4. The rotation: the control rotation of the player (the mouse, 0.025° for each count times the sensitivity of the
   state), or the rotation of the target lock. The camera manager clamps the pitch to the limits of the state.
5. The FOV: the FOV option of the player, 57° vertical (`UseUserDefinedFov` in each state of this stage). The view keeps
   the vertical FOV for each window shape (88.0° horizontal at 16:9).

The Z smoothing of the pivot (C 0x1049C20): the pivot height goes to its target with a rate (1/s) from a strength curve
of the height gap: `zSmoothingStrengthCurveDefault` (−0.6 m: 6, −0.3: 3, 0: 1, 0.75: 2.2, 1.0: 5), the airborne curve
in the air, the aiming curve in an aim state, with a blend of 0.3 s back to the default curve. The pivot is at most
0.2 × the arm × a FOV scalar (40°: 0.7, 70°: 1.2) above its target, and at most the pivot height less 1.1 × the near
plane below it. A move of more than 2 m in one frame sets the pivot at once. A state with `DisableZSmoothing` has no Z
smoothing.

## The target lock

The middle mouse turns the lock on and off (the slice default of `CameraLock_InputMode_PressAndHold` is false: toggle).
The values are the `CameraLockComponent` of the player slice:

| Value | Data |
| --- | --- |
| Range | `m_maxLockRange` 50 m from the character |
| First target | the smallest weighted cost of the scans: close (10 m, 95°, angle weight 3), mid (50 m, 90°, angle weight 10), precision (50 m long, 1.5 m wide); the combination of the scans is an inference |
| Camera pitch to the target | `m_minCamPitchToLockedTarget` −5°, `m_maxCamPitchToLockedTarget` 10° |
| Look-at point | the target point smoothed with `lookAtPositionSmoothingTime` 0.1 s |
| Blend after a lock or a switch | `m_targetSwitchInterpolationTime` 0.3 s |
| Line of sight lost | `m_targetLOSLossTime` 1 s, then the lock goes off |
| Next target | the mouse wheel; a mouse move of more than 700 counts on X in 0.25 s, then no switch for 0.2 s |

While the lock is on, the mouse does not turn the camera. The targets are the living creatures of the creature view at
their shown feet (`USwcCreatureSubsystem::GetTargets`, stage P2.6; a dead creature is not a target). With no creature
view the targets are the other entities of the backend with a position (`FSwcRepConnection::FSnapshot::Others`). The
point of a target is the lock point of its slice (stage P3.5: the CameraLockTargetComponent, `USwcCreatureSubsystem::GetLockPoint`;
the wolf: 1.0 m above the feet; [CREATURES.md](CREATURES.md)), and with no creature view 1.0 m above its feet (an inference).
When the component limits the pitch of its observer (`m_limitObserverPitch`), the camera pitch to the target stays in that
range and in the range of the player (an inference: both ranges apply). The client draws no sphere at the targets now: the
creatures have their models (`-SwcLockPoints` draws the lock points).

`DisableCameraLock` of the camera action list (stage P2.6):

- In the aim the lock key does nothing. The start of the aim ends a lock.
- In an attack before the mark `CanTransition` of its fragment the lock key does nothing. After the mark the lock key
  works again, also while the attack plays. A fragment with no mark `CanTransition` (22 of the 77 attack fragments of
  `actions.json`: the charge starts, `Attack_Sprint` of the axe) has no lock in the whole fragment.
- The log has "camera: target lock: not now (the aim, or an attack before its mark CanTransition)".

The line of sight: a trace on the visibility channel from the camera to the lock point. A hit within 1.5 m of the lock
point does not block the sight (`LockSightBody`, an inference): the creatures have collision from stage 1.9, and the
trace meets the body of the target. When the lock goes off for no line of sight, the log has the actor and the
component of the hit.

## Free look

Left Alt, when the state has `AllowFreeLook` and the lock is off: the press keeps the camera direction, the release sets
it again (C 0x1046420). In between the character does not turn to the camera.

## The test floor and the drive

The test floor ([MOVEMENT.md](MOVEMENT.md)) has a wall 10 m behind the spawn, 8 m wide and 5 m high, for the collision.
`tests/drive/camera.txt` checks the collision at the wall, the zoom (2 m to 8 m), the pitch limits, a sprint with a jump,
the aim state, free look, and the target lock with its two switches.

```sh
tools/dev-run.sh --window --enter --quit-when world --stay 53 -- -SwcDrive=$PWD/tests/drive/camera.txt
```

The drives of stage P2.6: `tests/drive/camera-armed.txt` (camera.txt with the sword drawn), `tests/drive/aim.txt` (a
character with a bow), and `tests/drive/lock.txt` with the creature routes `tests/drive/lock-routes.txt`:

```sh
tools/dev-run.sh --window --enter Mysticwarden --quit-when world --stay 18 -- -SwcDrive=$PWD/tests/drive/aim.txt
SWC_DEV_CHARACTER=Swordwarden SWC_STAGE=P2.6 tools/creature-check.sh lock 26 tests/drive/lock-routes.txt tests/drive/lock.txt
```

## Exit check (development run, 2026-10-18)

The video `~/Games/soulwarden-client/evidence/1.8/camera-world.mp4` and the log `camera-client.log`:

- the collision at the wall: the camera comes in at once, and is out again at 4.00 m 1.40 s after the last hit (the
  character walks away);
- the zoom: arm 3, 2, 3, 4, 5, 6, 7, 8, 7, 6, 5, 4 m;
- the pitch limits: 70° down, 60° up;
- MidCamera → DynamicCamera (0.50 s) in the sprint, back to MidCamera (0.30 s);
- MidCamera → AimCamera → MidCamera (0.09 s each way);
- the target lock on a target at 43.1 m, MidCamera → LockedCamera (0.25 s), a switch with the wheel, a switch with a
  mouse flick, off;
- the backend accepted each position report of the session (1,570, 0 refused).

## The combat check of stage P2.6 (development runs, 2026-10-18)

Evidence in `~/Games/soulwarden-client/evidence/P2.6/`:

- `armed-wall-camera.mp4` (`tests/drive/camera-armed.txt`, the sword and the shield drawn): the camera comes in at the
  wall at once, and is out again at 4.00 m 1.40 s after the last hit, as unarmed.
- `aim-camera.mp4` and `aim-client.log` (`tests/drive/aim.txt`, `Mysticwarden` with the bow): MidCamera → AimCamera
  (`Aim+Stand`, 0.09 s), the walk in the aim, the lock key refused in the aim, AimCamera → MidCamera (0.09 s).
- `lock.mp4`, `lock-client.log`, `lock-backend.log` (`tests/drive/lock.txt` with four `Wolf_Grey_7` at 6 m to the
  north, the east, the south, and the west, `tests/drive/lock-routes.txt`, `tools/creature-check.sh`, on the test
  floor): the lock on entity 2 at 6.0 m, MidCamera → LockedCamera (0.25 s), an attack with the lock, a switch with the
  wheel to entity 3 (to the right), off (LockedCamera → MidCamera 0.30 s), the lock key refused in an attack before
  `CanTransition`, the lock on at rest at 4.4 m, off. The backend accepted each position report of the session (712,
  0 refused). The routes go on the clock of the drive (the "tick" lines of `lock.txt`).
- A lock in the town of the world level went off after 1 s: the sight met a building (an instanced static mesh 4.4 m
  from the camera). That is the correct rule; thus the lock check used the open test floor.

## Differences and open points

- The side offset reduction with a second cast (`offsetReductionBySecondaryDistanceCastAllowed`) is not in the client.
- The option `m_percentCameraZSmoothing` (60 %) is not applied: how the client applies it is not known (R1.7).
- The inputs of the Z strength curves and the fields of the lower Z limit are inferences.
- `SuppressVerticalCameraMovement` of the fall actions, the tilt-based zoom (option off), the self-correcting follow
  camera (off for mouse and keyboard), and the gamepad are not in the client.
- The lock: the near zone rules (`lookAtNearZone...`) and the target position springs are not read; the vertical mouse
  switch is not in the client.
- RotationDampening is 0 in the states of this stage and is not in the client.
- The side-by-side check with the retail client (R1.7) is open, also for the combat cameras of stage P2.6.
