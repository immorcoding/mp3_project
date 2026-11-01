#!/usr/bin/env python3
"""大目录卡（--set bigdir，#180）的合同测试：python Tools/board_test_card/test_card_bigdir.py

文件数、文件名长度与前缀顺序、探针位置只看文件名清单与注入的输出，不写盘；
探针曲时长用到 lameenc，未安装时跳过那一条。
"""

import os
import sys
import unittest

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import card_bigdir as cb  # noqa: E402
import make_card  # noqa: E402

LIBRARY_PREFIX = "Music/"
PATH_MAX_BYTES = 255
FAT_FORBIDDEN = set('\/:*?"<>|') | {chr(c) for c in range(32)}


class BigDirNames(unittest.TestCase):
    def setUp(self):
        self.names = cb.file_names()

    def test_count_and_uniqueness(self):
        self.assertEqual(len(self.names), 12000)
        self.assertEqual(len(set(n.lower() for n in self.names)), 12000)

    def test_names_are_fifty_chars_and_fat_valid(self):
        for name in (self.names[0], self.names[2999], self.names[-1]):
            self.assertEqual(len(name), 50, name)
        for name in self.names:
            self.assertEqual(len(name), 50, name)
            self.assertTrue(name.endswith(".mp3"), name)
            self.assertFalse(FAT_FORBIDDEN & set(name), name)
            self.assertLessEqual(len((LIBRARY_PREFIX + name).encode("utf-8")), PATH_MAX_BYTES)

    def test_zero_padded_prefix_makes_directory_order_equal_library_order(self):
        self.assertTrue(self.names[0].startswith("00001-"), self.names[0])
        self.assertTrue(self.names[2999].startswith("03000-"), self.names[2999])
        self.assertTrue(self.names[-1].startswith("12000-"), self.names[-1])
        self.assertEqual(self.names, sorted(self.names))
        self.assertEqual(self.names, sorted(self.names, key=str.lower))
        self.assertEqual([int(n[:5]) for n in self.names], list(range(1, 12001)))

    def test_fat_directory_entries_per_file_is_about_five(self):
        # 长文件名每 13 个 UTF-16 字符占一个目录项，外加 1 个短名项。
        entries = -(-50 // 13) + 1
        self.assertEqual(entries, 5)

    def test_probe_positions(self):
        self.assertEqual(cb.PROBE_INDEXES, (1, 3000, 6000, 9000, 12000))
        self.assertEqual([i for i in range(1, 12001) if cb.is_probe(i)], [1, 3000, 6000, 9000, 12000])


class BigDirWrite(unittest.TestCase):
    def test_write_emits_in_order_with_probes_at_positions(self):
        out = []
        cb.write_set(lambda name, data: out.append((name, data)), b"small", b"PROBE", count=12, probe_indexes=(1, 5, 12))
        self.assertEqual([n for n, _ in out], cb.file_names(12))
        self.assertEqual([i + 1 for i, (_, d) in enumerate(out) if d == b"PROBE"], [1, 5, 12])
        self.assertEqual(sum(1 for _, d in out if d == b"small"), 9)

    def test_full_run_writes_12000_files_with_five_probes(self):
        count = {"probe": 0, "small": 0}

        def sink(name, data):
            count["probe" if data == b"P" else "small"] += 1

        cb.write_set(sink, b"s", b"P")
        self.assertEqual(count, {"probe": 5, "small": 11995})

    def test_report_lists_probes_and_requirements(self):
        text = "\n".join(cb.report_lines())
        for index in cb.PROBE_INDEXES:
            self.assertIn(cb.file_names()[index - 1], text)
        self.assertIn("12000", text)


class BigDirIntegration(unittest.TestCase):
    def test_not_in_all_and_selectable(self):
        self.assertNotIn("bigdir", make_card.SETS)
        self.assertIn("bigdir", make_card.SET_CHOICES)
        self.assertEqual(len(make_card.SETS["all"]), 8)

    def test_report_for_bigdir_set(self):
        text = "\n".join(make_card.render_report({}, "bigdir"))
        self.assertIn(cb.file_names()[2999], text)
        self.assertIn("复制顺序", text)

    def test_overview_mentions_bigdir(self):
        text = "\n".join(make_card.render_report({n: b"" for names in make_card.SETS.values() for n in names}))
        self.assertIn("--set bigdir", text)

    def test_probe_is_at_least_three_minutes_cbr(self):
        try:
            import lameenc  # noqa: F401
        except ImportError:
            self.skipTest("需要 lameenc")
        small, probe = make_card.make_bigdir_audio()
        self.assertGreaterEqual(len(probe) * 8 / (make_card.BIGDIR_PROBE_KBPS * 1000), 180)
        self.assertLess(len(small), 40000)


if __name__ == "__main__":
    unittest.main()
