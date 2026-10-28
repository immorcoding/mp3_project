#!/usr/bin/env python3
"""曲目元数据字形样本表的合同测试（不依赖 lameenc）：python Tools/board_test_card/test_card_glyphs.py"""

import os
import sys
import unittest

try:
    import lameenc  # noqa: F401  整文件生成需要 MP3 编码器
    HAVE_LAMEENC = True
except ImportError:
    HAVE_LAMEENC = False

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import card_glyphs as cg  # noqa: E402
from test_card_tags import parse  # noqa: E402

NAME_CAPACITY = 252  # #135 G：标题与歌手共用，含结尾 NUL
EXT_CODEPOINTS = "®é–™’“”…"


def text(payload):
    """按编码字节解出帧文本，返回 NUL 分隔的各项。"""
    enc, body = payload[0], payload[1:]
    if enc == 1:
        out = []
        for part in body.split(b"\x00\x00"):
            assert part[:2] == b"\xff\xfe"
            out.append(part[2:].decode("utf-16-le"))
        return out
    if enc == 3:
        return body.decode("utf-8").split("\x00")
    raise AssertionError("unexpected encoding %d" % enc)


class CardGlyphs(unittest.TestCase):
    def sample(self, key):
        return [s for s in cg.GLYPH_SAMPLES if s.key == key][0]

    def frames(self, key):
        version, frames = parse(self.sample(key).tag)
        return version, {f[0]: f[2] for f in frames}

    def test_four_kinds_in_order(self):
        self.assertEqual([s.key for s in cg.GLYPH_SAMPLES], ["cjk", "latin-ext", "multi-artist", "long-artist"])

    def test_filenames_distinct_ascii_and_not_clashing_with_tags(self):
        import card_tags
        names = [s.filename for s in cg.GLYPH_SAMPLES]
        self.assertEqual(len(set(n.lower() for n in names)), 4)
        for n in names:
            n.encode("ascii")
            self.assertTrue(n.endswith(".mp3"))
        self.assertFalse({n.lower() for n in names} & {s.filename.lower() for s in card_tags.TAG_SAMPLES})

    def test_cjk_is_v24_utf8_with_chinese_title_and_artist(self):
        version, by = self.frames("cjk")
        self.assertEqual(version, 4)
        self.assertEqual(by["TIT2"][0], 3)
        self.assertEqual(by["TPE1"][0], 3)
        title, artist = text(by["TIT2"]), text(by["TPE1"])
        self.assertEqual(len(title), 1)
        self.assertEqual(len(artist), 1)
        self.assertTrue(all(0x4E00 <= ord(c) <= 0x9FFF for c in artist[0]))
        self.assertTrue(any(0x4E00 <= ord(c) <= 0x9FFF for c in title[0]))
        self.assertEqual(self.sample("cjk").dial, title[0])
        self.assertEqual(self.sample("cjk").queue_artist, artist[0])

    def test_cjk_chars_are_in_gb2312(self):
        s = self.sample("cjk")
        for c in s.dial + s.queue_artist:
            if ord(c) > 0x7F:
                c.encode("gb2312")

    def test_latin_ext_title_and_artist_carry_every_codepoint(self):
        version, by = self.frames("latin-ext")
        self.assertEqual(version, 3)
        self.assertEqual(by["TIT2"][0], 1)
        self.assertEqual(by["TPE1"][0], 1)
        title, artist = text(by["TIT2"])[0], text(by["TPE1"])[0]
        for c in EXT_CODEPOINTS:
            self.assertIn(c, title, "title missing U+%04X" % ord(c))
            self.assertIn(c, artist, "artist missing U+%04X" % ord(c))
        self.assertEqual((self.sample("latin-ext").dial, self.sample("latin-ext").queue_artist), (title, artist))

    def test_multi_artist_is_v24_nul_separated_and_joined_with_slash(self):
        version, by = self.frames("multi-artist")
        self.assertEqual(version, 4)
        self.assertEqual(by["TPE1"][0], 3)
        items = text(by["TPE1"])
        self.assertGreaterEqual(len(items), 3)
        cleaned = [i.strip(" 　") for i in items if i.strip(" 　")]
        self.assertTrue(any(i != i.strip(" 　") for i in items), "needs an item with edge whitespace")
        self.assertIn("", items, "needs an empty item")
        self.assertIn("AC/DC", cleaned)  # 不按 / 拆分
        self.assertEqual(self.sample("multi-artist").queue_artist, " / ".join(cleaned))

    def test_long_artist_exceeds_capacity_and_cuts_on_char_boundary(self):
        version, by = self.frames("long-artist")
        self.assertEqual(version, 4)
        full = text(by["TPE1"])[0]
        self.assertGreater(len(full.encode("utf-8")), NAME_CAPACITY - 1)
        expected = self.sample("long-artist").queue_artist
        self.assertTrue(full.startswith(expected))
        self.assertLessEqual(len(expected.encode("utf-8")), NAME_CAPACITY - 1)
        # 下一个字符放不下：截断点落在多字节字符内部，且不留半个字符
        nxt = full[len(expected)]
        self.assertGreater(ord(nxt), 0x7F)
        self.assertGreater(len((expected + nxt).encode("utf-8")), NAME_CAPACITY - 1)
        self.assertFalse(expected.endswith("…"))
        # 远长于 Queue 与唱盘页可见宽度（12 px 字体，屏宽 240 px 以内）
        self.assertGreater(len(expected), 100)

    def test_expectations_filled(self):
        for s in cg.GLYPH_SAMPLES:
            self.assertTrue(s.dial and s.queue_title and s.source and s.reads, s.key)
            self.assertEqual(s.source, "标题", s.key)

    def test_generation_is_deterministic(self):
        self.assertEqual([s.tag for s in cg.build_samples()], [s.tag for s in cg.build_samples()])

    def test_glyphs_set_not_in_all(self):
        import make_card
        self.assertIn("glyphs", make_card.SETS)
        self.assertEqual(make_card.SETS["glyphs"], [s.filename for s in cg.GLYPH_SAMPLES])
        self.assertFalse(set(make_card.SETS["glyphs"]) & set(make_card.SETS["all"]))


@unittest.skipUnless(HAVE_LAMEENC, "整文件确定性需要 lameenc（pip install lameenc）；缺包时跳过")
class CardGlyphsFiles(unittest.TestCase):
    def test_glyphs_set_files_are_byte_identical_across_two_generations(self):
        import make_card
        first = make_card.make_all()
        second = make_card.make_all()
        for name in make_card.SETS["glyphs"]:
            self.assertIn(name, first, name)
            self.assertIn(name, second, name)
            self.assertGreater(len(first[name]), 0, name)
            self.assertEqual(first[name], second[name], "%s differs between two generations" % name)


if __name__ == "__main__":
    unittest.main()
