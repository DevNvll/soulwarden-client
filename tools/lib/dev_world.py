#!/usr/bin/env python3
"""The development routes of the backend of a development run of this project (docs/CREATURES.md, the checks).

The backend of a development session with SWC_SESSION_BACKEND_ARGS=--dev-commands has the routes /v1/dev/... of the
Soulwarden development tools (~/Developer/azoth/tools/world, read only). The backend is in the network namespace of the
session: the request goes with nsenter into that namespace (the user namespace of the session belongs to the user).
Only the backend of SWC_DEV_DATA with the mark SWC_DEV_RUN=1 is a target: never the data folder of the user.

Usage (tools/creature-world.sh):
  characters                         the characters of the development database (id and name)
  creatures [RANGE]                  the creatures of the world (near the character)
  kinds [TEXT]                       the creature kinds of the backend data
  spawn KIND [DX DY DZ] [--level N] [--yaw DEGREES]
                                     a creature of a kind at the character plus a distance (m: east, north, up)
  kill ID | remove ID                a creature
  health ID VALUE|PERCENT%           the health of a creature
  action ID NAME [SECONDS]           an action of the action table of the slice of a creature
  heal                               the health of the character
  level N                            the level of the character (its health grows with it)
  ignore on|off                      the creatures ignore the character (on) or not
  loot on|off                        each kill of the character drops loot (on; stage 3.5b.2: a loot bag) or the normal roll
  bags                               the loot bags of the character
  shoot CREATURE                     the creature starts its basic shot at the character now (stage 3.4f-A)
  area CREATURE                      the creature starts its next area attack at the character now (stage 3.4f-C)
  hurt AMOUNT                        damage of the character (an amount at or above its health kills it: the death, stage P2.10)
  travel X Y Z [auto|on|off]         a travel of the character to a place (engine m; the loading screen rule of the backend)
  travel PLACE [auto|on|off]         a travel to a travel place of the game data (shrine_40170, inn_40300)
  fixture NAME [DX DY DZ]            a fixture of the backend at the character (powderKeg: the timed use "Ignite Fuse", 5 s)
  remove-object ENTITY               removes a server child (the entity of the fixture reply, 16 hex digits)
  cancel                             the server ends the interaction of the character (3746)
  script FILE [--clock LOG]          a timed list of these commands: "SECONDS COMMAND WORDS..." each line (the seconds from
                                     now, or on the clock of the drive script in the client log LOG: its "drive: SECONDS s"
                                     lines; a drive line "SECONDS tick" is a clock mark that does nothing)
The character is the first character of the database, or SWC_DEV_CHARACTER (a name).
"""
from __future__ import annotations

import json
import os
import re
import sqlite3
import subprocess
import sys
import time
from pathlib import Path

BACKEND_NAME = "soulwarden-backend"
RUN_MARK = b"SWC_DEV_RUN=1"


def fail(text: str) -> None:
    sys.exit(f"creature-world: {text}")


def data_dir() -> Path:
    value = os.environ.get("SWC_DEV_DATA")
    if not value:
        fail("SWC_DEV_DATA is not set (tools/env.sh sets it)")
    path = Path(value).resolve()
    player = Path(os.environ.get("SWC_PLAYER_DATA", str(Path.home() / ".local/share/soulwarden"))).resolve()
    if path == player:
        fail("SWC_DEV_DATA is the data folder of the user: never a target of a development route")
    return path


def backend(folder: Path) -> tuple[int, int]:
    """@return The process id and the HTTP port of the backend of the development run with this data folder."""
    for directory in Path("/proc").iterdir():
        if not directory.name.isdigit():
            continue
        try:
            arguments = (directory / "cmdline").read_bytes().split(b"\0")
            if Path(arguments[0].decode("utf-8", "replace")).name != BACKEND_NAME:
                continue
            if RUN_MARK not in (directory / "environ").read_bytes().split(b"\0"):
                continue
        except OSError:
            continue
        words = [word.decode("utf-8", "replace") for word in arguments]
        run_folder = words[words.index("--data-dir") + 1] if "--data-dir" in words[:-1] else ""
        port = int(words[words.index("--http-port") + 1]) if "--http-port" in words[:-1] else 32200
        if run_folder and Path(run_folder).resolve() == folder:
            return int(directory.name), port
    fail(f"no backend of a development run uses {folder}: start tools/dev-run.sh with SWC_SESSION_BACKEND_ARGS=--dev-commands")
    return 0, 0


def characters(folder: Path) -> list[tuple[str, str]]:
    connection = sqlite3.connect(f"file:{folder / 'soulwarden.sqlite3'}?mode=ro", uri=True)
    try:
        return [(row[0], row[1]) for row in
                connection.execute("select character_id, name from characters where deleted_date is null order by name")]
    finally:
        connection.close()


def character(folder: Path) -> str:
    rows = characters(folder)
    wanted = os.environ.get("SWC_DEV_CHARACTER", "")
    for identifier, name in rows:
        if not wanted or name == wanted:
            return identifier
    fail("no character in the development database")
    return ""


def request(folder: Path, method: str, path: str) -> dict:
    process, port = backend(folder)
    command = ["nsenter", "-t", str(process), "-U", "-n", "--preserve-credentials", "curl", "--noproxy", "*", "--silent",
               "--max-time", "5", "--request", method, "--write-out", "\n%{http_code}", f"http://127.0.0.1:{port}{path}"]
    result = subprocess.run(command, capture_output=True, text=True, check=False)
    if result.returncode != 0:
        fail(f"no reply of the backend (status {result.returncode}): {result.stderr.strip()}")
    body, _, status = result.stdout.rpartition("\n")
    try:
        document = json.loads(body)
    except ValueError:
        document = {"message": body}
    if not status.startswith("2"):
        fail(f"{method} {path.split('?')[0]}: status {status}: {document.get('message', body)[:300] if isinstance(document, dict) else body[:300]}")
    return document


def run(words: list[str], folder: Path) -> object:
    if not words:
        fail("no command (see the head of tools/lib/dev_world.py)")
    command, rest = words[0], words[1:]
    if command == "characters":
        return [{"id": i, "name": n} for i, n in characters(folder)]
    if command == "creatures":
        query = f"?character={character(folder)}" + (f"&range={rest[0]}" if rest else "")
        return request(folder, "GET", "/v1/dev/creatures" + query)
    if command == "kinds":
        return request(folder, "GET", "/v1/dev/creatures/kinds" + (f"?q={rest[0]}" if rest else ""))
    if command == "spawn" and rest:
        options = ""
        for name in ("--level", "--yaw"):
            if name in rest:
                place = rest.index(name)
                options += f"&{name[2:]}={rest[place + 1]}"
                del rest[place:place + 2]
        if len(rest) == 4:
            options += f"&dx={float(rest[1])}&dy={float(rest[2])}&dz={float(rest[3])}"
        return request(folder, "POST", f"/v1/dev/creatures/spawn?character={character(folder)}&kind={rest[0]}{options}")
    if command in ("kill", "remove") and len(rest) == 1:
        return request(folder, "POST", f"/v1/dev/creatures/{command}?creature={rest[0]}")
    if command == "loot" and rest and rest[0] in ("on", "off"):
        return request(folder, "POST", f"/v1/dev/world/force-loot?character={character(folder)}&force={1 if rest[0] == 'on' else 0}")
    if command == "bags":
        return request(folder, "GET", f"/v1/dev/lootbags?character={character(folder)}")
    if command in ("shoot", "area") and len(rest) == 1:
        return request(folder, "POST", f"/v1/dev/creatures/{command}?creature={rest[0]}&character={character(folder)}")
    if command == "health" and len(rest) == 2:
        amount = f"percent={float(rest[1][:-1])}" if rest[1].endswith("%") else f"health={float(rest[1])}"
        return request(folder, "POST", f"/v1/dev/creatures/health?creature={rest[0]}&{amount}")
    if command == "action" and len(rest) in (2, 3):
        seconds = f"&seconds={float(rest[2])}" if len(rest) == 3 else ""
        return request(folder, "POST", f"/v1/dev/creatures/action?creature={rest[0]}&action={rest[1]}{seconds}")
    if command == "level" and len(rest) == 1:
        return request(folder, "POST", f"/v1/dev/progress/level?character={character(folder)}&level={int(rest[0])}")
    if command == "heal":
        return request(folder, "POST", f"/v1/dev/world/heal?character={character(folder)}")
    if command == "ignore" and rest and rest[0] in ("on", "off"):
        return request(folder, "POST", f"/v1/dev/world/ignore?character={character(folder)}&ignore={1 if rest[0] == 'on' else 0}")
    # The life, the travel, and the interactions (stage P2.10; the routes of ~/Developer/azoth/tools/world, protocol notes 4.16,
    # 4.18, 4.36).
    if command == "hurt" and len(rest) == 1:
        return request(folder, "POST", f"/v1/dev/world/hurt?character={character(folder)}&amount={float(rest[0])}")
    if command == "travel" and len(rest) in (1, 2, 3, 4):
        screen = f"&screen={rest[-1]}" if rest[-1] in ("auto", "on", "off") else ""
        place = [word for word in rest if word not in ("auto", "on", "off")]
        target = f"x={float(place[0])}&y={float(place[1])}&z={float(place[2])}" if len(place) == 3 else f"place={place[0]}"
        return request(folder, "POST", f"/v1/dev/world/travel?character={character(folder)}&{target}{screen}")
    if command == "fixture" and len(rest) in (1, 4):
        offset = f"&dx={float(rest[1])}&dy={float(rest[2])}&dz={float(rest[3])}" if len(rest) == 4 else ""
        # The rule 1 of the Ownership state gives the prompt (the rule 4 of the data: "No public access.").
        return request(folder, "POST", f"/v1/dev/interactions/fixture?character={character(folder)}&name={rest[0]}{offset}&permission=1")
    if command == "remove-object" and len(rest) == 1:
        return request(folder, "POST", f"/v1/dev/interactions/remove?character={character(folder)}&entity={rest[0]}")
    if command == "cancel":
        return request(folder, "POST", f"/v1/dev/interactions/cancel?character={character(folder)}")
    fail(f"unknown command {' '.join(words)}")
    return None


class DriveClock:
    """The time of the drive script of the client: the largest time of its lines "drive: SECONDS s COMMAND" in the client log
    (the drive counts the game time, which is slower than the wall clock while the world loads)."""

    def __init__(self, log: Path) -> None:
        self.log, self.offset, self.time, self.stopped = log, 0, -1.0, False

    def now(self) -> float:
        try:
            with self.log.open("rb") as file:
                file.seek(self.offset)
                data = file.read()
        except OSError:
            return self.time
        end = data.rfind(b"\n") + 1
        self.offset += end
        for match in re.finditer(rb"drive: (\d+\.\d+) s ", data[:end]):
            self.time = max(self.time, float(match.group(1)))
        # The client stopped: its drive clock does not go on.
        self.stopped = self.stopped or b"client: stop" in data[:end]
        return self.time


def script(path: str, folder: Path, clock: DriveClock | None = None) -> None:
    """Runs a timed list: "SECONDS COMMAND WORDS..." (seconds from the start of the script, or on the drive clock of the client
    log). $N is the id of the creature of the Nth spawn of the script (1 first)."""
    start = time.monotonic()
    spawned: list[str] = []
    for line in Path(path).read_text().splitlines():
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        at, *words = line.split()
        if clock is not None:
            # At most 10 minutes for one step: a client that stopped ends the script.
            waited = time.monotonic()
            while clock.now() < float(at) and not clock.stopped and time.monotonic() - waited < 600.0:
                time.sleep(0.05)
            if clock.stopped and clock.now() < float(at):
                print(f"{clock.now():7.2f} s  the client stopped: the routes from {at} s do not run", flush=True)
                return
        else:
            delay = float(at) - (time.monotonic() - start)
            if delay > 0:
                time.sleep(delay)
        words = [spawned[int(word[1:]) - 1] if word.startswith("$") and word[1:].isdigit() and int(word[1:]) <= len(spawned) else word
                 for word in words]
        try:
            result = run(words, folder)
        except SystemExit as error:
            print(f"{clock.now() if clock is not None else time.monotonic() - start:7.2f} s  {' '.join(words)}: {error}", flush=True)
            continue
        if words[0] == "spawn" and isinstance(result, dict) and "creature" in result:
            spawned.append(str(result["creature"].get("id")))
        summary = json.dumps(result)[:240]
        print(f"{clock.now() if clock is not None else time.monotonic() - start:7.2f} s  {' '.join(words)}: {summary}", flush=True)


def main() -> None:
    folder = data_dir()
    if len(sys.argv) in (3, 5) and sys.argv[1] == "script" and (len(sys.argv) == 3 or sys.argv[3] == "--clock"):
        script(sys.argv[2], folder, DriveClock(Path(sys.argv[4])) if len(sys.argv) == 5 else None)
        return
    print(json.dumps(run(sys.argv[1:], folder), indent=1))


if __name__ == "__main__":
    main()
