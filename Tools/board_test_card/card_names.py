"""板测卡的曲目显示名文件名样本（#117；规则见 #102 Resolution，板测行 #112 T7/T8）。

只定义名字与期望显示名；音频内容由 make_card.py 复用同一段可播样本。
与其他样本一样复制进卡的 Music/ 目录；曲库相对路径是 Music/<文件名>，整串须 ≤ SERVICE_FILESYSTEM_PATH_MAX_BYTES（255 B）。
所有字符都在固件 _CODE_PAGE 936（GBK）内，否则 FatFs 无法转换、文件不会入库。
"""

from collections import namedtuple

NameSample = namedtuple("NameSample", "filename display scroll note")

# 中文长名：81 个汉字（3 B）+ 2 个 ASCII = 245 B 主名，+ ".mp3" = 249 B 文件名，+ "Music/" = 255 B 整串路径（恰为上限）。
_CJK_CYCLE = "春夏秋冬风花雪月山水云天"
_LONG_STEM = "LN" + "".join(_CJK_CYCLE[i % len(_CJK_CYCLE)] for i in range(81))

LONG_NAME = _LONG_STEM + ".mp3"
MIXED_NAME = "Hello 世界 Mix 混排 Song.mp3"
MISSING_GLYPH_NAME = "繁體 龍 喆 镕.mp3"  # 龍／喆／镕 在 GBK 内、GB2312 外；繁體 亦同

NAME_SAMPLES = [
    NameSample("a.b.MP3", "a.b", False, "多点号 + 大写后缀：只去末尾 .MP3"),
    NameSample("a.mp3.mp3", "a.mp3", False, "主名内含 .mp3：只去最后一个"),
    NameSample("01 - 晴天.mp3", "01 - 晴天", False, "保留序号与空格，不拆歌手"),
    NameSample(MIXED_NAME, MIXED_NAME[:-4], False, "中英混排，含内部空格"),
    NameSample(MISSING_GLYPH_NAME, MISSING_GLYPH_NAME[:-4], False, "字库外字：应为占位框，名字不变 --"),
    NameSample(LONG_NAME, _LONG_STEM, True, "主名 245 B，整名 249 B，Music/ 路径 255 B：恰在上限"),
    NameSample("scroll-ascii-The Quick Brown Fox Jumps Over The Lazy Dog.mp3",
               "scroll-ascii-The Quick Brown Fox Jumps Over The Lazy Dog", True, "长 ASCII，需滚动"),
    NameSample("滚动测试曲目名称非常非常长需要横向滚动才能看完.mp3",
               "滚动测试曲目名称非常非常长需要横向滚动才能看完", True, "纯中文，约 23 字 ≈ 276 px"),
    NameSample("Scroll 中英混排 A Very Long Mixed Name 滚动显示.mp3",
               "Scroll 中英混排 A Very Long Mixed Name 滚动显示", True, "中英混排，需滚动"),
]
