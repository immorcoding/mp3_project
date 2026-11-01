#!/usr/bin/env python3
"""自写 Xing/Info 头与 TOC 的合同测试：python Tools/board_test_card/test_card_xing.py

头字段与 TOC 的用例用手算字面量，不需要 lameenc；整文件用例（帧数与实扫一致、逐字节可复现）缺包时跳过。
"""

import os
import struct
import sys
import unittest

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import card_xing as cx  # noqa: E402

try:
    import lameenc  # noqa: F401
    HAVE_LAMEENC = True
except ImportError:
    HAVE_LAMEENC = False

# 44.1 kHz 立体声 MPEG-1 L3 无 CRC 无填充的帧头：比特率索引 9=128 kbps（417 B）、14=320 kbps（1044 B）。
HEADER_128 = bytes([0xFF, 0xFB, 0x90, 0x00])
HEADER_320 = bytes([0xFF, 0xFB, 0xE0, 0x00])


def synthetic_stream(count_128, count_320):
    """手工拼的码流：先 count_128 个 417 B 帧，再 count_320 个 1044 B 帧；净荷为 0。"""
    return (HEADER_128 + bytes(417 - 4)) * count_128 + (HEADER_320 + bytes(1044 - 4)) * count_320


def walk(stream):
    """独立的帧遍历（直接按位算帧长），返回 [(偏移, 长度)]。"""
    out, pos = [], 0
    kbps = [0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320]
    while pos < len(stream):
        assert stream[pos] == 0xFF and stream[pos + 1] & 0xE0 == 0xE0, pos
        size = 144 * kbps[stream[pos + 2] >> 4] * 1000 // 44100 + ((stream[pos + 2] >> 1) & 1)
        out.append((pos, size))
        pos += size
    assert pos == len(stream)
    return out


class XingHeader(unittest.TestCase):
    def setUp(self):
        self.audio = synthetic_stream(50, 50)
        self.assertEqual(len(self.audio), 73050)

    def test_scan_reports_frames_and_bytes(self):
        frames = cx.scan_frames(self.audio)
        self.assertEqual(len(frames), 100)
        self.assertEqual(sum(size for _, size in frames), 73050)
        self.assertEqual(frames[50], (50 * 417, 1044))

    def test_vbr_xing_with_toc_fields(self):
        data = cx.add_header(self.audio, "Xing", with_toc=True)
        header_frame, rest = data[:417], data[417:]
        self.assertEqual(rest, self.audio)
        self.assertEqual(header_frame[:4], HEADER_128)
        self.assertEqual(header_frame[4:36], bytes(32), "边信息区全零")
        self.assertEqual(header_frame[36:40], b"Xing")
        self.assertEqual(struct.unpack(">III", header_frame[40:52]), (0x07, 100, 73050))
        toc = header_frame[52:152]
        # 手算：第 i 项取第 floor(i*100/100) 帧的偏移，× 256 / 73050 取整。
        self.assertEqual((toc[0], toc[1], toc[25], toc[50], toc[75], toc[99]), (0, 1, 36, 73, 164, 252))
        self.assertEqual(header_frame[152:], bytes(417 - 152))

    def test_toc_is_monotonic_and_bounded(self):
        toc = cx.toc_from_sizes([size for _, size in cx.scan_frames(self.audio)])
        self.assertEqual(len(toc), 100)
        self.assertEqual(list(toc), sorted(toc))
        self.assertLessEqual(max(toc), 255)

    def test_toc_with_fewer_frames_than_entries(self):
        toc = cx.toc_from_sizes([417] * 10)  # 第 i 项取第 floor(i/10) 帧：偏移 417*k，总长 4170
        self.assertEqual((toc[0], toc[9], toc[10], toc[55], toc[99]), (0, 0, 25, 128, 230))

    def test_cbr_info_without_toc(self):
        data = cx.add_header(self.audio, "Info", with_toc=False)
        self.assertEqual(data[36:40], b"Info")
        self.assertEqual(struct.unpack(">III", data[40:52]), (0x03, 100, 73050))
        self.assertEqual(data[52:417], bytes(417 - 52))

    def test_info_with_toc_flag(self):
        data = cx.add_header(self.audio, "Info", with_toc=True)
        self.assertEqual(struct.unpack(">I", data[44:48])[0], 100)
        self.assertEqual(struct.unpack(">I", data[40:44])[0], 0x07)

    def test_result_is_frame_aligned_and_frame_count_matches_scan(self):
        data = cx.add_header(self.audio, "Xing", with_toc=True)
        frames = walk(data)
        self.assertEqual(len(frames), 101)
        self.assertEqual(frames[1][0], 417)
        self.assertEqual(struct.unpack(">I", data[44:48])[0], len(frames) - 1)

    def test_deterministic(self):
        self.assertEqual(cx.add_header(self.audio, "Xing", True), cx.add_header(self.audio, "Xing", True))

    def test_rejects_bad_input(self):
        with self.assertRaises(AssertionError):
            cx.scan_frames(self.audio[:-1])
        with self.assertRaises(ValueError):
            cx.add_header(self.audio, "VBRI", True)


@unittest.skipUnless(HAVE_LAMEENC, "整文件用例需要 lameenc（pip install lameenc）；缺包时跳过")
class XingOnRealEncoding(unittest.TestCase):
    def test_frame_count_matches_independent_walk_and_bytes_reproducible(self):
        import make_card
        pcm = make_card.tile_pcm(44100, 2, 4)
        stream = make_card.gf.encode(pcm, 44100, 2, bitrate=64)
        first = cx.add_header(stream, "Xing", True)
        second = cx.add_header(make_card.gf.encode(pcm, 44100, 2, bitrate=64), "Xing", True)
        self.assertEqual(first, second)
        frames = walk(first)
        self.assertEqual(struct.unpack(">I", first[44:48])[0], len(frames) - 1)
        self.assertEqual(struct.unpack(">I", first[48:52])[0], len(first) - frames[1][0])


if __name__ == "__main__":
    unittest.main()
