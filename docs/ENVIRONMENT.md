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

## Development aids

| Switch or command | Effect |
| --- | --- |
| The debug line | "world time HH:MM:SS (PHASE), hour H, phase of the server PHASE" on the screen, after the first record. |
| `-SwcWorldTimeProbe` | The log line of the time each 1 s (else each 60 s, at each record, and at each phase edge). |
| `tools/lib/dev_world.py world-time [HH:MM[:SS]]` | The world time of the backend with the wall time of the reply (`wall`), or a command for the world (the development route `/v1/dev/world/time`; development data only). |
| `tools/convert-environment.sh volumes X Y Z` | The time-of-day volumes that hold a place (engine m), the highest priority first. |
| `tools/convert-environment.sh sample PRESET HOUR [NAME...]` | The values of a preset at an hour. |
| `tools/lib/world_time_compare.py FOLDER LABEL` | The compare of the client clock (the log lines of `-SwcWorldTimeProbe`) with the records, the phase edges, and the reads of the server in one run; exit status 1 above 1 game second. |
