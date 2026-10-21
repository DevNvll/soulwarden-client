"""The candidate shape of an Interact bound (AST-198; research ~/Games/soulwarden-client/research/prompt-range.md, "The rule for the
client" and "Shipped shapes and ranges").

Retail admits an Interact bound as a prompt candidate by a shape of its own, not by its validation range (m_interactRangeSq: the
use check). This module reads that shape from the slice of the bound. It reads only the direct components of the entity that has
the Interact component:

  shape       sphere (e24cbff0-...): Radius; box (5edf4b9e-...): Dimensions, the full sizes; b0c6aa97-... and 967ec13d-...:
              Height and Radius. The study names b0c6aa97 (the forge InteractBound1) a capsule; the navigation reader of the
              vendor folder names it a cylinder. Both keep the type id in "typeId".
  transform   m_worldTM (the place in the slice: each entity has m_onNewParentKeepWorldTM; the 3 x 3 part keeps the rotation and the
              scale), m_parentId
  Interact    m_useMarkerInteract (the marker path of the trees, the ore, the plants, the chests), and a direct TriggerAreaComponent
              (OnlyActiveForLocalPlayer, TriggerOnce: the trigger path of the bodies, the bag, the stations)

The values are the authored slice values. A variation table can change the radius of a sphere (InteractionRadius): objects.py applies
the variant of a natural node.
"""
from __future__ import annotations

import struct

from . import source  # noqa: F401  (sets the paths of the vendor readers)
from nav_assets import field_id, number, parse, real, walk
from nav_slices import (BOX_SHAPE_TYPE, COMPONENTS_FIELD, ENTITY_TYPE, ID_FIELD, PARENT_FIELD, SPHERE_SHAPE_TYPE, TRANSFORM_TYPE,
                        WORLD_FIELD, entity_id, matrix_of)

SHAPE_TYPES = {SPHERE_SHAPE_TYPE: "sphere", BOX_SHAPE_TYPE: "box"}
CAPSULE_TYPE_ID = "b0c6aa97-e754-4e33-8d32-33e267db622f"
CYLINDER_TYPE_ID = "967ec13d-364d-4696-ab5c-c00cc05a2305"
DIMENSIONS = field_id("Dimensions")
RADIUS = field_id("Radius")
HEIGHT = field_id("Height")
MARKER = field_id("m_useMarkerInteract")
RANGE_SQ = field_id("m_interactRangeSq")
LOCAL_ONLY = field_id("OnlyActiveForLocalPlayer")
TRIGGER_ONCE = field_id("TriggerOnce")
# The card rules (AST-231): the show flag and the name of an option binding, and the type of the NPC component (the study checked
# it in the inn slice npc_0422_innkeep: its field NPC key 0422_Innkeep).
SHOW = field_id("m_showOnUnifiedInteractUI")
OPTION_NAME = field_id("m_interactOption")
NPC_TYPE = bytes.fromhex("4bdb17d53b1e474da78329f47a7acd53")


def component_id(component) -> int | None:
    """@return The Id of a component: the first 8-byte field Id in it (the base class of AZ::Component)."""
    for node in walk(component):
        if node.name == ID_FIELD and node.value is not None and len(node.value) == 8:
            return int.from_bytes(node.value, "big")
    return None


def first(component, name: int):
    return next((node for node in walk(component) if node.name == name), None)


def shape_of(components) -> dict | None:
    """@return The shape of the direct components of an entity, its type and its fields, or None."""
    for component in components.children:
        kind = SHAPE_TYPES.get(component.type_id)
        uid = component.type_id.hex()
        type_id = f"{uid[0:8]}-{uid[8:12]}-{uid[12:16]}-{uid[16:20]}-{uid[20:32]}"
        if kind is None and type_id == CAPSULE_TYPE_ID:
            kind = "capsule"
        elif kind is None and type_id == CYLINDER_TYPE_ID:
            kind = "cylinder"
        if kind is None:
            continue
        out = {"type": kind, "typeId": type_id}
        if kind == "box":
            node = first(component, DIMENSIONS)
            if node is None or node.value is None or len(node.value) != 12:
                continue
            out["dimensions"] = [round(v, 5) for v in struct.unpack(">3f", node.value)]
        else:
            out["radius"] = round(real(first(component, RADIUS)), 5)
            if kind in ("capsule", "cylinder"):
                out["height"] = round(real(first(component, HEIGHT)), 5)
        return out
    return None


class BoundShapes:
    """The bound shapes of the slices (each slice is read once)."""

    def __init__(self) -> None:
        self.slices: dict[str, dict] = {}
        self.components: dict[str, dict] = {}
        self.missing: set[str] = set()

    def entities(self, path: str) -> dict:
        """@return entity id -> (its components element, its world matrix, its parent id) for a slice ({} when it is missing)."""
        if path in self.slices:
            return self.slices[path]
        out: dict = {}
        try:
            roots = parse(source.read(path))
        except Exception:  # noqa: BLE001 (a slice that is not in the game copy)
            self.missing.add(path)
            self.slices[path] = out
            return out
        for root in roots:
            for node in walk(root):
                if node.type_id != ENTITY_TYPE:
                    continue
                components = node.child(COMPONENTS_FIELD)
                identifier = entity_id(node.child(ID_FIELD))
                if components is None or identifier is None:
                    continue
                world = parent = None
                for component in components.children:
                    if component.type_id == TRANSFORM_TYPE:
                        world = matrix_of(getattr(component.child(WORLD_FIELD), "value", None))
                        parent = entity_id(component.child(PARENT_FIELD))
                out.setdefault(identifier, (components, world, parent))
        self.slices[path] = out
        return out

    def by_component(self, path: str) -> dict:
        """@return component id -> entity id for a slice (the Interact components of the objects of the layers)."""
        if path not in self.components:
            self.components[path] = {component_id(c): identifier for identifier, (components, _w, _p) in self.entities(path).items()
                                     for c in components.children if first(c, RANGE_SQ) is not None}
        return self.components[path]

    def shape(self, path: str, entity: str | None = None, component: str | None = None) -> dict | None:
        """@return The candidate shape of the bound with this entity id or Interact component id (hexadecimal), or None."""
        entities = self.entities(path)
        found = None
        if entity:
            found = entities.get(int(entity, 16))
        elif component:
            identifier = self.by_component(path).get(int(component, 16))
            found = entities.get(identifier) if identifier is not None else None
        if found is None:
            return None
        components, world, parent = found
        out = shape_of(components)
        if out is None:
            return None
        if world is not None:
            # matrix_of gives three rows: [r00 r01 r02 tx, r10 r11 r12 ty, r20 r21 r22 tz].
            out["basis"] = [round(world[i], 6) for i in (0, 1, 2, 4, 5, 6, 8, 9, 10)]
            out["translation"] = [round(world[i], 5) for i in (3, 7, 11)]
        if parent:
            out["parent"] = f"{parent:016x}"
        for c in components.children:
            if first(c, RANGE_SQ) is not None:
                marker = first(c, MARKER)
                out["marker"] = bool(number(marker)) if marker is not None else False
            local = first(c, LOCAL_ONLY)
            if local is not None:
                once = first(c, TRIGGER_ONCE)
                out["trigger"] = {"localOnly": bool(number(local)), "once": bool(number(once)) if once is not None else False}
        return out

    def card_rules(self, path: str, component: str) -> tuple[dict[str, bool], bool] | None:
        """@return The card rules of the bound with this Interact component id (hexadecimal; AST-231, research prompt-options.md):
        the authored show flag (m_showOnUnifiedInteractUI) of each option name of its bindings, and True when the entity of the bound
        has the NPC component (its client facet admits only the action 63, OpenConversation, to the card). None: no such bound."""
        wanted = int(component, 16)
        identifier = self.by_component(path).get(wanted)
        if identifier is None:
            return None
        components = self.entities(path)[identifier][0]
        flags: dict[str, bool] = {}
        for c in components.children:
            if component_id(c) != wanted or first(c, RANGE_SQ) is None:
                continue
            for node in walk(c):
                show = node.child(SHOW)
                name = node.child(OPTION_NAME)
                if show is None or name is None:
                    continue
                key = name.value.decode("utf-8", "replace").rstrip("\0")
                # The same option name in two bindings: the card shows it when one binding shows it.
                flags[key] = flags.get(key, False) or bool(number(show))
        npc = any(c.type_id == NPC_TYPE for c in components.children)
        return flags, npc
