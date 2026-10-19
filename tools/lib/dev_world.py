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
  heal [NAME]                        the health of the character (or of a character NAME; a dead one lives again)
  level N                            the level of the character (its health grows with it)
  ignore on|off [NAME]               the creatures ignore the character (on) or not (or a character NAME)
  loot on|off                        each kill of the character drops loot (on; stage 3.5b.2: a loot bag) or the normal roll
  bags                               the loot bags of the character
  effect CREATURE STATUS [SECONDS]   a status effect of the data on a creature (a row id; knockdown: 2 s, stagger: 1 s)
  mastery NAME WEAPON LEVEL          the mastery level of a weapon of a character NAME (greataxe, warhammer, ...)
  node NAME WEAPON NODE...           nodes of the tree of a weapon for a character NAME (one point each, in the order of the tree)
  bar NAME WEAPON SLOT ABILITY       an ability on place 0 (Q), 1 (R), or 2 (F) of the bar of a weapon of a character NAME
  place NAME [DX DY [DZ]]            a character NAME of the database into the world at the character (a remote test character)
  cast NAME PLACE [--yaw DEGREES]    the press of place PLACE of the bar for a character NAME (yaw from the north to the east)
  items                              the bag, the equipment, and the coin of the character
  give ITEM [GEARSCORE]              one item of the item catalog (ItemID) into the bag of the character
  equip FROM [SLOT]                  the item of the bag slot FROM into a slot of the paperdoll (no SLOT: the slot of its class)
  abilities                          the cooldowns of the server of the character and the counts of the abilities
  reset                              each cooldown of the server of the character ends (the client keeps its own cooldown)
  stack ITEM QUANTITY                a quantity of a stack item into the bag (stage P3.3: the bag weight, FishingTreasureBigAnchorT3
                                     5.0 each)
  unstack ITEM                       each bag slot with the item goes away
  status ROW [SECONDS]               a status row of the data on the character (0 or none: the duration of the row; stage P3.3:
                                     a root, a stun, a slow, the road haste)
  unstatus ROW                       the status ends (all: each status but the gear)
  shoot CREATURE                     the creature starts its basic shot at the character now (stage 3.4f-A)
  area CREATURE                      the creature starts its next area attack at the character now (stage 3.4f-C)
  hurt AMOUNT                        damage of the character (an amount at or above its health kills it: the death, stage P2.10)
  travel X Y Z [auto|on|off]         a travel of the character to a place (engine m; the loading screen rule of the backend)
  travel PLACE [auto|on|off]         a travel to a travel place of the game data (shrine_40170, inn_40300)
  fixture NAME [DX DY DZ]            a fixture of the backend at the character (powderKeg: the timed use "Ignite Fuse", 5 s)
  remove-object ENTITY               removes a server child (the entity of the fixture reply, 16 hex digits)
  cancel                             the server ends the interaction of the character (3746)
  gathering [RADIUS] | chests [RADIUS]   the gathering or the chests of the character (the nodes with a server state near it)
  respawn NODE SECONDS               a depleted node respawns after SECONDS (NODE: a UUID, REGION#RECORD, or G)
  chest-clear CHEST                  removes the claim of the character on a chest (its UUID): the chest can open again
  chest-reset CHEST SECONDS          the looted claim of the character on a chest resets after SECONDS (the light comes back)
  world-time [HH:MM[:SS]]            the world time of the backend (with the wall time of the reply), or a new time for the world
  tool ITEM SLOT                     a tool in a paperdoll slot (33 axe, 34 sickle, 35 knife, 36 pick): from the bag, else given
  untool SLOT                        the item of a paperdoll slot goes into the bag
  script FILE [--clock LOG]          a timed list of these commands: "SECONDS COMMAND WORDS..." each line (the seconds from
                                     now, or on the clock of the drive script in the client log LOG: its "drive: SECONDS s"
                                     lines; a drive line "SECONDS tick" is a clock mark that does nothing)
The character is the first character of the database, or SWC_DEV_CHARACTER (a name).
"""
from __future__ import annotations

import datetime
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


def character_named(folder: Path, name: str) -> str:
    """@return The id of the character with this name (a remote test character of the development database)."""
    for identifier, row_name in characters(folder):
        if row_name == name:
            return identifier
    fail(f"no character {name} in the development database")
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
    if command == "effect" and len(rest) in (2, 3):
        seconds = f"&seconds={float(rest[2])}" if len(rest) == 3 else ""
        return request(folder, "POST", f"/v1/dev/creatures/effect?creature={rest[0]}&status={rest[1]}{seconds}")
    if command == "mastery" and len(rest) == 3:
        return request(folder, "POST", f"/v1/dev/progress/mastery?character={character_named(folder, rest[0])}&weapon={rest[1]}&level={int(rest[2])}")
    if command == "node" and len(rest) >= 3:
        result = None
        for node in rest[2:]:
            result = request(folder, "POST", f"/v1/dev/progress/node?character={character_named(folder, rest[0])}&weapon={rest[1]}&node={node}")
        return result
    if command == "bar" and len(rest) == 4:
        return request(folder, "POST", f"/v1/dev/progress/bar?character={character_named(folder, rest[0])}&weapon={rest[1]}&slot={int(rest[2])}"
                                       f"&ability={rest[3]}")
    if command == "place" and len(rest) in (1, 3, 4):
        offsets = "".join(f"&{axis}={float(value)}" for axis, value in zip(("dx", "dy", "dz"), rest[1:]))
        return request(folder, "POST", f"/v1/dev/world/spawn?character={character_named(folder, rest[0])}&near={character(folder)}{offsets}")
    if command == "cast" and len(rest) >= 2:
        yaw = ""
        if "--yaw" in rest:
            place = rest.index("--yaw")
            yaw = f"&yaw={float(rest[place + 1])}"
            del rest[place:place + 2]
        return request(folder, "POST", f"/v1/dev/abilities/cast?character={character_named(folder, rest[0])}&place={int(rest[1])}{yaw}")
    # The items and the abilities of the character (stage P3.5: the build of the great axe and the war hammer, the cooldowns).
    if command == "items":
        return request(folder, "GET", f"/v1/dev/items?character={character(folder)}")
    if command == "give" and len(rest) in (1, 2):
        score = f"&gearScore={int(rest[1])}" if len(rest) == 2 else ""
        return request(folder, "POST", f"/v1/dev/items/give?character={character(folder)}&item={rest[0]}&quantity=1{score}")
    if command == "equip" and len(rest) in (1, 2):
        slot = f"&slot={int(rest[1])}" if len(rest) == 2 else ""
        return request(folder, "POST", f"/v1/dev/items/equip?character={character(folder)}&from={int(rest[0])}{slot}")
    if command == "abilities":
        return request(folder, "GET", f"/v1/dev/abilities?character={character(folder)}")
    if command == "reset":
        return request(folder, "POST", f"/v1/dev/abilities/reset?character={character(folder)}")
    # The bag weight (stage P3.3).
    if command == "stack" and len(rest) == 2:
        return request(folder, "POST", f"/v1/dev/items/give?character={character(folder)}&item={rest[0]}&quantity={int(rest[1])}")
    if command == "unstack" and len(rest) == 1:
        view = request(folder, "GET", f"/v1/dev/items?character={character(folder)}")
        removed = []
        for entry in view.get("items", []):
            if str(entry.get("item", "")).lower() == rest[0].lower():
                request(folder, "POST", f"/v1/dev/items/remove?character={character(folder)}&slot={entry['slot']}")
                removed.append(entry["slot"])
        return {"removed slots": removed}
    # The statuses of the character (stage P3.3: the movement columns of the status map and the speed table of the server).
    if command == "status" and len(rest) in (1, 2):
        seconds = f"&seconds={float(rest[1])}" if len(rest) == 2 else ""
        return request(folder, "POST", f"/v1/dev/status?character={character(folder)}&status={rest[0]}{seconds}")
    if command == "unstatus" and len(rest) == 1:
        return request(folder, "POST", f"/v1/dev/status/remove?character={character(folder)}&status={rest[0]}")
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
        who = character_named(folder, rest[0]) if rest else character(folder)
        return request(folder, "POST", f"/v1/dev/world/heal?character={who}")
    if command == "ignore" and rest and rest[0] in ("on", "off"):
        who = character_named(folder, rest[1]) if len(rest) > 1 else character(folder)
        return request(folder, "POST", f"/v1/dev/world/ignore?character={who}&ignore={1 if rest[0] == 'on' else 0}")
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
    # The objects of the level data (stage P4.1 part 2, docs/OBJECTS.md): the nodes and the chests of the character.
    if command == "gathering" and len(rest) <= 1:
        radius = f"&radius={float(rest[0])}" if rest else ""
        return request(folder, "GET", f"/v1/dev/gathering?character={character(folder)}{radius}")
    if command == "chests" and len(rest) <= 1:
        radius = f"&radius={float(rest[0])}" if rest else ""
        return request(folder, "GET", f"/v1/dev/chests?character={character(folder)}{radius}")
    if command == "respawn" and len(rest) == 2:
        node = rest[0].replace("#", "%23")
        return request(folder, "POST", f"/v1/dev/gathering/respawn?character={character(folder)}&node={node}&seconds={int(float(rest[1]))}")
    if command == "tool" and len(rest) == 2:
        # A tool in its paperdoll slot (stage P5.2: 33 the logging axe, 34 the sickle, 35 the skinning knife, 36 the pick): the item of the
        # bag, else a new one, equipped into the slot.
        who, item, slot = character(folder), rest[0], int(rest[1])
        view = request(folder, "GET", f"/v1/dev/items?character={who}")
        if any(entry["slot"] == slot and entry["item"].lower() == item.lower() for entry in view.get("equipment", [])):
            return {"message": f"{item} is in slot {slot}"}
        held = next((entry["slot"] for entry in view.get("items", []) if entry["item"].lower() == item.lower()), None)
        if held is None:
            request(folder, "POST", f"/v1/dev/items/give?character={who}&item={item}&quantity=1")
            view = request(folder, "GET", f"/v1/dev/items?character={who}")
            held = next((entry["slot"] for entry in view.get("items", []) if entry["item"].lower() == item.lower()), None)
        if held is None:
            fail(f"the bag has no {item} after the give")
        return request(folder, "POST", f"/v1/dev/items/equip?character={who}&from={held}&slot={slot}")
    if command == "untool" and len(rest) == 1:
        return request(folder, "POST", f"/v1/dev/items/unequip?character={character(folder)}&slot={int(rest[0])}")
    if command == "world-time" and len(rest) <= 1:
        # The wall time of the reply (local, ms) for the compare with the clock of the client (the logs of the run).
        if rest:
            reply = request(folder, "POST", f"/v1/dev/world/time?character={character(folder)}&time={rest[0]}")
        else:
            reply = request(folder, "GET", f"/v1/dev/world/time?character={character(folder)}")
        if isinstance(reply, dict):
            # The time first: a script line shows only the start of the reply.
            now = reply.get("worldTime", reply)
            reply = {"wall": datetime.datetime.now().isoformat(timespec="milliseconds"), "time": now.get("time"), "hour": now.get("hour"),
                     "phase": now.get("phase"), **reply}
        return reply
    if command == "chest-reset" and len(rest) == 2:
        return request(folder, "POST", f"/v1/dev/chests/reset?character={character(folder)}&chest={rest[0]}&seconds={int(float(rest[1]))}")
    if command == "chest-clear" and len(rest) == 1:
        return request(folder, "POST", f"/v1/dev/chests/clear?character={character(folder)}&chest={rest[0]}")
    fail(f"unknown command {' '.join(words)}")
    return None


class DriveClock:
    """The time of the drive script of the client: the largest time of its lines "LogSwcBackend: drive: SECONDS s COMMAND" in
    the client log (the drive counts the game time, which is slower than the wall clock while the world loads). The lines "ui
    drive: SECONDS s" of the UI drive are not the clock: the UI drive starts at the spawn, before the possession."""

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
        for match in re.finditer(rb"LogSwcBackend: drive: (\d+\.\d+) s ", data[:end]):
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
