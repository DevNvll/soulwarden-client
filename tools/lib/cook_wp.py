#!/usr/bin/env python3
"""Check NewWorld source times, or resave its cook revision inside Unreal."""
import argparse
import os
from pathlib import Path
import shutil
import struct
import time
import uuid

PACKAGE = "/SwcGameContent/World/NewWorld"
PACKAGE_SUFFIXES = {".uasset", ".umap", ".uexp", ".ubulk", ".uptnl"}


def package_files(content):
    source_map = content / "World/NewWorld.umap"
    if not source_map.is_file():
        raise RuntimeError("NewWorld source map is missing")
    yield source_map
    for relative in ("__ExternalActors__/World/NewWorld",
                     "__ExternalObjects__/World/NewWorld", "World/NewWorld"):
        root = content / relative
        if not root.exists():
            continue
        def walk_error(error):
            raise error
        for directory, _, names in os.walk(root, onerror=walk_error):
            for name in names:
                path = Path(directory) / name
                if path.suffix.lower() in PACKAGE_SUFFIXES:
                    yield path


def check(content, cooked_map, stamp=None):
    newest = max(((p.stat().st_mtime_ns, p) for p in package_files(content)),
                 key=lambda item: item[0])
    if not cooked_map.is_file():
        return "NewWorld has no cooked map; run tools/cook.sh"
    cutoff = cooked_map.stat().st_mtime_ns
    if stamp is not None:
        cutoff = min(cutoff, int(stamp * 1_000_000_000))
    if newest[0] > cutoff:
        return (f"World Partition package {newest[1].relative_to(content)} "
                "is newer than the cooked world or staged build; run tools/cook.sh")
    return None


def saved_hash(path):
    # UE 5.8 binary summary. PackageFileSummary.cpp:553-563 puts the hash at 24.
    # Reject a different format. Never write a package header from this tool.
    with path.open("rb") as stream:
        header = stream.read(44)
    if len(header) != 44:
        raise RuntimeError("short NewWorld package header")
    tag, legacy, ue3, ue4, ue5, licensee = struct.unpack("<Iiiiii", header[:24])
    if (tag, legacy, ue3, ue4, ue5, licensee) != (0x9E2A83C1, -9, 864, 522, 1018, 0):
        raise RuntimeError("NewWorld package format differs from the studied UE 5.8 format")
    value = header[24:44]
    if value == bytes(20):
        raise RuntimeError("NewWorld has a zero saved hash")
    return value


def editor_resave():
    import unreal

    content = Path(os.environ["SWC_GAME_CONTENT"]) / "Content"
    source_map = content / "World/NewWorld.umap"
    linked_map = (Path(os.environ["SWC_REPO"]) / "unreal/SoulwardenClient/Plugins/"
                  "SwcGameContent/Content/World/NewWorld.umap")
    if not linked_map.samefile(source_map):
        raise RuntimeError("the project plugin does not use SWC_GAME_CONTENT")
    before = saved_hash(source_map)
    backup_dir = Path(os.environ["SWC_WORK"]) / "work/cook-wp-map-backups"
    backup_dir.mkdir(parents=True, exist_ok=True)
    revision = uuid.uuid4().hex
    backup = backup_dir / f"NewWorld-{time.time_ns()}-{revision}.umap"
    shutil.copy2(source_map, backup)
    unreal.log(f"SWC cook WP: source map backup {backup}")
    world = unreal.load_object(None, PACKAGE + ".NewWorld")
    if not isinstance(world, unreal.World) or world.get_path_name() != PACKAGE + ".NewWorld":
        raise RuntimeError("cannot load the NewWorld world object")
    unreal.EditorAssetLibrary.set_metadata_tag(world, "SWCExternalPackagesCookRevision", revision)
    if not unreal.EditorLoadingAndSavingUtils.save_map(world, PACKAGE):
        raise RuntimeError("cannot save NewWorld; inspect the source map backup")
    after = saved_hash(source_map)
    if after == before:
        raise RuntimeError("NewWorld saved hash did not change; do not stage this cook")
    unreal.log(f"SWC cook WP: saved hash changed {before.hex()} -> {after.hex()}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("content", type=Path)
    parser.add_argument("cooked_map", type=Path)
    parser.add_argument("--stamp", type=float)
    args = parser.parse_args()
    try:
        reason = check(args.content, args.cooked_map, args.stamp)
    except (OSError, RuntimeError) as error:
        print(f"cannot check World Partition: {error}")
        return 2
    if reason:
        print(reason)
        return 3
    return 0


if __name__ == "__main__":
    if os.environ.get("SWC_WP_RESAVE") == "1":
        editor_resave()
    else:
        raise SystemExit(main())
