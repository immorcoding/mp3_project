#!/usr/bin/env python3
"""--set seek 与主机夹具的合同测试：python Tools/board_test_card/test_card_seek.py

刻度位置与集合关系不需要 lameenc；头部类型、字节确定性、与提交夹具逐字节一致的用例缺包时跳过。
"""

import os
import struct
import sys
import unittest

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import card_seek as cs  # noqa: E402

try:
    import lameenc  # noqa: F401
    HAVE_LAMEENC = True
except ImportError:
    HAVE_LAMEENC = False

FIXTURE_DIR = os.path.join(HERE, "..", "..", "Tests", "playback_service", "fixtures", "xing")
RATE = 44100


def peaks_by_second(pcm):
    """独立读回：每个 50 ms 窗口的峰值，返回 {窗口起点秒: 峰值}（只收非静音窗口）。"""
    samples = struct.unpack("<%dh" % (len(pcm) // 2), pcm)
    window = RATE // 20
    out = {}
    for w in range(len(samples) // 2 // window):
        peak = max(abs(samples[2 * i]) for i in range(w * window, (w + 1) * window))
        if peak:
            out[w / 20.0] = peak
    return out


class SeekTicks(unittest.TestCase):
    def test_beep_starts_literal(self):
        starts = cs.beeps()
        self.assertEqual(starts[:7], [10.0, 20.0, 30.0, 40.0, 50.0, 60.0, 60.25])
        self.assertEqual(starts.count(120.0) + starts.count(180.0), 2)
        self.assertIn(120.25, starts)
        self.assertIn(180.25, starts)
        self.assertEqual(len(starts), 20 + 3)  # 10..200 共 20 声，60/120/180 各多一声
        self.assertNotIn(0.0, starts)

    def test_pcm_is_silent_except_at_beeps(self):
        pcm = cs.tick_pcm()
        self.assertEqual(len(pcm), 210 * RATE * 4)
        loud = peaks_by_second(pcm)
        windows = sorted(loud)
        # 每声哔 100 ms = 2 个 50 ms 窗口；23 声 = 46 个窗口。
        self.assertEqual(len(windows), 46)
        expected = set()
        for start in cs.beeps():
            expected.update((start, start + 0.05))
        self.assertEqual(set(windows), expected)
        self.assertLessEqual(max(loud.values()), 12000)
        self.assertGreater(max(loud.values()), 11000)

    def test_four_samples_header_kinds_in_order(self):
        self.assertEqual([s.header for s in cs.SEEK_SAMPLES], [None, "Info", "Xing", None])
        self.assertEqual([s.bitrate for s in cs.SEEK_SAMPLES], [320, 320, None, None])
        self.assertEqual([s.with_toc for s in cs.SEEK_SAMPLES], [False, False, True, False])
        self.assertEqual(len({s.filename.lower() for s in cs.SEEK_SAMPLES}), 4)

    def test_duration_at_least_three_minutes(self):
        self.assertGreaterEqual(cs.DURATION_SECONDS, 180)

    def test_seek_set_registered_and_not_in_all(self):
        import make_card
        self.assertEqual(make_card.SETS["seek"], [s.filename for s in cs.SEEK_SAMPLES])
        self.assertFalse(set(make_card.SETS["seek"]) & set(make_card.SETS["all"]))
        self.assertFalse(set(make_card.SETS["host-fixtures"]) & set(make_card.SETS["all"]))


def first_frame_tag(data):
    """首帧里的头标识：Xing / Info / None（独立读回，按边信息区之后 36 B 处）。"""
    tag = data[36:40]
    return tag.decode("ascii") if tag in (b"Xing", b"Info") else None


@unittest.skipUnless(HAVE_LAMEENC, "整文件用例需要 lameenc（pip install lameenc）；缺包时跳过")
class SeekFiles(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        import make_card
        cls.make_card = make_card
        cls.data = make_card.make_seek()

    def test_files_in_set_order_with_expected_headers(self):
        self.assertEqual(list(self.data), self.make_card.SETS["seek"])
        for sample in cs.SEEK_SAMPLES:
            self.assertEqual(first_frame_tag(self.data[sample.filename]), sample.header, sample.filename)

    def test_toc_flag_only_on_vbr_xing(self):
        for sample in cs.SEEK_SAMPLES:
            data = self.data[sample.filename]
            if sample.header:
                flags = struct.unpack(">I", data[40:44])[0]
                self.assertEqual(bool(flags & 4), sample.with_toc, sample.filename)

    def test_cbr_files_are_320_vbr_files_vary(self):
        import card_xing
        for sample in cs.SEEK_SAMPLES:
            data = self.data[sample.filename]
            start = 417 if sample.header else 0
            rates = {[0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320][data[start + off + 2] >> 4]
                     for off, _ in card_xing.scan_frames(data[start:])}
            if sample.bitrate:
                self.assertEqual(rates, {sample.bitrate}, sample.filename)
            else:
                self.assertGreater(len(rates), 1, sample.filename)

    def test_two_generations_identical(self):
        again = self.make_card.make_seek()
        self.assertEqual(self.data, again)


HOST_FIXTURES = [
    # (文件名, 头部类型, 是否带 TOC)
    ("cbr_nohdr.mp3", None, False),
    ("cbr_info.mp3", "Info", False),
    ("vbr_toc.mp3", "Xing", True),
    ("vbr_nohdr.mp3", None, False),
]


class HostFixturesCommitted(unittest.TestCase):
    """提交进仓库的 Tests/playback_service/fixtures/xing/：只读文件，不需要 lameenc。"""

    def read(self, name):
        with open(os.path.join(FIXTURE_DIR, name), "rb") as handle:
            return handle.read()

    def test_four_files_match_spec(self):
        import card_xing
        for name, header, with_toc in HOST_FIXTURES:
            data = self.read(name)
            self.assertEqual(first_frame_tag(data), header, name)
            start = 417 if header else 0
            frames = card_xing.scan_frames(data[start:])
            self.assertEqual(data[start + 2] >> 2 & 3, 0, "44.1 kHz")
            seconds = len(frames) * 1152 / 44100.0
            self.assertAlmostEqual(seconds, 20.0, delta=0.1, msg=name)
            self.assertTrue(150000 <= len(data) <= 170000, "%s 约 160 KB：%d" % (name, len(data)))
            if header:
                flags, count, size = struct.unpack(">III", data[40:52])
                self.assertEqual((count, size), (len(frames), len(data) - 417), name)
                self.assertEqual(bool(flags & 4), with_toc, name)
            if name.startswith("cbr"):
                self.assertEqual({data[start + off + 2] >> 4 for off, _ in frames}, {5}, "CBR 64 kbps")

    def test_vbr_files_share_the_same_audio_and_cbr_files_too(self):
        self.assertEqual(self.read("cbr_info.mp3")[417:], self.read("cbr_nohdr.mp3"))
        self.assertEqual(self.read("vbr_toc.mp3")[417:], self.read("vbr_nohdr.mp3"))


@unittest.skipUnless(HAVE_LAMEENC, "重新生成需要 lameenc 1.9.0；缺包时跳过")
class HostFixturesReproducible(unittest.TestCase):
    def test_regeneration_is_byte_identical_to_committed_files(self):
        import make_card
        data = make_card.make_host_fixtures()
        self.assertEqual(list(data), make_card.SETS["host-fixtures"])
        for name in data:
            with open(os.path.join(FIXTURE_DIR, name), "rb") as handle:
                self.assertEqual(handle.read(), data[name], name)


if __name__ == "__main__":
    unittest.main()
