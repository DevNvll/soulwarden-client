"""AST-271: the ranged converter exports ClearMannequinTagGroup and the tag groups of the player controller (no game data: a small
tags file and a small action)."""
import sys
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "converter"))

from swc_convert import ranged  # noqa: E402

TAGS = b"""<TagDefinition version="2"><Tags>
<Group name="Aiming"><Tag name="ChargedAim" priority="1"/><Tag name="Aim"/><Tag name="PenetratingAim"/></Group>
<Group name="Posture"><Tag name="Stand"/><Tag name="Crouch" priority="2"/></Group>
<Tag name="AimDownSights"/>
</Tags></TagDefinition>"""

ACTION = """<Action Name="Bow_AimChargeShoot_SubState" FragmentName="Bow_Aim_UpperBody"><Activities>
<SetMannequinTag ResetWhenReinstalled="false"><Default TagName="AimDownSights" OnEnter="NoEffect" OnExit="Unset"/></SetMannequinTag>
<ClearMannequinTagGroup ResetWhenReinstalled="false"><Default TagGroupName="Aiming" OnEnter="NoEffect" OnExit="Clear"/></ClearMannequinTagGroup>
</Activities></Action>"""


class RangedTagsTest(unittest.TestCase):
    def test_tag_groups(self):
        with mock.patch.object(ranged.source, "read", return_value=TAGS) as read:
            groups = ranged.tag_groups()
        read.assert_called_once_with(ranged.TAGS)
        self.assertEqual(groups, {"aiming": ["aim", "chargedaim", "penetratingaim"], "posture": ["crouch", "stand"]})

    def test_group_clear_activity(self):
        activities = ranged.activities_of(ET.fromstring(ACTION))
        self.assertIn(["ClearMannequinTagGroup", {"TagGroupName": "Aiming", "OnEnter": "NoEffect", "OnExit": "Clear"}, None], activities)


if __name__ == "__main__":
    unittest.main()
