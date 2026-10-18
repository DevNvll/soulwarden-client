#!/usr/bin/env python3
"""The matched views of the fidelity checks (docs/ENVIRONMENT.md, "The matched views"; tools/match-view.sh).

Usage: python3 -B tools/lib/match_view.py SET [VIEW...] [--out FOLDER] [--label LABEL] [--settle S] [--lead S]

SET: a folder of retail references (index.json and, for each picture, NAME.png and NAME.json: the feet, the camera, the field of
view, the measured world hour), or its name in ~/Games/soulwarden/work/evidence/unreal-references. VIEW: the views to run (default:
each picture of the index).

One development run (tools/dev-run.sh, the window on the own agent desktop, the backend with the development routes) with the client
option -SwcMatchView=PLAN. For each view the client logs "ready"; this tool moves the character to the feet (the travel route); the
client sets the camera of the picture, waits for the streaming, and logs "in place"; this tool sets the world hour of the backend a
lead time before the hour of the picture (the world-time route); the client waits for the new record, the settle time, and the hour
of the picture on its own clock, and takes the picture (1280 x 720). Then, for each view: FOLDER/side/VIEW.png (retail | client,
with the hours and the place), FOLDER/contact-sheet.png, FOLDER/report.json, and the compare of the client clock with the server
(tools/lib/world_time_compare.py).
"""
from __future__ import annotations

import argparse
import datetime
import json
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

REPO = Path(__file__).resolve().parents[2]
REFERENCES = Path.home() / "Games/soulwarden/work/evidence/unreal-references"
WORK = Path(os.environ.get("SWC_WORK") or Path.home() / "Games/soulwarden-client")
FONT = "/usr/share/fonts/TTF/DejaVuSans.ttf"
DAY_RATE, NIGHT_RATE = 26.0, 44.0   # game seconds in one real second (backend DayCycle defaults; docs/ENVIRONMENT.md)


def font(size: int):
    try:
        return ImageFont.truetype(FONT, size)
    except OSError:
        return ImageFont.load_default()


def clock(hour: float) -> str:
    second = int(round(hour * 3600)) % 86400
    return f"{second // 3600:02d}:{second // 60 % 60:02d}:{second % 60:02d}"


def route(*words: str) -> dict:
    """@return The reply of a development route (tools/lib/dev_world.py; the backend of the own development data)."""
    result = subprocess.run([sys.executable, "-B", str(REPO / "tools/lib/dev_world.py"), *words], capture_output=True, text=True, check=False)
    try:
        return json.loads(result.stdout)
    except json.JSONDecodeError:
        return {"error": (result.stdout + result.stderr).strip()[-300:]}


def set_hour(target: float, lead_seconds: float) -> str:
    """@return The hour to set: the target hour less the lead time at the rate of the part of the cycle of the target."""
    rate = DAY_RATE if 6.0 <= target < 19.0 else NIGHT_RATE
    return clock((target - lead_seconds * rate / 3600.0) % 24.0)


def side_by_side(view: dict, reference: Path, client: Path, out: Path) -> None:
    left = Image.open(reference).convert("RGB").resize((1280, 720))
    right = Image.open(client).convert("RGB").resize((1280, 720)) if client.is_file() else Image.new("RGB", (1280, 720), (60, 20, 20))
    image = Image.new("RGB", (2560, 790), (19, 25, 34))
    image.paste(left, (0, 70))
    image.paste(right, (1280, 70))
    draw = ImageDraw.Draw(image)
    feet = view["feet"]
    draw.text((14, 8), f"{view['name']}  |  retail (left), client (right)  |  {view.get('place', '')}", font=font(22), fill=(238, 242, 249))
    draw.text((14, 40), f"hour: retail {clock(view['targetHour'])}, client {view.get('clientClock', '-')}  |  feet {feet[0]:.2f} {feet[1]:.2f} {feet[2]:.2f}  |  "
                        f"camera {view['camera'][0]:.2f} {view['camera'][1]:.2f} {view['camera'][2]:.2f}, heading {view['heading']:.1f}, pitch {view['pitch']:.1f}  |  "
                        f"vertical FOV {view['fov']:g}", font=font(17), fill=(175, 190, 209))
    image.save(out)


def contact_sheet(files: list[Path], out: Path, columns: int = 2) -> None:
    if not files:
        return
    tiles = [Image.open(f).convert("RGB") for f in files]
    width = 1280
    tiles = [t.resize((width, int(t.height * width / t.width))) for t in tiles]
    height = max(t.height for t in tiles)
    rows = (len(tiles) + columns - 1) // columns
    sheet = Image.new("RGB", (width * columns, height * rows), (10, 12, 16))
    for index, tile in enumerate(tiles):
        sheet.paste(tile, ((index % columns) * width, (index // columns) * height))
    sheet.save(out)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("set")
    parser.add_argument("views", nargs="*")
    parser.add_argument("--out")
    parser.add_argument("--label", default="matchview")
    parser.add_argument("--settle", type=float, default=4.0)
    parser.add_argument("--lead", type=float, default=14.0, help="real seconds from the hour command to the hour of the picture")
    parser.add_argument("--character", default=os.environ.get("SWC_DEV_CHARACTER", ""))
    parser.add_argument("--client-args", nargs=argparse.REMAINDER, default=[])
    args = parser.parse_args()
    folder = Path(args.set) if Path(args.set).is_dir() else REFERENCES / args.set
    index = json.loads((folder / "index.json").read_text())
    # The pictures of the index (a contact sheet or a folder of the set has no JSON of a view).
    entries = [e for e in index["files"] if e["file"].endswith(".png") and "json" in e and (not args.views or e["view"] in args.views)]
    out = Path(args.out) if args.out else WORK / "evidence" / "WF" / f"match-{folder.name}"
    (out / "client").mkdir(parents=True, exist_ok=True)
    (out / "side").mkdir(parents=True, exist_ok=True)
    views = []
    for entry in entries:
        source = json.loads((folder / entry["json"]).read_text())
        camera = source["camera"]
        views.append({"name": entry["view"], "json": str(folder / entry["json"]), "reference": str(folder / entry["file"]),
                      "out": str(out / "client" / entry["file"]), "place": source.get("place", ""),
                      "feet": [source["feet"]["x"], source["feet"]["y"], source["feet"]["z"]],
                      "camera": [camera["x"], camera["y"], camera["z"]], "heading": camera.get("heading_deg_cw_from_north", 0.0),
                      "pitch": camera.get("pitch_deg_positive_up", 0.0), "fov": camera["fov"]["vertical_deg"],
                      "targetHour": source["world_hour"]["client_hour_at_picture"]})
    plan = out / "plan.json"
    plan.write_text(json.dumps({"settle": args.settle, "width": 1280, "height": 720,
                                "views": [{"name": v["name"], "json": v["json"], "out": v["out"]} for v in views]}, indent=1))
    for view in views:
        Path(view["out"]).unlink(missing_ok=True)
    # The run: the window on the own agent desktop, the development routes of the backend.
    env = dict(os.environ, SWC_SESSION_BACKEND_ARGS="--dev-commands")
    env.pop("SOULWARDEN_DEV_RUN", None)
    log = WORK / "logs" / "dev-run" / f"{args.label}-client.log"
    log.unlink(missing_ok=True)
    stay = 120 + 75 * len(views)
    enter = ["--enter", args.character] if args.character else ["--enter"]
    command = [str(REPO / "tools/dev-run.sh"), "--window", *enter, "--quit-when", "world", "--stay", str(stay), "--label", args.label, "--",
               f"-SwcMatchView={plan}", "-SwcWorldTimeProbe", *args.client_args]
    subprocess.run(command, env=env, stdout=subprocess.DEVNULL, check=False)
    print(f"match-view: {len(views)} view(s), run {args.label}, stay at most {stay} s", flush=True)
    by_name = {v["name"]: v for v in views}
    position, started, done, ignored = 0, time.monotonic(), False, False
    while time.monotonic() - started < stay + 120 and not done:
        if not log.is_file():
            time.sleep(1)
            continue
        with open(log, errors="replace") as handle:
            handle.seek(position)
            lines = handle.readlines()
            position = handle.tell()
        for line in lines:
            if "world: own entity" in line and not ignored:
                print("match-view: ignore on:", route("ignore", "on").get("message", ""), flush=True)
                ignored = True
            match = re.search(r"matchview: view (\d+) (\S+): (ready|in place|the picture at the client hour ([\d.]+)|picture (\S+) \((\w+))", line)
            if match:
                view = by_name.get(match[2])
                if view is None:
                    continue
                if match[3] == "ready":
                    reply = route("travel", f"{view['feet'][0]:.4f}", f"{view['feet'][1]:.4f}", f"{view['feet'][2]:.4f}", "auto")
                    print(f"match-view: {view['name']}: travel: {reply.get('message', reply.get('error', ''))[:120]}", flush=True)
                elif match[3].startswith("in place"):
                    hour = set_hour(view["targetHour"], args.lead)
                    reply = route("world-time", hour)
                    view["setHour"] = hour
                    print(f"match-view: {view['name']}: world time {hour} for the picture at {clock(view['targetHour'])}: {reply.get('time', reply.get('error', ''))}", flush=True)
                elif match[4]:
                    view["clientHour"] = float(match[4])
                    view["clientClock"] = clock(float(match[4]))
                elif match[5]:
                    view["written"] = match[6] == "written"
                    print(f"match-view: {view['name']}: picture {match[6]} (client {view.get('clientClock')}, retail {clock(view['targetHour'])})", flush=True)
            if "matchview: done" in line:
                done = True
        time.sleep(0.5)
    time.sleep(3)
    subprocess.run([str(REPO / "tools/dev-stop.sh")], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)
    # The logs, the side-by-side pictures, the contact sheet, the compare of the clocks.
    shutil.copy(log, out / f"{args.label}-client.log")
    backend = Path(os.environ.get("SWC_DEV_DATA") or WORK / "dev-data") / "logs" / "backend.log"
    if backend.is_file():
        shutil.copy(backend, out / f"{args.label}-backend.log")
    sides = []
    for view in views:
        side = out / "side" / Path(view["out"]).name
        side_by_side(view, Path(view["reference"]), Path(view["out"]), side)
        sides.append(side)
    contact_sheet(sides, out / "contact-sheet.png")
    compare = subprocess.run([sys.executable, "-B", str(REPO / "tools/lib/world_time_compare.py"), str(out), args.label], capture_output=True, text=True, check=False)
    (out / "clock-compare.txt").write_text(compare.stdout + compare.stderr)
    report = {"set": str(folder), "run": args.label, "date": datetime.datetime.now().isoformat(timespec="seconds"), "done": done,
              "views": [{k: v for k, v in view.items() if k != "json"} | {"hourDifferenceGameSeconds": round((view.get("clientHour", float("nan")) - view["targetHour"]) * 3600, 1)}
                        for view in views],
              "clockCompare": compare.stdout.strip().splitlines()[-1] if compare.stdout.strip() else ""}
    (out / "report.json").write_text(json.dumps(report, indent=1))
    written = sum(1 for v in views if v.get("written"))
    print(f"match-view: {written} of {len(views)} pictures; {out}/contact-sheet.png; {report['clockCompare']}", flush=True)
    return 0 if done and written == len(views) else 1


if __name__ == "__main__":
    sys.exit(main())
