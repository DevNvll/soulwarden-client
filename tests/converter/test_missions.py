"""The mission data of the boards (swc_convert/missions.build, C-12 of ut567) with synthetic data (no game data): the town filter rows with
the parse rules of the backend loader (an empty whole-number cell is 0, a decimal is truncated, Enabled absent is true), the project CRC of
TerritoryProjectId, the world rows only in "missions", the problems (a level that is not a number, a bucket member that is not a town row,
a project whose CRC is not the CRC of its id)."""
import unittest
import zlib

from swc_convert.missions import build


def crc(text):
    return zlib.crc32(text.lower().encode()) & 0xFFFFFFFF


SOURCE = {
    "missions": [
        {"id": "Town_A", "crc": 11, "table": "town", "objective": "CM_x", "objectiveCrc": 5, "territories": {"ids": [16], "exclude": True},
         "cells": {"TerritoryProjectId": "CommunityProjectId", "MinLevel": "", "MaxLevel": "60.0", "TradeskillLevel": "0",
                   "RequiredTradeskill": "", "TitleOverride": "@t"}},
        {"id": "Town_B", "crc": 12, "table": "town", "territories": {"ids": [], "exclude": False},
         "cells": {"TerritoryProjectId": "CommunityProjectId", "MinLevel": "5", "MaxLevel": "0", "TradeskillLevel": "50",
                   "RequiredTradeskill": "Smelting", "Enabled": "0", "RequiredAchievementConditional": "A && !B"}},
        {"id": "Town_Bad", "crc": 13, "table": "town", "territories": {"ids": [], "exclude": False}, "cells": {"MinLevel": "x"}},
        {"id": "World_A", "crc": 21, "table": "world", "territories": {"ids": [2], "exclude": False}, "cells": {"RequiredFaction": "Marauder"}},
    ],
    "projects": [{"id": "CommunityProjectId", "crc": crc("CommunityProjectId"), "type": "AlwaysAvailable", "level": 1, "cells": {}},
                 {"id": "Wrong", "crc": 1, "type": "Settlement", "level": 2, "cells": {}}],
    "townBuckets": [{"key": 4, "goal": "Kill", "missions": [11, 21]}, {"key": 1, "goal": "Gather", "missions": [12]}],
    "events": {"e": {"id": "E"}}, "rewardModifiers": [{"id": "Mission"}], "weights": [{"cells": {}}],
}


class MissionsTest(unittest.TestCase):
    def test_build(self):
        doc, problems = build(SOURCE)
        rows = {r[0]: r for r in doc["town"]["rows"]}
        self.assertEqual(rows[11], [11, crc("CommunityProjectId"), True, [16], True, 0, 60, "", 0, ""])
        self.assertEqual(rows[12], [12, crc("CommunityProjectId"), False, [], False, 5, 0, "Smelting", 50, "A && !B"])
        self.assertNotIn(13, rows)      # the level cell is not a whole number
        self.assertNotIn(21, rows)      # a world row is not a town row
        self.assertEqual(set(doc["missions"]), {"11", "12", "13", "21"})
        self.assertEqual(doc["missions"]["11"]["cells"]["TitleOverride"], "@t")
        self.assertEqual(doc["town"]["projects"], [[crc("CommunityProjectId"), "AlwaysAvailable", 1], [1, "Settlement", 2]])
        self.assertEqual(doc["town"]["buckets"][0], {"key": 4, "goal": "Kill", "missions": [11, 21]})
        self.assertEqual(sorted(problems), ["Town_Bad: a level cell that is not a whole number",
                                            "the bucket Kill names 1 mission(s) that are not town rows",
                                            "the project Wrong: its CRC is not the CRC-32 of its lower-case id"])
        self.assertEqual((doc["events"], doc["rewardModifiers"], doc["weights"]), (SOURCE["events"], SOURCE["rewardModifiers"], SOURCE["weights"]))


if __name__ == "__main__":
    unittest.main()
