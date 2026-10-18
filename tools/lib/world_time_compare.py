#!/usr/bin/env python3
"""The compare of the world clock of the client with the hour of the server in one development run (docs/ENVIRONMENT.md).

Usage: python3 -B tools/lib/world_time_compare.py FOLDER LABEL

FOLDER/LABEL-client.log: the lines "world time: HH:MM:SS (PHASE), hour H ..." of the client (-SwcWorldTimeProbe: each 1 s; UTC times).
FOLDER/LABEL-backend.log: the records 7036 that the backend sent and its phase edges (local times).
FOLDER/LABEL-routes.txt: the reads and the commands "world-time" of tools/lib/dev_world.py (the wall time and the hour of each reply;
local times).

For each point of the server, the client line nearest in time (for a record or a command: the first line of the new record in the
client), moved
by the rate of the cycle (26 game seconds in one real second by day, 44 by night) over the gap of the two times. Exit status 1 when a
difference is more than 1 game second.
"""

from __future__ import annotations

import datetime
import re
import sys
from pathlib import Path

LIMIT = 1.0


def clock_seconds(text: str) -> float:
    hour, minute, second = (int(part) for part in text.split(":"))
    return hour * 3600 + minute * 60 + second


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    folder, label = Path(sys.argv[1]), sys.argv[2]
    utc_offset = datetime.datetime.now().astimezone().utcoffset() or datetime.timedelta()
    client: list[tuple[datetime.datetime, float, str, bool]] = []
    for line in (folder / f"{label}-client.log").read_text(errors="replace").splitlines():
        match = re.match(r"\[(\d{4})\.(\d\d)\.(\d\d)-(\d\d)\.(\d\d)\.(\d\d):(\d{3})\].*LogSwcEnvironment: world time: \d\d:\d\d:\d\d \((\w+)\), "
                         r"hour ([\d.]+).*?(record of the server)?$", line)
        if match:
            parts = [int(match[index]) for index in range(1, 8)]
            when = datetime.datetime(*parts[:6], parts[6] * 1000) + utc_offset
            client.append((when, float(match[9]) * 3600.0, match[8], bool(match[10])))
    if not client:
        print("no world time line in the client log")
        return 1
    day = client[0][0].date()
    points: list[tuple[datetime.datetime, float, str, str]] = []
    for line in (folder / f"{label}-backend.log").read_text(errors="replace").splitlines():
        match = re.match(r"(\d\d):(\d\d):(\d\d)\.(\d{3}) (.*)", line)
        if not match:
            continue
        when = datetime.datetime.combine(day, datetime.time(int(match[1]), int(match[2]), int(match[3]), int(match[4]) * 1000))
        record = re.search(r"rep: world time (\d\d:\d\d:\d\d) \((\w+)\) sent", match[5])
        edge = re.search(r"phase changes from \w+ to (\w+) \(the cycle\) at (\d\d:\d\d:\d\d)", match[5])
        if record:
            points.append((when, clock_seconds(record[1]), record[2], "record " + record[1]))
        if edge:
            points.append((when, clock_seconds(edge[2]), edge[1], "edge " + edge[2]))
    routes = folder / f"{label}-routes.txt"
    if routes.exists():
        for line in routes.read_text().splitlines():
            wall, hour, phase = (re.search(pattern, line) for pattern in (r'"wall": "([^"]+)"', r'"hour": ([\d.]+)', r'"phase": "(\w+)"'))
            if wall and hour and phase:
                # The reply of a command for the world time is a new record of the client.
                kind = "record (command)" if re.search(r"world-time \d", line) else "read at " + line.split("s ", 1)[0].strip() + " s"
                points.append((datetime.datetime.fromisoformat(wall[1]), float(hour[1]) * 3600.0, phase[1], kind))
    points.sort()
    largest = 0.0
    for when, second, phase, what in points:
        if what.startswith("record"):
            near = next((line for line in client if line[3] and line[0] >= when), None)
        else:
            near = min(client, key=lambda line: abs((line[0] - when).total_seconds()))
        if near is None:
            print(f"{what}: no client line")
            largest = float("inf")
            continue
        gap = (near[0] - when).total_seconds()
        # The rate of the part of the cycle between the two moments: the phase of the earlier one (a point at an edge).
        earlier = near[2] if gap < 0 else phase
        expected = second + (44.0 if earlier == "night" else 26.0) * gap
        difference = near[1] - expected
        largest = max(largest, abs(difference))
        print(f"{what:18} server {str(when.time())[:12]} {phase:5} {second:9.2f} | client {str(near[0].time())[:12]} {near[2]:5} {near[1]:9.2f} | "
              f"difference {difference:+6.2f} game s (gap {gap:+.3f} s)")
    print(f"{len(points)} points of the server, the largest difference {largest:.2f} game seconds (limit {LIMIT:.0f})")
    return 0 if largest <= LIMIT else 1


if __name__ == "__main__":
    sys.exit(main())
