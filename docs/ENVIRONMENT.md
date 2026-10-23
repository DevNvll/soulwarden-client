# The environment of the world

The runtime part of the world look (the milestone "WF World fidelity" of the roadmap): the time of day of the backend clock, and
later the sun, the sky, the fog, the exposure, the local lights of the slices, and the water. The world track does the terrain and the
mesh materials. The stages of the environment come from the research plan (`~/Games/soulwarden-client/research/world-environment.md`)
and the orchestrator.

The sources: Soulwarden `docs/systems/world-time-and-weather.md` and the protocol notes 4.43 (read only), the research
`~/Games/soulwarden/work/research/world-time.md` (section 2: the client code of the time), and the backend encoders
(`apps/backend/src/server/rep/time_messages.cpp`, `world/world_time.cpp`).

## The decisions of the user

| Date | Decision | Rule for this document |
| --- | --- | --- |
| 2026-10-18 | "The lighting of the unreal game seems better. Let's keep what Unreal does better like Unreal." | The content follows the retail client (the hour, the sun and moon path, the light sources of the data); the technique is Unreal (Lumen, its shadows and reflections, the volumetric clouds, the physical sky as the base). |
| 2026-10-19 | "The original game colors should be copied to the client." (docs/ROADMAP.md) | The color result copies retail: the exposure level, the film response, the black level, the color grade and the source LUTs, the white balance, the saturation, and the sky, fog, sun, and moon colors of the time-of-day data of the place. Only the technique stays Unreal. Where the Unreal sky gives other colors than the data, the data tints or drives it. The check is the color report of the matched views ("The color report"): the numbers before and after in each report. This decision makes void the statements "not the EV bounds and the film curve of the source" (E1) and the look choices of the exposure of E1. |

## The world clock

The world has one day and night cycle. The backend owns it; the client calculates the time of day itself from one record and its
copy of the server clock, as the retail client does. The server sends no hour stream.

| Part | Rule (source) |
| --- | --- |
| The request | 4248 `TimeComponentServerFacet_SyncToClient`: target G + D41EB32C0F154004 (the Time facet of the own player), entity G; 16 bytes. The session sends it one time at the world entry (when the own entity is known). The retail client sends it after the mission load of the world level (live check L0 of the backend); a travel and a respawn keep the record (L2). |
| The record | 7036 `TimeComponentClientFacet_SyncTimeAndPhase`: the call, the record of 60 bytes, the phase (80 bytes). The backend sends it to the request, at each command for the world time, and one cycle after the last record. The record: the cycle (06:00 to 19:00 in 1,800 real s, 19:00 to 06:00 in 900 real s: 26 and 44 game seconds in one real second), the anchor on the server clock (one cycle back), the offset, and the four phase edges. |
| The phase | 4247 `TimeComponentClientFacet_SyncDayPhase`: the phase of the backend at each phase change (night 0, dawn 1, day 2, dusk 3). Dawn is 06:00 to 06:26 (60 real s), dusk 18:34 to 19:00. |
| The server clock | The registration reply and 335 each 3 s (the clock of `SwcRepClient`: a difference of more than 1 s moves it at once). |
| The time of day | `Net/Codec/SwcWorldTime.h`: `cycle_part` (client code 0x6A0ADC0: the float32 time from the anchor plus the float32 year times the offset, then the remainders), `game_second` (0x6A09E60: the night part before dawnStart, the day part to nightStart, the night part after it), `phase_at` (0x6A08270), `engine_hour` (0x6434700: steps of one game second). |
| The world subsystem | `Environment/SwcWorldTimeSubsystem`: `HasWorldTime`, `GetGameSecond`, `GetHour`, `GetPhase` (the edges of the record), `GetServerPhase` (the last 7036 or 4247). It reads the record from the session each 0.25 s and calculates the time in each frame. Before the first record there is no world time (the retail client keeps the hour of the mission). |

Nothing uses the hour yet: the sun, the sky, and the lights are the next stages.

The tests: `tools/test-environment.sh` builds `tests/environment/world_time_tests.cpp` with the encoders of the backend (7036, 4247,
335), the checks of its session for 4248, and its day cycle (`day_cycle::record`, `client_part`, `game_second`, `phase_of`): the
fields of the record, the part of the cycle equal to the client model of the backend, and the hour and the phase of the client against
the backend cycle over two cycles (seven starts: the edges, the midnight wrap, an early server clock with the anchor 1, three days of
uptime; the largest difference 0.034 game seconds, below 1 ms of real time), the rates (260 game seconds in 10 real seconds by day,
440 by night).

### Check (development run, 2026-10-18)

`tests/environment/world-time-routes.txt` (the routes: reads of the world time of the backend with the wall time of each reply, and
the commands 18:58:30, 05:59:00, and 12:00 for the world), with
`SWC_DEV_CHARACTER=Devwarden SWC_STAGE=WF-clock tools/creature-check.sh worldtime 115 tests/environment/world-time-routes.txt -- -SwcWorldTimeProbe`,
then `python3 -B tools/lib/world_time_compare.py ~/Games/soulwarden-client/evidence/WF-clock worldtime`; evidence
`~/Games/soulwarden-client/evidence/WF-clock/` (`worldtime.mp4`, `worldtime-sheet.png`, the logs, `worldtime-compare.txt`).

- The session: one request 4248 at the world entry, 0 refused; the backend sent 4 records 7036 (the request and the three commands)
  and 3 phases 4247 (night at 19:00, dawn at 06:00, day at 06:26).
- The compare with the hour of the server at 18 points (the 4 records, the 3 phase edges of the backend log, 8 reads and 3 commands
  of the routes): the largest difference 0.42 game seconds (about 16 ms of real time: one frame of the log line). The phase edges of
  the client and of the backend log are within 10 ms.
- The debug line shows the time and the phase of the server: 13:00 (day), 19:05 (night), 06:03 (dawn), 06:27 (day).
- Tests: `tools/test-environment.sh` 67 checks; `tools/test-net.sh` 179, `tools/test-items.sh` 30, `tools/test-movement.sh` 27 (the
  tools that build `SwcRepClient.cpp`): 0 failures.

## The environment data (stage E0)

`tools/convert-environment.sh write` (converter `swc_convert/environment.py`) reads the environment of the world from the game copy
into `SWC_CONTENT/environment` (outside the repository). It runs in about 3.5 minutes for the 49 regions.

| File | Content (source) |
| --- | --- |
| `mission.json` | The `Environment` element of `mission_mission0.xml` of `levels/newworld_vitaeeterna/level.pak` (Lighting, EnvState, Moon, SkyBox, CloudShadows, Ocean, OceanAnimation, Fog, and the others) and its base `TimeOfDay`. The reader opens this archive by name: the shared pak index resolves the name to the introduction level (other sun and moon values). |
| `presets.json` | The 160 time presets of `libs/timeofday/`: for each variable in the order of the file its name, its spline keys `[time, value or [r, g, b], flags]`, and its saved editor value (not a sample: each file has its own saved hour). A name that comes two times keeps both variables (for example `Particle Brightness Multiplier`). |
| `regions/REGION.json` | The environment entities that the region places: the chunk slices, the capitals, and the slices that a capital spawns at the start (depth 12; the rules of `region.py`). The kinds: `light`, `probe` (LightType 3), `timeOfDay`, `fog`, `rain`, `snow`, `weather`, `ocean`, `waterVolume`, `river`. Each record has the world matrix (4 x 4, rows, engine m), the place, the shape of the entity (cylinder, box, sphere, capsule, polygon prism, spline), the chain of the slices from the placement (an NPC or a camp owns the lights of its slice: stage E2 binds them), and the raw fields of the component (`BaseClass1` stays). |
| `sky.json` | The 11 sky slices of the presets: their dome meshes and materials, with the textures, the texture motion (`TexMod`), and the public parameters: the cloud layers. |
| `index.json` | The counts of each region, the number of slices read, the failures. |

The rules:

- The time of a curve is `float32(hour) x float32(1/24)` (the retail evaluator; research, normalization evidence): noon is 0.5,
  midnight 0. The evaluator (`environment.evaluate`) is the Bezier rule of the public Lumberyard splines with the tangent flags of
  each key (bits 0 to 2 incoming, 3 to 5 outgoing: 2 and 3 zero or step, 4 linear; the higher bits are editor flags). A key time
  gives the key value. Between keys it is a compatible evaluator, not a proof of the retail one (research, open point 4).
- A shape of a volume is in the frame of its entity (the inverse of the world matrix): a cylinder and a box are centered on the
  origin of the entity, a polygon prism goes up from it. A volume of a near region can hold a place: a placement is in the region
  of its chunk (the Oxboro cylinders at y 3,966 and 4,045 are in r_+04_+01).
- The value types and the field names of the components are tables of the module (engine type ids and field names; the object
  stream has the CRC of a name, with no case). An unknown value is `{"type", "hex"}`.
- Not read: the distribution rows of a region (no environment component; the research scope), the slices that a chunk spawns
  (the rule of `region.py`), the activation of an entity (`OnInitially` and `Visible` stay in the raw fields: the counts are
  authored candidates, not the lights of a retail frame).

The world (2026-10-18): 84,763 lights, 1,837 probes, 1,244 time-of-day volumes, 164 fog volumes, 643 water volumes, 277 rivers,
13 ocean overrides, 4 rain, 3 snow, and 13 weather volumes from 121,123 chunk slices and 85,430 capitals (149,530 slices read, 0
failures). The research census has the same numbers of lights, probes, fog volumes, rivers, rain, snow, and weather volumes, and
633 water volumes (the deduplication of a slice at the same place differs).

### Check of E0 (2026-10-18)

`tools/convert-environment.sh check` (`swc_convert/environment_check.py`); evidence `~/Games/soulwarden-client/evidence/E0/`
(`e0-check.json`, `oxboro-noon-data.png`, `oxboro-midnight-data.png`, `everfall-curves.png`).

- The Oxboro point of the research (8880, 4250, 140.5) is in four Everfall cylinders (radius 350 m, height 400 m; centers 9049
  3966 104, 8730 4045 104, 9108 4266 140, 8718 4437 140): `libs/timeofday/zones/everfall_a.xml`, priority 3, blend time 8 s, no hour
  override, the sky `slices/sky/newworld01_skybox_a.dynamicslice`, no color grading. The same as the research.
- `everfall_a.xml` at 12:00 and 00:00: 258 values of the 129 variables, and their keys, equal the values of the research
  (`everfall-noon-midnight.json`: the exact keys and its compatible spline evaluation).
- The mission: SunRotation 215, Longitude 130, SunLinkedToTOD 1, the moon at latitude 240, longitude 35, size 0.8.
- The two diagrams of the research, made from the output of the reader, beside `l1-noon.png` and `l1-midnight.png` of the Soulwarden
  evidence, and the curves of the sun, the fog, the probes, and the exposure. A data proof: the client does not draw these values
  yet.
- Tests: `tools/test-environment.sh` (the reader: the keys, the evaluation with the tangent flags, the time of a curve, a repeated
  name, the decoder, the shapes, the paths: 14 tests with no game data).

### Not done, open points (E0)

- The owner of a light (an NPC, a camp) is the chain of the slices; the binding to the actor is stage E2.
- The equal-priority ties and the blend order of the time volumes (research, open point 5) are stage E3.
- The water records (polygons, rivers, ocean overrides) are in the data for the water stage (E4, the player track).

## The sun, the moon, and the base sky (stage E1)

`Environment/SwcEnvironmentSubsystem` (the runtime) with the plain rules of `Environment/SwcEnvironmentRules` (tested by
`tools/test-environment.sh`). The decision of the user (2026-10-18, docs/ROADMAP.md): the content follows the retail client (the hour
of the backend clock, the sun and moon path, the colors of the data where they look good); the rendering keeps the Unreal way where
it looks better. The retail pictures are the reference for the places, the times, and the colors, not for the exact brightness.

| Part | Rule (source) |
| --- | --- |
| The data | `SWC_CONTENT/environment` (stage E0: `mission.json`, `presets.json`, `volumes.json`), read on a worker thread at the start (about 0.45 s). |
| The preset | The time-of-day volume at the camera with the highest priority (the first one of a tie), with its hour override; else the base TimeOfDay of the main-world mission. At the Oxboro feet: `everfall_a.xml`. The blend between volumes (priority, blend time, distance) is stage E3. |
| The hour | The world clock (`USwcWorldTimeSubsystem::GetHour`, steps of one game second); the time of a curve is `float32(hour) x float32(1/24)`. |
| The sun | A movable directional light, atmosphere light 0, always on, on the sun path of the public engine with the mission angles (SunRotation 215, Longitude 130): the angle (hour + 12) / 24 x 2 pi about Z, the longitude about X, the rotation about Y, then the axes (x, -z, y). The sun rises at 06:00 and sets at 18:00; at noon it is toward -0.369, 0.527, 0.766 (north-west, 50 degrees high). The retail noon pictures agree: in noon-road-low (camera to the west) the shadow of the character goes to the east-south; in noon-wall-grass the sun lights a wall face to the north-west. Its illuminance and color are the "Sun intensity" and "Sun color" of the hour (from 2026-10-20, `swc.SunSource 1`; stage E5): the light color over the ground transmittance of the atmosphere toward the sun, so that the ground gets the source sun (C / Y(C) x E, no transmittance); the sun fades out as the moon fades in. Before (`swc.SunSource 0`): the noon values (100,000 lux in Everfall), and the atmosphere made the sunrise and the sunset. |
| The moon | A movable directional light, atmosphere light 1, at the fixed direction of the Moon element (latitude 240, longitude 35: toward 0.410, 0.709, 0.574), with the "Sun intensity" and "Sun color" of the hour (the night values of the data: 3,000 lux at 18:30 to 5,000 lux at midnight, a blue color). Its weight is the larger of the fade of the public engine (CTimeOfDay::UpdateEnvLighting) with the visual edges of the record (by night 1; out in the first half of the visual dawn, 6.0 to 6.1833; in with the second half of the visual dusk, 19.1833 to 19.3667) and a fade with the sun below the horizon (0 at the sunset, 1 at 4 degrees below it): the moon comes when the physical sun goes (the night values of "Sun intensity" start at 18:05, just after the sunset of the path at 18:00). Its source angle is the Size of the element as degrees (0.8; an inference). |
| The sky | A SkyAtmosphere with the Unreal defaults: the sunrise, the sunset, and the twilight come from its transmittance and its sky. By night its sky luminance is scaled by 0.2, faded in with the moon (a look choice: the atmosphere lit by the moon is a day sky; `-SwcNightSky=FACTOR`). The stars, the moon texture, the clouds, and the fog are stage E3. |
| The ambient | A SkyLight with a real-time capture of the sky at intensity 1: it follows the sky (also the twilight after the sunset) with no capture event. |
| The exposure | An unbound post process over the fixed exposure of the level (the volume of the world track). Stage E5 replaced the E1 rule (the Unreal auto exposure with the bounds EV100 3 to 16 and a night compensation of -2.5): the meter and the EV rule of the source (see "The exposure and the color grade"). `-SwcNightExposure=EV` stays as a look offset by night (default 0). |
| The level | The sun, the sky atmosphere, and the sky light of the level of the world track are turned off while the runtime environment runs; its fog and its ocean stay (stages E3 and E4). |

The Unreal way kept on purpose (the decision of the user):

- The sun is not the dominant light of the source (one light that changes to the moon at night and lights from below the horizon until
  the visual dusk at 19:11): it is the physical sun of the SkyAtmosphere, always on; its transmittance makes the colors of the sunrise
  and the sunset in place of the dawn and dusk keys of "Sun color". The moon comes when this sun goes (from 18:00), not at 19:11:
  with the source rule the scene was black from 18:50 to 19:11 (the twilight of the physical sky is over).
- The sky is the SkyAtmosphere, not the sky shader and the night sky colors of the source; the ambient is the real-time sky light and
  Lumen, not the baked probes and their multipliers.
- The exposure is the Unreal auto exposure with wide bounds, not the EV bounds and the film curve of the source. (Void since the decision
  of 2026-10-19: stage E5 copies the exposure and the grade of the source.)

### The menu mode (2026-10-20, stage F4 of docs/FRONTEND.md)

The front end has no world session, so it has no record of the world clock. Before this mode, the menu had the environment of the
level (its engine sun and its fixed exposure), and the physical units of the menu lights and of the emittance burned the image white.

| Part | Rule |
| --- | --- |
| The calls | `USwcEnvironmentSubsystem::SetMenuScene` (the menu preview, when it reads the scene record) and `ClearMenuScene` (the end of the preview actor: the token of PLAY). While the mode is set, the runtime does not wait for the world clock. After it, the runtime keeps its last state until the first record of the world (the loading screen covers it). |
| The preset | The preset of the TOD entity of the menu scene (`libs/timeofday/frontend/nw_frontend_nighthaven.xml`, in `presets.json`) is the one layer. It has all 127 variables of the mission, so it gives the same values as the base of the menu mission with this preset at weight 1. No time-of-day volume of the world is used. |
| The hour | The hour override of the TOD entity (16.7), fixed. The edges of dawn and dusk are the defaults of the time record (06:00 and 19:00): at 16.7 both lights have the multipliers of the day. |
| The sun and the moon | The Lighting and Moon elements of the menu mission (`levels/nw_frontend_nighthaven/level.pak:mission_mission0.xml`): SunRotation 190, Longitude 110, the moon size 0.5, and the dawn and dusk fields of the night delay (DawnTime 355, DawnDuration 10, DuskTime 365, DuskDuration 10). The world mission has 215, 130, and 0.8, and the preset has no sun path, so the scene record carries them (`tools/convert-frontend.sh scene`: "environment"). |
| The sky | The cloud cover of the sky slice of the TOD entity (`slices/sky/skydome_tropicalstorm_post_a.dynamicslice`, in `sky/sky.json`). Each layer of the blend now has its sky slice (a volume layer: the slice of its volume; the base layer of the world: none, as before). |
| The probes | The environment probes of the menu level (two, with `frontendv2_cm.dds`; the scene record "probes" and "cubes" of `tools/convert-frontend.sh scene`, the cube imported by `tools/import-sky.sh`) replace the local probes of the world, always (in the world the local probes stay a test, `swc.LocalProbes`). Its scene probe has a box of 5 km: the world probe gets no weight at the scene. `USwcEnvironmentSubsystem::ReadLocalProbe` reads both record lists (2026-10-20, stage F4b). |
| The rules | All rules of E1 to E5 are the ones of the world (the sun, the sky, the source fog, the exposure, the grade). The world probe, the fog volumes, and the precipitation are the data of the world at the camera place. |

The check (2026-10-20): `tools/test-environment.sh` and `tools/test-ui.sh` pass. The world path: two world entries at the beach of
the first development character, with the hour 12:00 two seconds after the entry (`tests/environment/menu-mode-world-routes.txt`),
before and after the change: the same hour, presets, sun, exposure, and sky slice in the log, and the pictures differ only by
noise (a mean difference of 1.9 of 255, on the water and the clouds; `evidence/K4/world-before-after.png`). The menu: the run
`f4-menu-mode` of docs/FRONTEND.md.

### Check of E1

`tools/match-view.sh set1-oxboro noon-n night-n noon-road-low dusk-road-low night-road-low --hours
noon-road-low=06:00,06:22,19:00,19:22 --hours noon-n=06:00,06:22,19:00,19:22 --out ~/Games/soulwarden-client/evidence/E1/check
--label e1check`; evidence `~/Games/soulwarden-client/evidence/E1/check/` (`side/`: retail | client; `client/`; `contact-sheet.png`;
`report.json`; `clock-compare.txt`; the logs). The views of the extra hours have the retail picture of their view (its own hour) on
the left.

- Each picture is at the hour of its retail picture (within 1 game second); the client clock against the server: 19 points, the
  largest difference 0.67 game seconds.
- The sun: at noon the shadows go the same way as in the retail pictures (noon-road-low: to the east-south; the south facade of
  noon-n in the shade of a north-west sun, lit by the sky). 06:22: the sunrise with long shadows.
- The moon and the night: at 00:04 (night-n, night-road-low) the sky is dark navy and the light is blue and from the north-east, as
  in the retail pictures (no stars yet: stage E3); 19:00 and 19:22 have the moon light; 06:00 has the last moon light before the
  sunrise.
- The dusk at 18:32 (dusk-road-low): a moonlit dusk with a dark sky, near the retail picture (the retail sky has a violet glow at
  the horizon and stars).
- Kept the Unreal way on purpose: the physical sun and sky, the real-time sky light and Lumen, the auto exposure (see above). The
  brightness differs from the retail pictures (the decision of the user): the noon ground is near white (the flat beige ground of
  the level, no terrain texture yet) and the moonlit road is bright; stage E5 tunes the exposure.
- Tests: `tools/test-environment.sh`: the evaluator of the client equals the evaluator of the converter (12,513 values of the 129
  variables of everfall_a, the largest relative difference 4e-16), the sun path equals the 721 samples of the research, the edges,
  the multipliers, the light color, and the volumes (20 checks), the world clock (67 checks), the reader (14 tests): 0 failures.

### Not done, open points (E1)

- The stars, the moon texture, the clouds, the regional sky, the fog, and the blend of the time-of-day volumes: stage E3.
- The local lights and the probes of interiors: stage E2. The exposure: stage E5.
- The night delay of the sun path (the retail clamp of open point 2; from 2026-10-20, research e5-menu-probe.md):
  `e_TimeOfDay_NightDelay` 0.7 h (`swc.NightDelay`). Strictly inside the daylight interval (a = dawnStart, b = duskEnd), the sun
  path takes the hour a + d + q x (b - a - 2 d), q = (hour - a) / (b - a), d = min(delay, (b - a) / 4); the curves keep the
  hour (`env::sun_path_hour`, tested). At noon it changes little (with the default edges 6:00 and 19:22, 12:00 -> 12:04). The
  menu takes the dawn and dusk fields of its mission (DawnTime 355, DawnDuration 10, DuskTime 365, DuskDuration 10 minutes:
  5:50 to 18:10, `env::mission_edges`): at 16.7 h the sun is 25.72 degrees high (it was 18.28), toward -0.901, 0.002, 0.434.

### The sun step (AST-273 P1, 2026-10-23)

The light direction is in the cache key of the virtual shadow map clipmap (the engine: `FVirtualShadowMapPerLightCacheEntry::UpdateClipmap`).
By day the sun path moves 0.108 degrees in one real second, in steps of one game second (26 each real second). A turn of the light
in each frame thus rendered the sun shadow with almost no cache. Now the light turns only when the sun path is a step from it:

| Item | Value |
| --- | --- |
| The rule | `env::sun_step_degrees`: step = `swc.SunStepShift` x sin(e)^2 (radians to degrees), at most `swc.SunStep`; e is the sun elevation. A turn of dTheta moves the tip of the shadow of an object of height h by h x dTheta / sin(e)^2: the rule keeps that move at most 0.5 cm for each meter of height. 0 at or below the horizon (a black light does not render). A loading screen or a jump of the hour turns the light at once. |
| The defaults | `swc.SunStepShift 0.005` (meters for each meter of height), `swc.SunStep 0.1` (degrees). `swc.SunStep 0`: a turn in each frame (as before). At e = 15 degrees the step is 0.019 degrees; from e = 34 degrees it is 0.1 degrees (about one turn each second). |
| The log | `environment: the sun step S degrees, N turns of the sun light in T s` with the environment line (each 60 s). |

Check (development runs on a quiet computer, evidence `~/Games/soulwarden-client/evidence/ast273/`):

- Cost (run p1-cost, 3416 x 1380, TSR from 1937 x 783, the Oxboro spawn lane, noon, 60 s for each fixed step in one run): GPU p50 13.04 ms
  (step 0), 12.15 (0.02), 12.23 (0.05), 12.04 (0.1), 11.99 (0.25), 13.07 (0 again). The static cached pages of the sun in each frame
  (`r.Shadow.Virtual.AccumulateStats 600`): 103 of 453 (step 0), 392 (0.02), 420 (0.05), 432 (0.1), 440 (0.25); new pages 350, 62, 29, 19,
  11. A step above 0.1 degrees gives no more gain.
- Look (run p1-look4, 1256 x 660, the west road of Oxboro at 06:40, a sun about 15 degrees high, heading 135, pitch -20): the 99th
  percentile of the frame-to-frame change of the road and the shadows (`work/ast273/jumps.py`): 0.264 (step 0), 0.278 (0.02), 0.493 (0.05),
  0.715 (0.1). 0.02 degrees (0.52 cm for each meter of height) is not measurable; 0.05 degrees (1.3 cm) is.
- The default (runs p1-check-cost and p1-check-look): at noon GPU p50 12.25 and 12.41 ms against 13.36 and 13.33 ms for step 0 (pairs in one
  run; -0.9 to -1.1 ms), 435 of 454 pages cached against 112; at 06:40 the frame change p99 0.278 and 0.356 against 0.257 and 0.352 for
  step 0. The video `p1-sun-step-side.mp4`: step 0, the default, and a fixed 0.1 degrees.
- Open (P1b): with a still sun, about 430 dynamic pages of the sun are invalidated in each frame; with
  `r.Shadow.Virtual.Cache.DebugSkipDynamicPageInvalidation 1` the shadow depths fall from 4.3 ms to 1.7 ms. The source is not known yet.

## The local lights (stage E2)

`Environment/SwcLocalLights` (`USwcLocalLightSubsystem`, the runtime) with the plain rules of `Environment/SwcLocalLightRules`
(tested by `tools/test-environment.sh`). The data: `tools/convert-environment.sh write` writes `lights/REGION.json` (the compact
lights of each region), and `tools/convert-environment.sh cookies` writes the projector textures; `tools/import-cookies.sh` imports
them. The decision of the user (2026-10-18): the places, the colors, and the ranges come from the data; shadows may be on where
they look better and cost little.

| Part | Rule (source) |
| --- | --- |
| The data | The light components of the placement walk of E0 (the chunks, the capitals, the slices that they spawn at the start): the type (`LightType` 0 point, 1 area, 2 projector; 3, the probes, are not lights), the place, the +X axis (the direction of a projector and of an area light: 988 of the 1,335 projectors of r_+04_+02 have +X down) and the +Z axis, `COLOR`, `DiffuseMultiplier`, `SpecMultiplier`, the range (`PointMaxDistance`, `ProjectorDistance`, `AreaMaxDistance`), the bulb (`PointAttenuationBulbSize`, `ProjectorAttenuationBulbSize`), `ProjectorFOV`, `AreaWidth` and `AreaHeight`, the projector texture, `OnInitially`, the shadow flag (`CastShadowsSpec` not `0xFFFFFFFF`, EngineSpec Never), the animation, and the view distance cap. A light with `Visible` false is not in the file (the light rigs of the mission NPC masters and the helpers of the level design). The world: 76,723 visible lights (57,323 point, 16,394 projector, 3,006 area), 1,383 with the shadow flag, 149 in NPC slices; 863 in 300 m about the Oxboro point. The research counts 76,717 visible lights that are on at the start; the client keeps a light that is off at the start (`OnInitially` false) off. |
| The intensity | The source attenuation of the bulb size r is 1 / (1 + d / r)^2 at the distance d from the bulb surface, that is r^2 / D^2 at the distance D from the center: the falloff of an Unreal sphere light with the source radius r. The light unit of the public engine is 10,000 cd/m2 (`RENDERER_LIGHT_UNIT_SCALE`; `TimeOfDay.cpp`: the illuminance of the sun / 10,000 / pi is its render color, and the diffuse shading of the source has no 1 / pi). So a point light and a spot light have pi x 10,000 x `DiffuseMultiplier` x r^2 candela (r at least 0.01 m), with `COLOR` (linear) as the light color; an area light has the same surface luminance (10,000 x `DiffuseMultiplier` cd/m2) over its area. The attenuation radius is the range. `SpecMultiplier` is the specular scale (an inference: the multiplier of the specular of the light). A look scale multiplies all intensities: 3 by default (the Unreal way, a look choice): with the moon of the data (3,000 to 5,000 lux by night, E1) and the auto exposure, the scale 1 gave lamp pools that were weaker than in the retail night pictures (night-n: the gate lamps, the pillar faces, the ground), 3 gave about the balance of the retail pictures, and 10 made the windows glare (`-SwcLightScale=`, console variable `swc.LightScale`). From 2026-10-20 (AST-210, `research/lights-emission.md`, docs/PARTICLES.md "The point lights"): a point light has no source radius and pi x 10,000 x `DiffuseMultiplier` x (1 + r)^2 cd (r = max(bulb, 0.001)); the light function `M_SwcPointFalloff` gives the near clamp max(d, r) and the source range fade within 40 m of the camera (`swc.PointFalloffDistance`); `swc.PointLightRule 0` gives the old rule. The spots keep the rule of this row. |
| The types | A point light is an Unreal point light. A projector is a spot light along +X with the outer cone `ProjectorFOV` / 2 (the full angle of the source) and no inner cone. An area light is a rect light along +X (`AreaWidth` x `AreaHeight`) with no barn doors (it lights its whole front half space). |
| The cookies | A projector with a texture (not the default spot texture) gets a light function: the material instance of its texture (`/SwcGameContent/Environment/Cookies`, parent `M_SwcCookie`: the texture on the emissive color). Unreal maps the UV of the light function of a spot light from one side of the outer cone to the other, that is over the frustum of `ProjectorFOV`. 44 textures (2 textures of the data are not in the game copy: 13 lights have no cookie). The textures are linear (the source formats are BC4 and BC2 UNORM). |
| The streaming | All lights are read on a worker thread at the start. The static lights are in a grid of 64 m cells. Each 0.25 s, a light comes in its draw distance plus 10 m and goes at its draw distance plus 30 m (at most 128 new lights in one update, the nearest first). The draw distance is 25 times the range, at least 50 m and at most 250 m, and not more than the view distance cap of the source; Unreal fades the light in the last 20 % of it (a choice of this client for the cost: the source rule of the view distance is in the client code). |
| The shadows | The source flags (`-SwcLightShadows=none|source|all`). |
| The owners | A light in an NPC slice follows its creature: a creature of the same slice path near the static place of the slice root (30 m) gets the light, attached with its matrix in the frame of the slice. A light with no such creature stays off. The camp lights are static props of the level data (the fires, the lanterns of the camps): always on. |
| The hour | No switch by the hour: the data has none (`TODInfluence` alone is not a switch; research), and the retail pictures show the lamps on at noon and at night. `USwcEnvironmentSubsystem::GetNightFactor()` (0 by day, 1 by night: the weight of the moon) is a read value for the emissive of the fixtures (the materials track, stage M4). |
| The light styles | `AnimIndex` and `AnimSpeed` select a light style of the client code (no table in the data): the lights are steady. |
| The emissive of the fixtures | The materials track (stage M4) owns the emissive materials. The agreement (2026-10-18, through the orchestrator): the light unit of the lights is 10,000 cd/m2 for one source unit, and the emissive of M4 uses the same look factor: it reads the console variable `swc.LightScale` (3), so that a fixture and its light stay in balance. |

The Unreal way kept on purpose (the decision of the user):

- The intensity had a look scale of 3 over the light unit of the source (see above). From 2026-10-20 `swc.LightScale` is 1 (stage E5,
  research/color-pipeline.md rule 6: one conversion for the whole scene; it also scales the Illum emission). The places, the colors,
  and the ranges are the data.
- The shadows are the source flags (13 lights in 300 m about Oxboro). All shadows on (`swc.LightShadows 2`) changed little in the
  check views (the inn is about the same; in the lane the windows of the houses glow less) for about 0.4 ms in the lane and 2.6 ms
  in the inn against no local lights: an option for the user, not the default.
- The probes (`LightType` 3: the authored fill and reflection of the interiors, for example the blue-white probe of the tavern) are
  not lights of the client: the ambient is the real-time sky light and Lumen (E1). So the inn walls are more orange than in the
  retail pictures (the source adds a cool fill to the warm lights).
- The lights are steady (no light styles), and they do not switch by the hour.

### Check of E2

`tools/match-view.sh set1-oxboro inn-noon inn-night noon-n night-n night-e night-s night-w night-road-low night-wall-grass` with
the variants (`--variant`) of the Lumen test, of the lights hidden, and of all shadows, and `-SwcFrameStats=2`; evidence
`~/Games/soulwarden-client/evidence/E2/` (`check/`: the side-by-side pictures with the light counts and the GPU time in their
caption, the contact sheet, `report.json`, the logs; `tries/`: the first tries and the look scales 1, 3, and 10).

- The data: 76,717 lights of 49 regions (76,572 static, 145 of NPC slices, 6 off at the start) and 44 cookies, read in 1.9 to 2.6 s
  on a worker thread.
- The inn at night (inn-night): the room is lit warm by its candles, its fireplace, and its accent lights, near the retail picture;
  the walls are more orange (no probe fill; the exposure is stage E5). At noon (inn-noon) the lights add their warm accents to the
  daylight, as in the retail picture.
- The lane at night (night-n, night-e, night-s, night-w): the lamps of the gate pillars and the lane light the pillar faces and the
  ground at the places of the retail pictures (night-w: the pools of the gate lamps; night-e: the light at the door of the house).
  The moonlit walls are much brighter than in the retail pictures: the moon of the data and the exposure (stages E1 and E5).
- The outdoor views (night-road-low, night-wall-grass) have no local light near the camera: they do not change.
- The counts at the shot (the active lights near the camera; "in the view": the active lights whose range is in the view cone and
  the draw distance, also behind walls): inn-night 605 active (312 point, 221 spot, 72 rect; 16 with a cookie), 278 in the view,
  8 with shadows (4 in the view); night-n 565 active, 52 in the view, 8 shadows (1 in the view); night-e 568, 272 in the view, 8 (5);
  night-s 571, 204, 8 (4); night-w 575, 76, 8 (0); night-road-low 370, 100, 7 (0); night-wall-grass 403, 160, 7 (5). No NPC light
  was bound: the backend sends no town NPC now (only the creatures).
- The cost (the GPU time of the frame statistics, the median while the view is in place; the first minutes of a run have about
  40 ms while the world streams, so only pairs near in time compare): inn-night 7.0 ms with the lights hidden, 8.0 ms with the
  lights (the second run), 10.0 ms with all shadows (the first run, against 7.4 ms hidden); night-n 7.6 ms hidden, 8.1 ms with the
  lights, 8.5 ms with all shadows.
- Tests: `tools/test-environment.sh`: the light rules (66 checks: the Unreal sphere light equals the source attenuation at 36
  points, the rect light, the draw distance, the grid, the owner rule, the matrix of a bound light), the reader of the lights (4 more
  tests), and the tests of E0 and E1: 0 failures.

### The Lumen mesh data test

The world meshes have no mesh distance field and no Lumen cards (`import_content.py`: the build was 6 times slower with them). The
test (`tools/lumen-test.sh`, `tools/lib/lumen_test.py`, `Scripts/lumen_test_copies.py`; the import settings of the world do not
change): the meshes of the inn of Oxboro (14 m about 8908, 4210) and of the two houses at the north side of the spawn lane (8877,
4267 and 8891, 4273), 1 m or larger, imported again from their glTF files into `/SwcGameContent/Test/E2` with the world pipeline
(PIPELINE_VERSION 5) and a distance field (resolution scale 1) and Lumen cards (12). `swc.LumenTest 1` puts the copies on all the
components that use these meshes (the house kit is shared: 891 to 909 components in the town), `0` gives the world meshes back; the
client waits for the distance fields and the cards after a swap. Evidence `~/Games/soulwarden-client/evidence/E2/lumen/`
(`pairs-without-with.png`: each view without and with, taken one after the other; `copies-report.json`).

| Measure | Result |
| --- | --- |
| The set | 124 meshes (105 of the inn, 12 of each house; 117 Nanite); 58 meshes under 1 m are not copied (below the detail of the distance fields). |
| The build cost | The import and the save of the 124 meshes with the distance fields and the cards: 99 s (batches of 16; 0.8 s for each mesh, 44.5 s for the last batch of 12 large merged meshes of the inn), 108 s for the commandlet. The client built 2 large distance fields again at the first swap (2.7 s each; 1.4 and 1.6 MB, 126 cubed sparse fields). The world track measured 80 s against 14 s for 20 meshes with and without. |
| The inn at night (inn-night) | No visible change (the mean brightness of the picture 35.1 and 35.2 of 255): the local lights light the room; the bounce of Lumen is small. |
| The inn at noon (inn-noon) | The walls are warmer and the picture is 4 % darker (110 to 105): with the distance fields the walls stop the sky light that came through them, so the warm local lights are the main light; the window glass reflects the room. The retail picture has whiter walls with warm accents (its probe fill). |
| The lane (noon-n, night-n) | Small local changes only (the doors and the windows of the houses). |
| The GPU time | No measurable change: inn-noon 7.43 ms without, 7.65 ms with; noon-n 5.89 and 5.85 ms; night-n 7.36 and 7.30 ms (pairs one after the other, the second run). |

`--views VIEW.json... [--radius M]` (stage E5, the matrix of the black of the shadows) also copies the meshes near the camera of
matched views (in front of the camera or within 15 m, up to the radius, default 80 m; not objects/volumetrics/). The five matrix
views gave 787 meshes, 626 copies (161 under 1 m), built in 740 s. With the copies, the walls of the lane noon-n match retail (the
middle band dE 11.9 to 0.9); the courtyard of keep-outside-noon gets black (a floor slab 40 m above closes the sky for Lumen; the
source lights it with the world probe and a local probe). Evidence: `~/Games/soulwarden-client/evidence/E5/matrix/`.

The result is for the user: the mesh data corrects the light leaks of the interiors at a small cost on the GPU and a build of
about 1 s for each mesh; the look at night does not depend on it. A world rebuild with it is the decision of the user (the world
track owns the import settings).

### Not done, open points (E2)

- No NPC light was bound in the check: the backend sends no town NPC now (only the creatures), so the 145 lights of NPC slices stay
  off. The binding rule is tested with the rules (`owns_lights`).
- The light styles (`AnimIndex`: the flicker of the fires) are in the client code, not in the data: the lights are steady.
- The orientation of an asymmetric cookie (a window, a chandelier) is not checked against a retail picture (the map of the space
  mirrors Y).
- The draw distance is a rule of this client (the source rule is in the client code).
- The moonlit walls are brighter than in the retail pictures (the balance of the moon and the exposure: stages E1 and E5); the
  probes stay replaced by the sky light and Lumen (the decision of the user).

## The sky, the clouds, the fog, and the weather (stage E3)

The client makes these parts in `Environment/SwcEnvironmentSky.cpp` from the blended time-of-day values of the place (the region
preset and the local time-of-day volumes, each with its weight from the blend distance and the blend time; a volume with an hour
override moves the hour of its part toward that hour by its weight).

| Part | Rule |
| --- | --- |
| The sky | The SkyAtmosphere of Unreal (the technique), with the Rayleigh color from "Sky light: Wavelength (R, G, B)" (the ratio (B / lambda)^4 of each channel; the Unreal default 0.175, 0.41, 1 is the ratio of 680, 550, and 440 nm). A sky sphere with `M_SwcSky` adds the stars of the engine catalog (`engineassets/sky/stars.dat`), the moon texture of the mission, and its two coronas, turned with the sun path (an inference: the stars turn with the sun). |
| The clouds | The volumetric clouds of the engine (`m_SimpleVolumetricCloud_Inst`). The cover of the sky slice of the place (`sky/sky.json`: 1 - the product of (1 - the mean of each cloud mask of its dome)) changes the coverage of the cloud material from the Everfall sky (0.6663) up and down (`swc.CloudCover`, `swc.CloudBias`: look values). The composite of the cloud target is nearest with a depth test (`r.VolumetricRenderTarget.UpsamplingMode 2`, set by the code in `USwcEnvironmentSubsystem::Initialize`, not in an ini file: docs/WORLD.md "The cooked build"; AST-261, 2026-10-22): with the default bilateral upsampling (4) of the quarter resolution target, each far silhouette against the sky (the far mountains) had a blocky light outline of the target cells (`swc.Clouds 0` and `ShowFlag.Cloud 0` removed it; `MinimumDistanceKmToDisableDisoclusion 0` did not; Mode 1 and Mode 3 removed it at a higher trace cost). |
| The fog | The "Volumetric fog 2" set of the data by the rule of the source (stage E5 step 3, below; `swc.SourceFog 0`: the fog of E3, the height fog of Unreal with the legacy fog layers and the "Volumetric fog 2" layers as its two layers and the color of the SkyAtmosphere). The fog volumes of the data are Unreal local fog volumes. |
| The rain | A post-process material before the tone mapping (`M_SwcPrecipitation`): the scene rain of the source (stage E5 step 3, below). The amount comes from the rain volumes of the data at the camera (the full amount in the inner radius, 0 at the outer radius); the drops have a part of the light of a white surface (`swc.DropLight`). |
| The snow | In the world, not on the screen (AST-257, below "The snow"). |

## The snow (AST-257, 2026-10-22)

The user asked for snow that is not a screen layer. The static study is `research/snow.md`. The retail takes are in
`~/Games/soulwarden/work/evidence/unreal-references/set-ast257-snow`: the Carious Pich shrine (in game "Sunderfort Shrine"), 82 m from
the centre of the Great Cleave Snow volume `4_3_Snow`, at noon and at 00:30, and the Inn of Warden's Rise. Two results decide the plan:

- Retail draws NO falling flakes of the Snow volume. The legacy flakes of the source would give thousands of points of 2 to 9 px at the
  shrine. The stills and the videos show none, so `r_SnowFlakeLegacy` is 0 in retail. Falling snow in retail comes only from placed
  particle effects (`env_Winter`, `ENV_DamnedPass.SnowBlown`, `Env_Snow`).
- Retail shows thick white snow on each upward face near the centre: the rock tops, the pots, the wooden structures, and the ground.
  This is the surface snow pass of the source (`DeferredSnow`; SnowAmount 8, FrostAmount 0.6, SurfaceFreezing 1).

| Part | The client | Source |
| --- | --- | --- |
| The Snow volumes | `regions/*.json` kind `snow` (3 volumes) into `FSwcSnowVolume`: the centre, the radius, and all the fields of "Snow Options" | [D] research/snow.md section 1 |
| The surface snow | `MF_SwcSnowSurface` in the masters `M_SwcSurface`, `M_SwcSurfaceMasked`, `M_SwcTerrain`, and `M_SwcTerrainMesh` (`swc_materials.snow_surface`), before the lighting. `MPC_SwcWeather` (`FSwcSnow::UpdateSurface`) gives the volume nearest to the camera (horizontal; its radius within 2 km [I]): the centre, 1 / radius, the amounts, the brightness, and the roof map. With no volume near, `SnowAmounts.w` is 0, and the function gives its inputs back unchanged. The rules: the radial mask (inner 1.2 / R and outer 1 / R), the 3 scales of the layout noise, the power of the upward normal, the spatter, and the freeze term. The roof test has the 4 taps of the source with a bilinear blend. The coverage test reads the normal of the surface after its normal map, in world space. The skinned meshes (the characters, the persons, and the creatures) take no snow: the function reads the flag PRIMITIVE_SCENE_DATA_FLAG_SKINNED_MESH of the primitive (also with the pass-through of the skin cache) [I]. The brightness of the snow blend is SnowBrightness x `swc.SnowSurfaceGain` 6 [I] (calibrated on the retail shrine views: the caps of the pots, the rock tops, the ground; the global factor of the source is not read [U]). Not ported [I]: the 3D volume noise (replaced by the layout noise on 3 planes), the frost bubbles, the derivative normal (the snow normal is flat), and the water fade | [R] deferredsnow.cfxb.txt lines 155 to 280 |
| The roof map | `FSwcWeatherOcclusion`: 96 x 96 downward lines (1 m, the channel Visibility) around the camera as async traces, into `RT_SwcWeatherOcclusion` (R32F heights). It is made again after 16 m of movement and each 4 s; its lines go out in 9 frames (1024 each: all of them in one frame crowded the async traces of that frame, and a check run had its camera in a wall). Only a structure is a roof: a first hit on the terrain, a rock or cliff, or a plant is open sky (with each first hit, a tall rock covered its own lower faces: hard straight edges of the snow along the cells). A surface or a flake below a roof takes no snow. Retail renders an overhead depth map of selected occluders (up to 768; 128 m, 256 x 256) | [I]; [R] research/e5-source-rain.md section 4 |
| The volume flakes | `FSwcSnow`: a validation mode, off by default (`swc.SnowFall 0`, as retail; open: in the check run of 2026-10-22 the flakes did not show with `swc.SnowFall 1`). With `swc.SnowFall 1`, the legacy rules of the source apply: 100 clusters of 500 flakes on one instanced mesh built at run time, the count fade 1 - d^2 / R^2, the spawn area, the lifetime, the fall, the turbulence, the size by depth^0.25, the atlas `T_SwcSnowflakesLinear` (linear), the soft depth fade, the sun term, and the roof map (`M_SwcSnowFlake`) | [R] research/snow.md section 2; [I] the mode |
| The screen snow | Removed: the client sets `SnowAmount` of `M_SwcPrecipitation` to 0 (the material branch goes out with the next precipitation import) | the user's request |
| The placed snow effects | Not shown: `tools/convert-particles.sh systems` builds the fire and the skill effects only; none of the 232 placed snow effects (48 `Snowflake_still01`, 42 `Snow_Wind_01`, 34 `Snowmist_slow`, 33 `Snowmist_up_small01`, the `Snow_WeatherBox` boxes, and others) has a system | an open point of the particle track |

The assets: `tools/convert-environment.sh snow` (the source textures of the surface pass into `environment/snow`), then
`tools/import-snow.sh` (`T_SwcSnowflakesLinear`, `M_SwcSnowFlake`, `MPC_SwcWeather`, `RT_SwcWeatherOcclusion`, `T_SwcSnowLayout`,
`T_SwcSnowSpatter`, `MF_SwcSnowSurface`; `--masters`: the two surface masters again; the terrain masters:
`tools/import-terrain.sh --material --mesh-material`).

The checks (2026-10-22, before the deploy, in a link copy of the shared plugin with the new masters; evidence
`~/Games/soulwarden-client/evidence/AST-257`): the shrine views retail | client (`side/ast257-shrine-*.jpg`): the snow on the upward
faces of the rocks, the ground, and the roofs, none on the character; the matched view noon-n of set1-oxboro (no Snow volume weight; the
function runs) against main: dE of the region means 0.016 to 0.068; cleave-inside-noon and -night of set4-weather: no screen snow, no
ground snow at 943 m (as retail); the inn of Warden's Rise (`ast257-inn1.mp4`): no snow on the floor inside, snow outside; the frame cost
at the shrine (the view fixed, late in the run): the GPU median 9.51 ms off and 9.62 ms on. The differences that stay: retail is brighter
and more even; the client misses the falling flakes of the placed effects (above).

## The exposure and the color grade (stage E5)

The decision of 2026-10-19: the client copies the colors of the source. The rules come from the compiled HDR shaders of the game copy
(`shaders/cache/d3d11/*.fxcb`: CPCK files with LZSS entries; each entry is a DXBC shader with its constant names, read with a local
reader, no game content in git):

| Shader | Rule |
| --- | --- |
| `hdrpostprocess@hdrsampleluminitialps` | The meter: the illuminance E = pi x L / albedo of each pixel (L the Rec. 709 luminance of the scene, the albedo the Rec. 709 luminance of the G-buffer color, at least 0.01; 0.2 where the G-buffer has no color: the sky). The geometric mean of the screen, smoothed in time. |
| `hdrpostprocess@hdrfinalsceneps` | EV = log2(E x 100 / 330) - (clamp(log10(E + 1), 0.1, 5.2) - 3) / 2 x "EV Auto compensation", clamped to "EV Min" and "EV Max"; the exposure is 1 / (1.2 x 2^EV) (E in lux: the scene unit is 10,000 cd/m2). Then the saturation ("Saturation": a mix with the Rec. 709 luminance) and the color balance ("Color balance": a multiply), both before the curve; the film curve of Hable (A = 0.22 x "Film curve shoulder scale", B = 0.3 x "midtones scale", C = 0.1, D = 0.2, E = 0.01 x "toe scale", F = 0.3, over its value at "Film curve whitepoint"); the exact sRGB encoding; the sun shafts (a screen blend); the color chart. |
| `posteffectsgame@combinecolorgradingwithcolorchartps` | The photo filter in the color chart: an overlay of "Filters: photofilter color" by the luminance of the sRGB value (2 x l x color under 0.5; color + (2 x l - 1) x (1 - color) over it), mixed in by saturate(l x "Filters: photofilter density"). |

The color chart of 181 of the 182 time-of-day volumes is `textures/colorcharts/default_cch.dds`, an exact identity chart (one quest
volume has its own chart). Thus no chart is copied.

The other controls of the chart (audit item 4, 2026-10-20; `research/e5-chart-controls.md`): the supported baseline has identity
levels (0, 255, 1, 0, 255), the chart brightness, contrast, and saturation 1, the saturation offset 0, and no selective color. The
merged chart differs from identity only by the photo filter of the time-of-day preset, applied once after the sRGB encode: that is
what `M_SwcGrade` does (the code of the saved asset is the code of `Scripts/import_sky.py`). The time-of-day presets have no other
control of the grade ("Filters: grain" and "HDR dynamic power factor" are 0 in all 160 presets). The cinematic chart and the
transient grades (whisper, injury, spell, spyglass) belong to their effect groups and are not in the world grade. Open: the 16-cube
bake of the source (its interpolation and its 8-bit steps) for an exact chart parity.

The client (`Environment/SwcEnvironmentSubsystem.cpp`, `M_SwcGrade` of `Scripts/import_sky.py`):

- The meter is the Unreal meter that ignores the materials (`r.AutoExposure.IgnoreMaterials 1`, the luminance over the base color
  times lerp(0.18, 1, 0.01)), with the Rec. 709 weights (`r.AutoExposure.LuminanceMethod 2`) and the widest window (1 % to 99 %).
  The bias curve of the Unreal auto exposure holds the whole rule of the source (`env::source_exposure_bias`, tested): at the
  metered EV100 of Unreal, the bias gives 1 / (1.2 x 2^EV). The bounds of Unreal stay wide. The renderer bakes the curve into a
  table only for a new curve object: the client makes a new curve when the rule changes (a change of the keys was not seen).
- The source meter of the frame corrects that meter (`swc.SourceMeter 2`, the default from 2026-10-20;
  `Environment/SwcMeterProbe.cpp`). Each 0.25 s an asynchronous readback copies the scene color after the depth of field (with
  the water, the fog, and the translucency) and G-buffer B and C; the render thread reads each sixth pixel in each direction. E is
  pi x L / the albedo with the denominator of `research/e5-meter-water.md`: the larger of the Rec. 709 luminance of the diffuse
  color (the base color x (1 - metallic)) and of the reflectance (lerp(0.08 x specular, the base color, metallic)), at least 0.01;
  0.2 where the diffuse luminance is 0 (the sky). G-buffer C is an sRGB target (the probe decodes it). Under the translucent water
  the G-buffer is the ground. The bias of the Unreal exposure is a closed loop: each new measure adds 0.1 x (the applied EV - the
  EV of the source rule for that E), 0.5 while a loading screen shows; the applied EV comes from the exposure of the frame. The
  loading gate has a check "meter": a screen ends only when four measures in a row are within 0.05 EV. `swc.SourceMeter 1` is the
  Unreal meter alone with the source curve, 0 the Unreal luminance.
- `M_SwcGrade` replaces the tone mapper: the scene times the exposure, then the source order (saturation and balance, curve, sRGB,
  photo filter). The bloom is the mix of the source (`swc.SourceBloom 1`, from 2026-10-20; research/color-pipeline.md section 6):
  exposure x ((1 - w) x scene + w x blur) with w = saturate("Bloom amount" x 0.3 x 0.125), 0.045 at Everfall noon and 0.138 in the
  menu. The Unreal bloom is the blur, with a gain of 1: the Gaussian stages 3 and 4 (sigma = the view width x size % / 1156: 8.7 and
  17 pixels at 1,256) with the weights 0.917 and 0.083 (the two blur images of the source, 0.0174 and 0.192 over their sum), no
  threshold, no lens flare. The grade adds it with the scale w / (1 - w), and the meter target has the factor 1 - w. An inference:
  the source blurs at a quarter of the screen (its 15-tap binomial kernel, sigma 2.12 taps), and the larger weight goes to the smaller
  blur. `swc.SourceBloom 0` is the Unreal bloom added (0.09 of a blur: the tints x 0.675 / 6).
- The ambient is the world probe of the source: `environmentprobe_thinpine_world02_cm` (two boxes of 100 km, the outdoor ambient of
  the world), a BC6H radiance cube (`tools/convert-environment.sh probes`: `converter/swc_convert/bc6h.py`, checked against the
  8-bit decoder of Pillow on all ten modes of the file; a long-lat Radiance picture; then `tools/import-sky.sh`). The sky light shows
  it (a specified cube, with its lower half: the ground of the probe) at 10,000 cd/m2 a unit times its DiffuseMultiplier and the
  time-of-day "Environment Probe Diffuse Multiplier" (1 by day, 0.15 by night) and "Environment Probe Multiplier". Lumen adds the
  bounce and the occlusion. The probe is a hazy gray sky over a pine forest: the source shade is gray, not blue. The real-time
  capture of the Unreal sky gave a blue shade by day and almost no ambient by night.

- The sky color is the sky model of the source (step 2b; `swc.SourceSky 0`: the SkyAtmosphere of Unreal): the single scattering
  of Nishita of the public engine (SkyLightNishita.cpp, SkyLightManager.cpp: an atmosphere of 100 km over an earth of 6,368 km, the
  Rayleigh density exp(-h / 7.994 km), the Mie density exp(-h / 1.2 km), Kr and Km 1e-4 times "Sky light: Rayleigh scattering"
  and "Mie scattering", 1 / lambda^4 of "Sky light: Wavelength (R, G, B)", the light "Sky light: Sun intensity" times its
  multiplier; `env::source_sky`, tested against a numeric reference) on a dome of the upper half of the sky (64 x 32, made on a
  worker thread when the sun moves by 0.2 degrees or a value changes), and the sky shader of the game copy (skyhdr@skyhdrps): the
  Mie part times miePart (1 + c^2) / (1 + g^2 + 2 g c)^1.5, the Rayleigh part times 0.75 (1 + c^2), plus the night sky gradient
  ("Night sky: Horizon color" to "Zenith color" by "Zenith shift"; the zenith shift rule of the public engine is an inference). By
  night the model has the direction of the moon (as the source). The sun disk stays the disk of the SkyAtmosphere.
- The clouds stay the volumetric clouds of Unreal (the decision of the user). Their albedo is the ratio of the cloud sun color of the
  data (TimeOfDay.cpp: lerp(the sun color x "Cloud shading: Sun light multiplier", "Sun custom color" x its multiplier, its
  influence)) to the color of the main light, in each channel (`swc.CloudTint`).
- The time-of-day volumes are selected at the feet of the player, not at the camera (the source: set4-weather/keep-walk-to-door.json,
  the hour of the keep interior starts when the feet pass y 5923.6 with the camera 4.3 m behind, outside the door). Before, the camera
  of keep-outside-noon was in the interior box and the view showed the night hour 0.0 of that box.

- The fog is the "Volumetric fog 2" of the source (step 3; the static study `~/Games/soulwarden-client/research/e5-source-fog.md`
  of the retail fog shaders and NewWorld.exe). The source: the density rho = "Global fog density" x 0.01 x exp of the line through
  ln("Fog layer density (bottom)") at "Fog height (bottom)" and ln(the top density) at "Fog height (top)"; the extinction e x rho
  ("Extinction factor"); the light s x rho x [Ce x E + B] ("In-scattering factor"; Ce "Fog albedo color (entities)"; E the probes
  of the fog cell, the diffuse cube in the ray direction / 12.566; B two sun lobes of the Schlick phase (1 - k^2) / (12.566
  (1 - k mu)^2): "Fog albedo color (atmosphere)" with its anisotropy x (1 - blend x mode) and "Fog albedo color (sun radial)" with
  its anisotropy x blend; the sun color S = the sun color over its luminance x the sun lux / (10,000 pi)); the light of the cells
  fades in from "Ramp start" to "Ramp end". The atmosphere color (blue in everfall_a) is a sun lobe, not an ambient color; the
  purple far haze of the Brightwood river is that blue lobe in the warm color balance and the purple photo filter of the grade.
  The client is the stock Unreal fit of section 5 of the study: the albedo Ce; the density 10 e rho / ln2^2 and the extinction
  scale 2 ln2^2 (the extinction of both Unreal fog paths); the max opacity "Final density clamp"; the ramp in the near fade-in of
  the volumetric fog; the scattering intensity of the sun and the moon s / (e pi), with the override of the light colors by the
  fog (the directional inscattering color: the normal sun color x the sum of the lobe colors / (Ce x the luminance of the sun
  light)); one Henyey-Greenstein g, the mean cosine of the two Schlick lobes weighted by their light; the fog inscattering color
  (the analytical tail, and the ambient of the volume) the world probe (the mean of its side faces) x its multipliers x Ce /
  12.566; no height fog part of the SkyAtmosphere. With the Lumen translucency volume, the Unreal fog takes its ambient from that
  volume with no gain (much brighter than the source term): the client turns it off (`r.Lumen.TranslucencyVolume.Enable=0`,
  DefaultEngine.ini; translucent materials then use the sky light).
- The rain is the scene rain of the source (step 3; the static study `e5-source-rain.md`): N = clamp(int(Amount + 1e-4), 1, 3)
  layers on the rain cone about the camera (one mesh: the middle band a cylinder of the radius ((i + 1) D)^1.5 m, D = 0.5 x
  r_RainDistMultiplier 2: 1 m and 2.83 m; the height 4 (1 + i / max(N - 1, 1)) m; the end bands narrow to 0.01 of the radius at
  the mesh z +-1). The alpha of a layer: rainfall.r (sRGB decoded) at 1.5 cone.yz and at 3 cone.xz (plus RainDropsSpeed x time x
  (0.1, 1.25)), weighted by |nx|^4 and |ny|^4 (n the horizontal direction), times the soft depth fade saturate(2 x (the opaque
  distance - the cone distance)) and the vertical fade g (1 - ...) with g = saturate(0.5 - 0.5 mesh z) (0.5 at the eye height).
  The streak light: Amount x RainDropsAmount x H, then an alpha blend for each layer. The fen volume has the Amount 2.5: 2 layers.
- The local probes (step b, in work): `tools/convert-environment.sh probes` also writes the 1835 local probes of the data (the box,
  the order, the color, the multipliers, the edge fade, the box projection, the vis area switches) and the pictures of their 35
  cubes; `tools/import-sky.sh` imports the cubes (BC6H). The source selects the probes for each pixel (the static study
  `e5-local-probes.md`). The client has a test of the weights at the camera (`swc.LocalProbes 1`: the sky light blends the world
  cube and the cube of the strongest local probe; off by default: one cube for the whole view does not make the per-pixel result,
  for example the dark wall and the lit floor of keep-outside-noon).

- The contrast step: `r.EyeAdaptation.CachedLightingPreExposure=8` (DefaultEngine.ini; the engine default 4 gives the safe range
  of the cached lighting EV -8 to 12, and the day exposure of the client is about EV 12 to 13: the red warning of the engine).
  Check (2026-10-20): the matched view noon-road-low with 8 and with 4 has the same picture (mean luma 119.7 and 119.3); a day
  run with the screen messages on (the Shrine of the Lion, EV 11.7 to 12.3) shows no warning with 8.

### Check of E5 (steps 1, 2a, and 2b, 2026-10-19)

The color report of the matched views, band "all" (dE / L* / spread / saturation / dark part), before (the old grade: the curve
after the balance, the Unreal luminance meter) and after:

| View | Before | Step 1 (the source grade) | Step 2a (the world probe) |
| --- | --- | --- | --- |
| noon-n | 19.3 / +17.8 / -1.8 / +0.060 / -0.041 | 9.6 / +5.5 / +0.6 / +0.123 / -0.038 | 9.1 / +9.7 / -0.3 / -0.073 / -0.040 |
| inn-noon | 20.7 / +20.8 / -8.1 / -0.054 / -0.209 | 12.4 / +12.5 / -5.6 / -0.006 / -0.199 | 13.0 / +14.2 / -4.2 / -0.159 / -0.189 |
| oxboro-road-sky-noon | 16.1 / +14.4 / -1.9 / -0.018 / -0.031 | 5.8 / +3.0 / +4.9 / +0.032 / -0.013 | 6.2 / +4.5 / +1.9 / -0.075 / -0.022 |
| night-n | 8.1 / -11.6 / -5.4 / +0.257 / +0.540 | 9.1 / -13.2 / -7.9 / +0.317 / +0.571 | 4.6 / -0.0 / +0.2 / -0.074 / +0.069 |
| noon-road-low | | | 1.9 / -1.1 / -1.2 / -0.047 / -0.020 |
| noon-wall-grass | | | 5.7 / +5.4 / -0.8 / -0.058 / -0.050 |
| night-road-low | | | 0.9 / -0.8 / -1.6 / -0.041 / -0.021 |
| night-e | | | 8.3 / +9.0 / +5.6 / -0.086 / -0.213 |

The Unreal luminance meter in the same run (noon-n): 23.5 / +22.2. The real-time capture in the same run: noon-n 10.1, night-n 9.1
(L* -13.3).

Step 2b (the sky model of the source, the cloud tint, the volumes at the feet), band "all" (step 2a / step 2b):

| View | Step 2a | Step 2b |
| --- | --- | --- |
| oxboro-road-sky-noon | 6.2 / +4.5 / +1.9 / -0.075 / -0.022 (the top band 13.6) | 3.2 / +2.4 / -4.2 / -0.033 / -0.026 (the top band 5.3) |
| oxboro-sky-night | 7.8 / -10.5 / -8.5 / +0.040 / +0.009 (the top band -19 L*) | 2.3 / -2.4 / -3.0 / -0.029 / -0.034 |
| noon-road-low | 1.9 / -1.1 / -1.2 / -0.047 / -0.020 | 1.7 / -1.2 / -2.7 / -0.017 / -0.020 |
| keep-outside-noon | 11.4 / +12.2 / +5.9 / +0.212 (the night hour of the interior box) | 10.5 / +13.9 / +7.4 / +0.018 (the noon hour) |
| fen-outside-south-noon | 3.9 / +4.7 / -1.2 / -0.052 | 7.0 / +3.5 / -3.8 / +0.043 (a clear blue sky: the fen fog is step 3) |
| noon-n, night-n | 9.1, 4.6 | 9.0, 4.5 |

In the same runs: the SkyAtmosphere of Unreal gave oxboro-road-sky-noon 6.0, the white clouds (`swc.CloudTint 0`) 4.0.

Step 3 (the source fog and the source rain, the Lumen translucency volume off; main 19db33a), band "all" (dE / L* / saturation)
and the top band dE, the old fog (`swc.SourceFog 0` with the Lumen translucency volume on) in the same run:

| View | The old fog | Step 3 |
| --- | --- | --- |
| noon-n | 13.6 / +14.0 / -0.119 | 13.5 / +14.0 / -0.115 |
| river-forest-noon | 23.2 / +24.3 / -0.096, top 19.3 | 22.6 / +24.5 / -0.090, top 24.8 (the purple haze over the far bank only) |
| keep-outside-noon | 10.5 / +13.9 / +0.018 | 10.4 / +13.8 / +0.021 |
| fen-inside-noon (rain) | 15.4 / +17.5 / -0.164, top 30.3 | 4.8 / -4.9 / -0.098, top 11.0 |
| fen-outside-south-noon | 9.7 / +9.8 / +0.071, top 13.3 (S +0.229) | 9.1 / +10.4 / -0.032, top 4.9 (S +0.003) |

The same code in a second run (main c5ba0ae) gave fen-inside-noon 3.0 / +2.0, top 3.9 (L* -4.0 against -14.1): the storm sky (the
moving clouds) changes the top band from run to run. In that run, the foliage back face of Lumen on again (the setting of main
19db33a, `r.Lumen.ScreenProbeGather.TwoSidedFoliageBackfaceDiffuse 1`) gave 9.2 / +10.3: the earlier result 2.4 / +0.1 (before
19db33a) is not comparable. The Lumen translucency volume on and off (inn-night, noon-n): the same numbers (9.6 and 9.7, 13.5 and
13.5) and no visible change of a translucent material (the window glass, the lanterns). The water part (`tools/lib/water_part.py`,
the four noon views of set 2 and the two swim views, docs/WATER.md "The water color check"): the mean water dE 28.9, the water dL -
all dL +4.8 (river-forest 26.5, river-highlands 38.1, ocean-clear 31.4, ocean-storm 2.1, swim-idle 38.5, swim-nav 37.0; after E5
steps 1 and 2a the mean was 39.8 and +7.3, before the cover and the decal changes of the other tracks). The swim views stay light
(the water L* 61 to 63 against 22 to 24) and gray (S 0.36 to 0.39 against 0.74 to 0.79). Evidence:
`~/Games/soulwarden-client/evidence/E5/step3-final/` (the side pictures, `translucent-pairs.png`, `water-part.txt`, `colors.json`),
`step3-fog/`, `step3-rain/`.

### Check of the source meter (2026-10-20)

The six views of the meter step, whole picture dE / L*: `swc.SourceMeter 1` (the matrix runs on b451e15) against 2 (run `level4`
on main ce819e5, the far-grid level; the same numbers within 0.3 dE on b451e15), and the difference of the applied EV and the EV
of the source rule at the picture:

| View | Meter 1 | Meter 2 | Gap at the picture | Dark part, client / retail |
| --- | --- | --- | --- | --- |
| noon-n | 13.5 / +14.0 | 9.7 / +10.2 | -0.04 EV | 0.01 / 0.04 |
| noon-road-low | 3.9 / +3.9 | 1.8 / +0.1 | -0.03 EV | 0.03 / 0.03 |
| keep-outside-noon | 10.4 / +13.8 | 6.1 / +7.4 | -0.09 EV | 0.16 / 0.36 |
| fen-outside-south-noon | 9.1 / +10.4 | 4.1 / +4.8 | 0.00 EV | 0.03 / 0.12 |
| ocean-clear-noon | 25.0 / +24.9 | 6.1 / +4.8 | -0.20 EV | 0.00 / 0.03 |
| swim-idle | 27.2 / +26.2 | 19.1 / +18.5 | -0.05 EV | 0.00 / 0.01 |

The water views of set 2 (`tools/lib/water_part.py`, run `water5` and `level4`): the mean water dE 28.9 -> 13.9, the water dL -
all dL +4.8 -> +4.9 (the water stays lighter than the scene by the same part). The whole pictures: river-forest-noon 23.1 / +24.9
-> 9.0 / +10.4, river-highlands-noon 25.3 / +28.4 -> 11.4 / +13.3, swim-nav 41.3 / +40.4 -> 16.9 / +17.6; ocean-storm-noon
stays 10.0 / -12.6 (the black storm sky). The cost (pairs at the same place late in one run, `-SwcFrameStats=5`): GPU p50 +0.0
to +0.4 ms, frame p50 +0.1 to +0.2 ms, the same frame maximums; no readback was dropped. The gate: in the travels of the matched
views the gate ended at all six screens with the meter check passed. After a screen the exposure moved 0.03 to 0.27 EV in 10 s:
the matched view sets its own camera after the screen (the ocean: the mean scene luminance 1,417 -> 1,098 cd/m2), and the meter
follows the scene. Evidence: `~/Games/soulwarden-client/evidence/E5/meter-final/` (crops of the same parts, retail | client, at
2.5x: `crops-VIEW.png`; `overview.png`, `colors.json`, `water-part.txt`).

The menu landing (`tools/menu-check.sh` with a drive that only waits, main 4a6c1ba, `swc.BeamScale 1`; the frame 40 s after the
landing against B6-04 with the mask of the UI panels and the character): meter 1 dE 27.5 / L* +30.9, meter 2 22.2 / +25.2, meter 2
with `swc.LightScale 1` 19.9 / +21.8. The bias of meter 2 settles at -0.43 EV. The menu stays too bright: the fog sheets are a pale
cream haze over the trees and the house, the tent has no light and shade of the trees, the fire is a white flare, and the flower
petals and the ground are pale (`evidence/E5/meter-menu/crops-menu-a.png`).

The differences that remain in the crops: the window glass of noon-n is flat light gray (retail: dark, with the room and
reflections); the cobbles are cool gray with less depth in the gaps (retail: warm beige-brown with dark gaps); the right wall and
the floor of the keep are lit (retail: black shade; the priority-2 box of a dark interior probe, step 3); the swim water is light
teal with a strong mirror of the island (retail: deep blue, a faint reflection); the far cliffs of the swim view are more hazed.

### Check of the sun of the hour, the bloom mix, and the light scale 1 (2026-10-20)

The audit items 1 to 3 of `research/color-pipeline.md` as the new defaults (`swc.SunSource 1`, `swc.SourceBloom 1`,
`swc.LightScale 1`), whole picture dE / L*, before (run `level5`, main 2f8501c) and after (run `defaults1`):

| View | Before | After |
| --- | --- | --- |
| noon-n | 9.7 / +10.2 | 6.7 / +7.2 |
| noon-road-low | 1.6 / +0.2 | 2.6 / -1.5 |
| keep-outside-noon | 6.1 / +7.4 (dark part 0.16) | 5.2 / +4.6 (0.25; retail 0.36) |
| fen-outside-south-noon | 4.2 / +4.9 | 3.9 / +4.2 |
| ocean-clear-noon | 6.4 / +5.2 | 3.0 / +3.0 |
| swim-idle | 18.8 / +17.7 | 16.9 / +18.1 |
| the menu (B6-04, masked) | 22.2 / +25.2 | 21.8 / +24.4 |

Each item alone (run `audit2`, the six views in one run): the bloom mix gives most of the change (noon-n 8.7 -> 6.7, fen 4.1 ->
2.7). The sun of the hour changes little at noon (ocean-clear: the saturation difference -0.067 -> -0.009, the hue -32 -> +1
degree; swim-idle L* +17.8 -> +20.6). `swc.LightScale 1` changes the day views by 0.1 to 0.4 dE. In the menu the bloom mix gives
22.2 -> 20.2, and the sun of the hour 22.2 -> 23.6. The old sun of the menu was the noon color times the transmittance of the
atmosphere at 18.3 degrees (0.797, 0.659, 0.389): on the ground (1, 0.741, 0.321), more orange than the source (1, 0.651, 0.514)
and about 20 % weaker. With the source sun the haze of the menu is pink-lilac: the fog sheets (GeometryBeam) take the sun color.

The fog sheets make half of the excess of the menu: `swc.BeamScale 0` gives 14.5 / +12.9 (the top band -0.2 L*), the volumetric
fog off only 21.6 / +24.3, both off 15.5 / +13.2. Without the sheets the trees (18.7 against 19.5) and the tent (28.7 against 32.2)
are near retail; the house wall is lighter (29.4 against 22.5), the ground under the flowers is pale gray-green (retail: dark brown),
the flower petals are cream-white, and the fire is a white flare. Evidence: `~/Games/soulwarden-client/evidence/E5/audit/`.

### Step 3: the local probes of each pixel and the SSAO test (switches, off; 2026-10-20)

- `swc.LocalProbes 2`: a pass after the base pass (module `SwcShaders`, loaded at PostConfigInit: `Shaders/Private/SwcLocalProbes.usf`,
  `Source/SwcShaders`; the view extension `Environment/SwcProbeShading.cpp`) multiplies the material AO of G-buffer C by the light
  of the local probes of the pixel over the world probe (research e5-local-probes.md: the order by the low byte of the priority,
  the radius, and the record; the fade of each box; the remaining weight to the world probe; the ambient cube of each diffuse cube,
  probes.json "diffuseMeans"), at most 1. The nearest 64 probes within `swc.LocalProbeReach` (500 m) go to the pass. It runs with
  no fault (the G-buffer targets found, 21 probes at noon-n) and changes nothing in the six views: their pixels are in no box.
- `swc.Ssao 1`: the post-process AO values of research e5-ssdo.md (intensity 1, power 2, a world radius of 480 cm, bias 0,
  static fraction 0, quality 50), with `r.Lumen.DiffuseIndirect.SSAO 1` and `r.Lumen.ScreenProbeGather.ShortRangeAO 0` in the
  same set. noon-n 6.7 -> 4.3, the keep 5.2 -> 4.9 (the dark part 0.25 -> 0.33, retail 0.36); swim-idle 16.8 -> 19.4 and the
  menu 17.4 -> 19.1 are worse: the meter takes back about 77 % of a light change (the slope of the EV rule), and the parts with
  no AO (the unlit water, the additive sheets) become lighter. Not a default.
- The level of the ambient: `swc.ProbeScale 0.5` gives noon-n 6.7 -> 2.9 but not the open views, and Lumen GI off changes
  nothing; so it is not one global gain. At matched exposure the displayed ratio is mostly the albedo ratio; noon-n is about 1.44
  times too bright in the sun and 2.1 times in the shade.

### The probe specular term (switch, off; AST-247 part 2, 2026-10-22)

`swc.ProbeSpecular 1` adds the specular light of the environment probes for each pixel, as the opaque tiled path of the source
(research weapon-materials.md section 2, e5-local-probes.md, e5-ssdo.md section 4):

- The cubes: `tools/convert-environment.sh probes-spec` writes each world and local probe cube with the pre-filtered mips of the
  source (BC6H 256 x 256, 7 mips: the split parts `NAME.dds.N` are one mip of the six faces, the highest N is mip 0; the tail of
  the base file has the smaller mips of each face in turn) as a DDS cube of RGBA16F (the mips below 4 x 4 are 2 x 2 means, as
  Unreal imports only a full chain): `content/environment/probes/spec/NAME.dds` and `probes-spec.json` (35 cubes, 140 MB).
  `tools/import-sky.sh` imports them as `/SwcGameContent/Environment/ProbesSpec/T_SwcProbeSpec_NAME` (the mips of the file,
  HDR, no streaming). The same command writes the environment BRDF table of the source (`engineassets/shading/environmentbrdf.dds`:
  BC1 sRGB 128 x 128, mip 0 in the highest split part; decoded with its sRGB view to linear R, G; research probe-flags.md section 3)
  as `probes/spec/environment_brdf.dds` (RGBA16F), imported as `T_SwcEnvironmentBrdf` (no mips, no sRGB, bilinear clamp). The face
  layout is the D3D cube lookup of the source directions east, north, up (Unreal +X, -Y, +Z); the
  seams of the world cube agree with this layout (mean seam error 0.008 against 0.39 for a swap of the north and south faces).
- The pass (`Shaders/Private/SwcProbeSpecular.usf`, `AddSwcProbeSpecularPass` in `Source/SwcShaders`; the view extension
  `Environment/SwcProbeShading.cpp`) runs after the base pass, before the lighting, and adds to the scene color, so the fog
  and the translucency come after it. For each lit pixel: R = reflect(-V, N); the ordered probe weights of the AO pass (all
  the probes of the list with a cube, at most 64, which share at most 16 distinct cubes; the world probe in the rest); each cube
  at R (when `BoxProject`: the box projection of the proxy box on the world axes about the probe center, x BoxLength, y BoxWidth,
  z BoxHeight, each / 2, apart from the rotated influence box) at the mip 6 - 6 x smoothness; times its color COLOR x
  DiffuseMultiplier x lerp(1, G x D, t) x SpecMultiplier x lerp(1, S, t) (G the RGB of "Environment Probe Multiplier", D and S
  the diffuse and specular multipliers of the time of day, t the TODInfluence); times R x (1 - F0) + G x F0 with (R, G) of the
  BRDF table of the source at (N.V, smoothness) (not the table and the formula of Unreal `EnvBRDF`: at F0 0.18, s 0.75, and N.V
  0.1 they give 2.61 times the response of the source); times the specular occlusion saturate((N.V + ao)^s - 1 + ao), ao =
  max(min(material AO, saturate(1 - a x 3)^2), 0.3), a the SSDO of the source (8 taps, the narrow and wide bands, the radius
  1.2 m with the UV bounds 0.06 to 0.20; `swc.ProbeSpecularSsdo`, 0 turns it off); times 10,000 cd/m2 a unit x
  `swc.ProbeScale` x the pre-exposure. The SSDO blur takes taps 1 pixel apart (the source: 2) so that the 2 x 2 pattern of the
  radius bound does not show on bright metal [I].
- The screen-space reflection of the source (`swc.ProbeSpecularSsr`, default 1, only with `swc.ProbeSpecular 1`; AST-247 part 3,
  research reflection-ssr.md fixes 1 and 2; `Shaders/Private/SwcProbeSsr.usf`, `AddSwcProbeSsrPasses`): a ray pass at half
  size (the High preset of the source) for each lit pixel: Z the linear depth (m), L = 1.5 x Z, D = normalize(reflect) x L, the
  direction weight saturate(dot(view, D) + 0.5); 4 + 28 x smoothness taps between the projected ends; the first tap within
  L / (taps x 1.6) of the scene depth is the hit (no refinement, no jitter); its color comes from the scene color of the previous
  frame before the depth of field (the view extension copies it at `BeforeDOF`) at the reprojected place (ClipToPrevClip); alpha =
  the edge weight (sqrt(b / 0.07) within 0.07 of a screen border) x the direction weight. Three reduced levels (3 x 3 binomial
  filter [I: the source uses one Gaussian]). The specular pass mixes the levels by g = s^2 (f = frac(min(g, 0.9999) x 3): g > 2/3
  lerp(B1, B0, f^2), g > 1/3 lerp(B2, B1, f), else lerp(B3, B2, f)) and sets the radiance to (1 - a) x probe x occlusion + a x
  SSR.rgb before the BRDF table. The first frame of a view has no history: no SSR in that frame. The client log has one line at
  each change: "probe shading: SSR on (history kept, ...)".
- With the switch on, the client sets `r.Lumen.Reflections.Allow`, `r.ReflectionEnvironment`, and `r.SSR.Quality` to 0 (the
  values before come back when it is off), so the reflection is not counted twice.
- Checks (the matched views of set-ast247-mv, set1-oxboro, and set4-weather; `work/ast247/sheets-spec/`; linear display values):
  - With the source table (2026-10-22, second check): the shoulders of both sets are near retail at matched dirt (Metal_SetB
    0.52 / dirt 0.19 against retail 0.43 / 0.15; Heavy Plate 0.62 / 0.18 against 0.54 / 0.13). The Metal_SetB chest is 1.25
    times retail and its belly (it faces down and forward) 1.6 times, and both are too neutral: retail shows a darker, green
    reflection of the forest. The iron fence of noon-n: fence / brick 3.23 with the term, 1.36 without it, retail 1.99 (1.6 times).
    The table changes these views only a little: most pixels have N.V in the middle, where the two tables are almost equal.
  - The SSDO removes about 6 to 8 % on the armor. The world views change little.
  - With the SSR (2026-10-22, the passes run: "SSR on, history kept, 1280 x 720"): the armor of HeavyHeavy (front, masked mean)
    0.255 without it, 0.232 with it (- 9 %); the iron fence of noon-n (the pixels that the term makes brighter) 0.700 without it,
    0.693 with it, retail 0.460, the term off 0.379. Most fence rays point to the sky or off the screen (a miss: the probe stays).
    The SSR does not close the gap on bright steel and iron (`evidence/ast247/spec/crop-fence-ssr.png`: retail, off, term, term
    and SSR).
  - The GPU time (pairs late in one run): noon-n 10.03 ms off, 10.01 and 10.09 ms on (eight cubes); with all the probes (21 at
    noon-n) 12.31 ms off, 12.34 and 12.43 ms on, the inn 13.32 ms off, 13.60 and 13.32 ms on (a busier GPU): at most about 0.3 ms.
- Open [U]: the level and the color on bright steel and iron (about 1.6 times). Kept as the source (research probe-flags.md): the
  world probe on the outdoor pixels (`AffectsThisAreaOnly` selects the outdoor area stencil when there is no volume; `IndoorOnly`
  is a legacy height-map flag, not a mesh indoor test), no extra gain, the SSDO with the scalar alpha. Not in the client: the
  area stencil of the probes with volumes; the SSDO as a separate generation and blur pass (reflection-ssr.md fix 3). The material instance of the Metal_SetB chest is right (M_SwcSurface, the
  spec map, F0 about 0.18 by the texels).

### Not done, open points (E5)

The E5 queue stopped on 2026-10-20 (the decision of the user: the next focus is a playable game). These items wait, in this
order of their expected effect on the whole picture:

- The materials against the light at noon-n: about 1.44 times too bright in the sun and 2.1 times in the shade at matched
  exposure (the meter takes back about 77 % of a light change, so the displayed ratio is mostly the albedo ratio). The next
  measure: an albedo view in the meter probe (the mean G-buffer diffuse of the wall and street parts in source units) against
  the source texels of the same materials (decode x tint x the diffuse factor of their records).
- The fog sheets of the menu (M_SwcBeam version 5, research beach-sheet-inputs.md): the sheets within 30 m of the Player Entity
  still add about 10 L* (the menu 15.7 / +18.0, with no sheets 13.5 / +8.3); the haze over the house is the strongest. Open: the
  live LOD at B6-04, the vertex wave, the air fog branch, and the alpha channels of the other sheet materials.
- The cobble decal of the surface layer (research e5-surface-decal.md): POM (height bias 0.5, displacement 0.02 UV), the road
  tangent frame from the road direction and the terrain normal, the white vertex RGB and the vertex alpha at the road ends, the
  blend of albedo and F0 in their square-root encoding. Its blue (R, G, B 0.90, 1.03, 1.17 against retail at equal luminance)
  stays with less ambient: probably the missing reflection occlusion of the source at a grazing view.
- The SSDO of the source as a pass (research e5-ssdo.md): the probe diffuse part (min(material AO, saturate(1 - a x 2)^2) with
  the color bleed), the reflection part (saturate(1 - a x 3)^2 and the grazing term), and the direct-light part (a change of the
  lighting). The Unreal SSAO test (`swc.Ssao 1`) helps the town and harms the water views and the menu.
- The menu: the diffuse light of the probes per pixel. The client samples the diffuse light of the probe cube by the world normal
  (the sky light, within 2 % of the _diff cube of the source) and adds no world ambient (weight 0); only the character probe box
  (2 x 2 x 2.5 m, the RGB factor 0.248, 0.808, 1.024 against 0.286, 0.808, 1.079 of the scene probe) is not per pixel. Also the
  pale ground under the flowers, the cream flower petals, the white flare of the fire, and the sun shafts.
- The sky color and the sun shafts of the source (research color-pipeline.md section 6: the shafts as a screen blend after the
  sRGB encode); the menu sky is pink-lilac against a warm brown with shafts in B6-04.
- The coast fog (research e5-coast-fog.md: the source ramp on the density, the zenith rule).
- The storm sky of Cutlass Keys (research e5-distance-clouds.md: the five Distanceclouds dome layers over the source base sky;
  the Unreal volumetric clouds off for the source sky slices).
- The numbers of AST-192.
- Outside E5: the window glass (flat light gray against dark with the room and reflections: the materials) and the swim water
  (light teal against deep blue, a strong mirror: AST-200).

- The final scene multiplies the exposure by a texture `t7` at the screen coordinates. An inference: the vignette of the source.
- The day walls are brighter than retail (noon-n top band +17 L*, inn-noon +10) and the day saturation is low (-0.05 to -0.16):
  the sun, the sky, and the interior probes (step 2). The interior probes of the source (for example `settlement_tavern`) are
  not in the client: Lumen and the world probe light the interiors.
- The clear sky of the source model is a deeper blue than the lilac retail sky: the retail sky is mostly the thin cloud layers of
  the sky slice (Distanceclouds: the cloud sun color and the sky color x SkyColorMultiplier 8 over their masks), and the volumetric
  clouds of Unreal do not cover the sky in the same way.
- The rain occlusion of the source (a top-down depth map of the occluders in a 64 m box about the camera, integrated along each
  view ray from 1 to 64 m, `SceneRainOccAccumulate`) is not in the client: the client streaks are more visible than retail over
  the tower and the trees of fen-inside-noon.
- The streak light H of the rain is the geometric mean of the scene luminance in the source. The client takes the illuminance E of
  the EV rule of the exposure x 0.18 / pi (an inference: the mean albedo of the meter; Unreal keeps the metered luminance only in
  the eye adaptation buffer, z).
- The second term of the streak light (RainDropsLighting x the bloom texture of the source) is 0: the post-process material has no
  bloom input before the tone mapping.
- The radial sun lobe of the far fog: Unreal shares the directional inscattering color of the height fog with the override of the
  volume, so the far tail has no correct radial lobe (section 5 of the fog study).

## The sun rays (AST-232, 2026-10-21)

The request of the user: "add godrays". The rule of the source: the static study `research/godrays.md` (GPT-6.1-Sol; the native
passes, the TOD keys, the moon). The client copies the normal SDR path of the source (the merged HDR final shader).

The parts:

| Part | What it does |
| --- | --- |
| `Environment/SwcEnvironmentSky.cpp` `UpdateGodRays` | Each frame: the five TOD keys with the volume blend ("Sun rays visibility" clamped to [0, 10], "Sun shafts visibility" clamped to [0, 0.3], "Sun rays attenuation", "Sun rays suncolor influence", "Sun rays custom color"); the gates of the source (shafts or rays above 0.05, one amount at least 0.01, the source sun RGB "Sun color" x "Sun intensity" with a squared length of at least 0.01); the tint V x lerp(the custom color, the normalized source RGB, the influence) for the grade (`RayTint`); the active light (the moon when its weight is at least 0.5, as the clouds); the half-size target. A log line "rays: on/off, ..." at each change and each 30 s. |
| `Environment/SwcGodRays` | A scene view extension after the depth of field (the HDR scene color with its pre-exposure): the screen point of the light (a far point toward the light through the view projection: UV = (0.5 (x + w), 0.5 (w - y)) / (w + 1e-6), not clamped), the signed facing f (the dot of the light direction with the view direction), the CPU gate, then the passes into the target. The grade reads the target later in the frame. |
| `Source/SwcShaders` `SwcGodRayPass`, `Shaders/Private/SwcGodRays.usf` | The mask of the source (SunShaftsMaskGen: M.rgb = d x 1.32 x (r + g + b) of the half-size scene color in source units, M.a = 1 - d; d the view depth over the far depth, the sky 1), then the radial pass twice (SunShaftsGen: 8 samples toward the light with the weights 1 - i/8, steps 0.1 and 0.025, rgb x 0.25 x D with D = saturate(f) x (1 - saturate(length(v x (1, a)) x A)), a = 1.333 x height / width). RGBA8 targets at half the size of the view (the clamp and the quantization after each pass), linear clamp samples. |
| `M_SwcGrade` (`Scripts/import_sky.py`, `tools/import-sky.sh --grade`) | The screen blend of the source after the film curve and the sRGB step, before the photo filter: s = saturate(s + Rays x RayTint x (1 - s)). `Rays` (the target over the view) and `RayTint` default to black: with no tint the grade is as before. |

The switches: `swc.GodRays` 1/0 (0: no passes and no blend), `swc.GodRays.Debug` 0 the rays, 1 the mask, 2 the first pass (shown with
`swc.GradeDebug 3`: the target as it is), `swc.GodRays.Far` (m, default 14,336: the view distance of the mission).

Rules of this client (the study leaves them open):
- The CPU gate of the source rejects the effect when a camera-matrix dot with the light is greater than 0.45; the axis of that matrix is
  not proved. The client takes it as the back of the view: no rays (a black target) when f < -0.45. The shader also gives no RGB when
  f <= 0, as the source.
- f is the dot of the light direction with the view direction (the renderer basis of the source is not proved).
- The sky area test of the source (an occlusion query of at least 1 percent of the screen at the far depth) is not copied; the depth term
  of the mask already gives no RGB where there is no sky.
- The depth reprojection of a jittered frame (SunShafts.cfx lines 61 to 77) is not copied (the jitter of the temporal upscaler moves the
  depth by less than a pixel of the half-size mask).
- The far depth of the mask: the view distance of the mission (14,336 m); the far plane of a retail frame is not proved.
- The UE light shafts: the sun and the moon lights of the client never set `bEnableLightShaftOcclusion` or `bEnableLightShaftBloom`
  (the defaults are off): no second screen effect. The volumetric fog stays as it was (the source has both).
- The standalone shaft path of the source (the soft light of the shaft alpha) is not used: the merged HDR path has no consumer of the
  shaft amount.

### Check of the sun rays (development runs, 2026-10-21; evidence `~/Games/soulwarden-client/evidence/godrays/`)

- The log: at noon "rays: on, visibility 1, attenuation 1, influence 1, tint 0.70 0.56 0.44, the sun"; at night "visibility 2,
  attenuation 0.5, tint 0.78 0.98 1.56, the moon" (the base TOD of the study).
- The debug views (`m3/client/oxboro-road-sky-noon@mask`, `@blur1`, `@rays`): the mask has the sky white (it saturates in RGBA8) and
  the near objects black; the first pass has the steps of 8 samples; the rays fall off from the sun beyond the top right. In the Cleave
  forest view (heading north at noon) the target is black: the sun is behind the camera (f <= 0), as the source rule.
- The sweep (`godrays-sweep2.mp4`, `tests/drive/godrays-sweep.txt` at the Oxboro road): a full turn with the camera 40 degrees up at
  noon with the rays and without, the same at 00:00 for the moon. Toward the sun the rays give a warm haze from the sun and light bands
  across the tower, the tree, and the character (the mean change of the frame +4.6 of 255, +6.1 in the top right; `still-noon` frames
  15.5 s and 16.7 s, the same heading); the moon gets a glow and short rays (+4.2). No streak at the screen edges and no cut while the
  sun crosses the edge of the view or goes behind the camera.
- The matched views with each view also with `swc.GodRays 0` (`final4`, `final1`; the color report against retail, dE all): the Oxboro
  road at noon 3.1 (off 3.9), at night 11.1 (off 14.1); the Oxboro lane under the canopy at noon 4.1 (off 3.9), at night 9.5 (off 9.2);
  Cleave inside at noon 4.7 (off 4.6, the sun behind the camera); set 1 noon-w 3.0 (3.0), dusk-w 5.0 (4.9), night-w 4.3 (4.3).
- Close crops retail | client | client without rays (`crop-moon.png`, `crop-noon-sky.png`). The differences: the retail moon glow is
  about twice as large and brighter than the client glow (the client sky at night is also darker, L -13); the retail noon haze at the
  top of the tower is warm, the client haze is fainter and pink (the client sky color); the retail pictures were not made with the sun
  disc in the view (no retail sweep).
- The GPU time (`ProfileGPU`, quiet frames): at 1280x720 (a 640x360 target) the mask 0.008 ms, the passes 0.009 and 0.011 ms; at
  1920x1080 (960x540) the mask 0.009 to 0.010 ms, the passes 0.010 to 0.011 ms: about 0.03 ms for the three passes, plus one texture
  sample in the grade. The study estimated 0.15 to 0.6 ms at 1080p.

Open points: a retail capture with the sun disc in the view through leaves and a retail sweep (approved retail reference runs; not
made); the axis of the CPU gate and of f (above); the sky area test; the far plane of a retail frame; the regional TOD presets that
do not resolve (7 preset strings, `research/godrays/tod-audit.json`: the client keeps the volume blend of its data and adds no value).

## The matched views

`tools/match-view.sh SET [VIEW...] [--out FOLDER] [--label LABEL]` (`tools/lib/match_view.py`, client `Environment/SwcMatchView`)
makes the client pictures of a set of retail references in one development run, for the fidelity checks of each track (the world,
the materials, the environment). A set is a folder with `index.json` and, for each picture, `NAME.png` and `NAME.json` (the feet,
the camera, the field of view, the measured world hour), or its name in `~/Games/soulwarden/work/evidence/unreal-references`.

| Step | Rule |
| --- | --- |
| The plan | The tool writes `FOLDER/plan.json` (the views, 1280 x 720, the settle time) and starts the run with `-SwcMatchView=PLAN` and `-SwcWorldTimeProbe`, the backend with the development routes, the window on the own agent desktop. The creatures ignore the character (the night creatures of a place). |
| The place | The client logs `matchview: view K NAME: ready`; the tool moves the character to the feet (the travel route). At the feet (the server place and the pawn within 2 m), the client turns the body to the yaw of the feet and sets the camera override: the exact place and forward vector of the JSON camera (engine to Unreal: x, -y, z), the roll 0, and the vertical field of view of the JSON (57 degrees; the horizontal one is 87.97 degrees at 16:9). The camera manager of the player track applies the override with one hook line. The client hides the debug text and the HUD canvas (a high resolution screenshot has no Slate user interface). |
| The streaming | The cells of the partition loaded and no package loading for 1 s, else at most 25 s at the feet (the log has the number of the loading packages). Then the textures of the view load (StreamAllResources, at most 5 s). |
| The hour | The client logs `in place`; the tool sets the world hour of the backend 14 real seconds before the hour of the retail picture (the world-time route; the rate of the part of the cycle). The client waits for the new record, the settle time (4 s), and the hour of the picture on its own world clock (1 game second), and takes the picture (a high resolution screenshot of 1280 x 720). |
| The output | `FOLDER/client/NAME.png`, `FOLDER/side/NAME.png` (retail on the left, client on the right, with the two hours, the feet, the camera, and the field of view), `FOLDER/contact-sheet.png`, `FOLDER/report.json` (the client hour of each picture and its difference from the retail hour), the logs, and `clock-compare.txt` (`tools/lib/world_time_compare.py`: the client clock against the server). |

An interior with an hour override (the client hour is not the world hour, for example the keep of Dunwood: the client hour 0.0 at
the world hour 12:04) gets the world hour of `backend_world_time_after_picture` of its JSON when it differs from the client hour by
more than 0.25 h; the client then shows the hour of its volume.

### The color report

`tools/lib/color_report.py` (the decision of the user, 2026-10-19: the colors of the client copy the retail colors). For each view,
the client picture against its retail picture, for the whole picture and for 3 bands (the top third: the sky or the high walls; the
middle third; the bottom third: the ground): the mean and the spread of R, G, and B (sRGB), of the luminance (CIE L*), of the
saturation (HSV), and of the hue (the circular mean weighted by the saturation); the mean CIELAB color; the dark part (L* under 10)
and the bright part (L* over 90); the differences (client - retail) and the CIEDE2000 distance of the two mean colors. The character
is masked out of both pictures: the box of its feet and head (2 m high, 0.55 m to each side, a margin of 8 pixels), projected with
the camera of the JSON. The HUD of the retail pictures is hidden (F10), so it needs no mask. The output: `FOLDER/colors.json`,
`FOLDER/colors.txt` (one line for each view: the distance, the L* difference, the L* spread difference (the contrast), the
saturation difference, and the hue difference of each band, and the dark parts), and `FOLDER/color-sheet.png` (each side-by-side
picture with its numbers). `tools/match-view.sh --colors FOLDER` makes the report for an existing folder (the numbers before a
change). The ground band differs in most views until the terrain materials of the world track (T1): judge the sky, the walls, and
the foliage first.

The client picture has the hour of the retail picture. The weather of a retail picture is not measured (null in the set); the
client selects its own volumes (stage E3).

## Development aids

| Switch or command | Effect |
| --- | --- |
| The debug line | "world time HH:MM:SS (PHASE), hour H, phase of the server PHASE" on the screen, after the first record (with `-SwcDebugLines`, stage WF7). |
| `-SwcWorldTimeProbe` | The log line of the time each 1 s (else each 60 s, at each record, and at each phase edge). |
| `tools/lib/dev_world.py world-time [HH:MM[:SS]]` | The world time of the backend with the wall time of the reply (`wall`), or a command for the world (the development route `/v1/dev/world/time`; development data only). |
| `-SwcEnvironmentProbe` | The log line of the runtime environment each 1 s (the hour, the preset, the lights, the exposure). |
| `-SwcNoEnvironment` | The environment of the level (no runtime environment). |
| `-SwcMatchView=PLAN.json` | The matched views of a plan (tools/match-view.sh). |
| `tools/convert-environment.sh volumes X Y Z` | The time-of-day volumes that hold a place (engine m), the highest priority first. |
| `tools/convert-environment.sh sample PRESET HOUR [NAME...]` | The values of a preset at an hour. |
| `-SwcLightProbe` | The log line of the local lights each 1 s (else each 30 s): the active lights by type, the bound and the cookie lights, the lights in the view, the shadows. |
| `-SwcNoLocalLights`, `-SwcNoCookies`, `-SwcLightShadows=none\|source\|all` | No local lights; no cookies; the shadows of the local lights. |
| `swc.LightScale F`, `swc.LocalLights 0\|1`, `swc.LightShadows 0\|1\|2` | Console variables: the look scale of the local lights (`-SwcLightScale=F` at the start), the local lights hidden or on (a compare of the cost in one run), the shadows of the local lights (none, the source flags, all). |
| `swc.LumenTest 0\|1` | The Lumen test copies of the inn and the two houses (`-SwcLumenTest` at the start; `tools/lumen-test.sh` makes them). |
| `tools/match-view.sh ... --variant VIEW@NAME=COMMAND;...` | One more picture of a view with client console commands (run when the view is in place): a look value or a switch to compare in one run. The next view sets the console variables of the commands back to their values before the first variant (from 2026-10-19). The report and the side-by-side picture have the local lights at the shot and the median GPU time while the view is in place (with `-SwcFrameStats=2`). |
| `swc.Grade 0\|1`, `swc.GradeDebug 0\|1\|2`, `swc.GradeBloom F`, `swc.SourceBloom 0\|1`, `swc.GradeBias EV`, `swc.SourceMeter 0\|1\|2`, `swc.SunSource 0\|1` | The color grade of the source or the Unreal tone mapper; a check of the exposed value or of the exposure; the part of the Unreal bloom; an offset of the exposure (a look check); the meter: 2 the source meter of the frame (the default), 1 the Unreal meter with the source curve, 0 the Unreal luminance; the bloom mix of the source (1, the default) or the Unreal bloom added (0); the sun of the hour (1, the default) or the noon sun of E1 (0) (stage E5). |
| `swc.SunStep DEGREES`, `swc.SunStepShift M` | The step of the sun light (AST-273 P1, "The sun step"): the largest step (0.1; 0 a turn in each frame) and the shadow move for each meter of height (0.005). |
| `swc.SourceSky 0\|1`, `swc.CloudTint F` | The sky color of the source model or the SkyAtmosphere of Unreal; the cloud tint of the data (stage E5). |
| `swc.SkyLightSource 0\|1`, `swc.ProbeScale F` | The ambient: the world probe of the data (1) or the real-time capture of the Unreal sky (0); a look scale of the probe light (stage E5). |
| `swc.Fog`, `swc.FogColorMode`, `swc.FogLayers`, `swc.FogDensityScale`, `swc.FogDensityScale2`, `swc.VolumetricFog`, `swc.LocalFogScale` | The fog of stage E3 (the help text of each variable has its values). |
| `swc.Clouds`, `swc.CloudCover`, `swc.CloudBias`, `swc.StarScale`, `swc.MoonScale`, `swc.MoonSize`, `swc.SkyWavelengths`, `swc.SkyRayleigh`, `swc.NightSkyTint` | The clouds, the stars, the moon, and the sky color of stage E3. |
| `swc.RainScale`, `swc.DropLight` | The rain of stage E3. |
| `swc.SnowSurface`, `swc.SnowSurfaceGain`, `swc.SnowFall`, `swc.SnowLight`, `swc.SnowRoof` | The snow (AST-257): the surface snow on or off, a gain on its brightness (6), the volume flakes (0 by default; 1: the legacy count; not verified: the flakes did not show in a check run), the light of a flake, and the roof map. |
| `tools/convert-environment.sh sky\|snow\|probes`, `tools/import-sky.sh`, `tools/import-snow.sh` | The stars, the moon, the rain and snow textures, the textures of the surface snow, and the world probes; then the textures, the sky, precipitation, and grade materials, and the probe cubes (the import lock). |
| `tools/convert-environment.sh cookies`, `tools/import-cookies.sh` | The projector textures of the lights: the PNGs, then the textures, the light function material, and the material instances (the import lock). |
| `tools/lib/world_time_compare.py FOLDER LABEL` | The compare of the client clock (the log lines of `-SwcWorldTimeProbe`) with the records, the phase edges, and the reads of the server in one run; exit status 1 above 1 game second. |
