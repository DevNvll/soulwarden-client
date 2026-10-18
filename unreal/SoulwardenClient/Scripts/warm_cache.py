"""Fills the file store of the derived data cache with the derived data of regions (tools/warm-cache.sh).

A development run in its network namespace cannot reach Zen and uses only the file store. With the graph
SWC_EDITOR_DDC (the file store before Zen), each Zen hit of this commandlet fills the file store: no new build. The
script loads the world level (its always loaded actors: the Landscapes and the far meshes) and the static meshes of
each region (with their materials and textures), and waits for their derived data. It needs the renderer (with
-nullrhi the editor does not cache the render data). After a batch that passes SWC_WARM_MAX_SECONDS, the call stops
("incomplete" in the report); the next call continues. SWC_GAME_CONTENT/warm-state.json has the meshes done for
each region.
Environment: SWC_CONTENT, SWC_GAME_CONTENT, SWC_WARM_REGIONS (the region names), SWC_WARM_MAX_SECONDS (default 300).
"""
import json
import os
import time

import unreal

content = os.environ["SWC_CONTENT"]
plugin = os.environ["SWC_GAME_CONTENT"]
regions = os.environ.get("SWC_WARM_REGIONS", "").split()
MAX_SECONDS = float(os.environ.get("SWC_WARM_MAX_SECONDS") or 300)
BATCH = 200
LEVEL = "/SwcGameContent/World/NewWorld"
started = time.time()
library = unreal.SwcEditorLibrary
assets = unreal.EditorAssetLibrary
unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous(["/SwcGameContent"], True)
manifest = json.load(open(os.path.join(content, "manifest.json")))["assets"]
state_path = os.path.join(plugin, "warm-state.json")
state = json.load(open(state_path)) if os.path.isfile(state_path) else {}


def log(text):
    unreal.log("SWC " + text)


def save_state():
    with open(state_path + ".tmp", "w") as handle:
        json.dump(state, handle, indent=1, sort_keys=True)
    os.replace(state_path + ".tmp", state_path)


def load_meshes(key):
    """Loads the static mesh of a manifest record. @return The number of loaded meshes."""
    count = 0
    for path in assets.list_assets(manifest[key]["unreal"], recursive=True, include_folder=False):
        if isinstance(assets.load_asset(path), unreal.StaticMesh):
            count += 1
    return count


# The always loaded actors of the level: the Landscapes (their textures and material instances) and the far meshes.
phase = time.time()
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level(LEVEL)
library.finish_compilation()
log(f"warm: level {LEVEL} in {time.time() - phase:.1f} s")
incomplete = False
loaded = 0
for name in regions:
    keys = [key for key in json.load(open(os.path.join(content, "regions", name, "meshes.json")))
            if manifest.get(key, {}).get("kind") == "static"]
    keys += sorted(key for key in manifest if key.startswith(f"impostor:{name}:"))
    done = state.get(name, 0)
    while done < len(keys):
        if time.time() - started > MAX_SECONDS:
            incomplete = True
            break
        phase = time.time()
        for key in keys[done:done + BATCH]:
            loaded += load_meshes(key)
        library.finish_compilation()
        unreal.SystemLibrary.collect_garbage()
        done = min(done + BATCH, len(keys))
        state[name] = done
        save_state()
        log(f"warm: region {name}: {done} of {len(keys)} meshes ({time.time() - phase:.1f} s)")
    if incomplete:
        break
report = {"regions": regions, "meshes loaded": loaded, "incomplete": incomplete,
          "seconds": round(time.time() - started, 1)}
log("WARM " + json.dumps(report))
