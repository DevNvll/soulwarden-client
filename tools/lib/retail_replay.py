#!/usr/bin/env python3
"""The replay of a retail reference take in the client (stage P3.G, AST-193): a drive script of the player controller and a
script of the development routes from the JSON of a take of set 6 (~/Games/soulwarden/work/evidence/unreal-references/
set6-combat/player-*.json): the keys of the retail harness at their video times, and before each part of the take a travel to
the feet of the retail character and the camera of its sample; the creatures of the take at their offsets from the feet.

Usage: python3 -B tools/lib/retail_replay.py TAKE.json OUT_PREFIX [--lead SECONDS] [--level N] [--ignore on|off] [--weapon 1|2] [--zoom M]
  OUT_PREFIX: writes OUT_PREFIX.txt (the drive) and OUT_PREFIX-routes.txt (the routes).
  --lead: the drive time of the first key (default 6.0 s); the drive time of a key is lead + its video time - the video time of
          the first key, thus the client video and the retail video align at the first key.
  --level: the level of the creatures (default 60, the level of the creatures of set 6).
  --ignore: the creatures ignore the character (default: on, then the ignore commands of the backend lines of the take at their
            times, with a longer lead when they come before the first key; with no command: on when the segment text says
            "ignore", else off).
  --weapon: the weapon key (1 or 2) 2.5 s before the first key, for a take that starts with that weapon in the hand (a weapon key
            of the weapon in the hand does nothing in the client).
  --zoom: the zoom option of the retail take (m; README.md of set 6 "The camera zoom": 3.2 for the player videos of the first run,
          2.0 for player-camera): a wheel step of 2.0 - M notches at 0.9 s (the client starts at 2.0).

The time of a key: the harness runs the keys of one list in order, and a tap "key 0xNN MS" lasts MS ms; the keys after a tap with the
same video time come MS ms later (the W of a dodge "keydown 0x57, key 0xA0 80, keyup 0x57" is down for the 80 ms of the tap).

The time of a key is the input step whose action bits show it (key_times; else the time of the harness). The keys (tools/hx of the
Soulwarden project): "key 0xNN MS" a tap (0x31 and 0x32 the weapon keys, 0xC0 tilde the sheathe,
0xA0 left Shift and 0x10 the generic Shift the dodge, 0x20 Space the jump), "keydown/keyup 0xNN" a hold (W S A D the move axes, 0xA2 left Ctrl the walk,
0x20 Space the jump), "mousedown/mouseup left|right" (the attack, the aim or the block), "look DX DY" (mouse counts).
"""
from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path

# The client counts for one unit of the look of the retail harness: 88.6 / 900 degrees for each unit, 0.025 degrees for each count.
LOOK_COUNTS = 88.6 / 900.0 / 0.025
MOVE_KEYS = {0x57: (0, 1), 0x53: (0, -1), 0x41: (-1, 0), 0x44: (1, 0)}
TAPS = {0x31: "key 1", 0x32: "key 2", 0xC0: "key sheathe", 0xA0: "dodge", 0x10: "dodge", 0x20: "jump"}


# The input bit of each key change (the input table of the research, combat-actions input-bits.json): the time of a key is the input
# step whose action bits show it first (the harness time can be late: the first W of player-move-drawn came 0.53 s after its harness
# time, the others 0.03 to 0.07 s).
KEY_BITS = {("keydown", 0x57): 416, ("keyup", 0x57): 416, ("keydown", 0x53): 327, ("keyup", 0x53): 327, ("keydown", 0x41): 288,
            ("keyup", 0x41): 288, ("keydown", 0x44): 540, ("keyup", 0x44): 540, ("key", 0xA0): 322, ("key", 0x10): 322, ("key", 0x20): 391,
            ("keydown", 0x20): 391, ("key", 0x31): 9, ("key", 0x32): 355, ("key", 0xC0): 428, ("mousedown", "left"): 555,
            ("mouseup", "left"): 567, ("mousedown", "right"): 484, ("mouseup", "right"): 562}


def key_times(take: dict) -> list[float]:
    """@return The video time of each key of the take: the first input step at or after the harness time - 0.05 s (to 0.8 s later) whose
    action bits show the key (its bit; for a key up of a move key, the step where its bit goes); else the harness time."""
    reports = sorted(take.get("input_reports") or [], key=lambda r: r["input"])
    out = []
    for key in take["keys"]:
        words = key["key"].split()
        code = words[1] if len(words) > 1 else ""
        bit = KEY_BITS.get((words[0], int(code, 16) if code.startswith("0x") else code))
        t = key["video_s"]
        window = [r for r in reports if t - 0.05 <= r["video_s"] <= t + 0.8] if bit is not None else []
        # A key up: the step where the bit goes, after a step with it (also one just before the window).
        down = any(bit in r["action_bits"] for r in reports if t - 0.2 <= r["video_s"] < t - 0.05) if window else False
        for report in window:
            has = bit in report["action_bits"]
            if (has and words[0] != "keyup") or (down and not has and words[0] == "keyup"):
                t = report["video_s"]
                break
            down = down or has
        out.append(t)
    return out


def sample_before(samples: list[dict], video_s: float) -> dict:
    """@return The last client sample at or before a video time (else the first one)."""
    best = samples[0]
    for sample in samples:
        if sample["video_s"] <= video_s + 1e-6:
            best = sample
        else:
            break
    return best


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("take")
    parser.add_argument("out")
    parser.add_argument("--lead", type=float, default=6.0)
    parser.add_argument("--level", type=int, default=60)
    parser.add_argument("--ignore", choices=("on", "off"))
    parser.add_argument("--weapon", choices=("1", "2"))
    parser.add_argument("--zoom", type=float, default=2.0)
    args = parser.parse_args(argv)
    take = json.loads(Path(args.take).read_text())
    keys = take["keys"]
    samples = sorted(take["client_samples"], key=lambda s: s["video_s"])
    if not keys or not samples:
        print(f"retail-replay: {args.take} has no keys or no samples", file=sys.stderr)
        return 1
    # The key times of the game (the input bits), the first key the origin of the clock.
    times = key_times(take)
    for key, t in zip(keys, times):
        key["video_s"] = t
    first = keys[0]["video_s"]
    # The ignore commands of the take (its backend lines): the routes give them at the same times. A fight that starts before the
    # first key needs a longer lead: the travel at 2.5 s, the creature at 4.5 s, the first command at 5.5 s or later.
    commands = [(line["video_s"], "off" if "do not ignore" in line["text"] else "on") for line in take.get("backend_lines") or []
                if "world: command: the creatures" in line.get("text", "") and "ignore character" in line.get("text", "")]
    lead = max(args.lead, max((5.5 - (t - first) for t, _ in commands), default=args.lead))
    args.lead = round(lead, 1)

    def drive_time(video_s: float) -> float:
        return round(args.lead + video_s - first, 3)

    # The parts: the first key of each part (a travel and the camera before it).
    parts: list[tuple[str, float]] = []
    for key in keys:
        part = key.get("part") or "part1"
        if not parts or parts[-1][0] != part:
            parts.append((part, key["video_s"]))
    segment = " ".join(take.get("segments") or []) if isinstance(take.get("segments"), list) else str(take.get("segments") or "")
    ignore = args.ignore or ("on" if commands or "ignore" in segment else "off")
    creatures = sorted(((cid, c) for cid, c in (take.get("server_creatures") or {}).items()),
                       key=lambda item: item[1]["rows"][0][0] if item[1].get("rows") else 0.0)

    drive: list[tuple[float, str]] = []
    routes: list[tuple[float, str]] = [(0.5, f"ignore {ignore}"), (0.6, "world-time 12:00")]
    spawned = 0
    for index, (part, start) in enumerate(parts):
        # The place of the first part: before the first ignore command when the fight starts before the first key.
        place_at = min([start] + [t for t, _ in commands]) if index == 0 else start
        feet_sample = sample_before(samples, place_at - 0.05)
        feet = feet_sample["feet"]
        travel_at = 2.5 if index == 0 else max(0.8, drive_time(start) - 3.5)
        routes.append((travel_at, f"travel {feet[0]:.3f} {feet[1]:.3f} {feet[2] + 0.4:.3f}"))
        drive.append((round(drive_time(start) - 1.0, 3), f"view {feet_sample['camera_heading']:.2f} {feet_sample['camera_pitch']:.2f}"))
        # The creature of this part: the creature whose first row is before the next part (a new wolf before each part).
        end = parts[index + 1][1] if index + 1 < len(parts) else math.inf
        for cid, creature in creatures:
            first_row = creature["rows"][0] if creature.get("rows") else None
            if first_row is None or not (first_row[0] < end and first_row[0] >= (start - 30.0 if index == 0 else parts[index - 1][1])):
                continue
            if spawned:
                routes.append((travel_at + 0.2, f"kill ${spawned}"))
            # The spawn 2.0 s after the travel: the offsets of the route are from the place of the character in the backend, which
            # is its landed place only after the drop of the travel (0.4 m) and its next input reports.
            # dz: the height above the floor at the place of the creature (the backend puts a creature on the floor of the navigation
            # plus dz when the character stands on it; the retail commands of set 6 used 0.4 m: "creature spawn Wolf_Grey_7 CHARACTER
            # 0 2.6 0.4").
            dx, dy = first_row[1] - feet[0], first_row[2] - feet[1]
            routes.append((travel_at + 2.0, f"spawn {creature['kind']} {dx:.2f} {dy:.2f} 0.40 --level {args.level} --yaw {first_row[4]:.0f}"))
            spawned += 1
            break
    if not args.ignore:
        routes += [(drive_time(t), f"ignore {state}") for t, state in commands if drive_time(t) >= 5.5]

    if abs(args.zoom - 2.0) > 1e-3:
        drive.append((0.9, f"wheel {2.0 - args.zoom:.2f}"))
    if args.weapon:
        drive.append((round(drive_time(first) - 2.5, 3), f"key {args.weapon}"))
    axes = {code: False for code in MOVE_KEYS}
    group, shift = None, 0.0
    for key in keys:
        # The taps before a key in the same list of the harness (the same video time) delay it.
        if group is None or abs(key["video_s"] - group) > 0.001:
            group, shift = key["video_s"], 0.0
        t = round(drive_time(key["video_s"]) + shift, 3)
        words = key["key"].split()
        if words[0] == "key" and len(words) >= 3:
            shift += float(words[2]) / 1000.0
        verb = words[0]
        if verb == "key" and len(words) >= 2:
            code = int(words[1], 16)
            drive.append((t, TAPS.get(code, f"tick  # an unknown tap {words[1]}")))
        elif verb in ("keydown", "keyup") and len(words) >= 2:
            code = int(words[1], 16)
            down = verb == "keydown"
            if code in MOVE_KEYS:
                axes[code] = down
                x = sum(MOVE_KEYS[c][0] for c, held in axes.items() if held)
                y = sum(MOVE_KEYS[c][1] for c, held in axes.items() if held)
                drive.append((t, f"move {x} {y}"))
            elif code == 0xA2:
                drive.append((t, f"walk {1 if down else 0}"))
            elif code in (0x20, 0xA0) and down:
                drive.append((t, TAPS[code]))
        elif verb in ("mousedown", "mouseup") and len(words) >= 2:
            down = verb == "mousedown"
            drive.append((t, f"{'attack' if words[1] == 'left' else 'aim'} {1 if down else 0}"))
        elif verb == "look" and len(words) >= 3:
            # The harness of the retail client moves the pointer in steps of 30 units each 20 ms (game-hook harness.cpp); 900 units
            # turned the retail camera 88.6 degrees (README of set 6, player-move-drawn), the client turns 0.025 degrees for each count
            # (docs/CAMERA.md): the same angles in the same steps.
            dx, dy = int(words[1]), int(words[2])
            steps = max(1, -(-max(abs(dx), abs(dy)) // 30))
            for step in range(1, steps + 1):
                ux = dx * step // steps - dx * (step - 1) // steps
                uy = dy * step // steps - dy * (step - 1) // steps
                drive.append((round(t + 0.02 * (step - 1), 3), f"look {ux * LOOK_COUNTS:.1f} {uy * LOOK_COUNTS:.1f}"))
    end_time = drive_time(max(k["video_s"] for k in keys)) + 4.0
    # The ticks: the clock of the route script (a route waits for the next drive line).
    tick = 1.0
    while tick < end_time:
        drive.append((tick, "tick"))
        tick += 1.0
    drive.sort(key=lambda item: item[0])
    routes.sort(key=lambda item: item[0])
    head = (f"# The replay of the retail take {Path(args.take).name} (stage P3.G, AST-193; tools/lib/retail_replay.py): the keys of the\n"
            f"# retail harness at their video times (the first key at {args.lead:.1f} s of the drive clock, video {first:.3f} s), a view before\n"
            f"# each of its {len(parts)} part(s). The routes: {Path(args.out).name}-routes.txt. Times in seconds from the possession.\n")
    Path(args.out + ".txt").write_text(head + "".join(f"{t:.3f} {command}\n" for t, command in drive))
    Path(args.out + "-routes.txt").write_text(
        f"# The routes of the replay of {Path(args.take).name} (tools/lib/retail_replay.py): the creatures ignore the character ({ignore}),\n"
        f"# the hour 12:00, a travel to the retail feet before each part, the creatures of the take at their retail offsets (level {args.level}).\n"
        f"# Times on the clock of the drive script.\n" + "".join(f"{t:.3f} {command}\n" for t, command in routes))
    print(f"retail-replay: {len(drive)} drive steps, {len(routes)} routes, {len(parts)} part(s), {spawned} creature(s), "
          f"the last key at {end_time - 4.0:.1f} s")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
