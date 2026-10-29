"""The frame marks of the Talk camera points (swc_convert/persons.camera_point, AST-244 item 6): the frame of the speaker with no mark or
"s", the frame of the player ("p": the dummy of the preset), and the frame of a named person ("n": not placed)."""
import math
import unittest

from swc_convert.persons import camera_point, line_camera

DUMMY = {"position": [0.25, 1.3, 0.0], "rotation": [0.0, 0.0, 180.0], "armed": False}


class CameraPointTest(unittest.TestCase):
    def test_speaker_frame(self):
        self.assertEqual(camera_point("-0.35 2.45 1.75", DUMMY), [-0.35, 2.45, 1.75])
        self.assertEqual(camera_point("s 1.4 2.2 1.7", DUMMY), [1.4, 2.2, 1.7])   # the line 01_Urda_Default_00
        self.assertEqual(camera_point("s 0 1.5 1.6", None), [0.0, 1.5, 1.6])

    def test_player_frame_through_the_dummy(self):
        # The dummy stands at (0.25, 1.3) and faces the speaker (180 degrees): a point 2.25 m behind the player is 3.55 m from the speaker.
        self.assertEqual(camera_point("p .5 -2.25 1.9", DUMMY), [-0.25, 3.55, 1.9])
        self.assertEqual(camera_point("p .5 -2.25 1.9", None), [])

    def test_named_person_and_bad_values(self):
        self.assertEqual(camera_point("n 01_AelstromWW_03 -3 .5 1.65", DUMMY), [])
        self.assertEqual(camera_point("", DUMMY), [])
        self.assertEqual(camera_point(None, DUMMY), [])


class LineCameraTest(unittest.TestCase):
    """The camera of a line of a conversation state (persons.line_camera; line-cameras.json, ut567): the retail Talk camera of Ser Payne
    Bennett (the line NPC_DefaultDialogue_00_SerPayneBennett_a_new, the preset NSR_MNP_MED_2SHT_OTS_Primary; set ut567-camera, state.py
    3.5 s after E: 7422.902 3622.254 106.015, heading 285.4) in his talk frame (7420.605 3623.176 104.2, yaw -120)."""

    PRESET = {"CutsceneCameraId": "P", "CameraState": "NPC_FOV20_CamState", "CameraStateOrigin": "-0.35 2.45 1.75",
              "CameraStateLookAt": "0.26 0.1 1.47", "HidePlayerAvatar": "true", "DummyPresetPos": "s .25 1.3 0",
              "DummyPresetRot": "s 0 0 180", "EnterBlendTime": 0.0}

    @staticmethod
    def world(point, place=(7420.605, 3623.176, 104.2), yaw=-120.0):
        # The client rule (swc::interact::scene_point): the place plus the point turned by the yaw (engine x east, y north).
        a = math.radians(yaw)
        x, y, z = point
        return (place[0] + x * math.cos(a) - y * math.sin(a), place[1] + x * math.sin(a) + y * math.cos(a), place[2] + z)

    def test_preset_of_the_line(self):
        camera = line_camera({"cutsceneCameraPresetId": "P"}, {"P": self.PRESET})
        self.assertEqual(camera["origin"], [-0.35, 2.45, 1.75])
        self.assertEqual(camera["lookAt"], [0.26, 0.1, 1.47])
        self.assertEqual(camera["fov"], 20.0)
        self.assertTrue(camera["hidePlayer"])
        self.assertEqual(camera["dummy"]["position"], [0.25, 1.3, 0.0])
        origin, look = self.world(camera["origin"]), self.world(camera["lookAt"])
        # The camera stands 0.065 m above its preset point (TalkCameraLift of the client).
        for got, want in zip((origin[0], origin[1], origin[2] + 0.065), (7422.902, 3622.254, 106.015)):
            self.assertAlmostEqual(got, want, delta=0.01)
        heading = math.degrees(math.atan2(look[0] - origin[0], look[1] - origin[1])) % 360
        self.assertAlmostEqual(heading, 285.4, delta=0.1)

    def test_line_attributes_replace_the_preset_columns(self):
        camera = line_camera({"cutsceneCameraPresetId": "P", "cameraStateOrigin": "s 1.4 2.2 1.7"}, {"P": self.PRESET})
        self.assertEqual(camera["origin"], [1.4, 2.2, 1.7])
        self.assertEqual(camera["lookAt"], [0.26, 0.1, 1.47])

    def test_no_camera(self):
        camera = line_camera({}, {})
        self.assertEqual(camera["origin"], [])
        self.assertFalse(camera["hidePlayer"])


if __name__ == "__main__":
    unittest.main()
