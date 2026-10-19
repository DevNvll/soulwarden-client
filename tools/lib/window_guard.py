#!/usr/bin/env python3
"""The window guard of a window run (tools/dev-run.sh --window; AGENTS.md, the run rules): the window of a run must stay on its own agent
desktop, never on a workspace of the user.

The trap: the agent desktop places a window only when it appears in the wait of `omarchy-agent-desktop run --wait` (at most 300 s).
gamescope shows its window when the client starts, and tools/lib/session.sh can first wait up to SWC_WORLD_RUN_WAIT seconds for the
world lock (a level build or an import). A window that comes later goes to the focused workspace of the user (2026-10-19).

The guard watches the run for its whole life: each second it reads `hyprctl clients -j` (read only) and finds the windows of the
launch (their process has AGENT_DESKTOP_LAUNCH=<the folder name of the launch> in its environment, the token of the agent desktop).
A window that is not on the workspace __agent:DESKTOP is moved with `omarchy-agent-desktop adopt DESKTOP ADDRESS`; when that fails
or the window is still not there, the guard stops the run (the process of the launch, then tools/dev-stop.sh). It ends with the
launch (its status file has an exit code, or its process is gone).

Usage: window_guard.py DESKTOP LAUNCH_OUTPUT_FILE [LOG]   (LAUNCH_OUTPUT_FILE: the output of omarchy-agent-desktop run: the JSON, or the
error text with the log path of the launch)
"""
from __future__ import annotations

import json
import os
import re
import signal
import subprocess
import sys
import time
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]


def log(handle, text: str) -> None:
    handle.write(f"{time.strftime('%H:%M:%S')} {text}\n")
    handle.flush()


def launch_folder(output: str) -> Path | None:
    """@return The folder of the launch from the output of omarchy-agent-desktop run (its JSON, or its error text)."""
    match = re.search(r"(/[^\s\"']*/agent-desktops/launches/[^/\s\"']+)/(?:output\.log|status\.json)", output)
    return Path(match[1]) if match else None


def launch_pid(folder: Path) -> int:
    try:
        return int(json.loads((folder / "status.json").read_text()).get("pid") or 0)
    except (OSError, ValueError):
        return 0


def launch_ended(folder: Path, pid: int) -> bool:
    try:
        if json.loads((folder / "status.json").read_text()).get("exitCode") is not None:
            return True
    except (OSError, ValueError):
        pass
    return pid > 0 and not Path(f"/proc/{pid}").exists()


def clients() -> list[dict]:
    result = subprocess.run(["hyprctl", "clients", "-j"], capture_output=True, text=True, check=False)
    try:
        return json.loads(result.stdout) if result.returncode == 0 else []
    except ValueError:
        return []


def launch_windows(token: bytes) -> list[dict]:
    """@return The windows whose process has the launch token in its environment."""
    out = []
    for client in clients():
        try:
            environment = Path(f"/proc/{int(client.get('pid') or 0)}/environ").read_bytes()
        except (OSError, ValueError):
            continue
        if token in environment.split(b"\0"):
            out.append(client)
    return out


def stop_run(handle, pid: int) -> None:
    if pid > 0:
        try:
            os.kill(pid, signal.SIGTERM)
            log(handle, f"stopped the process {pid} of the launch")
        except OSError as error:
            log(handle, f"cannot stop the process {pid}: {error}")
    subprocess.run([str(REPO / "tools/dev-stop.sh")], capture_output=True, check=False)
    log(handle, "tools/dev-stop.sh done")


def main() -> int:
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    desktop, output_file = sys.argv[1], Path(sys.argv[2])
    handle = open(sys.argv[3], "a") if len(sys.argv) > 3 else sys.stderr
    workspace = f"__agent:{desktop}"
    folder = launch_folder(output_file.read_text(errors="replace")) if output_file.is_file() else None
    if folder is None:
        log(handle, f"no launch folder in {output_file}: the guard cannot watch this run")
        return 1
    token = f"AGENT_DESKTOP_LAUNCH={folder.name}".encode()
    pid = launch_pid(folder)
    log(handle, f"watching the launch {folder.name} (process {pid}) for the workspace {workspace}")
    placed: set[str] = set()
    while not launch_ended(folder, pid):
        for window in launch_windows(token):
            address = str(window.get("address") or "")
            name = str((window.get("workspace") or {}).get("name") or "")
            if name == workspace:
                if address not in placed:
                    placed.add(address)
                    log(handle, f"the window {address} ({window.get('class')}) is on {workspace}")
                continue
            log(handle, f"the window {address} ({window.get('class')}) is on the workspace {name!r}, not on {workspace}: adopt")
            result = subprocess.run(["omarchy-agent-desktop", "adopt", desktop, address], capture_output=True, text=True, check=False)
            time.sleep(0.5)
            now = next((c for c in clients() if str(c.get("address") or "") == address), None)
            if result.returncode != 0 or now is None or str((now.get("workspace") or {}).get("name") or "") != workspace:
                log(handle, f"adopt failed (status {result.returncode}: {result.stderr.strip()[:200]}): the run stops")
                stop_run(handle, pid)
                return 3
            placed.add(address)
            log(handle, f"the window {address} is on {workspace} now")
        time.sleep(1.0)
    log(handle, "the launch ended")
    return 0


if __name__ == "__main__":
    sys.exit(main())
