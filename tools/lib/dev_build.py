#!/usr/bin/env python3
"""Development runs: gives a character of the development account a build preset of the backend (the equipment, the
level, the attributes, the weapon masteries, the tree nodes, and the ability bars: apps/backend/src/world/build_presets.h
of the Soulwarden repository), then runs a command. It runs in the network namespace of tools/lib/session.sh, after the
backend is ready (tools/dev-build.sh). The development database keeps the build for the next runs.

Usage: dev_build.py [--list] [--preset NAME] [--variant NAME] [--character NAME] -- COMMAND...
       dev_build.py --list-source
  --list        the presets of the backend and their state (the backend route: only in a session)
  --list-source the presets of the backend source at the commit of the backend copy (no session, read only: release.json
                of SWC_BACKEND_DIR, git show in SOULWARDEN_REPO; tools/dev-build.sh --list). A preset that the game data
                disables is in the backend log of a session ("presets: the build preset NAME is disabled")
  --preset      the preset (default bow-boltcaster)
  --character   the name of the character (default: the first character of the account)

The account is the made-up identity of the development run (SWC_STEAM_ID, SWC_PERSONA_NAME). The preset route is a
management route of the backend: the request has no service host.
"""
from __future__ import annotations

import json
import os
import re
import subprocess
import sys
import urllib.error
import urllib.parse
import urllib.request

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dev_character  # noqa: E402  (the token and the character list of the development account)

HTTP = dev_character.HTTP


def management(method: str, path: str):
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    request = urllib.request.Request(HTTP + path, method=method, data=b"" if method == "POST" else None)
    try:
        with opener.open(request, timeout=30) as reply:
            return reply.status, reply.read().decode("utf-8", "replace")
    except urllib.error.HTTPError as error:
        return error.code, error.read().decode("utf-8", "replace")


def characters() -> list[dict]:
    status, reply = dev_character.request("POST", "tokenservice.soulwarden.local", "/games/new-world/tokens",
                                          {"platformIdentityType": "steam", "platformAuth": {"ticket": dev_character.ticket()},
                                           "fallbackToken": "", "attributionData": {}})
    token = reply.get("accessToken", "")
    if status != 200 or not token:
        raise RuntimeError(f"no account token ({status})")
    status, reply = dev_character.request("GET", "gateway.soulwarden.local",
                                          f"/prod/game/getlogininfo?channelId={dev_character.CHANNEL}", token=token)
    if status != 200:
        raise RuntimeError(f"no character list ({status})")
    return reply.get("LoginInfoList", {}).get("Characters", [])


PRESETS_SOURCE = "apps/backend/src/world/build_presets.cpp"


def source_presets() -> int:
    """Prints the presets of the backend source at the commit of the backend copy: the name, the title, the variants."""
    backend = os.environ.get("SWC_BACKEND_DIR", "")
    repository = os.environ.get("SOULWARDEN_REPO", "")
    try:
        release = json.load(open(os.path.join(backend, "release.json")))
    except OSError as error:
        print(f"dev-build: no release.json in the backend copy {backend} ({error.strerror})", file=sys.stderr)
        return 1
    commit = release.get("commit", "")
    done = subprocess.run(["git", "-C", repository, "show", f"{commit}:{PRESETS_SOURCE}"], capture_output=True, text=True)
    if done.returncode != 0 or not commit:
        print(f"dev-build: no {PRESETS_SOURCE} at the commit {commit or '(none)'} of the backend copy: {done.stderr.strip()}", file=sys.stderr)
        return 1
    text = done.stdout
    starts = [m.start() for m in re.finditer(r'preset\.name\s*=\s*"', text)] + [len(text)]
    print(f"dev-build: the presets of the backend {release.get('version', commit[:7])} (the source; the backend log of a "
          "session names a preset that the game data disables)")
    for start, end in zip(starts, starts[1:]):
        block = text[start:end]
        name = re.match(r'preset\.name\s*=\s*"([^"]+)"', block).group(1)
        title = re.search(r'preset\.title\s*=\s*"([^"]*)"', block)
        variants = re.search(r"preset\.variants\s*=\s*\{(.*?)\};", block, re.S)
        found = re.findall(r'\{"([^"]+)",\s*"([^"]*)"', variants.group(1)) if variants else []
        names = ", ".join(f"{v} ({label})" for v, label in found) or "none"
        print(f"  {name}: {title.group(1) if title else ''}; variants: {names}")
    return 0


def main(argv: list[str]) -> int:
    if argv[1:] == ["--list-source"]:
        return source_presets()
    if "--" not in argv:
        print(__doc__, file=sys.stderr)
        return 2
    split = argv.index("--")
    options = argv[1:split]
    command = argv[split + 1:]
    preset, variant, name, listing = "bow-boltcaster", "", "", False
    i = 0
    while i < len(options):
        if options[i] == "--list":
            listing = True
        elif options[i] == "--preset":
            i += 1
            preset = options[i]
        elif options[i] == "--variant":
            i += 1
            variant = options[i]
        elif options[i] == "--character":
            i += 1
            name = options[i]
        i += 1
    if listing:
        status, text = management("GET", "/v1/dev/presets")
        print(f"dev-build: presets ({status}): {text}", file=sys.stderr)
    else:
        found = characters()
        chosen = [c for c in found if not name or c.get("Name") == name]
        if not chosen:
            print(f"dev-build: no character {name or '(first)'} in the development account", file=sys.stderr)
            return 1
        character = chosen[0]["CharacterId"]
        query = {"character": character, "preset": preset}
        if variant:
            query["variant"] = variant
        status, text = management("POST", "/v1/dev/presets/apply?" + urllib.parse.urlencode(query))
        print(f"dev-build: preset {preset} for character {character}: {status} {text[:400]}", file=sys.stderr)
        if status != 200:
            return 1
    if command:
        os.execvp(command[0], command)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
