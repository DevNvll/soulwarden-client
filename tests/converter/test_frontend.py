"""Tests of the scene reader of the front end (converter/swc_convert/frontend.py, stage F4 of docs/FRONTEND.md) with no game
data: a made-up object stream in the XML form of the menu level (AZ::Entity, TransformComponent, CameraComponent,
LightComponent, SequenceComponent with a compound spline track). The names and the numbers are made up."""
import sys
import unittest
import xml.etree.ElementTree as ET
from xml.sax.saxutils import quoteattr
from pathlib import Path

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "converter"))
from swc_convert import frontend  # noqa: E402


def field(name: str, cls: str, value: str | None = None, children: str = "") -> str:
    v = f" value={quoteattr(value)}" if value is not None else ""
    return f"<Class name={quoteattr(cls)} field={quoteattr(name)}{v}>{children}</Class>"


def vec2(name: str, x: float, y: float) -> str:
    return field(name, "Vec2", children=field("x", "float", str(x)) + field("y", "float", str(y)))


def key(time: float, flags: int, value: float, ds: tuple, dd: tuple) -> str:
    inner = (field("time", "float", str(time)) + field("flags", "int", str(flags)) + vec2("value", time, value)
             + vec2("ds", *ds) + vec2("dd", *dd))
    return field("element", "spline::SplineKeyEx<Vec2>", children=field("BaseClass1", "spline::SplineKey<Vec2>", children=inner))


def sub_track(default: float, keys: str) -> str:
    spline = field("Spline", "spline::TrackSplineInterpolator<Vec2>", children=field(
        "BaseClass1", "BezierSplineVec2", children=field("BaseClass1", "TSplineBezierBasisVec2", children=field(
            "Keys", "AZStd::vector", children=keys))))
    track = field("element", "TAnimSplineTrack<Vec2 >", children=vec2("DefaultValue", 0.0, default) + spline)
    return field("element", "AZStd::intrusive_ptr", children=track)


def compound(param: str, subs: str) -> str:
    return field("element", "CCompoundSplineTrack", children=field(
        "ParamType", "CAnimParamType", children=field("Name", "AZStd::string", param)) + field("SubTracks", "AZStd::vector", children=subs))


def node(cls: str, ident: int, name: str, parent: int, tracks: str = "") -> str:
    base = field("BaseClass1", "CAnimNode", children=field("ID", "int", str(ident)) + field("Name", "AZStd::string", name)
                 + field("Parent", "int", str(parent)) + field("Tracks", "AZStd::vector", children=tracks))
    return field("element", "AZStd::intrusive_ptr", children=field("element", cls, children=base))


def sequence() -> str:
    px = sub_track(10.0, key(0.0, 9, 12.0, (1.0, 0.0), (0.3, -0.1)) + key(1.0, 9, 10.0, (0.3, -0.1), (0.3, 0.0)))
    py = sub_track(20.0, key(0.0, 18, 20.0, (0.3, 0.0), (0.3, 0.0)))
    pz = sub_track(30.0, "")
    position = compound("Position", px + py + pz)
    nodes = node("CAnimAzEntityNode", 1, "MainCamera", 0) + node("CAnimComponentNode", 2, "Transform", 1, position)
    seq = field("element", "CAnimSequence", children=field("Name", "AZStd::string", "MadeIntro") + field("Flags", "int", "0")
                + field("TimeRange", "Range", children=field("Start", "float", "0") + field("End", "float", "1"))
                + field("Nodes", "AZStd::vector", children=nodes))
    return field("element", "SequenceComponent", children=field("Sequence", "AZStd::intrusive_ptr", children=seq))


def transform(world: str, local: str | None = None, parent: str | None = None) -> str:
    inner = field("Transform", "AZ::Transform", world)
    if local:
        inner += field("LocalTransform", "AZ::Transform", local)
    if parent:
        inner += field("Parent", "EntityId", children=field("id", "AZ::u64", parent))
    return field("element", "TransformComponent", children=inner)


class SceneReader(unittest.TestCase):
    def setUp(self):
        self.root = ET.fromstring(
            "<Components>"
            + transform("0 1 0 -1 0 0 0 0 1 3001 2002 84", "0 1 0 -1 0 0 0 0 1 1981 3702 122", "77")
            + field("element", "CameraComponent", children=field("Field of View", "float", "55.0")
                    + field("Near Clip Plane Distance", "float", "0.2") + field("Far Clip Plane Distance", "float", "8000")
                    + field("EnableOnActivate", "bool", "true"))
            + field("element", "LightComponent", children=field("LightType", "int", "2") + field("Visible", "bool", "true")
                    + field("OnInitially", "bool", "true") + field("Color", "Color", "1 0.5 0.25 1")
                    + field("DiffuseMultiplier", "float", "1.5") + field("ProjectorDistance", "float", "2.3")
                    + field("ProjectorFOV", "float", "90") + field("Unused", "float", "7"))
            + sequence()
            + "</Components>")

    def test_transform(self):
        t = frontend._transform(list(self.root))
        self.assertEqual(t["world"][9:12], [3001.0, 2002.0, 84.0])
        self.assertEqual(t["local"][9:12], [1981.0, 3702.0, 122.0])
        self.assertEqual(t["parent"], "77")

    def test_camera(self):
        self.assertEqual(frontend._camera(list(self.root)),
                         {"fov": 55.0, "near": 0.2, "far": 8000.0, "enableOnActivate": True})

    def test_light(self):
        light = frontend._light(list(self.root))
        self.assertEqual(light["LightType"], "2")
        self.assertEqual(light["Color"], "1 0.5 0.25 1")
        self.assertEqual(light["ProjectorDistance"], "2.3")
        self.assertNotIn("Unused", light)

    def test_sequence(self):
        component = next(c for c in self.root if c.get("name") == "SequenceComponent")
        seq = frontend._sequence(component)
        self.assertEqual((seq["name"], seq["flags"], seq["start"], seq["end"]), ("MadeIntro", 0, 0.0, 1.0))
        # The tracks go to the entity node (MainCamera), not to its component node (Transform).
        self.assertEqual(sorted(seq["nodes"]), ["MainCamera"])
        tracks = seq["nodes"]["MainCamera"]
        self.assertEqual(sorted(tracks), ["PX", "PY", "PZ"])
        first, last = tracks["PX"]["keys"]
        self.assertEqual((first["time"], first["flags"], first["value"], first["dd"]), (0.0, 9, [0.0, 12.0], [0.3, -0.1]))
        self.assertEqual((last["value"], last["ds"]), ([1.0, 10.0], [0.3, -0.1]))
        self.assertEqual(tracks["PY"]["keys"][0]["flags"], 18)
        self.assertEqual((tracks["PZ"]["default"], tracks["PZ"]["keys"]), (30.0, []))


class MenuEnvironment(unittest.TestCase):
    MISSION = (b'<Mission><Environment><Lighting SunRotation="191" Longitude="111"/><Moon Latitude="241" Longitude="36" Size="0.4"/>'
               b'</Environment><TimeOfDay Time="15.5"/></Mission>')

    def test_override(self):
        tod = {"FileName": "Libs\\TimeOfDay\\Made\\Made_Up.xml", "HasTimeOfDayOverride": "true", "TimeOfDayOverride": "16.25",
               "SliceAsset": "id={0}:2,type={1},hint={slices/sky/made_up_sky.dynamicslice}"}
        out = frontend.environment(tod, self.MISSION)
        self.assertEqual((out["preset"], out["hour"], out["sky"]), ("libs/timeofday/made/made_up.xml", 16.25, "slices/sky/made_up_sky.dynamicslice"))
        self.assertEqual((out["sunRotation"], out["longitude"], out["moonLatitude"], out["moonLongitude"], out["moonSize"]),
                         (191.0, 111.0, 241.0, 36.0, 0.4))

    def test_no_override(self):
        # With no hour override, the hour of the mission; with no TOD entity, no preset and no sky.
        out = frontend.environment({"FileName": "Libs/TimeOfDay/x.xml", "HasTimeOfDayOverride": "false", "TimeOfDayOverride": "3"}, self.MISSION)
        self.assertEqual(out["hour"], 15.5)
        out = frontend.environment(None, self.MISSION)
        self.assertEqual((out["preset"], out["sky"], out["hour"]), ("", "", 15.5))


class FarPart(unittest.TestCase):
    # A made-up camera at the origin that looks along +Y (its X axis to the right, Z up).
    CAMERA = [1, 0, 0, 0, 1, 0, 0, 0, 1, 0.0, 0.0, 0.0]

    def test_in_view(self):
        self.assertTrue(frontend.in_view(self.CAMERA, [0.0, 100.0, 0.0], 5.0))
        # The horizontal half angle is about 44.7 degrees: at 100 m ahead the edge is 99 m to the side (and 7 m more for 5 m).
        self.assertTrue(frontend.in_view(self.CAMERA, [95.0, 100.0, 0.0], 5.0))
        self.assertFalse(frontend.in_view(self.CAMERA, [130.0, 100.0, 0.0], 5.0))
        self.assertFalse(frontend.in_view(self.CAMERA, [0.0, -100.0, 0.0], 5.0))     # behind the camera
        self.assertFalse(frontend.in_view(self.CAMERA, [0.0, 100.0, 90.0], 5.0))     # above the vertical FOV of 55 degrees
        self.assertTrue(frontend.in_view(self.CAMERA, [0.0, 100.0, 90.0], 50.0))     # a large mesh at the edge

    def test_mesh_radius_of_no_file(self):
        self.assertEqual(frontend.mesh_radius(None), 0.0)
        self.assertEqual(frontend.mesh_radius({"kind": "empty"}), 0.0)


if __name__ == "__main__":
    unittest.main()
