# The environment of the world

The runtime part of the world look (the milestone "WF World fidelity" of the roadmap): the time of day of the backend clock, and
later the sun, the sky, the fog, the exposure, the local lights of the slices, and the water. The world track does the terrain and the
mesh materials. The stages of the environment come from the research plan (`~/Games/soulwarden-client/research/world-environment.md`)
and the orchestrator.

The sources: Soulwarden `docs/systems/world-time-and-weather.md` and the protocol notes 4.43 (read only), the research
`~/Games/soulwarden/work/research/world-time.md` (section 2: the client code of the time), and the backend encoders
(`apps/backend/src/server/rep/time_messages.cpp`, `world/world_time.cpp`).

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
| The exposure | An unbound post process over the fixed exposure of the level: the Unreal auto exposure (histogram) with the bounds EV100 3 to 16 and a compensation of -2.5 by night, faded in with the moon (a night look; `-SwcNightExposure=EV`). Stage E5 refines it; the user judges the look. |
| The level | The sun, the sky atmosphere, and the sky light of the level of the world track are turned off while the runtime environment runs; its fog and its ocean stay (stages E3 and E4). |

The Unreal way kept on purpose (the decision of the user):

- The sun is not the dominant light of the source (one light that changes to the moon at night and lights from below the horizon until
  the visual dusk at 19:11): it is the physical sun of the SkyAtmosphere, always on; its transmittance makes the colors of the sunrise
  and the sunset in place of the dawn and dusk keys of "Sun color". The moon comes when this sun goes (from 18:00), not at 19:11:
  with the source rule the scene was black from 18:50 to 19:11 (the twilight of the physical sky is over).
- The sky is the SkyAtmosphere, not the sky shader and the night sky colors of the source; the ambient is the real-time sky light and
  Lumen, not the baked probes and their multipliers.
- The exposure is the Unreal auto exposure with wide bounds, not the EV bounds and the film curve of the source.

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
| `tools/match-view.sh ... --variant VIEW@NAME=COMMAND;...` | One more picture of a view with client console commands (run when the view is in place): a look value or a switch to compare in one run. The report and the side-by-side picture have the local lights at the shot and the median GPU time while the view is in place (with `-SwcFrameStats=2`). |
| `tools/convert-environment.sh cookies`, `tools/import-cookies.sh` | The projector textures of the lights: the PNGs, then the textures, the light function material, and the material instances (the import lock). |
| `tools/lib/world_time_compare.py FOLDER LABEL` | The compare of the client clock (the log lines of `-SwcWorldTimeProbe`) with the records, the phase edges, and the reads of the server in one run; exit status 1 above 1 game second. |
