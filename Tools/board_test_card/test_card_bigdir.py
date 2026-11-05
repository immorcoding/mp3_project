#!/usr/bin/env python3
"""大目录卡（--set bigdir，#180）的合同测试：python Tools/board_test_card/test_card_bigdir.py

文件数、文件名长度与前缀顺序、探针位置只看文件名清单与注入的输出，不写盘；
探针曲时长用到 lameenc，未安装时跳过那一条。
"""

import os
import sys
import tempfile
import unittest

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import card_bigdir as cb  # noqa: E402
import make_card  # noqa: E402

LIBRARY_PREFIX = "Music/"
PATH_MAX_BYTES = 255
FAT_FORBIDDEN = set('\\/:*?"<>|') | {chr(c) for c in range(32)}


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


class BigDirTailSubdir(unittest.TestCase):
    """库容量上限 12000（STOR-17），曲库深度优先、子目录排在当前目录之后：
    末尾子目录里的曲目要进库，根目录文件数就得让出位置，合计恰为 12000。"""

    def setUp(self):
        self.paths = cb.relative_paths()
        self.root = [p for p in self.paths if "/" not in p]
        self.tail = [p for p in self.paths if "/" in p]

    def test_library_total_is_the_catalog_cap(self):
        self.assertEqual(len(self.paths), 12000)
        self.assertEqual(len(self.root), 11998)
        self.assertEqual(len(self.tail), 2)

    def test_tail_files_live_in_one_subdirectory_with_a_fat_valid_name(self):
        dirs = {p.split("/")[0] for p in self.tail}
        self.assertEqual(dirs, {cb.TAIL_DIR})
        self.assertFalse(cb.TAIL_DIR.startswith("."))
        self.assertFalse(FAT_FORBIDDEN & set(cb.TAIL_DIR))

    def test_subdirectory_name_sorts_after_every_root_file(self):
        self.assertGreater(cb.TAIL_DIR.lower(), max(self.root).lower())

    def test_paths_fit_the_path_limit_and_numbering_continues_across_the_split(self):
        for path in self.paths:
            self.assertLessEqual(len((LIBRARY_PREFIX + path).encode("utf-8")), PATH_MAX_BYTES)
        self.assertEqual([int(p.split("/")[-1][:5]) for p in self.paths], list(range(1, 12001)))
        self.assertTrue(self.tail[0].split("/")[1].startswith("11999-"))
        self.assertTrue(self.tail[1].split("/")[1].startswith("12000-"))

    def test_last_library_track_is_a_probe_inside_the_subdirectory(self):
        self.assertEqual(self.paths[-1], self.tail[-1])
        self.assertTrue(cb.is_probe(12000))


class BigDirWrite(unittest.TestCase):
    def test_write_emits_in_order_with_probes_at_positions(self):
        out = []
        cb.write_set(lambda path, data: out.append((path, data)), b"small", b"PROBE", count=12, probe_indexes=(1, 5, 12))
        self.assertEqual([p for p, _ in out], cb.relative_paths(12))
        self.assertEqual([i + 1 for i, (_, d) in enumerate(out) if d == b"PROBE"], [1, 5, 12])
        self.assertEqual(sum(1 for _, d in out if d == b"small"), 9)

    def test_subdirectory_files_come_after_every_root_file(self):
        out = []
        cb.write_set(lambda path, data: out.append(path), b"s", b"P")
        first_tail = next(i for i, p in enumerate(out) if "/" in p)
        self.assertEqual(first_tail, 11998)
        self.assertTrue(all("/" in p for p in out[first_tail:]))

    def test_full_run_writes_12000_files_with_five_probes(self):
        count = {"probe": 0, "small": 0}

        def sink(name, data):
            count["probe" if data == b"P" else "small"] += 1

        cb.write_set(sink, b"s", b"P")
        self.assertEqual(count, {"probe": 5, "small": 11995})

    def test_report_lists_probes_and_requirements(self):
        text = "\n".join(cb.report_lines())
        for index in cb.PROBE_INDEXES:
            self.assertIn(cb.relative_paths()[index - 1], text)
        self.assertIn("12000", text)

    def test_report_explains_the_tail_subdirectory(self):
        text = "\n".join(cb.report_lines())
        self.assertIn(cb.TAIL_DIR, text)
        self.assertIn("11998", text)


class BigDirOnDisk(unittest.TestCase):
    def test_write_bigdir_creates_the_subdirectory_with_the_tail_files(self):
        with tempfile.TemporaryDirectory() as out:
            make_card.write_bigdir(out, audio=(b"s", b"P"))
            top = os.listdir(out)
            self.assertEqual(len(top), 11998 + 1)
            self.assertIn(cb.TAIL_DIR, top)
            self.assertEqual(sorted(os.listdir(os.path.join(out, cb.TAIL_DIR))), [p.split("/")[1] for p in cb.relative_paths()[-2:]])
            with open(os.path.join(out, cb.TAIL_DIR, cb.relative_paths()[-1].split("/")[1]), "rb") as handle:
                self.assertEqual(handle.read(), b"P")

    def test_copy_in_order_creates_the_subdirectory_after_the_root_files(self):
        paths = ["a.mp3", "b.mp3", cb.TAIL_DIR + "/c.mp3"]
        with tempfile.TemporaryDirectory() as src, tempfile.TemporaryDirectory() as dst:
            os.makedirs(os.path.join(src, cb.TAIL_DIR))
            for path in paths:
                with open(os.path.join(src, path), "wb") as handle:
                    handle.write(path.encode())
            order = []
            real_copy = make_card.shutil.copyfile
            make_card.shutil.copyfile = lambda a, b: (order.append(os.path.relpath(b, dst).replace(os.sep, "/")), real_copy(a, b))
            try:
                make_card.copy_in_order(src, dst, paths)
            finally:
                make_card.shutil.copyfile = real_copy
            self.assertEqual(order, paths)
            with open(os.path.join(dst, cb.TAIL_DIR, "c.mp3"), "rb") as handle:
                self.assertEqual(handle.read(), (cb.TAIL_DIR + "/c.mp3").encode())


class BigDirIntegration(unittest.TestCase):
    def test_not_in_all_and_selectable(self):
        self.assertNotIn("bigdir", make_card.SETS)
        self.assertIn("bigdir", make_card.SET_CHOICES)
        self.assertEqual(len(make_card.SETS["all"]), 8)

    def test_report_for_bigdir_set(self):
        text = "\n".join(make_card.render_report({}, "bigdir"))
        self.assertIn(cb.file_names()[2999], text)
        self.assertIn(cb.TAIL_DIR, text)
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
