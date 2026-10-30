"""The game modes of the UI (C-13 step 1b part B, ut567): the rows of javelindata_gamemodes for GameModeDataManagerBus GetGameModeStaticData
(the entrance window dungeonenterscreen.luac) and the text ids of the leave of the escape menu (escapemenu.luac: QuitButtonTextId,
LeaveTitleTextId, LeaveDescTextId, LeaveTimeDescTextId, CannotFastTravelTextId of GameModeParticipantComponentRequestBus).

Output (outside git): SWC_CONTENT/ui/gamedata/gamemodes.json: {"format": 1, "modes": {"CRC": {the non-empty columns, camel case, and
"id"}}}; the key is the CRC-32 of the lower-case GameModeId (QuestMedea 95B632E9, the mode of the GameModeParticipant state).
"""
from __future__ import annotations

import json
import zlib

from . import datasheet, env

TABLE = "javelindata_gamemodes.datasheet"


def camel(column: str) -> str:
    """@return The name of a column in the native class: "DisplayName" -> "displayName", "IsSoloTrial" -> "isSoloTrial"."""
    return column[:1].lower() + column[1:]


def build(rows) -> dict:
    modes = {}
    for row in rows:
        ident = str(row.get("GameModeId", "") or "")
        if not ident:
            continue
        entry = {"id": ident}
        for column, value in row.items():
            if value == "" or column == "GameModeId":
                continue
            entry[camel(column)] = value
        modes[str(zlib.crc32(ident.lower().encode()) & 0xFFFFFFFF)] = entry
    return {"format": 1, "modes": modes}


def write() -> dict:
    doc = build(datasheet.load(TABLE).rows())
    target = env.content_dir() / "ui" / "gamedata" / "gamemodes.json"
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(json.dumps(doc, separators=(",", ":")))
    return {"modes": len(doc["modes"]), "file": str(target)}


def main(argv=None) -> int:
    print(json.dumps(write(), indent=1))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
