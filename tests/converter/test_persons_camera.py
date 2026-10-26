"""The frame marks of the Talk camera points (swc_convert/persons.camera_point, AST-244 item 6): the frame of the speaker with no mark or
"s", the frame of the player ("p": the dummy of the preset), and the frame of a named person ("n": not placed)."""
import unittest

from swc_convert.persons import camera_point

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


if __name__ == "__main__":
    unittest.main()
