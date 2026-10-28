"""板测卡的 ID3 标签样本（#142；规则见 #130/#131，验收项 #135 I1–I6）。

只构造标签字节与期望显示；音频由 make_card.py 接在标签后（与 --set names 同一段可播样本）。
不依赖 lameenc。内容以 ASCII 为主，生成确定性（无随机、无时间）。
"""

import struct
from collections import namedtuple

TagSample = namedtuple("TagSample", "key filename tag dial queue_title queue_artist source reads note")

APIC_PICTURE_BYTES = 8000  # TIT2 帧头落在偏移 8040 > 4096：必须跨首块 seek


def _syncsafe(n):
    return bytes([(n >> 21) & 0x7F, (n >> 14) & 0x7F, (n >> 7) & 0x7F, n & 0x7F])


def _frame(fid, body, version):
    size = struct.pack(">I", len(body)) if version == 3 else _syncsafe(len(body))
    return fid.encode("ascii") + size + b"\x00\x00" + body


def _tag(version, frames):
    payload = b"".join(frames)
    return b"ID3" + bytes([version, 0, 0]) + _syncsafe(len(payload)) + payload


def _utf16(text):
    return b"\x01\xff\xfe" + text.encode("utf-16-le")


def _apic():
    head = b"\x00image/jpeg\x00\x03\x00"
    state, pic = 12345, bytearray(APIC_PICTURE_BYTES)
    for i in range(APIC_PICTURE_BYTES):
        state = (state * 1103515245 + 12345) & 0x7FFFFFFF
        pic[i] = (state >> 16) % 255  # 0..254，不含 0xFF
    return head + bytes(pic)


def build_samples():
    v23 = lambda fid, body: _frame(fid, body, 3)  # noqa: E731
    v24 = lambda fid, body: _frame(fid, body, 4)  # noqa: E731
    return [
        TagSample("v23-utf16le", "tag-1-v23-utf16le.mp3",
                  _tag(3, [v23("TIT2", _utf16("Tag Sample UTF16LE")), v23("TPE1", _utf16("Test Artist A"))]),
                  "Tag Sample UTF16LE", "Tag Sample UTF16LE", "Test Artist A", "标题", "1",
                  "v2.3，编码字节 1（UTF-16 + BOM FF FE），#128 中 42 个文件的形态"),
        TagSample("v24-utf8", "tag-2-v24-utf8.mp3",
                  _tag(4, [v24("TIT2", b"\x03Tag Sample UTF8"), v24("TPE1", b"\x03Test Artist B")]),
                  "Tag Sample UTF8", "Tag Sample UTF8", "Test Artist B", "标题", "1",
                  "v2.4，编码字节 3（UTF-8），帧大小 syncsafe"),
        TagSample("latin1-gbk", "tag-3-latin1-gbk.mp3",
                  _tag(3, [v23("TIT2", b"\x00" + "测试标题 GBK".encode("gbk")),
                           v23("TPE1", b"\x00" + "测试歌手".encode("gbk"))]),
                  "测试标题 GBK", "测试标题 GBK（占位框）", "测试歌手（占位框）", "标题", "1",
                  "v2.3，声明 Latin-1（编码字节 0）实为 GBK；唱盘页中文正确、Queue 占位框（0.8.1 字体补丁解决）"),
        TagSample("apic-first", "tag-4-apic-first.mp3",
                  _tag(3, [v23("APIC", _apic()), v23("TIT2", _utf16("Tag Sample APIC First")),
                           v23("TPE1", _utf16("Test Artist D"))]),
                  "Tag Sample APIC First", "Tag Sample APIC First", "Test Artist D", "标题", "2",
                  "APIC（%d B 图片）在 TIT2/TPE1 之前，TIT2 帧头越过首块 4 KiB：首块 + 1 次 seek 读取，不读 APIC 负载" % APIC_PICTURE_BYTES),
        TagSample("blank", "tag-5-blank-title.mp3",
                  _tag(3, [v23("TALB", b"\x00Unused Album"), v23("TIT2", b"\x00   \x00\x00")]),
                  "tag-5-blank-title", "tag-5-blank-title", "（空）", "文件主名（原因：帧缺失）", "1",
                  "TIT2 全空白、无 TPE1：清理后为空按帧缺失回退，歌手空；正常情形，无 WARN"),
        TagSample("notag", "tag-6-no-tag.mp3", b"",
                  "tag-6-no-tag", "tag-6-no-tag", "（空）", "文件主名（原因：无 ID3v2）", "1",
                  "文件开头即 MP3 帧，无 ID3 头；正常情形，无 WARN"),
    ]


TAG_SAMPLES = build_samples()
