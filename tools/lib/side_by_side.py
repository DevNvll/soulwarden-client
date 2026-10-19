#!/usr/bin/env python3
"""The side-by-side video of a retail take of set 6 and its client replay (stage P3.G, AST-193; tools/lib/retail_replay.py makes the
replay, tools/lib/retail_compare.py the numbers): the retail video on the left, the client video on the right, both at the same time
from the first key of the take, with a label and a line of text for each part.

The clocks: the retail time of a key is its `video_s` in the JSON of the take. The client time of a drive time D (the drive clock of
the client log, "drive: D s ...") is the wall time of that point of the log (UTC stamps of the lines, interpolated between the drive
lines) minus the wall time of the start of the recorder (LABEL-recorder-start.txt, written by the check script just before
wf-recorder). The drive time of the first key is the lead of the replay (6.0 s).

Usage: python3 -B tools/lib/side_by_side.py TAKE.json RETAIL.mp4 CLIENT_LOG CLIENT.mp4 RECORDER_START OUT.mp4
         [--lead 6.0] [--part FROM TO TEXT]... [--client-shift S]
  --part: a part of the take (seconds from the first key) and its text; with no part, the whole take from -1 s.
  --client-shift: seconds added to the client time (a correction of the recorder start, found by the frames).
"""
from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
import tempfile
from datetime import datetime, timezone
from pathlib import Path

STAMP = re.compile(r"^\[(\d{4})\.(\d\d)\.(\d\d)-(\d\d)\.(\d\d)\.(\d\d):(\d{3})\]")
DRIVE = re.compile(r"drive: ([0-9.]+) s ")
# The client window in the frame of the agent desktop (gamescope 1256 x 660 under the bar of the desktop).
CLIENT_CROP = "1256:660:12:48"
WIDTH, HEIGHT = 960, 540
FONT = "/usr/share/fonts/TTF/DejaVuSans.ttf"


def wall(line: str) -> float | None:
    m = STAMP.match(line)
    if not m:
        return None
    y, mo, d, h, mi, s, ms = map(int, m.groups())
    return datetime(y, mo, d, h, mi, s, ms * 1000, tzinfo=timezone.utc).timestamp()


def drive_to_wall(log: Path) -> list[tuple[float, float]]:
    """(drive seconds, wall seconds) of the drive lines of a client log."""
    out = []
    for line in log.read_text(errors="replace").splitlines():
        m = DRIVE.search(line)
        t = wall(line)
        if m and t is not None:
            out.append((float(m.group(1)), t))
    return sorted(out)


def wall_at(points: list[tuple[float, float]], drive: float) -> float:
    for (d0, w0), (d1, w1) in zip(points, points[1:]):
        if d0 <= drive <= d1:
            return w0 + (w1 - w0) * (drive - d0) / (d1 - d0) if d1 > d0 else w0
    d0, w0 = points[0] if drive < points[0][0] else points[-1]
    return w0 + drive - d0


def text_file(directory: Path, name: str, text: str) -> str:
    path = directory / name
    path.write_text(text)
    return str(path).replace(":", r"\:")


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
    for name in ("take", "retail", "log", "client", "start", "out"):
        parser.add_argument(name)
    parser.add_argument("--lead", type=float)
    parser.add_argument("--part", nargs=3, action="append", metavar=("FROM", "TO", "TEXT"))
    parser.add_argument("--client-shift", type=float, default=0.0)
    parser.add_argument("--origin", type=float, default=0.0, help="the retail video time of time 0 for a take with no keys")
    args = parser.parse_args(argv)
    if args.lead is None:
        args.lead = drive_lead(Path(args.log))
    take = json.loads(Path(args.take).read_text())
    # The key times of the game (the input bits; the clock of tools/lib/retail_replay.py).
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from retail_replay import key_times
    first = key_times(take)[0] if take.get("keys") else args.origin
    points = drive_to_wall(Path(args.log))
    start = float(Path(args.start).read_text().split()[0])
    if not points:
        print("side-by-side: no drive lines in the client log", file=sys.stderr)
        return 1
    parts = [(float(a), float(b), text) for a, b, text in args.part] if args.part else [(-1.0, take["video"]["duration_s"] - first, "")]
    name = Path(args.take).stem
    with tempfile.TemporaryDirectory() as folder:
        work = Path(folder)
        pieces = []
        for index, (t0, t1, text) in enumerate(parts):
            retail_t = max(first + t0, 0.0)
            client_t = wall_at(points, args.lead + t0) - start + args.client_shift
            length = t1 - t0
            label_r = text_file(work, f"r{index}.txt", f"RETAIL  {name}  {t0:+.2f} s")
            label_c = text_file(work, f"c{index}.txt", "CLIENT (Unreal)")
            note = text_file(work, f"n{index}.txt", text or " ")
            clock = f"%{{eif\\:t+{t0:.3f}\\:d}}.%{{eif\\:mod((t+{t0:.3f})*100\\,100)\\:d\\:2}} s"
            draw = f"fontfile={FONT}:fontcolor=white:box=1:boxcolor=black@0.6:boxborderw=6"
            piece = work / f"part{index}.mp4"
            graph = (f"[0:v]scale={WIDTH}:{HEIGHT},setpts=PTS-STARTPTS,drawtext={draw}:expansion=none:textfile='{label_r}':x=10:y=10:fontsize=22[r];"
                     f"[1:v]crop={CLIENT_CROP},scale={WIDTH}:{HEIGHT},setpts=PTS-STARTPTS,drawtext={draw}:expansion=none:textfile='{label_c}':x=10:y=10:fontsize=22,"
                     f"drawtext={draw}:text='{clock}':x=w-tw-10:y=10:fontsize=22[c];"
                     f"[r][c]hstack=inputs=2,fps=30,pad=iw:ih+60:0:0:black,"
                     f"drawtext={draw}:expansion=none:textfile='{note}':x=10:y=h-48:fontsize=24[v]")
            command = ["nice", "-n", "10", "ffmpeg", "-v", "error", "-y", "-ss", f"{retail_t:.3f}", "-t", f"{length:.3f}", "-i", args.retail,
                       "-ss", f"{max(client_t, 0.0):.3f}", "-t", f"{length:.3f}", "-i", args.client, "-filter_complex", graph, "-map", "[v]",
                       "-c:v", "libx264", "-preset", "veryfast", "-crf", "23", "-pix_fmt", "yuv420p", str(piece)]
            subprocess.run(command, check=True)
            pieces.append(piece)
            print(f"side-by-side: part {index}: {t0:+.2f} to {t1:+.2f} s, retail {retail_t:.3f} s, client {client_t:.3f} s")
        listing = work / "list.txt"
        listing.write_text("".join(f"file '{piece}'\n" for piece in pieces))
        subprocess.run(["ffmpeg", "-v", "error", "-y", "-f", "concat", "-safe", "0", "-i", str(listing), "-c", "copy", args.out], check=True)
    print(f"side-by-side: {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
