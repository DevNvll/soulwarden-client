#!/usr/bin/env python3
"""Development runs: creates a character with the create request of the retail client (Soulwarden protocol notes 2.2.3),
then starts the client. It runs in the network namespace of tools/lib/session.sh, after the backend is ready.

Usage: dev_character.py SPEC -- CLIENT COMMAND...
  SPEC: name=NAME,gender=Male|Female,race=face01,skin=skin04,hair=hairstyle01m,facial=None,haircolor=haircolor1,
        facialcolor=haircolor1,eyecolor=eyecolor1,archetype=Archetype_Soldier,gear=ITEM@SLOT+ITEM@SLOT
        (each key is optional but name)
  gear: items of the item catalog in slots of the paperdoll (24 to 26: weapon 1 to 3, 27 to 29: their off hand). It
        needs the development routes of the backend (SWC_SESSION_BACKEND_ARGS=--dev-commands): the helper gives each item
        (/v1/dev/items/give) and equips it (/v1/dev/items/equip). An item that is in its slot stays.

The creation data has the form of the create requests of the client and of the menu document of the backend
(protocol notes 2.2.5; apps/backend/src/state/creation_data.cpp): base64(zlib(JSON)), the JSON member visualsBlob is a
binary object stream of PlayerSaveData (class version 3) with the appearance names, and an empty equipment map: the
backend gives a new character the clothing of its archetype. A name that is in use is not an error: the character is
there. The identity is the made-up identity of the development run (SWC_STEAM_ID, SWC_PERSONA_NAME).
"""
from __future__ import annotations

import base64
import json
import os
import struct
import sys
import urllib.error
import urllib.request
import zlib

HTTP = f"http://127.0.0.1:{os.environ.get('SWC_HTTP_PORT', '32200')}"
CHANNEL = "STEAM_APP_ID.1063730"
TEXT_TYPE = bytes.fromhex("03AAAB3F5C475A669EBCD5FA4DB353C9")
SAVE_DATA_TYPE = bytes.fromhex("144C75767FBA4250B0C8F06803657D79")
INTEGER_TYPE = bytes.fromhex("72B9409A7D1A48319CFEFCB3FADD3426")
APPEARANCE_DATA_TYPE = bytes.fromhex("7EF0E808E5B85116BB2B00A1610BF1D3")
EQUIPMENT_TYPE = bytes.fromhex("44C93A82189756D68CF74BC0289915DE")
# The fields of PlayerSaveData in the order of the create request: (key of the spec, field name, default).
APPEARANCE = [("gender", "gender", "Male"), ("race", "race", "face01"), ("skin", "skinTone", "skin04"),
              ("hair", "hairstyle", "hairstyle01m"), ("facial", "facialHair", "None"), ("haircolor", "hairColor", "haircolor1"),
              ("facialcolor", "facialHairColor", "haircolor1"), ("eyecolor", "eyeColor", "eyecolor1"),
              ("facemark", "faceMark", "none"), ("scar", "scar", "none"), ("tattoo", "tattoo", "none"),
              ("tattoocolor", "tattooColor", "tattoocolor1")]


def name_crc(field: str) -> int:
    return zlib.crc32(field.lower().encode()) & 0xFFFFFFFF


def element(name: str | None, type_id: bytes, value: bytes | None = None, version: int = 0, children: bytes = b"") -> bytes:
    flags = 0x08 | (0x40 if name else 0) | (0x80 if version else 0)
    out = bytearray()
    if value is not None:
        flags |= 0x10 | (len(value) if len(value) <= 7 else 0x21)
    out.append(flags)
    if name:
        out += struct.pack(">I", name_crc(name))
    if version:
        out.append(version)
    out += type_id
    if value is not None:
        if len(value) > 7:
            out.append(len(value))
        out += value
    return bytes(out) + children + b"\0"


def visuals_blob(name: str, values: dict[str, str], archetype: str) -> bytes:
    fields = element("playerName", TEXT_TYPE, name.encode())
    fields += element("companyName", TEXT_TYPE, (name + "'s Company").encode())
    fields += element("entityClassName", TEXT_TYPE, b"PlayerFemale" if values["gender"] == "Female" else b"PlayerMale")
    fields += element("guildCrestName", TEXT_TYPE, b"")
    for _key, field, _default in APPEARANCE:
        fields += element(field, TEXT_TYPE, values[field].encode())
    fields += element("backstory", TEXT_TYPE, archetype.encode())
    fields += element("startingFaction", INTEGER_TYPE, b"\0")
    fields += element("appearanceData", APPEARANCE_DATA_TYPE)
    fields += element("equipment", EQUIPMENT_TYPE)
    return b"\0" + struct.pack(">I", 3) + element(None, SAVE_DATA_TYPE, version=3, children=fields) + b"\0"


def json_bytes(data: bytes) -> bytes:
    """The JSON text of the client writer: escapes below 0x20, '"' and '\\'; each other byte as it is (not UTF-8)."""
    escapes = {0x08: b"\\b", 0x0C: b"\\f", 0x0A: b"\\n", 0x0D: b"\\r", 0x09: b"\\t", 0x22: b'\\"', 0x5C: b"\\\\"}
    out = bytearray(b'"')
    for byte in data:
        if byte in escapes:
            out += escapes[byte]
        elif byte < 0x20:
            out += b"\\u%04X" % byte
        else:
            out.append(byte)
    return bytes(out + b'"')


def creation_params(name: str, values: dict[str, str], archetype: str, world_id: str) -> str:
    document = (b'{"currency":0,"visualsBlob":' + json_bytes(visuals_blob(name, values, archetype))
                + b',"position":{"x":0.0,"y":0.0,"z":0.0},"currentLevel":0,"worldId":"' + world_id.encode() + b'"}')
    return base64.b64encode(zlib.compress(document, 9)).decode()


def request(method: str, host: str, path: str, body: dict | None = None, token: str = "", content_type: str = "application/json"):
    data = json.dumps(body).encode() if body is not None else None
    headers = {"X-Soulwarden-Host": host, "X-Soulwarden-Scheme": "https"}
    if token:
        headers["x-nw-auth"] = token
    if data is not None:
        headers["Content-Type"] = content_type
    handler = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    try:
        with handler.open(urllib.request.Request(HTTP + path, data=data, headers=headers, method=method), timeout=10) as reply:
            return reply.status, json.loads(reply.read() or b"{}")
    except urllib.error.HTTPError as error:
        return error.code, json.loads(error.read() or b"{}")


def management(method: str, path: str):
    """A development route of the backend: a management request (no service host)."""
    handler = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    try:
        with handler.open(urllib.request.Request(HTTP + path, method=method), timeout=10) as reply:
            return reply.status, json.loads(reply.read() or b"{}")
    except urllib.error.HTTPError as error:
        return error.code, json.loads(error.read() or b"{}")


def equip(character: str, name: str, gear: str) -> int:
    """Gives and equips the items of a gear spec (ITEM@SLOT+ITEM@SLOT)."""
    for entry in filter(None, gear.split("+")):
        item, _, slot = entry.partition("@")
        status, view = management("GET", f"/v1/dev/items?character={character}")
        if status != 200:
            print(f"dev-character: no item list of {name} ({status}): the gear needs SWC_SESSION_BACKEND_ARGS=--dev-commands", file=sys.stderr)
            return 1
        if any(e.get("item") == item and str(e.get("slot")) == slot for e in view.get("equipment", [])):
            continue
        status, reply = management("POST", f"/v1/dev/items/give?character={character}&item={item}&quantity=1")
        if status != 200:
            print(f"dev-character: give {item} to {name}: {status} {reply}", file=sys.stderr)
            return 1
        _status, view = management("GET", f"/v1/dev/items?character={character}")
        bag = [e["slot"] for e in view.get("items", []) if e.get("item") == item]
        if not bag:
            print(f"dev-character: {item} is not in the bag of {name}", file=sys.stderr)
            return 1
        status, reply = management("POST", f"/v1/dev/items/equip?character={character}&from={bag[-1]}" + (f"&slot={slot}" if slot else ""))
        if status != 200:
            print(f"dev-character: equip {item} in slot {slot} of {name}: {status} {reply}", file=sys.stderr)
            return 1
        print(f"dev-character: {name} has {item} in slot {slot or 'of its class'}", file=sys.stderr)
    return 0


def ticket() -> str:
    steam_id = os.environ.get("SWC_STEAM_ID", "")
    persona = os.environ.get("SWC_PERSONA_NAME", "Soulwarden").encode()
    return f"SOULWARDEN-LOCAL-TICKET;1;{steam_id};{persona.hex()}".encode().hex()


def create(spec: str) -> int:
    values = dict(part.split("=", 1) for part in spec.split(",") if "=" in part)
    name = values.pop("name")
    archetype = values.pop("archetype", "Archetype_Soldier")
    gear = values.pop("gear", "")
    names = {field: values.get(key, default) for key, field, default in APPEARANCE}
    status, reply = request("POST", "tokenservice.soulwarden.local", "/games/new-world/tokens",
                            {"platformIdentityType": "steam", "platformAuth": {"ticket": ticket()}, "fallbackToken": "", "attributionData": {}})
    token = reply.get("accessToken", "")
    if status != 200 or not token:
        print(f"dev-character: no account token ({status})", file=sys.stderr)
        return 1
    status, reply = request("GET", "gateway.soulwarden.local", f"/prod/game/getlogininfo?channelId={CHANNEL}", token=token)
    worlds = reply.get("LoginInfoList", {}).get("Worlds", [])
    if status != 200 or not worlds:
        print(f"dev-character: no world ({status})", file=sys.stderr)
        return 1
    world_id = worlds[0]["WorldId"]
    existing = [character for character in reply["LoginInfoList"].get("Characters", []) if character.get("Name") == name]
    if existing:
        print(f"dev-character: {name} is there", file=sys.stderr)
        return equip(existing[0]["CharacterId"], name, gear) if gear else 0
    body = {"CreateCharacterRequest": {"CharacterCreationParams": creation_params(name, names, archetype, world_id),
                                       "ClientCapabilities": [], "Name": name, "SkipFtue": False, "SteamAppId": 1063730,
                                       "SteamAuthTicket": ticket()}}
    status, reply = request("POST", "gateway.soulwarden.local", f"/prod/game/worlds/{world_id}/characters?channelId={CHANNEL}",
                            body, token, "application/x-amz-json-1.1")
    if status == 200:
        character = reply["Character"]["CharacterId"]
        print(f"dev-character: created {name} ({character}), {archetype}, {names}", file=sys.stderr)
        return equip(character, name, gear) if gear else 0
    print(f"dev-character: the create request of {name} got {status} {reply}", file=sys.stderr)
    return 1


def main(argv: list[str]) -> int:
    if len(argv) < 2 or "--" not in argv:
        print(__doc__, file=sys.stderr)
        return 2
    split = argv.index("--")
    for spec in argv[1:split]:
        if create(spec) != 0:
            return 1
    command = argv[split + 1:]
    if command:
        os.execvp(command[0], command)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
