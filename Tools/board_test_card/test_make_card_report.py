#!/usr/bin/env python3
"""make_card.py 的报告输出合同：--set X 只打印该子集的样本表与复制顺序，不带 --set 时列出全部。

用假数据（每个文件名对应一小段字节）调用 render_report，不需要 lameenc，也不写盘。
"""

import os
import sys
import unittest

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_card  # noqa: E402

DATA = {name: name.encode("utf-8") for names in make_card.SETS.values() for name in names}
SET_MARKERS = {
    "names": "曲目显示名样本",
    "tags": "ID3 标签样本",
    "glyphs": "曲目元数据字形样本",
}


def report(set_key=None):
    return "\n".join(make_card.render_report(DATA, set_key))


class RenderReport(unittest.TestCase):
    def test_without_set_lists_everything(self):
        text = report()
        for marker in SET_MARKERS.values():
            self.assertIn(marker, text)
        self.assertIn("子集（均按上面的相对顺序复制）", text)
        for key in make_card.SETS:
            self.assertIn("--set %-16s" % key, text)
        for name, _ in make_card.FILES:
            self.assertIn(name, text)

    def test_sample_sets_print_only_their_own_table(self):
        for key, marker in SET_MARKERS.items():
            text = report(key)
            self.assertIn(marker, text, key)
            for other_key, other_marker in SET_MARKERS.items():
                if other_key != key:
                    self.assertNotIn(other_marker, text, "%s 报告不应含 %s 的样本表" % (key, other_key))
            self.assertNotIn("子集（均按上面的相对顺序复制）", text, key)
            self.assertNotIn("card-01-32k-320k.mp3", text, key)
            for name in make_card.SETS[key]:
                self.assertIn(name, text, "%s 缺少 %s" % (key, name))

    def test_file_subsets_print_only_their_files(self):
        for key in ("all", "first-bad", "all-unsupported"):
            text = report(key)
            self.assertNotIn("子集（均按上面的相对顺序复制）", text, key)
            for marker in SET_MARKERS.values():
                self.assertNotIn(marker, text, key)
            in_subset = set(make_card.SETS[key])
            for name, _ in make_card.FILES:
                self.assertEqual(name in text, name in in_subset, "%s 中 %s 的出现应与子集成员一致" % (key, name))

    def test_copy_order_follows_set_order(self):
        for key in make_card.SETS:
            text = report(key)
            order_line = [line for line in text.split("\n") if "复制顺序：" in line]
            self.assertEqual(len(order_line), 1, key)
            self.assertEqual(order_line[0].split("复制顺序：", 1)[1], " -> ".join(make_card.SETS[key]), key)

    def test_notice_kept(self):
        for key in (None, "first-bad", "glyphs"):
            self.assertIn("不要整夹拖拽", report(key), str(key))


if __name__ == "__main__":
    unittest.main()
