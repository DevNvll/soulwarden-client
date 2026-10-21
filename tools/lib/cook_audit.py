#!/usr/bin/env python3
"""The audit of the asset paths of the cooked build (stage MVP-K; docs/WORLD.md "The cooked build").

The client loads many assets by a path at run time: paths in its code (TEXT("/SwcGameContent/...")) and paths in the converter data
that it reads from SWC_CONTENT (tools/lib/loose-data.txt). The cook includes the default map, its references, and the folders of
DirectoriesToAlwaysCook (Config/DefaultGame.ini). This tool takes each such path and checks that its package is in the cooked
files of the checkout (Saved/Cooked/Linux, the input of the staged build of tools/cook.sh).

Usage: python3 -B tools/lib/cook_audit.py [--cooked DIR] [--report FILE] | --newest
Output: one line for each class (cooked; imported after the cook of the stamp SWC_COOKED/Linux/swc-cook.json: the next cook takes
it; imported before the cook and not cooked: a gap of the cook rules; not imported), the first paths of each class with the data
file that names them, and the path templates of the code (a %s in the path) with the rule that covers them. Status 1 for a gap of
the cook rules.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
PROJECT = REPO / "unreal/SoulwardenClient"
WORK = Path(os.environ.get("SWC_WORK") or Path.home() / "Games/soulwarden-client")
CONTENT = Path(os.environ.get("SWC_CONTENT") or WORK / "content")
PLUGIN = Path(os.environ.get("SWC_GAME_CONTENT") or WORK / "unreal/SwcGameContent") / "Content"
ENGINE = Path(os.environ.get("UE_ROOT") or Path.home() / "Applications/UnrealEngine-5.8.0") / "Engine/Content"

PATH_RE = re.compile(r'(/(?:SwcGameContent|Game|Engine)/[A-Za-z0-9_\-/. %]+)')
CODE_RE = re.compile(r'TEXT\("(/(?:SwcGameContent|Game|Engine)/[^"]*)"\)')


def loose_globs() -> list[str]:
    """The patterns of tools/lib/loose-data.txt (relative to SWC_CONTENT; # comments)."""
    lines = (REPO / "tools/lib/loose-data.txt").read_text().splitlines()
    return [line.split("#", 1)[0].strip() for line in lines if line.split("#", 1)[0].strip()]


def loose_files() -> list[Path]:
    out = []
    for pattern in loose_globs():
        out.extend(sorted(p for p in CONTENT.glob(pattern) if p.is_file()))
    return out


def always_cook() -> list[str]:
    text = (PROJECT / "Config/DefaultGame.ini").read_text()
    return re.findall(r'\+DirectoriesToAlwaysCook=\(Path="([^"]+)"\)', text)


def package_of(path: str) -> str:
    """'/SwcGameContent/A/B.B' or '/SwcGameContent/A/B.B:Sub' -> '/SwcGameContent/A/B'."""
    path = path.split(":", 1)[0]
    head, _, tail = path.rpartition("/")
    return head + "/" + tail.split(".", 1)[0]


def composed(document) -> list[str]:
    """The packages of the entries with an 'unreal' folder and no 'asset': FOLDER/STEM/StaticMeshes/STEM, the rule of the menu scene
    (Menu/SwcMenuPreview: 'stem') and of the roads (World/SwcSurfaceLayer: the name of 'file'; the glTF import of Interchange)."""
    out: list[str] = []
    stack = [document]
    while stack:
        item = stack.pop()
        if isinstance(item, list):
            stack.extend(item)
        elif isinstance(item, dict):
            folder = item.get("unreal")
            if isinstance(folder, str) and folder.startswith("/") and not item.get("asset"):
                stem = item.get("stem") or Path(str(item.get("file") or "")).stem
                if stem:
                    out.append(f"{folder}/{stem}/StaticMeshes/{stem}")
            stack.extend(v for v in item.values() if isinstance(v, (list, dict)))
    return out


def files_of(package: str, cooked: Path) -> tuple[Path | None, Path | None]:
    """The source and the cooked file of a package (None: not mapped)."""
    mount, _, rest = package[1:].partition("/")
    if mount == "SwcGameContent":
        source, target = PLUGIN / rest, cooked / "SoulwardenClient/Plugins/SwcGameContent/Content" / rest
    elif mount == "Engine":
        source, target = ENGINE / rest, cooked / "Engine/Content" / rest
    elif mount == "Game":
        source, target = PROJECT / "Content" / rest, cooked / "SoulwardenClient/Content" / rest
    else:
        return None, None
    return source, target


def exists(base: Path) -> bool:
    return base.with_suffix(".uasset").is_file() or base.with_suffix(".umap").is_file() or \
        Path(str(base) + ".uasset").is_file() or Path(str(base) + ".umap").is_file()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cooked", default=str(PROJECT / "Saved/Cooked/Linux"))
    parser.add_argument("--report", default="")
    parser.add_argument("--show", type=int, default=12)
    parser.add_argument("--newest", action="store_true",
                        help="print the time and the path of the newest loose data file (tools/lib/cooked.sh), nothing else")
    args = parser.parse_args()
    if args.newest:
        newest = max(((f.stat().st_mtime, f) for f in loose_files()), default=(0.0, Path("-")))
        print(f"{newest[0]:.0f} {newest[1].relative_to(CONTENT) if newest[0] else '-'}")
        return 0
    cooked = Path(args.cooked)
    if not cooked.is_dir():
        print(f"cook-audit: no cooked files in {cooked} (tools/cook.sh)", file=sys.stderr)
        return 2
    refs: dict[str, set[str]] = {}
    templates: dict[str, set[str]] = {}
    files = loose_files()
    for path in files:
        if path.suffix not in (".json", ".txt"):
            continue
        where = str(path.relative_to(CONTENT))
        text = path.read_text(errors="replace")
        for match in PATH_RE.findall(text):
            # An object path (the last part has '.Object'); a bare folder is not a load by itself (the visual rows of the objects
            # name the folders of their meshes beside the full paths).
            if "%" in match:
                templates.setdefault(match, set()).add(where)
            elif "." in match.rsplit("/", 1)[-1]:
                refs.setdefault(package_of(match), set()).add(where)
        if path.suffix == ".json" and '"unreal"' in text:
            for package in composed(json.loads(text)):
                refs.setdefault(package, set()).add(where)
    for source in sorted((PROJECT / "Source").rglob("*.cpp")) + sorted((PROJECT / "Source").rglob("*.h")):
        for match in CODE_RE.findall(source.read_text(errors="replace")):
            where = str(source.relative_to(PROJECT))
            if "%" in match:
                templates.setdefault(match, set()).add(where)
            elif "." in match.rsplit("/", 1)[-1]:
                refs.setdefault(package_of(match), set()).add(where)
    stamp = Path(os.environ.get("SWC_COOKED") or WORK / "cooked") / "Linux/swc-cook.json"
    cook_time = json.loads(stamp.read_text())["time"] if stamp.is_file() else 0.0
    classes: dict[str, list[str]] = {"cooked": [], "imported after the cook": [], "imported, not cooked": [], "not imported": [],
                                     "not mapped": []}
    for package in sorted(refs):
        source, target = files_of(package, cooked)
        if source is None:
            classes["not mapped"].append(package)
        elif exists(target):
            classes["cooked"].append(package)
        elif exists(source):
            imported = max((f.stat().st_mtime for f in (source.with_suffix(".uasset"), Path(str(source) + ".uasset")) if f.is_file()),
                           default=0.0)
            classes["imported after the cook" if imported > cook_time else "imported, not cooked"].append(package)
        else:
            classes["not imported"].append(package)
    rules = always_cook()
    lines = [f"cook-audit: {len(files)} loose data files, {len(refs)} asset packages by path, {len(templates)} path templates"]
    for name, packages in classes.items():
        lines.append(f"  {name}: {len(packages)}")
        if name != "cooked":
            for package in packages[: args.show]:
                lines.append(f"    {package}  ({', '.join(sorted(refs[package])[:3])})")
    for template, wheres in sorted(templates.items()):
        folder = template.split("%", 1)[0].rstrip("/")
        covered = [rule for rule in rules if folder == rule or folder.startswith(rule + "/")]
        lines.append(f"  template {template} ({', '.join(sorted(wheres)[:2])}): "
                     + (f"always cooked ({covered[0]})" if covered else "NOT in DirectoriesToAlwaysCook"))
    text = "\n".join(lines)
    print(text)
    if args.report:
        Path(args.report).write_text(json.dumps({k: {p: sorted(refs[p]) for p in v} for k, v in classes.items()}, indent=1))
    return 1 if classes["imported, not cooked"] else 0


if __name__ == "__main__":
    sys.exit(main())
