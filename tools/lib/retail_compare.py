#!/usr/bin/env python3
"""The comparison of a client replay with its retail take (stage P3.G, AST-193; tools/lib/retail_replay.py makes the replay):
the feet of both sides on the time from the first key (the retail video time; the client drive clock, from the lines "drive: X s"
and "move: sample" of the client log, -SwcMoveSamples=0.02), and for each move segment (a move key down to the next move key) the
steady speed, the time to 90 % of it, the stop time and the stop distance after the key up, and the distance of the segment.

With --backend BACKEND_LOG (the backend log of the client run): the events of the backend lines of both sides on the same clock
(the hits, the dodges, the stamina payments, the refused dodges; the retail backend_lines of the take), and the distance of the
feet in the first 0.6 s of each dodge.

Usage: python3 -B tools/lib/retail_compare.py TAKE.json CLIENT_LOG [--lead SECONDS (default: the header of the drive)] [--backend BACKEND_LOG] [--json OUT]
"""
from __future__ import annotations

import argparse
import json
import math
import re
import sys
from datetime import datetime, timezone
from pathlib import Path

STAMP = re.compile(r"^\[(\d{4})\.(\d\d)\.(\d\d)-(\d\d)\.(\d\d)\.(\d\d):(\d{3})\]")
SAMPLE = re.compile(r"move: sample (\S+), ([^,]*), ([0-9.]+) m/s, feet ([0-9.-]+) ([0-9.-]+) ([0-9.-]+) m")
DRIVE = re.compile(r"drive: ([0-9.]+) s ")


def wall(line: str) -> float | None:
    m = STAMP.match(line)
    if not m:
        return None
    y, mo, d, h, mi, s, ms = map(int, m.groups())
    # The stamps of the client log are UTC.
    return datetime(y, mo, d, h, mi, s, ms * 1000, tzinfo=timezone.utc).timestamp()


WORLD = re.compile(r"\(world ([0-9.]+) s\)")


def client_track(log: Path, lead: float) -> list[tuple[float, float, float, float, str]]:
    """@return (time from the first key, x, y, z, state) of the client samples. The clock: the game time of the lines ("(world T
    s)"; the drive clock is the game time from the possession), else the wall time of the log line between the drive lines."""
    drive, rows = [], []
    for line in log.read_text(errors="replace").splitlines():
        world = WORLD.search(line)
        t = float(world.group(1)) if world else wall(line)
        if t is None:
            continue
        m = DRIVE.search(line)
        if m:
            drive.append((t, float(m.group(1))))
        m = SAMPLE.search(line)
        if m:
            rows.append((t, float(m.group(4)), float(m.group(5)), float(m.group(6)), m.group(1).replace("ESwcMove::", "")))
    drive.sort()

    def clock(t: float) -> float:
        for (t0, d0), (t1, d1) in zip(drive, drive[1:]):
            if t0 <= t <= t1:
                return d0 + (d1 - d0) * (t - t0) / (t1 - t0) if t1 > t0 else d0
        t0, d0 = drive[0] if t < drive[0][0] else drive[-1]
        return d0 + t - t0
    return [(clock(t) - lead, x, y, z, state) for t, x, y, z, state in rows]


BACKEND_STAMP = re.compile(r"^(\d\d):(\d\d):(\d\d)\.(\d{3}) (.*)$")
# The events of the backend lines (the same backend code on both sides): the kind and its pattern. The first group is the key of
# the pair (a retail event and a client event of the same kind and key), the groups together the text.
EVENTS = [("hit", re.compile(r"world: hit: \S+ hurts creature .*? with (?!no item)\S+ \((?:light|heavy) attack, (\S+ \S+),")),
          ("dodge", re.compile(r"world: dodge: \S+ starts a dodge \((\S+),")),
          ("pay", re.compile(r"world: stamina: \S+ pays ([0-9.]+ stamina \([^)]*\)), stamina ([0-9.]+ to [0-9.]+)")),
          ("refused", re.compile(r"world: dodge: \S+, input \d+: no dodge of the server: (.*?) \[")),
          ("replaced", re.compile(r"world: dodge: \S+: an attack at ([0-9.]+) s replaces the dodge")),
          ("shot", re.compile(r"world: projectile \d+: \S+ launches shot (\S+)")),
          ("arrow", re.compile(r"world: projectile \d+ of \S+ ends (after) ([0-9.]+ m and \d+ ms) at [0-9. ]+: (.*)$")),
          ("bitten", re.compile(r"world: hit: creature \S+ \(\S+, level \d+\) hurts \S+ with (\S+): ([0-9.]+ damage).*?(health [0-9.]+ to [0-9.]+)")),
          ("dead", re.compile(r"world: character \S+ \(\S+\) (is dead)")),
          ("respawn", re.compile(r"world: respawn of character \S+ \(\S+\): (it lives again) (with the health [0-9.]+ at .*?) \["))]


def event_of(text: str) -> tuple[str, str, str] | None:
    """@return (kind, key, text) of an event line, or None."""
    for kind, pattern in EVENTS:
        m = pattern.search(text)
        if m:
            return kind, m.group(1), " ".join(m.groups())
    return None


def client_events(client_log: Path, backend_log: Path, lead: float) -> list[tuple[float, str, str, str]]:
    """@return (time from the first key, kind, key, text) of the events of the backend log of the client run. The clock: the wall time
    of the drive lines of the client log (UTC) against the local wall time of the backend lines."""
    drive = []
    day = None
    for line in client_log.read_text(errors="replace").splitlines():
        m = DRIVE.search(line)
        t = wall(line)
        if t is not None and day is None:
            day = datetime.fromtimestamp(t).date()
        if m and t is not None:
            drive.append((t, float(m.group(1))))
    if not drive or day is None:
        return []
    drive.sort()

    def clock(t: float) -> float:
        for (t0, d0), (t1, d1) in zip(drive, drive[1:]):
            if t0 <= t <= t1:
                return d0 + (d1 - d0) * (t - t0) / (t1 - t0) if t1 > t0 else d0
        t0, d0 = drive[0] if t < drive[0][0] else drive[-1]
        return d0 + t - t0
    out = []
    for line in backend_log.read_text(errors="replace").splitlines():
        m = BACKEND_STAMP.match(line)
        if not m:
            continue
        found = event_of(m.group(5))
        if found is None:
            continue
        h, mi, s, ms = map(int, m.groups()[:4])
        t = datetime(day.year, day.month, day.day, h, mi, s, ms * 1000).timestamp()
        out.append((clock(t) - lead, found[0], found[1], found[2]))
    return out


def retail_events(take: dict, first: float) -> list[tuple[float, str, str, str]]:
    out = []
    for line in take.get("backend_lines") or []:
        found = event_of(line.get("text", ""))
        if found is not None:
            out.append((line["video_s"] - first, found[0], found[1], found[2]))
    return out


def feet_after(track: list[tuple], start: float, span: float = 0.6) -> float | None:
    """The horizontal distance of the feet from start to start + span (s; 0.6 s: the dodge). The client logs no sample at rest: the
    place at a time is the last sample before it, if the track goes on after it."""
    before = [row for row in track if row[0] <= start]
    after = [row for row in track if row[0] <= start + span]
    if not before or not after or track[-1][0] < start + span:
        return None
    a, b = before[-1], after[-1]
    return math.hypot(b[1] - a[1], b[2] - a[2])


def client_moves(log: Path, lead: float) -> list[float]:
    """The times of the drive lines "move" of a client log (the drive clock of the frame that ran them, from the first key)."""
    out = []
    for line in log.read_text(errors="replace").splitlines():
        m = re.search(r"drive: ([0-9.]+) s move ", line)
        if m:
            out.append(float(m.group(1)) - lead)
    return out


def speed_at(track: list[tuple], index: int, span: float = 0.1) -> float:
    """The horizontal speed around a sample from the samples within span seconds."""
    t0 = track[index][0]
    lo = next((i for i in range(index, -1, -1) if t0 - track[i][0] >= span / 2), 0)
    hi = next((i for i in range(index, len(track)) if track[i][0] - t0 >= span / 2), len(track) - 1)
    dt = track[hi][0] - track[lo][0]
    return math.hypot(track[hi][1] - track[lo][1], track[hi][2] - track[lo][2]) / dt if dt > 0 else 0.0


def place_at(track: list[tuple], t: float) -> tuple[float, float] | None:
    """The feet (x, y) at a time: linear between the samples."""
    before = [row for row in track if row[0] <= t]
    after = [row for row in track if row[0] > t]
    if not before:
        # The client logs no sample at rest: before its first sample the character stands at that place.
        return (after[0][1], after[0][2]) if after else None
    a = before[-1]
    if not after:
        return a[1], a[2]
    b = after[0]
    f = (t - a[0]) / (b[0] - a[0]) if b[0] > a[0] else 0.0
    return a[1] + (b[1] - a[1]) * f, a[2] + (b[2] - a[2]) * f


def segment_values(track: list[tuple], start: float, end: float, limit: float) -> dict:
    """The values of a move segment: the key down at start, the key up at end, the next key change at limit (times from the first
    key). The stop: from the key up to the first sample below 0.1 m/s (the client logs no sample at rest: the last sample), before
    limit and at most 1.5 s; its distance from the place at the key up."""
    inside = [i for i, row in enumerate(track) if start <= row[0] <= end]
    stop_end = min(end + 1.5, limit)
    after = [i for i, row in enumerate(track) if end <= row[0] <= stop_end]
    if len(inside) < 3:
        return {"samples": len(inside)}
    speeds = [(track[i][0], speed_at(track, i)) for i in inside]
    steady_part = [s for t, s in speeds if t >= start + (end - start) * 0.5]
    steady = sum(steady_part) / len(steady_part) if steady_part else 0.0
    reach = next((t - start for t, s in speeds if s >= 0.9 * steady), None)
    a, b = place_at(track, start), place_at(track, end)
    out = {"samples": len(inside), "steady_m_s": round(steady, 3), "to_90_percent_s": round(reach, 3) if reach is not None else None,
           "distance_m": round(math.hypot(b[0] - a[0], b[1] - a[1]), 3) if a and b else None}
    if after and b:
        rest = next((i for i in after if speed_at(track, i) < 0.1), after[-1])
        last = track[rest]
        out["stop_s"] = round(last[0] - end, 3)
        out["stop_distance_m"] = round(math.hypot(last[1] - b[0], last[2] - b[1]), 3)
    return out


def drive_lead(log: Path, default: float = 6.0) -> float:
    """The drive time of the first key of a replay: the header of the drive script that the client log names ("drive: N step(s) in
    PATH"; tools/lib/retail_replay.py writes "the first key at X s of the drive clock")."""
    m = re.search(r"drive: \d+ step\(s\) in (\S+)", log.read_text(errors="replace"))
    script = Path(m.group(1)) if m else None
    if script is not None and script.exists():
        found = re.search(r"the first key at ([0-9.]+) s of the drive clock", script.read_text())
        if found:
            return float(found.group(1))
    return default


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("take")
    parser.add_argument("log")
    parser.add_argument("--lead", type=float)
    parser.add_argument("--backend")
    parser.add_argument("--json")
    args = parser.parse_args(argv)
    if args.lead is None:
        args.lead = drive_lead(Path(args.log))
    take = json.loads(Path(args.take).read_text())
    # The key times of the game (the input bits; the clock of tools/lib/retail_replay.py).
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from retail_replay import key_times
    for key, t in zip(take["keys"], key_times(take)):
        key["video_s"] = t
    first = take["keys"][0]["video_s"]
    # One sample for each input step: the last one of the step (the first sample of a step can repeat the feet of the step before).
    # The feet of a step are the place after the move of that step (the requested velocity of step N moves the feet of step N): the
    # place at the time of the step plus 1/30 s.
    steps: dict[int, dict] = {}
    for sample in take["client_samples"]:
        steps[sample["step"]] = sample
    retail = [(s["video_s"] - first + 1.0 / 30.0, s["feet"][0], s["feet"][1], s["feet"][2], str((s.get("layer0") or {}).get("action", "")))
              for s in sorted(steps.values(), key=lambda s: s["video_s"])]
    client = client_track(Path(args.log), args.lead)
    # The move segments: each move key down to the next change of the move keys. The client times of a key change: the drive line
    # "move" that made it (the frame that ran it: up to one frame after its drive time), in the same order.
    moves = client_moves(Path(args.log), args.lead)
    changes = []
    for key in take["keys"]:
        words = key["key"].split()
        if words[0] in ("keydown", "keyup") and words[1].lower() in ("0x57", "0x53", "0x41", "0x44"):
            changes.append((key["video_s"] - first, words[0], {"0x57": "W", "0x53": "S", "0x41": "A", "0x44": "D"}[words[1].lower()]))
    own = [t for t in moves] if len(moves) == len(changes) else [t for t, _, _ in changes]
    segments, held = [], set()
    for index, (t, verb, letter) in enumerate(changes):
        held = held | {letter} if verb == "keydown" else held - {letter}
        if held and index + 1 < len(changes):
            segments.append(("+".join(sorted(held)), t, changes[index + 1][0], own[index], own[index + 1],
                              changes[index + 2][0] if index + 2 < len(changes) else None, own[index + 2] if index + 2 < len(changes) else None))
    report = []
    print(f"{'segment':10} {'from s':>7} {'to s':>6}   {'retail: steady, to 90 %, stop, stop dist, dist':48} client")
    for name, start, end, own_start, own_end, limit, own_limit in segments:
        r = segment_values(retail, start, end, limit if limit is not None else end + 1.5)
        c = segment_values(client, own_start, own_end, own_limit if own_limit is not None else own_end + 1.5)
        report.append({"segment": name, "from": round(start, 3), "to": round(end, 3), "retail": r, "client": c})

        def text(v: dict) -> str:
            return (f"{v.get('steady_m_s', '-')!s:>6} {v.get('to_90_percent_s', '-')!s:>6} {v.get('stop_s', '-')!s:>6} "
                    f"{v.get('stop_distance_m', '-')!s:>6} {v.get('distance_m', '-')!s:>7}")
        print(f"{name:10} {start:7.2f} {end:6.2f}   {text(r):48} {text(c)}")
    events = []
    if args.backend:
        # The pairs: each retail event with the nearest client event of the same kind and key within 1.0 s; then the rest of each side.
        theirs, ours = retail_events(take, first), client_events(Path(args.log), Path(args.backend), args.lead)
        used: set[int] = set()
        rows = []
        for ra in theirs:
            near = [(abs(cb[0] - ra[0]), i) for i, cb in enumerate(ours) if i not in used and cb[1] == ra[1] and cb[2] == ra[2] and abs(cb[0] - ra[0]) <= 1.0]
            index = min(near)[1] if near else None
            if index is not None:
                used.add(index)
            rows.append((ra[0], ra, ours[index] if index is not None else None))
        rows += [(cb[0], None, cb) for i, cb in enumerate(ours) if i not in used]
        rows.sort(key=lambda row: row[0])
        print(f"\n{'event':9} {'retail s':>9} {'client s':>9} {'diff s':>7}  text")
        for _, ra, cb in rows:
            kind = (ra or cb)[1]
            text = ra[3] if ra and (not cb or cb[3] == ra[3]) else f"{ra[3] if ra else '-'} / {cb[3] if cb else '-'}"
            row = {"kind": kind, "text": text, "retail": round(ra[0], 3) if ra else None, "client": round(cb[0], 3) if cb else None}
            extra = ""
            if kind == "dodge":
                rf = feet_after(retail, ra[0]) if ra else None
                cf = feet_after(client, cb[0]) if cb else None
                row["retail_feet"] = round(rf, 3) if rf is not None else None
                row["client_feet"] = round(cf, 3) if cf is not None else None
                extra = f"  feet in 0.6 s: {row['retail_feet']} / {row['client_feet']} m"
            events.append(row)
            diff = f"{cb[0] - ra[0]:+7.3f}" if ra and cb else "      -"
            print(f"{kind:9} {f'{ra[0]:9.3f}' if ra else '        -'} {f'{cb[0]:9.3f}' if cb else '        -'} {diff}  {text}{extra}")
    # The camera (the take player-camera: the pitch limits and the distance of the camera from the feet at the limits).
    camera = {}
    views = [(s["camera_pitch"], math.dist(s["camera"], s["feet"])) for s in steps.values() if s.get("camera") and s.get("camera_pitch") is not None]
    ours = [((float(m.group(1)) + 180.0) % 360.0 - 180.0, float(m.group(2))) for m in re.finditer(r"camera: sample pitch ([0-9.-]+) yaw [0-9.-]+, ([0-9.]+) m from the feet",
                                                                          Path(args.log).read_text(errors="replace"))]
    if views and ours and max(abs(p) for p, _ in views) > 20.0:
        def ends(rows: list[tuple[float, float]]) -> dict:
            top, bottom = max(rows), min(rows)
            return {"pitch_max": round(top[0], 2), "distance_at_max": round(top[1], 2), "pitch_min": round(bottom[0], 2),
                    "distance_at_min": round(bottom[1], 2)}
        camera = {"retail": ends(views), "client": ends(ours)}
        print(f"\ncamera: retail pitch {camera['retail']['pitch_min']} to {camera['retail']['pitch_max']} degrees (distance "
              f"{camera['retail']['distance_at_min']} / {camera['retail']['distance_at_max']} m), client {camera['client']['pitch_min']} to "
              f"{camera['client']['pitch_max']} ({camera['client']['distance_at_min']} / {camera['client']['distance_at_max']} m)")
    if args.json:
        Path(args.json).write_text(json.dumps({"take": Path(args.take).name, "segments": report, "events": events, "camera": camera}, indent=1))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
