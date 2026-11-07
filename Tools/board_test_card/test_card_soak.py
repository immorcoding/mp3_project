#!/usr/bin/env python3
"""长跑样本（--set soak，#214）的合同测试：python Tools/board_test_card/test_card_soak.py

文件数、采样率与比特率、顺序、不进 all 只看样本表，不需要 lameenc；
读回 MPEG 帧头核对采样率与比特率、确定性要编码 3:30 音频，未安装 lameenc 时跳过。
"""

import os
import sys
import unittest

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import card_soak  # noqa: E402
import make_card  # noqa: E402

try:
    import lameenc  # noqa: F401
    HAVE_LAMEENC = True
except ImportError:
    HAVE_LAMEENC = False

# CHANGED（pre-push）设 MP3_SKIP_SLOW_TESTS=1 跳过慢用例；FULL 不设，全跑。
SKIP_SLOW = os.environ.get("MP3_SKIP_SLOW_TESTS") == "1"

# MPEG-1 Layer III 帧头（ISO 11172-3 表）：采样率索引 0/1/2 与比特率索引 14（320 kbps），独立于被测代码。
MPEG1_RATES = {0: 44100, 1: 48000, 2: 32000}
MPEG1_L3_KBPS = {9: 128, 10: 160, 11: 192, 12: 224, 13: 256, 14: 320}


def first_frame(data):
    """返回 (采样率, kbps)：第一个 MPEG-1 Layer III 帧头。"""
    for i in range(len(data) - 3):
        if data[i] == 0xFF and (data[i + 1] & 0xFE) == 0xFA:  # 同步 + MPEG-1 + Layer III
            return MPEG1_RATES[(data[i + 2] >> 2) & 3], MPEG1_L3_KBPS[data[i + 2] >> 4]
    raise AssertionError("没有帧头")


class SoakTable(unittest.TestCase):
    def test_three_tracks_at_32_441_48_khz_in_order(self):
        self.assertEqual([(s.filename, s.rate, s.bitrate) for s in card_soak.SOAK_SAMPLES], [
            ("soak-1-32k-320k.mp3", 32000, 320),
            ("soak-2-44k1-320k.mp3", 44100, 320),
            ("soak-3-48k-320k.mp3", 48000, 320),
        ])

    def test_each_track_is_long_enough_to_loop_unattended(self):
        self.assertGreaterEqual(card_soak.DURATION_SECONDS, 180)

    def test_names_are_fat_valid_and_short(self):
        for sample in card_soak.SOAK_SAMPLES:
            self.assertTrue(sample.filename.endswith(".mp3"))
            self.assertLessEqual(len(sample.filename), 32)
            self.assertFalse(set('\\/:*?"<>|') & set(sample.filename))


class SoakSet(unittest.TestCase):
    def test_set_lists_the_three_files_in_copy_order(self):
        self.assertEqual(make_card.SETS["soak"], [s.filename for s in card_soak.SOAK_SAMPLES])
        self.assertIn("soak", make_card.SET_CHOICES)

    def test_not_in_all_and_not_in_other_sets(self):
        self.assertEqual(len(make_card.SETS["all"]), 8)
        soak = set(make_card.SETS["soak"])
        for key, names in make_card.SETS.items():
            if key != "soak":
                self.assertFalse(soak & set(names), key)

    def test_report_prints_table_and_copy_order(self):
        data = {n: b"x" * 7 for n in make_card.SETS["soak"]}
        text = "\n".join(make_card.render_report(data, "soak"))
        for name in make_card.SETS["soak"]:
            self.assertIn(name, text)
        self.assertIn("复制顺序：" + " -> ".join(make_card.SETS["soak"]), text)

    def test_overview_mentions_soak(self):
        data = {n: b"x" for names in make_card.SETS.values() for n in names}
        text = "\n".join(make_card.render_report(data))
        self.assertIn("长跑样本", text)
        self.assertIn("--set soak", text)


@unittest.skipIf(SKIP_SLOW, "慢用例（约 25 s），MP3_SKIP_SLOW_TESTS=1 时跳过，FULL 全跑")
@unittest.skipUnless(HAVE_LAMEENC, "需要 lameenc")
class SoakAudio(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.data = make_card.make_soak()

    def test_only_the_three_files(self):
        self.assertEqual(list(self.data), make_card.SETS["soak"])

    def test_frame_headers_match_table(self):
        for sample in card_soak.SOAK_SAMPLES:
            self.assertEqual(first_frame(self.data[sample.filename]), (sample.rate, sample.bitrate), sample.filename)

    def test_size_matches_320_kbps_cbr_for_the_duration(self):
        expected = 320000 // 8 * card_soak.DURATION_SECONDS
        for sample in card_soak.SOAK_SAMPLES:
            self.assertAlmostEqual(len(self.data[sample.filename]) / expected, 1.0, delta=0.01, msg=sample.filename)

    def test_deterministic(self):
        again = make_card.make_soak()
        self.assertEqual(again, self.data)


if __name__ == "__main__":
    unittest.main()
