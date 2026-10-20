#!/usr/bin/env python3
"""The compare of a creature fight against a retail reference (stage P3.C, docs/CREATURES.md): the measured values of a retail take
of set6-combat (its JSON: the backend lines and the creature rows of the server at 20 Hz) and of a client run of tools/creature-check.sh
(its backend log and the creature probe lines of the client log, -SwcCreatureProbe: the shown place, yaw, and fragments at 20 Hz).

Usage: python3 -B tools/lib/creature_compare.py RETAIL.json LABEL [--kind Wolf_Grey_7] [--name "Grey Wolf"] [--animations NAME]
           [--video OUT.mp4 [--pre S] [--post S]]
  RETAIL.json   a take of ~/Games/soulwarden/work/evidence/unreal-references/set6-combat
  LABEL         the label of the client run (SWC_WORK/evidence/SWC_STAGE/LABEL-client.log and LABEL-backend.log)
  --animations  the animation file of the slice (SWC_CONTENT/creatures/animations/NAME.json): the names of the fragments
  --video       a side-by-side video (retail on the left, the client on the right, 960 x 540 each, 30 frames each second), both
                from PRE seconds before the aggro to POST seconds after it: the retail time of the aggro from the JSON, the client time
                from LABEL-video-start.txt (the start of the recording, tools/creature-check.sh) and the backend log

The values: the aggro distance, the moves (distance, time, speed), the attacks (the start, the time of the attack, the time to
the next attack), the reactions, the death, the leash and the return; for the client also the shown values: the speed of the
shown place, the yaw rate, the fragment of each attack and its delay after the start of the server, and the death fragment.
All times are seconds after the aggro (the "has a target" line).
"""
from __future__ import annotations

import argparse
import datetime
import json
import math
import os
import re
import subprocess
import sys
from pathlib import Path

WORK = Path(os.environ.get("SWC_WORK", Path.home() / "Games/soulwarden-client"))
CONTENT = Path(os.environ.get("SWC_CONTENT", WORK / "content"))
EVENTS = [
    ("target", re.compile(r"creature (\w+) \((\S+)\) has a target: (\S+) \(in its vision, ([\d.]+) m\)")),
    ("attack", re.compile(r"creature (\w+) \((\S+)\) starts the attack (\S+) at (\S+) \(([\d.]+) m, (\d+) contact\(s\), ([\d.]+) s\)")),
    ("reaction", re.compile(r"reaction: creature (\w+) \((\S+)\) plays the (reaction of power \d+|additive reaction) \((fragment \d+[^)]*)\) for ([\d.]+) s")),
    ("dead", re.compile(r"creature (\w+) \((\S+)\) is dead")),
    ("leash", re.compile(r"creature (\w+) \((\S+)\) lost its target \((.*?)\): it goes back to its anchor, ([\d.]+) m")),
    ("home", re.compile(r"creature (\w+) \((\S+)\) is at its home")),
]
PROBE = re.compile(r"^\[(\d{4})\.(\d\d)\.(\d\d)-(\d\d)\.(\d\d)\.(\d\d):(\d{3})\].*creature probe: (\d+) (.+?): at ([-\d.]+) ([-\d.]+) ([-\d.]+), "
                   r"yaw ([-\d.]+), move ([-\d.]+) m/s, turn ([-\d]+) deg/s, travel ([-\d]+) deg(, dead)?; (.*)$")
FRAGMENT = re.compile(r"(?:scope (\d+) )?f(-?\d+) t([-\d.]+) x([-\d.]+) w([-\d.]+)")


def events_of(lines: list[tuple[float, str]], kind: str) -> list[dict]:
    out = []
    for t, text in lines:
        for name, pattern in EVENTS:
            m = pattern.search(text)
            if m and m.group(2) == kind:
                out.append({"t": t, "event": name, "groups": m.groups()})
    return out


def retail_lines(doc: dict) -> list[tuple[float, str]]:
    return [(b["video_s"], b["text"]) for b in doc["backend_lines"]]


def backend_lines(path: Path) -> list[tuple[float, str]]:
    """The backend log of a client run: local wall times HH:MM:SS.mmm of the day of the file (epoch seconds)."""
    day = datetime.datetime.fromtimestamp(path.stat().st_mtime).date()
    out = []
    for line in path.read_text(errors="replace").splitlines():
        m = re.match(r"(\d\d):(\d\d):(\d\d)\.(\d{3}) (.*)", line)
        if m:
            h, mi, s, ms, text = m.groups()
            stamp = datetime.datetime.combine(day, datetime.time(int(h), int(mi), int(s), int(ms) * 1000)).timestamp()
            out.append((stamp, text))
    return out


def probe_rows(path: Path, name: str) -> list[dict]:
    """The probe lines of one creature name (the client log times are UTC)."""
    rows = []
    for line in path.read_text(errors="replace").splitlines():
        m = PROBE.match(line)
        if not m or m.group(9) != name:
            continue
        y, mo, d, h, mi, s, ms = (int(x) for x in m.groups()[:7])
        stamp = datetime.datetime(y, mo, d, h, mi, s, ms * 1000, tzinfo=datetime.timezone.utc).timestamp()
        layers = [g for g in FRAGMENT.findall(m.group(18))]
        rows.append({"t": stamp, "id": int(m.group(8)), "x": float(m.group(10)), "y": float(m.group(11)), "z": float(m.group(12)),
                     "yaw": float(m.group(13)), "move": float(m.group(14)), "turn": float(m.group(15)), "dead": bool(m.group(17)),
                     "layers": [{"scope": int(sc) if sc else 0, "fragment": int(f), "time": float(t), "scale": float(x), "weight": float(w)}
                                for sc, f, t, x, w in layers]})
    return rows


def moves(rows: list[dict], t0: float, key_x="x", key_y="y", key_t="t", gap=0.3) -> list[tuple[float, float, float]]:
    """@return The moving parts (start, end, distance) of a place track: the steps faster than gap m/s."""
    out, cur = [], None
    for a, b in zip(rows, rows[1:]):
        dt = b[key_t] - a[key_t]
        v = math.hypot(b[key_x] - a[key_x], b[key_y] - a[key_y]) / dt if dt > 0 else 0.0
        if v > gap:
            if cur is None:
                cur = [a[key_t] - t0, b[key_t] - t0, 0.0]
            cur[1] = b[key_t] - t0
            cur[2] += v * dt
        elif cur is not None:
            out.append(tuple(cur))
            cur = None
    if cur is not None:
        out.append(tuple(cur))
    return [m for m in out if m[1] - m[0] > 0.25]


def yaw_rates(rows: list[dict], key_t="t", key_yaw="yaw") -> list[float]:
    rates = []
    for a, b in zip(rows, rows[1:]):
        dt = b[key_t] - a[key_t]
        d = (b[key_yaw] - a[key_yaw] + 180.0) % 360.0 - 180.0
        if dt > 0 and abs(d) > 0.5:
            rates.append(abs(d) / dt)
    return sorted(rates)


def summary_of_events(events: list[dict], t0: float) -> list[str]:
    out = []
    attacks = [e for e in events if e["event"] == "attack"]
    for e in events:
        g, t = e["groups"], e["t"] - t0
        if e["event"] == "target":
            out.append(f"{t:7.2f}  target {g[2]} at {g[3]} m")
        elif e["event"] == "attack":
            nxt = next((a["t"] - e["t"] for a in attacks if a["t"] > e["t"]), None)
            out.append(f"{t:7.2f}  attack {g[2]} ({g[6]} s){'' if nxt is None else f', the next one after {nxt:.2f} s'}")
        elif e["event"] == "reaction":
            out.append(f"{t:7.2f}  {g[2]} ({g[3]}), {g[4]} s")
        elif e["event"] == "dead":
            out.append(f"{t:7.2f}  dead")
        elif e["event"] == "leash":
            out.append(f"{t:7.2f}  leash ({g[2]}) at {g[3]} m")
        elif e["event"] == "home":
            out.append(f"{t:7.2f}  at home")
    return out


FONT = "/usr/share/fonts/TTF/DejaVuSans.ttf"


def side_by_side(retail: Path, retail_aggro: float, folder: Path, label: str, client_aggro: float, out: Path, pre: float, post: float) -> None:
    """Writes the side-by-side video of a retail take and a client run, both from the aggro less PRE seconds."""
    start_file = folder / f"{label}-video-start.txt"
    if not start_file.exists():
        print(f"creature-compare: no {start_file.name} (a run before the start time of the recording): no video", file=sys.stderr)
        return
    client_aggro_video = client_aggro - float(start_file.read_text().split()[0])
    seconds = pre + post

    def text(name: str) -> str:
        return (f"drawtext=fontfile={FONT}:text='{name}':x=12:y=10:fontsize=26:fontcolor=white:box=1:boxcolor=black@0.55:boxborderw=6,"
                f"drawtext=fontfile={FONT}:text='%{{eif\\:t-{pre:g}\\:d}} s after the aggro':x=12:y=48:fontsize=20:fontcolor=white:"
                f"box=1:boxcolor=black@0.55:boxborderw=4")

    graph = (f"[0:v]fps=30,scale=960:540,setpts=PTS-STARTPTS,{text('retail')}[a];"
             # The client recording is the output of the agent desktop: its top bar (48 px) and left border (12 px) around the
             # game window (1256 x 660, tools/dev-run.sh --window) are cut off; the game picture keeps its aspect.
             f"[1:v]fps=30,crop=1256:660:12:48,scale=960:-2,pad=960:540:0:(540-ih)/2,setpts=PTS-STARTPTS,{text('client')}[b];"
             f"[a][b]hstack=inputs=2[v]")
    cmd = ["ffmpeg", "-v", "error", "-y", "-ss", f"{max(retail_aggro - pre, 0.0):.3f}", "-t", f"{seconds:.3f}", "-i", str(retail.with_suffix(".mp4")),
           "-ss", f"{max(client_aggro_video - pre, 0.0):.3f}", "-t", f"{seconds:.3f}", "-i", str(folder / f"{label}.mp4"),
           "-filter_complex", graph, "-map", "[v]", "-c:v", "libx264", "-crf", "22", "-pix_fmt", "yuv420p", str(out)]
    subprocess.run(cmd, check=True)
    print(f"-- the side-by-side video: {out} (the client aggro at {client_aggro_video:.2f} s of its video)")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("retail")
    parser.add_argument("label")
    parser.add_argument("--kind", default="Wolf_Grey_7")
    parser.add_argument("--name", default="Grey Wolf")
    parser.add_argument("--animations", default="wolf_controllerdefs_51e224")
    parser.add_argument("--stage", default=os.environ.get("SWC_STAGE", "P3.C"))
    parser.add_argument("--video")
    parser.add_argument("--pre", type=float, default=3.0)
    parser.add_argument("--post", type=float, default=32.0)
    args = parser.parse_args()
    names = json.loads((CONTENT / "creatures" / "animations" / f"{args.animations}.json").read_text())["fragmentNames"]
    fragment_name = (lambda f: names[f] if 0 <= f < len(names) else f"f{f}")

    doc = json.loads(Path(args.retail).read_text())
    retail = events_of(retail_lines(doc), args.kind)
    r0 = next((e["t"] for e in retail if e["event"] == "target"), 0.0)
    folder = WORK / "evidence" / args.stage
    client = events_of(backend_lines(folder / f"{args.label}-backend.log"), args.kind)
    c0 = next((e["t"] for e in client if e["event"] == "target"), None)
    rows = probe_rows(folder / f"{args.label}-client.log", args.name)
    if c0 is None or not rows:
        print(f"creature-compare: no aggro line of {args.kind} in the client run, or no probe line of {args.name}", file=sys.stderr)
        return 1
    # The probe rows of the creature of the fight: the interest whose track is nearest the backend place at the aggro.
    ids = sorted({r["id"] for r in rows})
    fight_id = min(ids, key=lambda i: min((abs(r["t"] - c0) for r in rows if r["id"] == i), default=1e9))
    track = [r for r in rows if r["id"] == fight_id]

    print(f"== {Path(args.retail).name} against the client run {args.label} ({args.kind}, the probe interest {fight_id})")
    print("-- the server, retail take (seconds after the aggro):")
    print("\n".join(summary_of_events(retail, r0)))
    print("-- the server, client run:")
    print("\n".join(summary_of_events(client, c0)))

    for cid, creature in doc["server_creatures"].items():
        if creature["kind"] != args.kind:
            continue
        server = [dict(zip(creature["columns"], row)) for row in creature["rows"]]
        print("-- the moves of the server, retail take:")
        for s, e, dist in moves(server, r0, key_t="video_s"):
            print(f"{s:7.2f} to {e:7.2f}: {dist:6.2f} m, {dist / (e - s):5.2f} m/s")
        rates = yaw_rates(server, key_t="video_s", key_yaw="yaw_deg")
        if rates:
            print(f"   the server yaw rate: median {rates[len(rates) // 2]:.0f} deg/s, max {rates[-1]:.0f} ({len(rates)} steps)")
    print("-- the shown moves, client run (the probe):")
    for s, e, dist in moves(track, c0):
        print(f"{s:7.2f} to {e:7.2f}: {dist:6.2f} m, {dist / (e - s):5.2f} m/s")
    rates = yaw_rates(track)
    if rates:
        print(f"   the shown yaw rate: median {rates[len(rates) // 2]:.0f} deg/s, p90 {rates[int(len(rates) * 0.9)]:.0f}, max {rates[-1]:.0f} ({len(rates)} steps)")
    print("-- the shown fragments, client run (the top layer of scope 0, and the overlays):")
    last = None
    for r in track:
        main = [l for l in r["layers"] if l["scope"] == 0]
        top = main[-1] if main else None
        overlays = [l for l in r["layers"] if l["scope"] != 0]
        key = (fragment_name(top["fragment"]) if top else "none", tuple(fragment_name(o["fragment"]) for o in overlays), r["dead"])
        if key != last:
            extra = f" + overlays {', '.join(key[1])}" if key[1] else ""
            print(f"{r['t'] - c0:7.2f}  {key[0]}{extra}{' (dead)' if r['dead'] else ''}, move {r['move']:.2f} m/s, scale {top['scale'] if top else 0:.2f}")
            last = key
    # The delay of the shown attack after the start of the server.
    if args.video:
        side_by_side(Path(args.retail), r0, folder, args.label, c0, Path(args.video), args.pre, args.post)
    print("-- the shown attacks (the delay of the fragment after the start of the server, and its shown time):")
    def top_name(r):
        main = [l for l in r["layers"] if l["scope"] == 0]
        return fragment_name(main[-1]["fragment"]) if main else ""

    for e in client:
        if e["event"] != "attack":
            continue
        name = e["groups"][2]
        shown = [r for r in track if e["t"] - 0.5 <= r["t"] <= e["t"] + 2.0 and top_name(r) == name]
        if not shown:
            print(f"{e['t'] - c0:7.2f}  {name}: not shown")
            continue
        start = shown[0]["t"]
        end = start
        for r in track:
            if r["t"] >= start:
                if top_name(r) == name:
                    end = r["t"]
                else:
                    break
        print(f"{e['t'] - c0:7.2f}  {name}: shown {start - e['t']:+.2f} s after the server start, for {end - start:.2f} s (the server: {e['groups'][6]} s)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
