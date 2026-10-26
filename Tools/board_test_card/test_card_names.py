#!/usr/bin/env python3
"""文件名样本表的合同测试（不依赖 lameenc）：python Tools/board_test_card/test_card_names.py"""

import os
import sys
import unittest

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import card_names as cn  # noqa: E402

PATH_MAX_BYTES = 255  # Service/filesystem/filesystem_types.h: SERVICE_FILESYSTEM_PATH_MAX_BYTES
FAT_FORBIDDEN = set('\\/:*?"<>|') | {chr(c) for c in range(32)}


def width_px_12(text):
    """12 px 字体的保守宽度估计：CJK 12 px，其余 ASCII 按 6 px。"""
    return sum(12 if ord(ch) >= 0x2E80 else 6 for ch in text)


class CardNames(unittest.TestCase):
    def by_name(self, name):
        return [s for s in cn.NAME_SAMPLES if s.filename == name][0]

    def test_issue_102_examples(self):
        self.assertEqual(self.by_name("a.b.MP3").display, "a.b")
        self.assertEqual(self.by_name("a.mp3.mp3").display, "a.mp3")
        self.assertEqual(self.by_name("01 - 晴天.mp3").display, "01 - 晴天")

    def test_every_name_is_valid_on_fat_and_firmware(self):
        seen = set()
        for s in cn.NAME_SAMPLES:
            self.assertTrue(s.filename.startswith(s.display), s.filename)
            self.assertEqual(s.filename[len(s.display):].lower(), ".mp3", s.filename)
            self.assertFalse(FAT_FORBIDDEN & set(s.filename), s.filename)
            self.assertNotIn(s.filename[-1], " .")
            self.assertFalse(s.filename.startswith("."), s.filename)
            self.assertLessEqual(len(s.filename.encode("utf-16-le")) // 2, 255)
            self.assertLessEqual(len(s.filename.encode("utf-8")), PATH_MAX_BYTES, s.filename)
            self.assertNotIn(s.filename.lower(), seen)
            seen.add(s.filename.lower())
            s.filename.encode("gbk")  # 固件 _CODE_PAGE 936：必须能转成 OEM，否则不会入库

    def test_long_chinese_name_is_about_250_bytes(self):
        s = self.by_name(cn.LONG_NAME)
        stem_bytes = len(s.display.encode("utf-8"))
        self.assertGreaterEqual(stem_bytes, 245)
        self.assertLessEqual(stem_bytes, 251)
        self.assertLessEqual(len(s.filename.encode("utf-8")), PATH_MAX_BYTES)

    def test_missing_glyph_name_has_chars_outside_gb2312(self):
        s = self.by_name(cn.MISSING_GLYPH_NAME)
        outside = []
        for ch in s.display:
            try:
                ch.encode("gb2312")
            except UnicodeEncodeError:
                outside.append(ch)
        self.assertGreaterEqual(len(outside), 3)

    def test_scrolling_names_exceed_160px(self):
        scrolling = [s for s in cn.NAME_SAMPLES if s.scroll]
        self.assertGreaterEqual(len(scrolling), 3)
        for s in scrolling:
            self.assertGreater(width_px_12(s.display), 160, s.filename)

    def test_mixed_name_has_both_scripts(self):
        s = self.by_name(cn.MIXED_NAME)
        self.assertTrue(any(ord(c) < 128 and c.isalpha() for c in s.display))
        self.assertTrue(any(ord(c) > 0x4E00 for c in s.display))


if __name__ == "__main__":
    unittest.main()
