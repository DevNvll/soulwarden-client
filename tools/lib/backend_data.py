"""The game data of the backend copy against its release record (tools/play.sh, stage MVP-K).

Usage: python3 -B -I tools/lib/backend_data.py BACKEND_DIR
The release record (BACKEND_DIR/release.json, written by the release of the Soulwarden project) has "gamedata": "entries",
each file with its bytes and each folder with its file count and bytes. A copy that misses a file of its release (a new
file of a release, for example quest-data.json and poi-data.json of schema 15 and 16), or has one of another size (a
part copy), fails. The client reads some of these files at run time (SWC_GAMEDATA_DIR: item-catalog.json, perk-data.json,
crafting-data.json, progression-tables.json); the backend reads all of them. Sizes only: the check takes no time.
Output: one line "ok N entries, M files, B bytes" (status 0) or "FAIL ..." (status 1).
"""
import json
import os
import sys


def folder_counts(path):
    files = size = 0
    for root, _dirs, names in os.walk(path):
        for name in names:
            files += 1
            size += os.path.getsize(os.path.join(root, name))
    return files, size


def main(backend_dir):
    try:
        release = json.load(open(os.path.join(backend_dir, "release.json")))
        entries = release["gamedata"]["entries"]
    except (OSError, KeyError, ValueError) as error:
        print(f"FAIL no game data record in {backend_dir}/release.json ({error})")
        return 1
    gamedata = os.path.join(backend_dir, "gamedata")
    wrong = []
    files = size = 0
    for name, entry in sorted(entries.items()):
        path = os.path.join(gamedata, name)
        if "files" in entry:
            count, total = folder_counts(path) if os.path.isdir(path) else (0, 0)
            if (count, total) != (entry["files"], entry["bytes"]):
                wrong.append(f"{name}/ ({count} files, {total} bytes; the release: {entry['files']}, {entry['bytes']})")
            files, size = files + count, size + total
        elif not os.path.isfile(path):
            wrong.append(f"{name} (missing)")
        elif os.path.getsize(path) != entry["bytes"]:
            wrong.append(f"{name} ({os.path.getsize(path)} bytes; the release: {entry['bytes']})")
        else:
            files, size = files + 1, size + entry["bytes"]
    if wrong:
        print(f"FAIL the game data of the backend copy differs from its release: {', '.join(wrong[:5])}"
              + (f" and {len(wrong) - 5} more" if len(wrong) > 5 else "") + " (run tools/backend-update.sh)")
        return 1
    print(f"ok {len(entries)} entries, {files} files, {size} bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
