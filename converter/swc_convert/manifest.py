"""The manifest of the converted content: the Unreal import script reads it."""
from __future__ import annotations

import hashlib
import re
import json
from pathlib import Path

from . import env


def path() -> Path:
    return env.content_dir() / "manifest.json"


def load() -> dict:
    p = path()
    if p.is_file():
        doc = json.loads(p.read_text())
        for asset, entry in doc["assets"].items():   # the folder rule can change: compute it again
            entry["unreal"] = unreal_folder(entry["kind"], asset)
        return doc
    return {"format": 2, "assets": {}}


def refresh() -> dict:
    """Writes the manifest again with the current folder rule. @return The counts of each kind."""
    doc = load()
    save(doc)
    counts: dict[str, int] = {}
    for entry in doc["assets"].values():
        counts[entry["kind"]] = counts.get(entry["kind"], 0) + 1
    return counts


ROOT = "/SwcGameContent"   # the content plugin outside the repository (tools/content-link.sh)
FOLDERS = {"static": "Meshes", "character": "Characters", "test": "Test", "texture": "Textures", "material": "Materials"}


def unreal_folder(kind: str, asset: str) -> str:
    """@return A short Unreal folder for an asset: /SwcGameContent/KIND/STEM_HASH (a material: /SwcGameContent/Materials,
    with the key of the material as the asset name). Interchange has a budget for the length of an asset path; the game
    paths are too long for it (names became empty, and the imports collided)."""
    if kind == "material":
        return f"{ROOT}/{FOLDERS[kind]}"
    stem = re.sub(r"[^A-Za-z0-9_]", "_", Path(asset.split(":", 1)[-1]).stem)[:40]   # no spaces, commas, dots
    digest = hashlib.sha1(asset.encode()).hexdigest()[:6]
    return f"{ROOT}/{FOLDERS.get(kind, 'Other')}/{stem}_{digest}"


def save(doc: dict) -> None:
    doc["format"] = 2
    path().parent.mkdir(parents=True, exist_ok=True)
    temporary = path().with_suffix(".tmp")
    temporary.write_text(json.dumps(doc, indent=1, sort_keys=True))
    temporary.replace(path())


def add(kind: str, asset: str, gltf: Path) -> None:
    """Adds one converted asset. `kind` is static, character, or test (made by the converter, not game content)."""
    add_many({asset: {"kind": kind, "file": str(gltf.relative_to(env.content_dir()))}})


# The fields of an entry that another step writes (collision.py: the collision file of the physics file of the mesh). A new
# conversion of the asset does not make them, so a replaced entry keeps them. Before (AST-206): a new conversion of a mesh
# removed its "collision" record, and the next import deleted its collision mesh (5,599 meshes lost the record, 115
# collision meshes were deleted: the platform of the Shrine of the Lion, a stone at a travel place).
KEPT_FIELDS = ("collision",)


def add_many(entries: dict[str, dict]) -> None:
    """Adds or replaces entries (asset key -> entry with its kind) and writes the manifest one time. A replaced entry of
    the same kind keeps the KEPT_FIELDS of the old entry when the new entry does not have them."""
    doc = load()
    for asset, entry in entries.items():
        old = doc["assets"].get(asset, {})
        kept = {k: old[k] for k in KEPT_FIELDS if k in old and k not in entry and old.get("kind") == entry["kind"]}
        doc["assets"][asset] = {**entry, **kept, "unreal": unreal_folder(entry["kind"], asset)}
    save(doc)
