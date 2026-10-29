"""The territory card data (swc_convert/territorycards.build, step 3 of ut567, QG1) with synthetic data (no game data): the cards of the
backend copy with the UI columns of ProgressionPointData, the card CRC keys, the rank rows as card CRCs, the standing pool CRCs, and the
problems (a card with no row, a column that differs, a rank row with an unknown card)."""
import unittest
import zlib

from swc_convert.territorycards import build

BACKEND = {
    "standings": [["6", 300, "Territory_Standing", "TerritoryRankBonus_6", True, 1.25]],
    "territoryCards": [["TB_GainStanding", "GainStanding", 1, 102, False, 0, 0.05, 0.135, 0.0025],
                       ["TB_PurchaseHouse", "HousePurchase", 10, 1, True, 1, 1.0, 0.0, 0.0]],
    "rankTables": {"Territory_Standing": {"experience": [150, 375], "cards": [["TB_GainStanding", "", "", "TB_PurchaseHouse"],
                                                                              ["TB_GainStanding", "TB_Unknown", "", ""]]}},
}
ROWS = [{"ProgressionPointId": "TB_GainStanding", "TerritoryBonusCategory": "GainStanding", "MaxLevel": 102.0,
         "UpgradeCardSprite": "LyShineUI/Images/Territory/StandingRewards/GainStanding.dds", "UpgradeCardStat": "@stat"},
        {"ProgressionPointId": "TB_PurchaseHouse", "TerritoryBonusCategory": "HousePurchase", "MaxLevel": 1.0, "DoNotSpendPoint": "TRUE",
         "RequiredCategoricalProgressionLevel": 2.0}]


class TerritoryCardsTest(unittest.TestCase):
    def test_build(self):
        doc, problems = build(BACKEND, ROWS)
        self.assertEqual(doc["standings"], [{"territory": "6", "crc": 0x1DB87A14, "maxRank": 300, "rankTable": "Territory_Standing",
                                             "pool": "TerritoryRankBonus_6", "poolCrc": 0xCCDDF19B, "autoRankUp": True}])
        standing = doc["cards"][str(0x68C1F9AA)]
        self.assertEqual((standing["id"], standing["maxLevel"], standing["doNotSpend"], standing["requiredRank"]),
                         ("TB_GainStanding", 102, False, 0))
        self.assertEqual(standing["sprite"], "LyShineUI/Images/Territory/StandingRewards/GainStanding.dds")
        self.assertEqual(standing["stat"], "@stat")
        self.assertEqual(doc["rankCards"][0], [0x68C1F9AA, zlib.crc32(b"tb_purchasehouse")])   # the empty columns are dropped
        # The row of TB_PurchaseHouse has RequiredCategoricalProgressionLevel 2 (the backend copy: 1), and the rank row 1 names an unknown card.
        self.assertEqual(sorted(problems), ["TB_PurchaseHouse: a column differs from the backend copy",
                                            "the rank row 1 names TB_Unknown, not a territory card"])

    def test_missing(self):
        doc, problems = build({"territoryCards": [["TB_X", "GainXP", 2, 100, False, 0, 0.03, 0.1, 0.0025]]}, [])
        self.assertIn("TB_X: no row of ProgressionPointData", problems)
        self.assertIn("the backend copy has no standings (a file before format 3)", problems)
        self.assertIn("the rank table Territory_Standing has no cards (a file before QG1)", problems)
        self.assertEqual(doc["cards"][next(iter(doc["cards"]))]["sprite"], "")


if __name__ == "__main__":
    unittest.main()
