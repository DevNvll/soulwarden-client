# Feature parity with the official client

Request of the user (2026-10-18): "plan a feature parity with official client". The full plan is the research of
AST-155: `~/Games/soulwarden-client/research/parity.md` (outside the repository: it names game files). This file keeps
the structure, the order, and the decisions. The status of each package is in Linear (project "Soulwarden Client",
P-AST-7).

## The rules of the plan

- Parity means the same playable behavior and UI on the unchanged Soulwarden backend, with the original Lua scripts,
  the streamed open world, and the same characters. The look comes last.
- A screen that shows before its server operation works is screen coverage, not gameplay parity.
- A package that needs a backend system names its Soulwarden roadmap id as a gate (`B ...`). The client does not
  invent a server rule.
- Each package has a check: a picture or a video (a movement needs a video), a test with bytes that the backend makes,
  and for the feel and the UI a side-by-side comparison with the retail client and the opinion of the user.
- Sizes are planning estimates: S one stage run, M two or three, L four or more, XL open.

## Milestones after milestone 1

| Milestone | Content | Packages | Size | Linear |
| --- | --- | --- | --- | --- |
| P2 | First fight loop and essential UI: equip and draw a weapon, attack, dodge, hits and effects, loot, death, respawn | 10 (5 M, 5 L), at least 30 runs | L | AST-156 to AST-165 |
| P3 | Player systems and combat families: character screens, stances, gear rules, all weapon families, the combat matrix | 10 (2 M, 5 L, 3 XL) | XL | AST-166 |
| P4 | World and story: collision and world defects, quest-driven world, vegetation, water, time and weather, map, travel, persons, quests, cutscenes | 10 (1 M, 8 L, 1 XL) | XL | AST-167 |
| P5 | Ordinary economy loop: loot, gathering, crafting, salvage and repair, shops, storage, trading post, standing, factions | 10 (3 M, 6 L, 1 XL) | XL | AST-168 |
| P6 | Extended solo systems: cosmetics, housing, mounts, achievements, fishing, music, seasons, the introduction, settings | 10 (1 M, 5 L, 4 XL) | XL | AST-169 |
| P7 | Instances and events: level transfer, expeditions, raids, trials, arenas, breaches, world events | 6 (2 M, 1 L, 3 XL) | XL | AST-170 |
| P8 | Social play and large modes, only on a request of the user | 9 (2 M, 4 L, 3 XL) | XL | AST-171 |
| P9 | Appearance and final closure: materials, foliage, water, cloth, cinematics, audio, packaging | 6 (3 L, 3 XL) | XL | AST-172 |

Total: 71 packages; the 53 sized packages need at least 180 stage runs, plus 18 XL packages. This is a lower bound,
not a schedule. Milestone 1 and the backend work are not in it.

## Decisions for the user (open)

| Id | Decision | Proposed default | Needed before |
| --- | --- | --- | --- |
| D1 | What counts as parity | The reachable gameplay and UI of build 1.400.6031.6004151 on the Soulwarden backend; no public services, no diagnostic screens; dormant modes only after a reachability proof | the stage issues of P3 and later |
| D2 | Input devices, language, displays | Keyboard and mouse, the game language, the resolutions of the user | P2.3, P6.9 |
| D3 | Activities with other participants | Solo parity first, social play last; all connections local | P5.7, P8 |
| D4 | Audio target and playback route | Silence for now; a separate study after the choice | P9.5, music |
| D5 | When the look is worth its cost | The simple Unreal look until the behavior and the UI pass | P9.1 to P9.4 |
| D6 | Calendar content offline | A fixed set of seasons and events, or a backend-approved schedule | P6.7, P7.5 |
| D7 | Evidence to accept a milestone | Reference flows, frame-rate and disk limits, the source of the retail videos | the acceptance of P2 |
