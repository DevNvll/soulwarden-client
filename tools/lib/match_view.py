#!/usr/bin/env python3
"""The matched views of the fidelity checks (docs/ENVIRONMENT.md, "The matched views"; tools/match-view.sh).

Usage: python3 -B tools/lib/match_view.py SET [VIEW...] [--out FOLDER] [--label LABEL] [--settle S] [--lead S] [--hours VIEW=HH:MM,...] [--variant VIEW@NAME=COMMAND;...]

SET: a folder of retail references (index.json and, for each picture, NAME.png and NAME.json: the feet, the camera, the field of
view, the measured world hour), or its name in ~/Games/soulwarden/work/evidence/unreal-references. VIEW: the views to run (default:
each picture of the index).

One development run (tools/dev-run.sh, the window on the own agent desktop, the backend with the development routes) with the client
option -SwcMatchView=PLAN. For each view the client logs "ready"; this tool moves the character to the feet (the travel route); the
client sets the camera of the picture, waits for the streaming, and logs "in place"; this tool sets the world hour of the backend a
lead time before the hour of the picture (the world-time route); the client waits for the new record, the settle time, and the hour
of the picture on its own clock, and takes the picture (1280 x 720). Then, for each view: FOLDER/side/VIEW.png (retail | client,
with the hours and the place), FOLDER/contact-sheet.png, FOLDER/report.json, the compare of the client clock with the server
(tools/lib/world_time_compare.py), and the color report (tools/lib/color_report.py: FOLDER/colors.json, FOLDER/colors.txt with one
line for each view, FOLDER/color-sheet.png with the numbers next to each picture). tools/match-view.sh --colors FOLDER: the color
report of an existing folder.
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

sys.path.insert(0, str(Path(__file__).resolve().parent))
import color_report  # noqa: E402

REPO = Path(__file__).resolve().parents[2]
REFERENCES = Path.home() / "Games/soulwarden/work/evidence/unreal-references"
WORK = Path(os.environ.get("SWC_WORK") or Path.home() / "Games/soulwarden-client")
FONT = "/usr/share/fonts/TTF/DejaVuSans.ttf"
MONO = "/usr/share/fonts/TTF/DejaVuSansMono.ttf"
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


def view_measures(log: Path, views: list[dict]) -> None:
    """Adds to each view the counts of the local lights at its picture (stage E2) and the median GPU time of the frame statistics
    (-SwcFrameStats=SECONDS) while the view is in place, when the client log has them."""
    by_name = {v["name"]: v for v in views}
    gpu: dict[str, list[float]] = {}
    current = None
    for line in log.read_text(errors="replace").splitlines() if log.is_file() else []:
        match = re.search(r"matchview: view \d+ (\S+): (in place|picture)", line)
        if match:
            current = match[1] if match[2] == "in place" else None
            continue
        match = re.search(r"match view (\S+): local lights at the shot: (.*)", line)
        if match and match[1] in by_name:
            by_name[match[1]]["lights"] = match[2].strip()
        match = re.search(r"SWC FRAMES (\{.*\})", line)
        if match and current in by_name:
            gpu.setdefault(current, []).append(float(json.loads(match[1])["gpu p50 ms"]))
    for name, values in gpu.items():
        values.sort()
        by_name[name]["gpuP50Ms"] = values[len(values) // 2]
        by_name[name]["gpuWindows"] = len(values)


def world_hour_of(source: dict) -> float | None:
    """@return The world hour of a view whose client hour is not the world hour (an hour override of an interior): the backend world
    time after the picture when it differs from the client hour by more than 0.25 h; else no value."""
    hours = source.get("world_hour") or {}
    match = re.match(r"(\d+):(\d+):(\d+)", str(hours.get("backend_world_time_after_picture", "")))
    if not match:
        return None
    world = int(match[1]) + int(match[2]) / 60.0 + int(match[3]) / 3600.0
    client = float(hours.get("client_hour_at_picture", world))
    difference = abs(world - client) % 24.0
    return world if min(difference, 24.0 - difference) > 0.25 else None


def side_by_side(view: dict, reference: Path, client: Path, out: Path) -> None:
    left = Image.open(reference).convert("RGB").resize((1280, 720))
    right = Image.open(client).convert("RGB").resize((1280, 720)) if client.is_file() else Image.new("RGB", (1280, 720), (60, 20, 20))
    extra = "  |  ".join(part for part in (f"local lights: {view['lights']}" if view.get("lights") else "",
                                           f"GPU p50 {view['gpuP50Ms']:.1f} ms (median of {view['gpuWindows']} windows in place)" if "gpuP50Ms" in view else "",
                                           f"commands: {'; '.join(view['commands'])}" if view.get("commands") else "") if part)
    top = 100 if extra else 70
    image = Image.new("RGB", (2560, 720 + top), (19, 25, 34))
    image.paste(left, (0, top))
    image.paste(right, (1280, top))
    draw = ImageDraw.Draw(image)
    feet = view["feet"]
    draw.text((14, 8), f"{view['name']}  |  retail (left), client (right)  |  {view.get('place', '')}", font=font(22), fill=(238, 242, 249))
    retail_hour = clock(view.get("referenceHour", view["targetHour"]))
    draw.text((14, 40), f"hour: retail {retail_hour}, client {view.get('clientClock', '-')}  |  feet {feet[0]:.2f} {feet[1]:.2f} {feet[2]:.2f}  |  "
                        f"camera {view['camera'][0]:.2f} {view['camera'][1]:.2f} {view['camera'][2]:.2f}, heading {view['heading']:.1f}, pitch {view['pitch']:.1f}  |  "
                        f"vertical FOV {view['fov']:g}", font=font(17), fill=(175, 190, 209))
    if extra:
        draw.text((14, 68), extra, font=font(17), fill=(214, 196, 150))
    image.save(out)


def number_panel(lines: list[str], height: int, width: int = 640) -> Image.Image:
    """@return A panel with the numbers of the color report of a view (the contact sheet)."""
    panel = Image.new("RGB", (width, height), (19, 25, 34))
    draw = ImageDraw.Draw(panel)
    try:
        mono = ImageFont.truetype(MONO, 15)
    except OSError:
        mono = ImageFont.load_default()
    for index, line in enumerate(lines):
        draw.text((10, 10 + 22 * index), line, font=mono, fill=(238, 242, 249) if index == 0 else (175, 190, 209))
    return panel


def contact_sheet(files: list[Path], out: Path, columns: int = 2, panels: list[list[str]] | None = None) -> None:
    """Writes a contact sheet of the side-by-side pictures; with panels, each picture gets a column with the numbers of its color
    report at its right."""
    if not files:
        return
    tiles = [Image.open(f).convert("RGB") for f in files]
    width = 1280
    tiles = [t.resize((width, int(t.height * width / t.width))) for t in tiles]
    if panels:
        joined = []
        for tile, lines in zip(tiles, panels):
            row = Image.new("RGB", (width + 640, tile.height), (10, 12, 16))
            row.paste(tile, (0, 0))
            row.paste(number_panel(lines, tile.height), (width, 0))
            joined.append(row)
        tiles, width, columns = joined, width + 640, 1
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
    parser.add_argument("--hours", action="append", default=[],
                        help="VIEW=HH:MM[,HH:MM...]: more views of a reference view at other world hours (the left side: its retail picture)")
    parser.add_argument("--variant", action="append", default=[],
                        help="VIEW@NAME=COMMAND[;COMMAND...]: one more picture of a view with client console commands run when it is in place "
                             "(a look value to compare in one run; the picture VIEW@NAME)")
    parser.add_argument("--character", default=os.environ.get("SWC_DEV_CHARACTER", ""))
    parser.add_argument("--client-args", nargs=argparse.REMAINDER, default=[])
    if len(sys.argv) == 3 and sys.argv[1] == "--colors":
        # The color report of an existing folder (the numbers before a change): FOLDER/colors.json, colors.txt.
        folder = Path(sys.argv[2])
        colors = color_report.folder_report(folder)
        sides = [folder / "side" / f"{name}.png" for name in colors if (folder / "side" / f"{name}.png").is_file()]
        contact_sheet(sides, folder / "color-sheet.png", panels=[color_report.panel_lines(p.stem, colors[p.stem]) for p in sides])
        print((folder / "colors.txt").read_text(), end="")
        return 0
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
        view_json = folder / entry["json"]
        world = world_hour_of(source)
        if world is not None:
            # An interior with an hour override (set4-weather keep-inside-noon: the client hour 0.0, the world 12:04): the run sets and
            # waits for the world hour; the view JSON for the client gets it (views/NAME.json).
            source["world_hour"]["interior_client_hour"] = source["world_hour"]["client_hour_at_picture"]
            source["world_hour"]["client_hour_at_picture"] = world
            (out / "views").mkdir(exist_ok=True)
            view_json = out / "views" / f"{entry['view']}.json"
            view_json.write_text(json.dumps(source, indent=1))
        camera = source["camera"]
        views.append({"name": entry["view"], "json": str(view_json), "reference": str(folder / entry["file"]),
                      "out": str(out / "client" / entry["file"]), "place": source.get("place", ""),
                      "feet": [source["feet"]["x"], source["feet"]["y"], source["feet"]["z"]],
                      "camera": [camera["x"], camera["y"], camera["z"]], "heading": camera.get("heading_deg_cw_from_north", 0.0),
                      "pitch": camera.get("pitch_deg_positive_up", 0.0), "fov": camera["fov"]["vertical_deg"],
                      "targetHour": source["world_hour"]["client_hour_at_picture"]})
    # More hours of a view: a copy of its JSON with the hour (the retail picture of the view stays the left side, with its own hour).
    for spec in args.hours:
        name, _, hours = spec.partition("=")
        base = next((v for v in views if v["name"] == name), None)
        if base is None:
            entry = next((e for e in index["files"] if e.get("view") == name and "json" in e), None)
            if entry is None:
                print(f"match-view: no view {name} for --hours", flush=True)
                continue
            source = json.loads((folder / entry["json"]).read_text())
        else:
            source = json.loads(Path(base["json"]).read_text())
        for text in hours.split(","):
            hh, mm = (int(x) for x in text.split(":")[:2])
            hour = hh + mm / 60.0
            derived = json.loads(json.dumps(source))
            derived["world_hour"]["client_hour_at_picture"] = hour
            label = f"{name}@{hh:02d}{mm:02d}"
            (out / "views").mkdir(exist_ok=True)
            path = out / "views" / f"{label}.json"
            path.write_text(json.dumps(derived, indent=1))
            camera = derived["camera"]
            views.append({"name": label, "json": str(path), "reference": str(folder / f"{name}.png"), "referenceHour": source["world_hour"]["client_hour_at_picture"],
                          "out": str(out / "client" / f"{label}.png"), "place": derived.get("place", ""),
                          "feet": [derived["feet"]["x"], derived["feet"]["y"], derived["feet"]["z"]], "camera": [camera["x"], camera["y"], camera["z"]],
                          "heading": camera.get("heading_deg_cw_from_north", 0.0), "pitch": camera.get("pitch_deg_positive_up", 0.0),
                          "fov": camera["fov"]["vertical_deg"], "targetHour": hour})
    # Variants of a view: the same place and hour, with console commands of the client (the left side: its retail picture).
    for spec in args.variant:
        label, _, commands = spec.partition("=")
        name = label.partition("@")[0]
        base = next((v for v in views if v["name"] == name), None)
        if base is None:
            print(f"match-view: no view {name} for --variant (name it in the views)", flush=True)
            continue
        views.append(dict(base, name=label, out=str(out / "client" / f"{label}.png"), commands=[c.strip() for c in commands.split(";") if c.strip()]))
    plan = out / "plan.json"
    plan.write_text(json.dumps({"settle": args.settle, "width": 1280, "height": 720,
                                "views": [{"name": v["name"], "json": v["json"], "out": v["out"], "commands": v.get("commands", [])} for v in views]},
                               indent=1))
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
    view_measures(out / f"{args.label}-client.log", views)
    sides = []
    for view in views:
        side = out / "side" / Path(view["out"]).name
        side_by_side(view, Path(view["reference"]), Path(view["out"]), side)
        sides.append(side)
    contact_sheet(sides, out / "contact-sheet.png")
    # The color report (tools/lib/color_report.py): client against retail, the character masked out of both pictures.
    colors, panels, written_sides = {}, [], []
    for view, side in zip(views, sides):
        if not Path(view["out"]).is_file():
            continue
        colors[view["name"]] = color_report.compare(Path(view["out"]), Path(view["reference"]), json.loads(Path(view["json"]).read_text()))
        view["colorSummary"] = color_report.summary(view["name"], colors[view["name"]])
        panels.append(color_report.panel_lines(view["name"], colors[view["name"]]))
        written_sides.append(side)
    (out / "colors.json").write_text(json.dumps(colors, indent=1))
    (out / "colors.txt").write_text("".join(v["colorSummary"] + "\n" for v in views if "colorSummary" in v))
    contact_sheet(written_sides, out / "color-sheet.png", panels=panels)
    for view in views:
        if "colorSummary" in view:
            print("match-view: colors " + view["colorSummary"], flush=True)
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
