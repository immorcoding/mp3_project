#!/usr/bin/env python3
"""ID3 标签样本表的合同测试（不依赖 lameenc）：python Tools/board_test_card/test_card_tags.py"""

import os
import struct
import sys
import unittest

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import card_tags as ct  # noqa: E402

FIRST_BLOCK = 4096  # #132：首块 4 KiB


def parse(tag):
    """独立的最小解析：返回 (版本, [(帧ID, 帧头偏移, 负载)])；无 ID3 头返回 None。"""
    if tag[:3] != b"ID3":
        return None
    version = tag[3]
    size = sum((tag[6 + i] & 0x7F) << (7 * (3 - i)) for i in range(4))
    assert len(tag) == 10 + size, "标签总长与头不符"
    frames, pos = [], 10
    while pos + 10 <= len(tag) and tag[pos] != 0:
        fid = tag[pos:pos + 4].decode("ascii")
        raw = tag[pos + 4:pos + 8]
        if version == 4:
            n = sum((raw[i] & 0x7F) << (7 * (3 - i)) for i in range(4))
        else:
            n = struct.unpack(">I", raw)[0]
        frames.append((fid, pos, tag[pos + 10:pos + 10 + n]))
        pos += 10 + n
    return version, frames


class CardTags(unittest.TestCase):
    def sample(self, key):
        return [s for s in ct.TAG_SAMPLES if s.key == key][0]

    def frames(self, key):
        return parse(self.sample(key).tag)

    def test_six_kinds_in_order(self):
        self.assertEqual([s.key for s in ct.TAG_SAMPLES],
                         ["v23-utf16le", "v24-utf8", "latin1-gbk", "apic-first", "blank", "notag"])

    def test_filenames_are_distinct_ascii_mp3(self):
        names = [s.filename for s in ct.TAG_SAMPLES]
        self.assertEqual(len(set(n.lower() for n in names)), 6)
        for n in names:
            n.encode("ascii")
            self.assertTrue(n.endswith(".mp3"))

    def test_v23_utf16le_has_bom_and_le_text(self):
        version, frames = self.frames("v23-utf16le")
        self.assertEqual(version, 3)
        by = {f[0]: f[2] for f in frames}
        self.assertEqual(by["TIT2"], b"\x01\xff\xfe" + "Tag Sample UTF16LE".encode("utf-16-le"))
        self.assertEqual(by["TPE1"][:3], b"\x01\xff\xfe")
        self.assertEqual(by["TPE1"][3:].decode("utf-16-le"), "Test Artist A")

    def test_v24_utf8_uses_syncsafe_frame_sizes_and_encoding_3(self):
        version, frames = self.frames("v24-utf8")
        self.assertEqual(version, 4)
        by = {f[0]: f[2] for f in frames}
        self.assertEqual(by["TIT2"], b"\x03Tag Sample UTF8")
        self.assertEqual(by["TPE1"], b"\x03Test Artist B")

    def test_latin1_gbk_declares_encoding_0_with_gbk_bytes(self):
        version, frames = self.frames("latin1-gbk")
        by = {f[0]: f[2] for f in frames}
        self.assertEqual(by["TIT2"][0], 0)
        self.assertEqual(by["TIT2"][1:], "测试标题 GBK".encode("gbk"))
        self.assertTrue(any(b >= 0x80 for b in by["TIT2"][1:]))
        self.assertEqual(by["TPE1"][0], 0)
        self.assertEqual(by["TPE1"][1:], "测试歌手".encode("gbk"))

    def test_apic_precedes_tit2_and_pushes_it_past_first_block(self):
        version, frames = self.frames("apic-first")
        ids = [f[0] for f in frames]
        self.assertLess(ids.index("APIC"), ids.index("TIT2"))
        tit2_at = [f[1] for f in frames if f[0] == "TIT2"][0]
        self.assertGreater(tit2_at, FIRST_BLOCK)
        by = {f[0]: f[2] for f in frames}
        self.assertEqual(by["TIT2"][3:].decode("utf-16-le"), "Tag Sample APIC First")
        self.assertNotIn(0xFF, by["APIC"])  # 避免伪同步

    def test_blank_has_no_usable_title(self):
        version, frames = self.frames("blank")
        by = {f[0]: f[2] for f in frames}
        self.assertNotIn("TPE1", by)
        self.assertEqual(by["TIT2"][1:].decode("latin-1").strip(" \x00"), "")

    def test_notag_has_no_id3_header(self):
        s = self.sample("notag")
        self.assertEqual(s.tag, b"")
        self.assertIsNone(parse(s.tag))

    def test_tagged_samples_have_valid_id3_headers(self):
        for s in ct.TAG_SAMPLES:
            if s.key != "notag":
                self.assertEqual(s.tag[:3], b"ID3", s.key)
                self.assertIsNotNone(parse(s.tag))

    def test_expectations_filled_and_gbk_noted(self):
        for s in ct.TAG_SAMPLES:
            self.assertTrue(s.dial and s.queue_title is not None and s.source and s.reads, s.key)
        self.assertIn("占位框", self.sample("latin1-gbk").note)
        self.assertEqual(self.sample("apic-first").reads, "2")

    def test_generation_is_deterministic(self):
        self.assertEqual([s.tag for s in ct.build_samples()], [s.tag for s in ct.build_samples()])

    def test_tags_set_not_in_all(self):
        import make_card
        self.assertIn("tags", make_card.SETS)
        self.assertEqual(make_card.SETS["tags"], [s.filename for s in ct.TAG_SAMPLES])
        self.assertFalse(set(make_card.SETS["tags"]) & set(make_card.SETS["all"]))


if __name__ == "__main__":
    unittest.main()
