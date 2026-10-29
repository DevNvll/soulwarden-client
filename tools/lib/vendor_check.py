#!/usr/bin/env python3
"""The vendored wire codec and state decoders of the client against the backend release of the backend copy.

Usage: vendor_check.py BACKEND_DIR VENDOR_DIR SOULWARDEN_REPO
Prints one line: "ok ..." or "FAIL ...". The client reads each state fragment to its end with these readers (a fragment has no
length): a vendored decoder older than the backend skips the rest of each bundle with a new state type (2026-10-29, the states 6786,
2133, 14, and the Objectives field 5 of release 2026-10-29-87865ea). The Soulwarden repository is read only: `git show` of the
release commit (release.json "commit"). The files: Net/Vendor/soulwarden/NOTICE.md.
"""
import json
import subprocess
import sys
from pathlib import Path

# The vendored files and their place in the Soulwarden repository (apps/backend/ + the same path).
FILES = ("src/server/hub/wire.h", "src/server/hub/wire.cpp", "tests/support/state_decoder.h", "tests/support/item_decoder.h",
         "tests/support/paperdoll_decoder.h", "tests/support/progress_decoder.h")


def main(argv: list[str]) -> int:
    backend, vendor, repo = Path(argv[1]), Path(argv[2]), Path(argv[3])
    try:
        release = json.loads((backend / "release.json").read_text())
    except (OSError, ValueError) as error:
        print(f"FAIL the vendored decoder: no release.json in {backend} ({error})")
        return 0
    commit, version = str(release.get("commit", "")), str(release.get("version", "?"))
    if not commit:
        print(f"FAIL the vendored decoder: release.json of {version} has no commit")
        return 0
    old = []
    for name in FILES:
        try:
            source = subprocess.run(["git", "-C", str(repo), "show", f"{commit}:apps/backend/{name}"], check=True, capture_output=True).stdout
        except (OSError, subprocess.CalledProcessError):
            print(f"FAIL the vendored decoder: the commit {commit[:9]} of the release {version} is not readable in {repo} ({name})")
            return 0
        try:
            mine = (vendor / name).read_bytes()
        except OSError:
            mine = b""
        if mine != source:
            old.append(name)
    if old:
        print(f"FAIL the vendored decoder is older than the backend release {version}: copy it (Net/Vendor/soulwarden/NOTICE.md): "
              + ", ".join(old))
    else:
        print(f"ok the vendored decoder is the one of the backend release {version} ({commit[:9]}, {len(FILES)} files)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
