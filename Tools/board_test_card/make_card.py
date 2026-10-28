#!/usr/bin/env python3
"""生成板级验收用的测试卡（一组合成 MP3，无任何第三方录音）。

用法（需要 ``pip install lameenc``，与 Tests/playback_service/fixtures 相同）：

    python Tools/board_test_card/make_card.py <输出目录>
    python Tools/board_test_card/make_card.py <输出目录> --copy-to I:/Music --set all

复用 ``generate_fixtures.py`` 的合成与编码函数。音频是确定性的整数 Hz 正弦和弦，
每个和弦恰好 1 s、起止采样为 0，循环拼接成整曲，所以曲内没有人为咔哒声，
听到的咔哒声就是固件造成的。输出目录不进 Git。
"""

import argparse
import hashlib
import os
import shutil
import struct
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "Tests", "playback_service", "fixtures"))
import generate_fixtures as gf  # noqa: E402
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import card_names  # noqa: E402
import card_tags  # noqa: E402
import card_glyphs  # noqa: E402

# 整数 Hz 和弦：1 s 周期，包络 3 Hz 也是整周期，首尾采样都为 0。
PROGRESSION = [
    [[(220, 0.5), (330, 0.3)], [(277, 0.5), (440, 0.3)]],
    [[(196, 0.5), (294, 0.3)], [(247, 0.5), (392, 0.3)]],
    [[(175, 0.5), (262, 0.3)], [(220, 0.5), (349, 0.3)]],
    [[(165, 0.5), (247, 0.3)], [(208, 0.5), (330, 0.3)]],
]

ID3_TOTAL_BYTES = 1024 * 1024  # 约 1 MB 的 ID3v2.3 标签（含 10 字节标签头）

# 复制顺序即 FAT 目录项创建顺序；固件按目录顺序入库。
FILES = [
    # (文件名, 说明)
    ("bad-zero.mp3", "200000 字节全零：坏文件（先复制，才能测“首个文件是坏文件”）"),
    ("card-01-32k-320k.mp3", "MPEG-1 L3 32 kHz 立体声 320 kbps，3:30"),
    ("card-02-44k1-320k.mp3", "MPEG-1 L3 44.1 kHz 立体声 320 kbps，3:30"),
    ("card-03-48k-320k.mp3", "MPEG-1 L3 48 kHz 立体声 320 kbps，3:30"),
    ("card-04-id3-1mb.mp3", "ID3v2.3 约 1 MB（APIC 形填充）+ 44.1 kHz 192 kbps 30 s：标签跳过压力"),
    ("card-05-short.mp3", "44.1 kHz 128 kbps 约 0.5 s：短于 1 s"),
    ("card-06-mpeg2-22k.mp3", "MPEG-2 L3 22.05 kHz 单声道 32 kbps 10 s：不支持采样率，应被跳过"),
    ("bad-empty.mp3", "0 字节：坏文件"),
]
SECTION_SECONDS = 210
NAME_SAMPLE_SECONDS = 30  # 文件名样本的可播内容：44.1 kHz 192 kbps 30 s，与 card-04 同音频

# 命名子集；顺序沿用 FILES 的相对顺序。
SETS = {
    "all": [f for f, _ in FILES],
    "first-bad": ["bad-zero.mp3", "card-02-44k1-320k.mp3", "card-05-short.mp3"],
    "all-unsupported": ["bad-zero.mp3", "card-06-mpeg2-22k.mp3", "bad-empty.mp3"],
    # 曲目显示名样本（#117）：内容都是同一段可播音频，只有文件名不同；不属于 all，需单独复制进卡的 Music/ 目录（--copy-to 指向 Music）。
    "names": [sample.filename for sample in card_names.NAME_SAMPLES],
    # ID3 标签样本（#142）：六类标签形态，音频同 names；不属于 all，换空卡单独复制进 Music/。
    "tags": [sample.filename for sample in card_tags.TAG_SAMPLES],
    # 曲目元数据字形样本（#150）：中文、西文扩展字符、多值与超长歌手，音频同 names；不属于 all，换空卡单独复制进 Music/。
    "glyphs": [sample.filename for sample in card_glyphs.GLYPH_SAMPLES],
}


def tile_pcm(rate, channels, seconds):
    """把 4 个 1 s 和弦循环拼到 seconds 秒（seconds 取整）。"""
    loop = b"".join(gf.synth_pcm(rate, channels, 1.0, chord) for chord in PROGRESSION)
    reps, rest = divmod(int(seconds), 4)
    return loop * reps + loop[: rest * rate * channels * 2]


def build_id3_tag(total=ID3_TOTAL_BYTES):
    """ID3v2.3 标签：TIT2 + 一个大 APIC 帧；图片数据是不含 0xFF 的确定性字节，避免伪同步。"""
    title = b"\x00board test card"
    tit2 = b"TIT2" + struct.pack(">I", len(title)) + b"\x00\x00" + title
    head = b"\x00image/jpeg\x00\x03\x00"  # 编码、MIME、图片类型、描述
    apic_body = total - 10 - len(tit2) - 10
    pic_len = apic_body - len(head)
    state, pic = 12345, bytearray(pic_len)
    for i in range(pic_len):
        state = (state * 1103515245 + 12345) & 0x7FFFFFFF
        pic[i] = (state >> 16) % 255  # 0..254
    apic = b"APIC" + struct.pack(">I", apic_body) + b"\x00\x00" + head + bytes(pic)
    payload = tit2 + apic
    size = len(payload)
    syncsafe = bytes([(size >> 21) & 0x7F, (size >> 14) & 0x7F, (size >> 7) & 0x7F, size & 0x7F])
    return b"ID3\x03\x00\x00" + syncsafe + payload


def make_all():
    """返回 {文件名: 字节}（确定性）。"""
    out = {}
    out["bad-zero.mp3"] = bytes(200000)
    out["bad-empty.mp3"] = b""
    for name, rate in (("card-01-32k-320k.mp3", 32000), ("card-02-44k1-320k.mp3", 44100), ("card-03-48k-320k.mp3", 48000)):
        out[name] = gf.encode(tile_pcm(rate, 2, SECTION_SECONDS), rate, 2, bitrate=320)
    out["card-04-id3-1mb.mp3"] = build_id3_tag() + gf.encode(tile_pcm(44100, 2, 30), 44100, 2, bitrate=192)
    short = gf.synth_pcm(44100, 2, 0.5, PROGRESSION[0])
    out["card-05-short.mp3"] = gf.encode(short, 44100, 2, bitrate=128)
    loop = b"".join(gf.synth_pcm(22050, 1, 1.0, [c[0]]) for c in PROGRESSION)
    out["card-06-mpeg2-22k.mp3"] = gf.encode(loop * 2 + loop[: 2 * 22050 * 2], 22050, 1, bitrate=32)
    name_audio = gf.encode(tile_pcm(44100, 2, NAME_SAMPLE_SECONDS), 44100, 2, bitrate=192)
    for sample in card_names.NAME_SAMPLES:
        out[sample.filename] = name_audio
    for sample in card_tags.TAG_SAMPLES + card_glyphs.GLYPH_SAMPLES:
        out[sample.filename] = sample.tag + name_audio
    return out


def copy_in_order(src_dir, dst_dir, names):
    for name in names:
        shutil.copyfile(os.path.join(src_dir, name), os.path.join(dst_dir, name))
        print("已复制", name)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("output_dir")
    parser.add_argument("--copy-to", help="生成后按顺序复制到此目录（SD 卡挂载点；应为空目录）")
    parser.add_argument("--set", choices=sorted(SETS), default="all", help="--copy-to 复制的子集，默认 all")
    args = parser.parse_args()

    os.makedirs(args.output_dir, exist_ok=True)
    data = make_all()
    notes = dict(FILES)
    print("测试卡（复制顺序 = FAT 创建顺序 = 入库顺序）：")
    for index, (name, _) in enumerate(FILES, 1):
        with open(os.path.join(args.output_dir, name), "wb") as handle:
            handle.write(data[name])
        digest = hashlib.sha256(data[name]).hexdigest()[:12]
        print("%d. %-24s %9d 字节  sha256:%s  %s" % (index, name, len(data[name]), digest, notes[name]))
    print("\n曲目显示名样本（--set names；内容相同、只改文件名；不含在 all 内；复制进卡的 Music/ 目录，按此顺序，建议用另一张空卡）：")
    for index, sample in enumerate(card_names.NAME_SAMPLES, 1):
        with open(os.path.join(args.output_dir, sample.filename), "wb") as handle:
            handle.write(data[sample.filename])
        print("%d. %s  期望显示名 %r  %s%s" % (index, sample.filename, sample.display,
              "[需滚动] " if sample.scroll else "", sample.note))
    print("\nID3 标签样本（--set tags；音频同上、只改标签；不含在 all 内；复制进卡的 Music/ 目录，按此顺序，建议用另一张空卡）：")
    for index, sample in enumerate(card_tags.TAG_SAMPLES, 1):
        with open(os.path.join(args.output_dir, sample.filename), "wb") as handle:
            handle.write(data[sample.filename])
        print("%d. %-24s %8d 字节  唱盘页 %r  Queue %r / %r  来源 %s 读取 %s 次  %s" % (
              index, sample.filename, len(data[sample.filename]), sample.dial, sample.queue_title,
              sample.queue_artist, sample.source, sample.reads, sample.note))
    print("\n曲目元数据字形样本（--set glyphs；音频同上、只改标签；不含在 all 内；复制进卡的 Music/ 目录，按此顺序，建议用另一张空卡）：")
    for index, sample in enumerate(card_glyphs.GLYPH_SAMPLES, 1):
        with open(os.path.join(args.output_dir, sample.filename), "wb") as handle:
            handle.write(data[sample.filename])
        print("%d. %-26s %8d 字节  唱盘页 %r  Queue %r / %r  来源 %s 读取 %s 次  %s" % (
              index, sample.filename, len(data[sample.filename]), sample.dial, sample.queue_title,
              sample.queue_artist, sample.source, sample.reads, sample.note))
    print("\n子集（均按上面的相对顺序复制）：")
    for key, names in SETS.items():
        print("  --set %-16s %s" % (key, ", ".join(names)))
    print("\n注意：FAT 目录顺序取决于创建顺序，复制到卡上必须按此顺序逐个复制，不要整夹拖拽；")
    print("测“首个文件是坏文件”时 bad-zero.mp3 必须最先复制到空卡（或先格式化）。")

    if args.copy_to:
        os.makedirs(args.copy_to, exist_ok=True)
        copy_in_order(args.output_dir, args.copy_to, SETS[args.set])


if __name__ == "__main__":
    main()
