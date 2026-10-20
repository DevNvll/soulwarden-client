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
| The sun | A movable directional light, atmosphere light 0, always on, on the sun path of the public engine with the mission angles (SunRotation 215, Longitude 130): the angle (hour + 12) / 24 x 2 pi about Z, the longitude about X, the rotation about Y, then the axes (x, -z, y). The sun rises at 06:00 and sets at 18:00; at noon it is toward -0.369, 0.527, 0.766 (north-west, 50 degrees high). The retail noon pictures agree: in noon-road-low (camera to the west) the shadow of the character goes to the east-south; in noon-wall-grass the sun lights a wall face to the north-west. Its illuminance and color are the noon "Sun intensity" (100,000 lux in Everfall) and "Sun color" of the preset (the color with its luminance kept, Rec. 709). |
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
| The sun and the moon | The Lighting and Moon elements of the menu mission (`levels/nw_frontend_nighthaven/level.pak:mission_mission0.xml`): SunRotation 190, Longitude 110, the moon size 0.5. The world mission has 215, 130, and 0.8, and the preset has no sun path, so the scene record carries them (`tools/convert-frontend.sh scene`: "environment"). |
| The sky | The cloud cover of the sky slice of the TOD entity (`slices/sky/skydome_tropicalstorm_post_a.dynamicslice`, in `sky/sky.json`). Each layer of the blend now has its sky slice (a volume layer: the slice of its volume; the base layer of the world: none, as before). |
| The rules | All rules of E1 to E5 are the ones of the world (the sun, the sky, the source fog, the exposure, the grade). The world probe, the local probes, the fog volumes, and the precipitation are the data of the world at the camera place. The probes of the menu level (`frontendv2_cm.dds`) are not converted. |

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
- The optional retail clamp of the sun path (research, open point 2): the noon shadows agree with the public rule.

## The local lights (stage E2)

`Environment/SwcLocalLights` (`USwcLocalLightSubsystem`, the runtime) with the plain rules of `Environment/SwcLocalLightRules`
(tested by `tools/test-environment.sh`). The data: `tools/convert-environment.sh write` writes `lights/REGION.json` (the compact
lights of each region), and `tools/convert-environment.sh cookies` writes the projector textures; `tools/import-cookies.sh` imports
them. The decision of the user (2026-10-18): the places, the colors, and the ranges come from the data; shadows may be on where
they look better and cost little.

| Part | Rule (source) |
| --- | --- |
| The data | The light components of the placement walk of E0 (the chunks, the capitals, the slices that they spawn at the start): the type (`LightType` 0 point, 1 area, 2 projector; 3, the probes, are not lights), the place, the +X axis (the direction of a projector and of an area light: 988 of the 1,335 projectors of r_+04_+02 have +X down) and the +Z axis, `COLOR`, `DiffuseMultiplier`, `SpecMultiplier`, the range (`PointMaxDistance`, `ProjectorDistance`, `AreaMaxDistance`), the bulb (`PointAttenuationBulbSize`, `ProjectorAttenuationBulbSize`), `ProjectorFOV`, `AreaWidth` and `AreaHeight`, the projector texture, `OnInitially`, the shadow flag (`CastShadowsSpec` not `0xFFFFFFFF`, EngineSpec Never), the animation, and the view distance cap. A light with `Visible` false is not in the file (the light rigs of the mission NPC masters and the helpers of the level design). The world: 76,723 visible lights (57,323 point, 16,394 projector, 3,006 area), 1,383 with the shadow flag, 149 in NPC slices; 863 in 300 m about the Oxboro point. The research counts 76,717 visible lights that are on at the start; the client keeps a light that is off at the start (`OnInitially` false) off. |
| The intensity | The source attenuation of the bulb size r is 1 / (1 + d / r)^2 at the distance d from the bulb surface, that is r^2 / D^2 at the distance D from the center: the falloff of an Unreal sphere light with the source radius r. The light unit of the public engine is 10,000 cd/m2 (`RENDERER_LIGHT_UNIT_SCALE`; `TimeOfDay.cpp`: the illuminance of the sun / 10,000 / pi is its render color, and the diffuse shading of the source has no 1 / pi). So a point light and a spot light have pi x 10,000 x `DiffuseMultiplier` x r^2 candela (r at least 0.01 m), with `COLOR` (linear) as the light color; an area light has the same surface luminance (10,000 x `DiffuseMultiplier` cd/m2) over its area. The attenuation radius is the range. `SpecMultiplier` is the specular scale (an inference: the multiplier of the specular of the light). A look scale multiplies all intensities: 3 by default (the Unreal way, a look choice): with the moon of the data (3,000 to 5,000 lux by night, E1) and the auto exposure, the scale 1 gave lamp pools that were weaker than in the retail night pictures (night-n: the gate lamps, the pillar faces, the ground), 3 gave about the balance of the retail pictures, and 10 made the windows glare (`-SwcLightScale=`, console variable `swc.LightScale`). |
| The types | A point light is an Unreal point light. A projector is a spot light along +X with the outer cone `ProjectorFOV` / 2 (the full angle of the source) and no inner cone. An area light is a rect light along +X (`AreaWidth` x `AreaHeight`) with no barn doors (it lights its whole front half space). |
| The cookies | A projector with a texture (not the default spot texture) gets a light function: the material instance of its texture (`/SwcGameContent/Environment/Cookies`, parent `M_SwcCookie`: the texture on the emissive color). Unreal maps the UV of the light function of a spot light from one side of the outer cone to the other, that is over the frustum of `ProjectorFOV`. 44 textures (2 textures of the data are not in the game copy: 13 lights have no cookie). The textures are linear (the source formats are BC4 and BC2 UNORM). |
| The streaming | All lights are read on a worker thread at the start. The static lights are in a grid of 64 m cells. Each 0.25 s, a light comes in its draw distance plus 10 m and goes at its draw distance plus 30 m (at most 128 new lights in one update, the nearest first). The draw distance is 25 times the range, at least 50 m and at most 250 m, and not more than the view distance cap of the source; Unreal fades the light in the last 20 % of it (a choice of this client for the cost: the source rule of the view distance is in the client code). |
| The shadows | The source flags (`-SwcLightShadows=none|source|all`). |
| The owners | A light in an NPC slice follows its creature: a creature of the same slice path near the static place of the slice root (30 m) gets the light, attached with its matrix in the frame of the slice. A light with no such creature stays off. The camp lights are static props of the level data (the fires, the lanterns of the camps): always on. |
| The hour | No switch by the hour: the data has none (`TODInfluence` alone is not a switch; research), and the retail pictures show the lamps on at noon and at night. `USwcEnvironmentSubsystem::GetNightFactor()` (0 by day, 1 by night: the weight of the moon) is a read value for the emissive of the fixtures (the materials track, stage M4). |
| The light styles | `AnimIndex` and `AnimSpeed` select a light style of the client code (no table in the data): the lights are steady. |
| The emissive of the fixtures | The materials track (stage M4) owns the emissive materials. The agreement (2026-10-18, through the orchestrator): the light unit of the lights is 10,000 cd/m2 for one source unit, and the emissive of M4 uses the same look factor: it reads the console variable `swc.LightScale` (3), so that a fixture and its light stay in balance. |

The Unreal way kept on purpose (the decision of the user):

- The intensity has a look scale of 3 over the light unit of the source (see above). The places, the colors, and the ranges are the
  data.
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
| The clouds | The volumetric clouds of the engine (`m_SimpleVolumetricCloud_Inst`). The cover of the sky slice of the place (`sky/sky.json`: 1 - the product of (1 - the mean of each cloud mask of its dome)) changes the coverage of the cloud material from the Everfall sky (0.6663) up and down (`swc.CloudCover`, `swc.CloudBias`: look values). |
| The fog | The "Volumetric fog 2" set of the data by the rule of the source (stage E5 step 3, below; `swc.SourceFog 0`: the fog of E3, the height fog of Unreal with the legacy fog layers and the "Volumetric fog 2" layers as its two layers and the color of the SkyAtmosphere). The fog volumes of the data are Unreal local fog volumes. |
| The rain and the snow | A post-process material before the tone mapping (`M_SwcPrecipitation`): the scene rain of the source (stage E5 step 3, below) and the snowflakes of the engine atlas. The amount comes from the rain and snow volumes of the data at the camera (the full amount in the inner radius, 0 at the outer radius); the flakes have a part of the light of a white surface (`swc.DropLight`). |

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

The client (`Environment/SwcEnvironmentSubsystem.cpp`, `M_SwcGrade` of `Scripts/import_sky.py`):

- The meter is the Unreal meter that ignores the materials (`r.AutoExposure.IgnoreMaterials 1`, the luminance over the base color
  times lerp(0.18, 1, 0.01)), with the Rec. 709 weights (`r.AutoExposure.LuminanceMethod 2`) and the widest window (1 % to 99 %).
  The bias curve of the Unreal auto exposure holds the whole rule of the source (`env::source_exposure_bias`, tested): at the
  metered EV100 of Unreal, the bias gives 1 / (1.2 x 2^EV). The bounds of Unreal stay wide. The renderer bakes the curve into a
  table only for a new curve object: the client makes a new curve when the rule changes (a change of the keys was not seen).
- `M_SwcGrade` replaces the tone mapper: the scene times the exposure, then the source order (saturation and balance, curve, sRGB,
  photo filter). The bloom is the bloom of Unreal (PostProcessInput2), added as the Unreal tone mapper adds it.
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

### Not done, open points (E5)

- The bloom weight of the source (`HDRBloomColor`: the final scene mixes the scene toward the bloom texture by
  saturate(HDRBloomColor)) is not known: the client keeps the bloom of Unreal (about 2 L* at noon-n).
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
| `swc.Grade 0\|1`, `swc.GradeDebug 0\|1\|2`, `swc.GradeBloom F`, `swc.GradeBias EV`, `swc.SourceMeter 0\|1` | The color grade of the source or the Unreal tone mapper; a check of the exposed value or of the exposure; the part of the Unreal bloom; an offset of the exposure (a look check); the meter of the source or the Unreal luminance meter (stage E5). |
| `swc.SourceSky 0\|1`, `swc.CloudTint F` | The sky color of the source model or the SkyAtmosphere of Unreal; the cloud tint of the data (stage E5). |
| `swc.SkyLightSource 0\|1`, `swc.ProbeScale F` | The ambient: the world probe of the data (1) or the real-time capture of the Unreal sky (0); a look scale of the probe light (stage E5). |
| `swc.Fog`, `swc.FogColorMode`, `swc.FogLayers`, `swc.FogDensityScale`, `swc.FogDensityScale2`, `swc.VolumetricFog`, `swc.LocalFogScale` | The fog of stage E3 (the help text of each variable has its values). |
| `swc.Clouds`, `swc.CloudCover`, `swc.CloudBias`, `swc.StarScale`, `swc.MoonScale`, `swc.MoonSize`, `swc.SkyWavelengths`, `swc.SkyRayleigh`, `swc.NightSkyTint` | The clouds, the stars, the moon, and the sky color of stage E3. |
| `swc.RainScale`, `swc.SnowScale`, `swc.DropLight` | The rain and the snow of stage E3. |
| `tools/convert-environment.sh sky\|probes`, `tools/import-sky.sh` | The stars, the moon, the rain and snow textures, and the world probes; then the textures, the sky, precipitation, and grade materials, and the probe cubes (the import lock). |
| `tools/convert-environment.sh cookies`, `tools/import-cookies.sh` | The projector textures of the lights: the PNGs, then the textures, the light function material, and the material instances (the import lock). |
| `tools/lib/world_time_compare.py FOLDER LABEL` | The compare of the client clock (the log lines of `-SwcWorldTimeProbe`) with the records, the phase edges, and the reads of the server in one run; exit status 1 above 1 game second. |
