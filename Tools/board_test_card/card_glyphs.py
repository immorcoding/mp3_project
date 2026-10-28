"""板测卡的曲目元数据字形样本（#150；验收项 #145 F2、F3，0.8.2 #146 V3、V4 复用）。

只构造标签字节与期望显示；音频由 make_card.py 接在标签后（与 --set names/tags 同一段可播样本）。
字段规则以 #135 E 节为准：多值用 " / " 连写，超过容量按 UTF-8 字符边界截断、不加省略号。
不依赖 lameenc；生成确定性。
"""

from card_tags import TagSample, _frame, _tag

NAME_CAPACITY = 252  # #135 G：标题与歌手共用，含结尾 NUL，可用 251 B


def _u16(*items):
    """编码字节 1：每项各带 BOM，项间以 UTF-16 NUL 分隔。"""
    return b"\x01" + b"\x00\x00".join(b"\xff\xfe" + i.encode("utf-16-le") for i in items)


def _u8(*items):
    return b"\x03" + b"\x00".join(i.encode("utf-8") for i in items)


def cut_utf8(text, limit=NAME_CAPACITY - 1):
    out = ""
    for ch in text:
        if len((out + ch).encode("utf-8")) > limit:
            break
        out += ch
    return out


def build_samples():
    v23 = lambda fid, body: _frame(fid, body, 3)  # noqa: E731
    v24 = lambda fid, body: _frame(fid, body, 4)  # noqa: E731

    cjk_title, cjk_artist = "晴天 夜曲 测试标题", "周杰伦"
    ext_title = "Beyoncé – F1® Theme ™ ’Tis “Live” …"
    ext_artist = "Déjà Vu ® – “Zoé” ’ ™ …"
    multi = ["Artist One", "  ", " Artist Two ", "", "AC/DC", "　Artist Three　"]
    multi_shown = "Artist One / Artist Two / AC/DC / Artist Three"
    # 前 249 B 为 ASCII（词尾不留空格），其后全是 3 B 汉字：251 B 上限落在第一个汉字内部
    prefix = ("Long Artist Name " * 15)[:249]
    prefix = prefix[:-1] + "X" if prefix.endswith(" ") else prefix
    long_full = prefix + "歌手名超长尾巴" * 4
    long_shown = cut_utf8(long_full)

    return [
        TagSample("cjk", "glyph-1-cjk.mp3",
                  _tag(4, [v24("TIT2", _u8(cjk_title)), v24("TPE1", _u8(cjk_artist))]),
                  cjk_title, cjk_title, cjk_artist, "标题", "1",
                  "v2.4，编码字节 3（UTF-8），中文标题与中文歌手，字均在 GB2312 内；F2：Queue 无占位框"),
        TagSample("latin-ext", "glyph-2-latin-ext.mp3",
                  _tag(3, [v23("TIT2", _u16(ext_title)), v23("TPE1", _u16(ext_artist))]),
                  ext_title, ext_title, ext_artist, "标题", "1",
                  "v2.3，编码字节 1（UTF-16 + BOM），标题与歌手都含 ® é – ™ ’ “ ” …；F3"),
        TagSample("multi-artist", "glyph-3-multi-artist.mp3",
                  _tag(4, [v24("TIT2", _u8("Multi Artist Sample")), v24("TPE1", _u8(*multi))]),
                  "Multi Artist Sample", "Multi Artist Sample", multi_shown, "标题", "1",
                  "v2.4，TPE1 六项 NUL 分隔，含全空白项、空项、首尾空白与 U+3000，另有 AC/DC（不按 / 拆分）；多值用 \" / \" 连写"),
        TagSample("long-artist", "glyph-4-long-artist.mp3",
                  _tag(4, [v24("TIT2", _u8("Long Artist Sample")), v24("TPE1", _u8(long_full))]),
                  "Long Artist Sample", "Long Artist Sample", long_shown, "标题", "1",
                  "v2.4，TPE1 共 %d B（上限 251 B）：前 249 B 为 ASCII，其后汉字，截断点在汉字内部；"
                  "按字符边界留 %d B，不加省略号；远宽于 Queue 与唱盘页可见宽度"
                  % (len(long_full.encode("utf-8")), len(long_shown.encode("utf-8")))),
    ]


GLYPH_SAMPLES = build_samples()
