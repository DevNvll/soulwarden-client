"""Paths and sources of the converter.

The converter reads the game copy of the user and writes the converted content to a folder outside the
repository. Nothing that it writes goes into git.
"""
from __future__ import annotations

import os
import sys
from pathlib import Path

sys.dont_write_bytecode = True

HOME = Path.home()
VENDOR = Path(__file__).resolve().parent.parent / "vendor" / "soulwarden"
if str(VENDOR) not in sys.path:
    sys.path.insert(0, str(VENDOR))

# The Linux Oodle library of the installed Unreal Engine (the pak entries with method 15 need it).
DEFAULT_OODLE = (HOME / "Applications/UnrealEngine-5.8.0/Engine/Binaries/DotNET/AutomationTool/AutomationScripts/"
                 "BuildGraph/net10.0/runtimes/linux-x64/native/liboo2core.so")
os.environ.setdefault("SOULWARDEN_OODLE_LIB", str(DEFAULT_OODLE))
os.environ.setdefault("SOULWARDEN_GAME_DIR", str(HOME / "Games/soulwarden/game"))


def work_dir() -> Path:
    """The work folder of the client project (outside the repository)."""
    return Path(os.environ.get("SWC_WORK") or HOME / "Games/soulwarden-client")


def content_dir() -> Path:
    """The converted content (glTF, PNG, JSON). Made from the game copy: never in git."""
    return Path(os.environ.get("SWC_CONTENT") or work_dir() / "content")


def cache_dir() -> Path:
    path = work_dir() / "cache"
    path.mkdir(parents=True, exist_ok=True)
    return path
