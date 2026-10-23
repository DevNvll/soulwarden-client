"""The in-game scenes of the quests (stage Q5, Linear AST-240; docs/QUESTS.md "The scenes, the cinematics, and the whispers").

The data path (research conversations.md section 5 of the Soulwarden research; research/quest-scenes.md, the study of the native rules):

- A cutscene spawner is a slice `coatgen/*/igc_master_spawner_*.dynamicslice` that the capitals of a region place. Its root entity has
  the AchievementEvents script (RequiredAchievementConditional, for example `Start_IGC_Aelstrom_WW && !Watched_IGC_Aelstrom_WW`, and the
  entity events that activate its child "TriggerCutscene"). The child has a TriggerPlayerCutsceneComponent: the cutscene name, the scene
  slice (m_sliceAsset), m_despawnDelay, m_playCutsceneOnLoad, m_triggerOnActivate. [D]
- The scene slice (`slices/igc/...`) has the TriggerActivatePlayerCutsceneComponent (CutsceneInfo: canSkip, blockPlayerInput, hideUI, the
  camera states, the blend times) and its entities (the actors with a SkinnedMeshComponent, the props with an AttachmentComponent, the
  camera, the lights, the effects, the audio) in local values. Its TimelineComponents name the timelines and the LayerEntityAssignments:
  the slice entity of each layer. [D]
- A timeline (`timelines/igc/...`) has the layers and their clips (the animation clips with AnimationAlias, ShowMesh and HideMesh,
  ActivateEntity and DeactivateEntity, the curves of the camera with Bezier keyframes, CameraProperties, the trigger clips). [D]
The converter writes the data as it is (no rule of the play): the client applies the rules of the study. The field names come from the
engine names of the classes (CRC-32 of the lower-case name); a field with no name in the table stays "#CRC".

Inputs: the game copy (the regions, the capitals, the slices, the timelines) and SWC_CONTENT/ui/gamedata/quests.json (the tasks
TaskCutsceneComplete: the names of the quest scenes).
Outputs (SWC_CONTENT/quests, never in git):
  scenes.json          the spawners (the place, the condition, the trigger), the scenes (name, CRC, slice, the tasks), the wanted
                       actors (CDF and animation aliases), and the counts
  whispers.json        the whispers (WhisperData, the whisper and voice timelines), the cinematic registry (the Bink header of the
                       videos of the quest tasks), the timed localization entries (start, end, speaker, the next key)
  scenes/NAME.json     one scene: CutsceneInfo, the entities (id, name, parent, local matrix, the components), the timelines (the
                       owner entity, the entry, the assignments, the layers and clips)
Then `convert [NAME...] [--force]` converts the actors of the scenes (default: the beach chain) with the player pipeline (the bodies of the
person and player skeletons, one scene clip pack with the root track: then tools/import-player.sh) and as characters (other
skeletons: then tools/import.sh).
Usage: python3 -B -m swc_convert.scenes write [--only NAME] | convert [NAME...] [--force]
"""
from __future__ import annotations

import json
import struct
import sys
import uuid
import zlib
from pathlib import Path

import numpy as np

from . import census, character, datasheet, env, gltf, persons, player, region, source

FORMAT = 1
F = source.field_id


def _uuid(text: str) -> bytes:
    return uuid.UUID(text).bytes


# The classes that the client reads (the type ids of the image text table of the client; research tools objstream.py). The type
# 711f015c is the TimelineComponent (the persons converter; the text table gives a wrong neighbour name for it).
CLASSES = {_uuid(k): v for k, v in {
    "75651658-8663-478d-9090-2432dfcafa44": "Entity",
    "484ae67d-abd0-4d9c-b2c8-9bb0eec900e0": "TransformComponent",
    "711f015c-c03b-4735-8c69-b21a224ef2dd": "TimelineComponent",
    "9217f896-b2b1-4812-9ee4-e7789fcb3ec7": "AchievementEvent",
    "754c1847-63da-47ea-942a-a1f0c8655086": "ActivateEntity",
    "2d17a64a-7ac5-4c02-ac36-c5e8141ffddf": "AttachmentComponent",
    "74b5dc69-de44-4640-836a-55339e116795": "AttachmentConfiguration",
    "d5085d04-2522-4585-9e65-d337c5bbb8a7": "AudioEnvironmentComponent",
    "cbbb1234-4dca-427e-80ff-e2bb0866eeb1": "AudioPreloadComponent",
    "0ee6ee0f-7939-4ab8-b0e3-f9b3925d61ee": "AudioProxyComponent",
    "c54c7ae6-08aa-49e0-b6cd-e1bbb4950daf": "AudioRtpcComponent",
    "85fd9037-a5ea-4783-b49a-7959bbb34011": "AudioSwitchComponent",
    "8cbbb54b-7435-4d33-844d-e7f201bd581a": "AudioTriggerComponent",
    "e409f5c0-9919-4ca5-9488-1fe8a041768e": "CameraComponent",
    "4d839880-14d5-48e5-a4f6-aa847b991da2": "CameraProperties",
    "6675b91a-94d6-4bbc-bfbd-402cd33d507c": "Curve",
    "c33ee41d-3ec0-49b0-af72-1fcd9db46a1d": "CurveAxis",
    "45396c99-e50d-4c63-a248-c135888ca3d3": "CutsceneInfo",
    "adc0760a-6d9b-4329-afc4-9ca11703434b": "DeactivateEntity",
    "d353f7e8-4266-f749-19a1-aa2c3841fbca": "DelayedEventComponent",
    "82897793-be85-48cd-bac9-a1ad3b487133": "EntityEvent",
    "83c9e723-862e-4507-9f3c-f30f9366b536": "HideMesh",
    "476b4b9f-9c00-4476-afa2-2d9f1179bad5": "ItemManagementComponent",
    "1b469e23-ab5d-476f-bdf1-32b62ad67b63": "Keyframe",
    "2acd7f8c-9f1a-41f3-b8b9-a64d2fc3bb09": "LayerEntityAssignment",
    "d159813f-9b6b-430d-b7e0-89b20322501c": "LightClip",
    "6b9ab512-ca8a-4d2b-b570-df128ea7ce6a": "LightComponent",
    "65bc817a-abf6-440f-ad4f-581c40f92795": "ParticleComponent",
    "1244dff9-f100-4b2c-a8c0-c896c65ce07c": "ShowMesh",
    "c99eb110-ca74-4d95-83f0-2fcdd1ff418b": "SkinnedMeshComponent",
    "e13aab0e-a554-45cc-8a0b-f71f60482bb8": "SoundEffect",
    "d8d4045d-ffa2-40df-9454-476ca09c901d": "TimelineClip",
    "e14cf7d7-9f3b-4aae-a320-a284dffd02ee": "TimelineEntry",
    "f99a21ab-f403-4b90-ad52-25ee26c36f25": "TimelineLayer",
    "45f688e1-bbbf-456f-99a1-7d01f10ad002": "TriggerActivatePlayerCutsceneComponent",
    "760023bd-e41e-4ca5-8bd1-596ea620128d": "TriggerPlayerCutsceneComponent",
    "a4422aea-6278-4cd0-90f0-2490e32668c0": "UnTriggerEntity",
    "0e0ef911-f8e7-4d3c-afaa-68d5d370f244": "Y_QuaternionXYZW",
    "8e425cd0-98e6-487a-9302-a1ad6c0d02de": "SlayerScriptForwarderComponent",
    "3d939617-3f24-41dc-a05f-2f298e8f4cb8": "SlayerScriptComponent",
}.items()}
ENTITY = _uuid("75651658-8663-478d-9090-2432dfcafa44")
TRANSFORM = _uuid("484ae67d-abd0-4d9c-b2c8-9bb0eec900e0")
TRIGGER = _uuid("760023bd-e41e-4ca5-8bd1-596ea620128d")
SCRIPT = _uuid("3d939617-3f24-41dc-a05f-2f298e8f4cb8")

# The value types (public engine source; research tools objstream.py KNOWN_TYPES).
T_STRING = _uuid("03aaab3f-5c47-5a66-9ebc-d5fa4db353c9")
T_BOOL = _uuid("a0ca880c-afe4-43cb-926c-59ac48496112")
T_FLOAT = _uuid("ea2c3e90-afbe-44d4-a90d-faaf79baf93d")
T_DOUBLE = _uuid("110c4b14-11a8-4e9d-8638-5051013a56ac")
T_INT = _uuid("72b9409a-7d1a-4831-9cfe-fcb3fadd3426")
T_UUID = _uuid("e152c105-a133-4d03-bbf8-3d4b2fba3e2a")
T_VECTOR3 = _uuid("8379eb7d-01fa-4538-b64b-a6543b4be73d")
T_TRANSFORM = (_uuid("5d9958e9-9f1e-4985-b532-ffffde75fedd"), _uuid("5d9958e9-9f1e-4985-b532-fffde75fedfd"))
T_ASSET = _uuid("77a19d40-8731-4d3c-9041-1b43047366a4")

# The field names (the engine names; a CRC-32 of the lower-case name). id, name, time, axes, and loop have no text in the table:
# their CRCs match these words.
FIELD_NAMES = [
    "id", "name", "time", "axes", "loop", "value", "value1", "value2", "element", "BaseClass1", "BaseClass2", "Components", "Entities",
    "IsRuntimeActive", "IsDependencyReady", "m_worldTM", "m_localTM", "m_parentId", "m_isStatic", "m_onNewParentKeepWorldTM",
    "Director", "Layers", "Clips", "NameCrc", "Ignore", "StartTime", "EndTime", "Keyframes", "IncomingType", "IncomingHandleX",
    "IncomingHandleY", "OutgoingHandleX", "OutgoingHandleY", "TranslationCurve", "RotationCurve", "QuaternionCurve", "ScaleCurve",
    "XYZCurve", "RGBCurve", "HSVCurve", "TransformMode", "EulerRotationOrder", "RewindOnFinish", "SnapToTerrain", "AnimationAlias",
    "LoopToStartOffset", "StopOnEnd", "playbackSpeed", "AnimationStartTime", "BlendInTime", "BlendOutTime", "Layer", "Weight",
    "ApplyToChildren", "ApplyToAllChildren", "FieldOfViewCurve", "NearClipCurve", "FarClipCurve", "FrustumWidthCurve",
    "FieldOfViewstumHeightCurve", "DepthOfFieldEnable", "DepthOfFieldBlurAmountCurve", "DepthOfFieldFocusDistanceCurve",
    "DepthOfFieldFocusRangeCurve", "ColorCorrectionBrightnessCurve", "ColorCorrectionContrastCurve", "ColorCorrectionCyanCurve",
    "ColorCorrectionHueCurve", "ColorCorrectionLuminenceCurve", "ColorCorrectionMagentaCurve", "ColorCorrectionSaturationCurve",
    "ColorCorrectionYellowCurve", "PropertyName", "RestoreOnEnd", "RemoveOnEnd", "m_timelines", "TimelineOwnerId", "TimelineAsset",
    "RunOnActivate", "RunOnServer", "Environment", "OnTriggerAction", "OnUntriggerAction", "LayerEntityAssignments", "EntityId",
    "LayerId", "LayerName", "LayerIndex", "GroupName", "m_cutsceneInfo", "m_cutsceneName", "m_canSkip", "m_blockPlayerInput",
    "m_hideUIOnTrigger", "m_hidePlayerAvatar", "m_hideNearbyAIAvatars", "m_hideNearbyBuildables", "m_hideNearbyNPCAvatars",
    "m_hideNearbyPlayerAvatars", "m_playFadeEffect", "m_enterBlendTime", "m_exitBlendTime", "m_originEnterBlendTime",
    "m_originExitBlendTime", "releaseCameraOverridesIn", "m_cameraState", "m_cameraStateOrigin", "m_cameraStateLookAt",
    "m_cameraStateToSetOnExit", "m_lockCameraStateOnExitUntilInput", "m_forceInstantExitTransition", "m_forceInstantLookAts",
    "m_forceSmoothLookAtCameraOnTransitionIn", "m_interruptInCombat", "m_interruptOnMovement", "m_dismountPlayer",
    "m_playerInvincibility", "m_localPlayerOnly", "m_useCutsceneObserver", "m_onCutsceneStartEvents", "m_onCutsceneStoppedEvents",
    "m_playPreFadeOutEffect_TargetColor", "m_playPreFadeOutEffect_TimeToTargetColorInSeconds", "m_depthOfFieldOverride",
    "m_dummyAnimationInfo", "m_dummyIsArmed", "m_dummyLookAtInfo", "m_dummyPresetPos", "m_dummyPresetRot", "m_npcAnimationInfo",
    "m_npcLookAtInfo", "m_npcToInteract", "m_speakingNpcId", "m_multiNpcIds", "m_gestureAnimationInfo", "m_originEntity",
    "m_followEntity", "m_spectatorMode", "m_spectatorCameraOriginPitch", "m_preloadCutsceneName", "m_sliceAsset",
    "m_preloadEntityPositions", "m_despawnDelay", "m_playCutsceneOnLoad", "m_triggerOnActivate", "m_waitForPreloading",
    "m_startDelayOnInit", "m_startDelayOnTrigger", "m_clientFacetPtr", "m_serverFacetPtr", "m_replicationIndex", "m_replicateToClient",
    "m_scriptDataContainer", "m_scriptName", "m_scriptData", "useVariationData", "events", "debugName", "debugIndex",
    "RequiredAchievementConditional", "RunEventsOnConditionalSatisfied", "RunEventsOnEnterAOI", "eventDelayDuration",
    "reverseEventsOnConditionalUnsatisfied", "entityEvents", "EventType", "applyOnChildren", "entityNames", "opacityEvents",
    "rotationEvents", "conditionalReferenceIndex", "m_string", "m_crc", "m_debugIndex", "Skinned Mesh", "Visible", "Material Override",
    "Material Override Asset", "HideMeshUntilAnimated", "Configuration", "Target ID", "Target Entity", "Target Bone Name",
    "Target Offset", "Attached Initially", "Scale Source", "Field of View", "Near Clip Plane Distance", "Far Clip Plane Distance",
    "Specify Dimensions", "FrustumWidth", "FrustumHeight", "Preload Name", "Play Trigger", "Stop Trigger", "Plays Immediately",
    "AudioTriggerOnBegin", "AudioTriggerOnEnd", "m_timeDelaySeconds", "m_eventData", "m_initPaperdollData", "m_paperdollAsset",
    "Character Race", "Skin Tone", "Character Hairstyle", "m_name", "m_type", "Enable", "Enabled", "EnableOnActivate", "Particle",
    "SelectedEmitter", "Load Emitter On Activate", "Kill On Deactivate", "OnInitially", "LightType", "m_data", "m_valueStr",
    "m_valueCrc", "m_achievementToUnlock", "contextId", "subId", "TypeId", "AssetPath", "HumanName",
]
NAMES = {zlib.crc32(n.lower().encode()) & 0xFFFFFFFF: n for n in FIELD_NAMES}
SKIP = {F("EditorMetdata"), F("EditorMetadata"), F("Id_newForCurveViewingUndoRedo"), F("edit_only_layerEntityAssignments_statusText"),
        F("edit_only_layerEntityAssignments_AutoCorrected")}


def norm(path) -> str:
    return (path or "").replace("\\", "/").lower().lstrip("/")


def value_of(e):
    """@return The JSON value of a leaf element by its type (big-endian values)."""
    v, t = e.value, e.type_id
    if t == T_STRING:
        return v.decode("utf-8", "replace")
    if t == T_BOOL:
        return bool(v[0]) if v else False
    if t == T_FLOAT and len(v) == 4:
        return round(struct.unpack(">f", v)[0], 6)
    if t == T_DOUBLE and len(v) == 8:
        return struct.unpack(">d", v)[0]
    if t == T_INT and len(v) == 4:
        return struct.unpack(">i", v)[0]
    if t == T_UUID and len(v) == 16:
        return str(uuid.UUID(bytes=v))
    if t == T_VECTOR3 and len(v) >= 12:
        return [round(x, 6) for x in struct.unpack(">3f", v[:12])]
    if t in T_TRANSFORM and len(v) == 48:
        return [round(x, 6) for x in struct.unpack(">12f", v)]
    if t == T_ASSET:
        path = region.asset_path(e)
        return {"asset": str(uuid.UUID(bytes=v[:16])) if len(v) >= 16 else "", "path": norm(path)}
    if len(v) in (1, 2, 4, 8):
        return int.from_bytes(v, "big")
    return v.hex()


def tojson(e):
    """@return An element as JSON: a leaf value, else a dict of its named children ("class" for a known class; the unnamed children
    in "items"). A field with no name in the table is "#CRC"."""
    if not e.children:
        return value_of(e) if e.value is not None else None
    out: dict = {}
    if e.type_id in CLASSES:
        out["class"] = CLASSES[e.type_id]
    elif e.type_id:
        out["type"] = str(uuid.UUID(bytes=e.type_id))
    if e.value is not None:
        out["value"] = value_of(e)
    items = []
    for c in e.children:
        if c.name in SKIP:
            continue
        if c.name is None or c.name == F("element"):
            items.append(tojson(c))
            continue
        key = NAMES.get(c.name, "#%08X" % c.name)
        if key in out:
            key = key + "#" + str(sum(1 for k in out if k.startswith(key)))
        out[key] = tojson(c)
    if items:
        out["items"] = items
    return out


def entity_id(e) -> str:
    """@return The id of an EntityId element (its u64 child), as 16 hex digits."""
    if e is None:
        return ""
    node = e.child(F("id")) or e
    return "%016x" % int.from_bytes(node.value, "big") if node.value else ""


def matrix_of(value) -> list:
    """@return A 4x4 row-major matrix (lists) of a 12-float transform (the columns of the engine form)."""
    m = region.az_matrix(value)
    return [[round(float(x), 6) for x in row] for row in m]


def entities_of(data: bytes) -> list:
    """@return The entities of a slice: id, name, active, parent, local and world matrix, the components (JSON)."""
    out = []
    for root in source.parse(data):
        for entity in source.walk(root):
            components = entity.child(F("Components"))
            if entity.type_id != ENTITY or components is None:
                continue
            if any(c.child(F("Entities")) is not None for c in components.children):
                continue   # the slice container
            name_node = entity.child(F("name"))
            active = entity.child(F("IsRuntimeActive"))
            rec = {"id": entity_id(entity.child(F("id"))), "name": source.text(name_node) if name_node is not None else "",
                   "active": bool(active.value[0]) if active is not None and active.value else None, "components": []}
            for component in components.children:
                if component.type_id == TRANSFORM:
                    for key, field in (("local", "m_localTM"), ("world", "m_worldTM")):
                        node = component.child(F(field))
                        if node is not None and node.value and len(node.value) == 48:
                            rec[key] = matrix_of(node.value)
                    rec["parent"] = entity_id(component.child(F("m_parentId")))
                    continue
                rec["components"].append(tojson(component))
            out.append(rec)
    return out


_timelines: dict[str, dict | None] = {}


def timeline(path: str) -> dict | None:
    path = norm(path)
    if path not in _timelines:
        _timelines[path] = tojson(source.parse(source.read(path))[0]) if path and source.has(path) else None
    return _timelines[path]


def script_events(component) -> list:
    """@return The AchievementEvents of a SlayerScript component: the condition, the flags, the delay, the entity events."""
    out = []
    data = tojson(component)
    container = data.get("m_scriptDataContainer", {}) if isinstance(data, dict) else {}
    if container.get("m_scriptName") != "AchievementEvents":
        return out
    for block in container.get("m_scriptData", {}).get("items", []):
        for event in (block.get("events", {}) or {}).get("items", []):
            entities = []
            for entity_event in (event.get("entityEvents", {}) or {}).get("items", []):
                names = [n.get("m_string") for n in (entity_event.get("entityNames", {}) or {}).get("items", [])]
                entities.append({"eventType": entity_event.get("EventType"), "applyOnChildren": entity_event.get("applyOnChildren"),
                                 "entities": names})
            out.append({"name": event.get("debugName"), "condition": event.get("RequiredAchievementConditional", ""),
                        "onSatisfied": event.get("RunEventsOnConditionalSatisfied"), "onEnterAoi": event.get("RunEventsOnEnterAOI"),
                        "delay": event.get("eventDelayDuration"), "reverse": event.get("reverseEventsOnConditionalUnsatisfied"),
                        "entityEvents": entities})
    return out


def spawner_content(path: str) -> dict | None:
    """@return The facts of a cutscene spawner slice: the root (name, matrix, the script events), the trigger child (name, matrix, the
    cutscene, the scene slice, the flags), or None for a slice with no TriggerPlayerCutsceneComponent."""
    data = source.read(path)
    if TRIGGER not in data:
        return None
    out = {"slice": path, "root": None, "triggers": []}
    for root in source.parse(data):
        for entity in source.walk(root):
            components = entity.child(F("Components"))
            if entity.type_id != ENTITY or components is None or any(c.child(F("Entities")) is not None for c in components.children):
                continue
            name_node = entity.child(F("name"))
            name = source.text(name_node) if name_node is not None else ""
            active = entity.child(F("IsRuntimeActive"))
            matrix = np.eye(4)
            events = []
            trigger = None
            for component in components.children:
                if component.type_id == TRANSFORM:
                    node = component.child(F("m_worldTM"))
                    if node is not None and node.value and len(node.value) == 48:
                        matrix = region.az_matrix(node.value)
                elif component.type_id == SCRIPT:
                    events += script_events(component)
                elif component.type_id == TRIGGER:
                    facet = find_key(tojson(component), "m_clientFacetPtr") or {}
                    trigger = {"cutscene": facet.get("m_preloadCutsceneName", ""), "slice": (facet.get("m_sliceAsset") or {}).get("path", ""),
                               "despawnDelay": facet.get("m_despawnDelay"), "playOnLoad": facet.get("m_playCutsceneOnLoad"),
                               "triggerOnActivate": facet.get("m_triggerOnActivate")}
            if events and out["root"] is None:
                out["root"] = {"entity": name, "matrix": matrix, "events": events}
            if trigger is not None:
                trigger.update({"entity": name, "active": bool(active.value[0]) if active is not None and active.value else None,
                                "matrix": matrix})
                out["triggers"].append(trigger)
    return out


def find_key(node, key: str):
    """@return The first value of a key in a JSON tree (depth first), or None."""
    if isinstance(node, dict):
        if key in node:
            return node[key]
        for value in node.values():
            found = find_key(value, key)
            if found is not None:
                return found
    elif isinstance(node, list):
        for value in node:
            found = find_key(value, key)
            if found is not None:
                return found
    return None


def rows(m: np.ndarray) -> list:
    return [[round(float(x), 6) for x in row] for row in m]


def place_spawners(spawners: dict) -> list:
    """@return Each placement of a cutscene spawner in the capitals of the regions of the world (the capital transform)."""
    found = []
    for region_name in census.world_regions():
        folder = region.WORLD + "regions/" + region_name + "/"
        for manifest in source.paks().paths(folder + "capitals/", ".capitals.json"):
            layer = manifest.split("/capitals/")[1].split("/")[0]
            for capital in json.loads(source.read(manifest)).get("Capitals", []):
                key = region.slice_key(capital.get("sliceName", ""))
                path = next((p for p in (key + ".dynamicslice", "slices/" + key + ".dynamicslice") if p in spawners), None)
                if path is None:
                    text = str(capital.get("sliceAssetId", ""))
                    product = source.product_of_text(text, ".dynamicslice") if text.startswith("{") and not text.startswith("{00000000") else None
                    path = product if product in spawners else None
                if path is None:
                    continue
                q, p = capital.get("rotation", {}), capital.get("worldPosition", {})
                m = np.eye(4)
                m[:3, :3] = region.quat_matrix(q.get("x", 0.0), q.get("y", 0.0), q.get("z", 0.0), q.get("w", 1.0)) * float(capital.get("scale", 1.0))
                m[:3, 3] = (p.get("x", 0.0), p.get("y", 0.0), p.get("z", 0.0))
                found.append({"region": region_name, "layer": layer, "capital": str(capital.get("id", "")), "slice": path, "matrix": m})
    return found


def actors_of(entities: list) -> list:
    """@return The skinned actors of a scene: the entity, its CDF, its visibility."""
    out = []
    for e in entities:
        for c in e["components"]:
            if isinstance(c, dict) and c.get("class") == "SkinnedMeshComponent":
                mesh = c.get("Skinned Mesh")
                found = None
                stack = [c]
                while stack and found is None:
                    node = stack.pop()
                    if isinstance(node, dict):
                        if "path" in node and str(node.get("path", "")).endswith((".cdf", ".chr", ".skin")):
                            found = node["path"]
                        stack += list(node.values())
                    elif isinstance(node, list):
                        stack += node
                out.append({"entity": e["name"], "id": e["id"], "mesh": found or (mesh if isinstance(mesh, str) else "")})
    return out


def assigned(entry: dict) -> dict:
    """@return The slice entity id (16 hex digits) of each layer id of a TimelineEntry (its LayerEntityAssignments)."""
    out = {}
    for item in (entry.get("LayerEntityAssignments") or {}).get("items", []):
        node = item.get("EntityId") or {}
        if isinstance(node, dict) and node.get("id") is not None:
            out[item.get("LayerId", "")] = "%016x" % int(node["id"])
    return out


def clips_of_timeline(tl: dict | None, entry: dict) -> list:
    """@return The layers of a timeline with an AnimationAlias: the layer name, its assigned slice entity, and the aliases."""
    out = []
    owners = assigned(entry)
    for layer in (tl or {}).get("Layers", {}).get("items", []):
        aliases = []
        for holder in (layer.get("Clips", {}) or {}).get("items", []):
            for clip in (holder.get("items", []) if isinstance(holder, dict) else []):
                if isinstance(clip, dict) and clip.get("AnimationAlias"):
                    aliases.append(clip["AnimationAlias"])
        if aliases:
            out.append({"layer": layer.get("name", ""), "entityId": owners.get(layer.get("id", ""), ""), "aliases": aliases})
    return out


def wanted_of(actors: list, clips: list) -> list:
    """@return For each actor: its CDF, its skeleton, the kind of the pipeline (person: the player and person skeleton of the player
    pipeline; character: another skeleton, tools/convert.sh character), and the clip files of the aliases of the layers that the
    LayerEntityAssignments give to its entity."""
    out = []
    for actor in actors:
        cdf = actor["mesh"]
        skeleton = persons.cdf_model(cdf) if cdf.endswith(".cdf") else ""
        names = {k.lower(): v for k, v in (persons.clips_of(cdf) if cdf.endswith(".cdf") else {}).items()}
        aliases = [a for group in clips for layer in group["layers"] if layer["entityId"] == actor["id"] for a in layer["aliases"]]
        out.append({"entity": actor["entity"], "id": actor["id"], "cdf": cdf, "skeleton": skeleton,
                    "kind": "person" if skeleton in persons.PERSON_SKELETONS else "character",
                    "clips": [{"alias": a, "caf": names.get(a.lower(), "")} for a in aliases]})
    return out


CINEMATICS = "sharedassets/springboardentitites/datatables/cinematics/javelindata_cinematicvideoregistry.datasheet"
WHISPER_TIMELINES = "sharedassets/springboardentitites/datatables/timelines/javelindata_whispertimelines.datasheet"


def bink_header(path: str) -> dict | None:
    """@return The frames and the rate of a Bink 2 file (the header: KB2 signature, size, frames, largest frame, frames, width,
    height, rate dividend, rate divider; little-endian), and the length in seconds."""
    if not source.has(path):
        return None
    head = source.read(path)[:36]
    if len(head) < 36 or head[:3] != b"KB2":
        return None
    frames, _, _, width, height, dividend, divider = struct.unpack("<7I", head[8:36])
    return {"frames": frames, "width": width, "height": height, "rate": [dividend, divider],
            "seconds": round(frames * divider / dividend, 6) if dividend else None}


def subtitle_attributes() -> dict:
    """@return The timed localization entries of en-us (a start attribute): key -> start, end, speaker, the next key, the file."""
    import re
    out: dict[str, dict] = {}
    for path in source.paks().paths("localization/en-us/"):
        if not path.endswith(".loc.xml"):
            continue
        text = source.read(path).decode("utf-8", "replace")
        if " start=" not in text:
            continue
        for match in re.finditer(r"<string\s+([^>]*?)>", text):
            attrs = dict(re.findall(r'([\w-]+)="([^"]*)"', match.group(1)))
            key = attrs.get("key", "").lower()
            if key and "start" in attrs and key not in out:
                out[key] = {"start": attrs.get("start"), "end": attrs.get("end"), "speaker": attrs.get("speaker"),
                            "next": (attrs.get("dialogue-next") or "").lower(), "file": path.rsplit("/", 1)[-1]}
    return out


def write_whispers(out_dir: Path) -> dict:
    """quests/whispers.json: the WhisperData rows (WhisperId, CRC-32 of the lower-case id, the duration, the timeline, the voice
    timeline of en-us, the subtitle key), the whisper timelines (JSON), the cinematic registry with the Bink header of each video of
    the quest tasks, and the timed localization entries."""
    whisper_paths = {p.rsplit("/", 1)[-1]: p for p in source.paks().paths("timelines/whispers/", ".timeline")}
    voice_paths = {p.rsplit("/", 1)[-1]: p for p in source.paks().paths("timelines/vo/en-us/", ".timeline")}
    entries = {str(r.get("TimelineEntryName", "")).lower(): str(r.get("TimelineAssetPath", "")).lower()
               for r in datasheet.Sheet(source.read(WHISPER_TIMELINES)).rows()}
    whispers = {}
    for row in datasheet.load("javelindata_whisperdata.datasheet").rows():
        wid = str(row.get("WhisperId", ""))
        timeline_file = entries.get(str(row.get("TimelineId", "")).lower(), "")
        timeline_path = whisper_paths.get(timeline_file.rsplit("/", 1)[-1], "")
        voice = str(row.get("VoiceOver", "")).lower()
        voice_path = voice_paths.get(voice[len("play_"):] + ".timeline", "") if voice.startswith("play_") else ""
        whispers[wid.lower()] = {"row": {k: row[k] for k in row}, "crc": "%08x" % (zlib.crc32(wid.lower().encode()) & 0xFFFFFFFF),
                                 "timelinePath": timeline_path, "timeline": timeline(timeline_path) if timeline_path else None,
                                 "voicePath": voice_path}
    quests = json.loads((env.content_dir() / "ui" / "gamedata" / "quests.json").read_text())
    named = {str(t["cells"].get("ItemName", "")).lower() for t in quests["tasks"].values()
             if t.get("type") in ("taskcinematiccomplete", "taskcinematicstart")}
    cinematics = {}
    for row in datasheet.Sheet(source.read(CINEMATICS)).rows():
        cid = str(row.get("CinematicVideoId", ""))
        rec = {"row": {k: row[k] for k in row}, "crc": "%08x" % (zlib.crc32(cid.lower().encode()) & 0xFFFFFFFF), "quest": cid.lower() in named}
        if rec["quest"]:
            rec["video"] = bink_header(norm(row.get("VideoPath", "")))
        cinematics[cid.lower()] = rec
    doc = {"format": FORMAT, "whispers": whispers, "cinematics": cinematics, "subtitles": subtitle_attributes()}
    (out_dir / "whispers.json").write_text(json.dumps(doc, indent=1))
    return {"whispers": len(whispers), "whisperTimelines": sum(1 for w in whispers.values() if w["timeline"]),
            "voiceTimelines": sum(1 for w in whispers.values() if w["voicePath"]), "cinematics": len(cinematics),
            "questCinematics": sum(1 for c in cinematics.values() if c["quest"]), "timedSubtitles": len(doc["subtitles"])}


def write(only: str | None = None) -> dict:
    quests = json.loads((env.content_dir() / "ui" / "gamedata" / "quests.json").read_text())
    tasks: dict[str, list] = {}
    for task in quests["tasks"].values():
        if task.get("type") == "taskcutscenecomplete":
            tasks.setdefault(str(task["cells"].get("ItemName", "")).lower(), []).append(task["id"])
    spawners = {}
    for path in source.paks().paths("coatgen/", ".dynamicslice"):
        if "igc" in path:
            content = spawner_content(path)
            if content is not None:
                spawners[path] = content
    placements = place_spawners(spawners)
    out_dir = env.content_dir() / "quests"
    (out_dir / "scenes").mkdir(parents=True, exist_ok=True)
    index = {"format": FORMAT, "source": "the cutscene spawners of the capitals, the scene slices and timelines (swc_convert.scenes)",
             "spawners": [], "scenes": {}, "counts": {}}
    scene_slices: dict[str, str] = {}
    for place in placements:
        content = spawners[place["slice"]]
        root = content["root"] or {"entity": "", "matrix": np.eye(4), "events": []}
        for trigger in content["triggers"]:
            name = trigger["cutscene"]
            scene_slices.setdefault(name.lower(), trigger["slice"])
            index["spawners"].append({
                "region": place["region"], "layer": place["layer"], "capital": place["capital"], "slice": place["slice"],
                "matrix": rows(place["matrix"]), "position": [round(float(v), 3) for v in place["matrix"][:3, 3]],
                "root": {"entity": root["entity"], "world": rows(place["matrix"] @ root["matrix"]), "events": root["events"]},
                "trigger": {k: v for k, v in trigger.items() if k != "matrix"} | {"world": rows(place["matrix"] @ trigger["matrix"]),
                                                                                  "crc": "%08x" % (zlib.crc32(name.lower().encode()) & 0xFFFFFFFF)}})
    for name in sorted(set(scene_slices) | set(tasks)):
        if only and name != only.lower():
            continue
        path = scene_slices.get(name) or next((p for p in source.paks().paths("slices/igc/", ".dynamicslice") if p.endswith("/" + name + ".dynamicslice")), "")
        crc = "%08x" % (zlib.crc32(name.encode()) & 0xFFFFFFFF)
        record = {"name": name, "crc": crc, "slice": path, "tasks": tasks.get(name, []), "placed": name in scene_slices}
        if path and source.has(path):
            entities = entities_of(source.read(path))
            info = None
            timelines = []
            for e in entities:
                for c in e["components"]:
                    if not isinstance(c, dict):
                        continue
                    if c.get("class") == "TriggerActivatePlayerCutsceneComponent":
                        info = find_key(c, "m_cutsceneInfo")
                    if c.get("class") == "TimelineComponent":
                        for entry in (c.get("m_timelines") or {}).get("items", []):
                            asset = (entry.get("TimelineAsset") or {}).get("path", "")
                            timelines.append({"entity": e["name"], "entityId": e["id"], "asset": asset, "entry": entry,
                                              "timeline": timeline(asset)})
            scene = {"format": FORMAT, "name": name, "crc": crc, "slice": path, "info": info, "entities": entities, "timelines": timelines}
            (out_dir / "scenes" / (name + ".json")).write_text(json.dumps(scene, indent=1))
            record["actors"] = actors_of(entities)
            record["clips"] = [{"entity": t["entity"], "asset": t["asset"], "layers": clips_of_timeline(t["timeline"], t["entry"])}
                               for t in timelines]
            record["wanted"] = wanted_of(record["actors"], record["clips"])
            record["length"] = max([c.get("BaseClass1", {}).get("EndTime", 0) for t in timelines
                                    for layer in ((t["timeline"] or {}).get("Layers", {}) or {}).get("items", [])
                                    for holder in (layer.get("Clips", {}) or {}).get("items", [])
                                    for c in (holder.get("items", []) if isinstance(holder, dict) else []) if isinstance(c, dict)] or [0])
            record["info"] = {k: v for k, v in (info or {}).items() if not isinstance(v, (dict, list))}
        index["scenes"][name] = record
    index["counts"] = {"spawnerSlices": len(spawners), "placements": len(index["spawners"]), "scenes": len(index["scenes"]),
                       "questScenes": len(tasks), "questScenesPlaced": sum(1 for n in tasks if n in scene_slices),
                       "questScenesWithSlice": sum(1 for n in tasks if index["scenes"].get(n, {}).get("slice"))}
    (out_dir / "scenes.json").write_text(json.dumps(index, indent=1))
    return index["counts"] | write_whispers(out_dir)


def scene_clip_pack(files: list, force: bool, doc: dict) -> dict:
    """Converts the scene clips of the person and player skeletons into one pack on the player skeleton, with the root bone track (a
    scene clip moves its actor from the place of the slice entity; the clips of the persons lock the root)."""
    gender = persons.GENDER
    key = f"clips|{player.GENDERS[gender]}|scenes"
    path = player.player_dir() / "clips" / player.GENDERS[gender] / "scenes" / "clips.gltf"
    known = doc["assets"].get(key, {}).get("clips", [])
    if path.is_file() and not force and sorted(known) == sorted(files):
        return {"clips": key, "skipped": "exists"}
    path.parent.mkdir(parents=True, exist_ok=True)
    skeleton = player.Skeleton(gender)
    w = gltf.Writer()
    first, roots = skeleton.write(w)
    w.doc["scenes"][0]["nodes"] += roots
    failed, clips = {}, []
    for file in files:
        try:
            clips.append(skeleton.add_clip(w, first, file, file.rsplit("/", 1)[-1].rsplit(".", 1)[0], root_lock=False))
        except Exception as error:
            failed[file] = str(error)[:120]
    w.save(path)
    doc["assets"][key] = {"kind": "clips", "gender": gender, "tags": ["scenes"], "file": str(path.relative_to(env.content_dir())),
                          "unreal": f"{player.UNREAL_ROOT}/{gender}/Clips/scenes", "clips": files}
    return {"clips": key, "count": len(clips), "failed": failed, "durations": {c["clip"]: c["duration"] for c in clips}}


def convert(names: list | None = None, force: bool = False) -> dict:
    """Converts the actors of the scenes (default: the scenes of the beach chain, the tasks 06_*): the bodies of the person and
    player skeletons with the player pipeline (kind part) and their clips in one scene pack (then tools/import-player.sh); the actors of
    other skeletons as characters with their clips (tools/convert.sh character; then tools/import.sh)."""
    index = json.loads((env.content_dir() / "quests" / "scenes.json").read_text())
    chosen = [n.lower() for n in names] if names else sorted(n for n, v in index["scenes"].items()
                                                             if any(t.lower().startswith("06_") for t in v.get("tasks", [])))
    doc = player.load_manifest()
    report: dict = {"scenes": chosen, "bodies": [], "characters": [], "clips": None, "failed": {}}
    scene_clips: list = []
    for name in chosen:
        for actor in index["scenes"].get(name, {}).get("wanted", []):
            cdf = actor["cdf"]
            cafs = [c["caf"] for c in actor["clips"] if c["caf"]]
            if not cdf.endswith(".cdf"):
                report["failed"][f"{name}:{actor['entity']}"] = "no .cdf: " + cdf
                continue
            try:
                if actor["kind"] == "person":
                    report["bodies"].append(persons.body(cdf, persons.cdf_look(cdf), force, doc))
                    scene_clips += [c for c in cafs if c not in scene_clips]
                else:
                    result = character.convert(cdf, cafs, force=force)
                    report["characters"].append({k: result.get(k) for k in ("cdf", "skeleton", "gltf", "skipped")}
                                                | {"clips": len(result.get("clips", []))})
            except Exception as error:
                report["failed"][f"{name}:{actor['entity']}"] = str(error)[:160]
    report["clips"] = scene_clip_pack(scene_clips, force, doc)
    player.save_manifest(doc)
    return report


def main(argv=None) -> int:
    argv = list(sys.argv[1:] if argv is None else argv)
    if argv and argv[0] == "write":
        only = argv[argv.index("--only") + 1] if "--only" in argv else None
        print(json.dumps(write(only), indent=1))
        return 0
    if argv and argv[0] == "convert":
        names = [a for a in argv[1:] if not a.startswith("--")]
        print(json.dumps(convert(names or None, "--force" in argv), indent=1))
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main())
