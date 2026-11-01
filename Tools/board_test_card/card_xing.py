"""自写 Xing/Info 头（#174，spec #172 J 节）。

lameenc 的流式接口不写 Xing/Info，这里逐帧扫描码流得到帧数与字节数，再按 Xing 格式拼出头帧并前置。
纯 Python、无第三方依赖，输出只由码流决定，所以逐字节可复现。仅支持 MPEG-1 Layer III（夹具与样本卡都是 44.1 kHz）。

约定（与 Tests/playback_service/fixtures/generate_fixtures.py 一致）：
* “字节数”与 TOC 的分母都只计音频帧，不含头帧本身，也不含前置标签；TOC 偏移从第一个音频帧起算。
* 头帧用 128 kbps、无 CRC、无填充的 417 B 帧（取首个音频帧的采样率与声道模式），边信息区与未用部分全零。
* TOC 第 i 项（0..99）= min(255, 256 × 第 floor(i × 帧数 / 100) 帧的偏移 ÷ 音频总字节)。
"""

import struct

BITRATE_MPEG1_L3 = [0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320]
SAMPLE_RATE_MPEG1 = [44100, 48000, 32000]
TOC_ENTRIES = 100
FLAG_FRAMES, FLAG_BYTES, FLAG_TOC = 0x01, 0x02, 0x04
_TEMPLATE_BITRATE_INDEX = 9  # 128 kbps


def _frame_length(header):
    assert header[0] == 0xFF and (header[1] & 0xE0) == 0xE0, "帧同步丢失"
    assert (header[1] & 0x18) == 0x18 and (header[1] & 0x06) == 0x02, "只支持 MPEG-1 Layer III"
    bitrate = BITRATE_MPEG1_L3[header[2] >> 4]
    rate = SAMPLE_RATE_MPEG1[(header[2] >> 2) & 3]
    assert bitrate and rate, "非法比特率或采样率索引"
    return 144 * bitrate * 1000 // rate + ((header[2] >> 1) & 1)


def scan_frames(stream):
    """返回 [(偏移, 帧长)]；码流必须从帧头开始、不含标签，且以整帧结束。"""
    frames, pos = [], 0
    while pos < len(stream):
        size = _frame_length(stream[pos:pos + 4])
        assert pos + size <= len(stream), "码流末尾有残缺帧"
        frames.append((pos, size))
        pos += size
    return frames


def toc_from_sizes(sizes):
    """由各帧字节数算 100 项 TOC（每项 1/256 总长）。"""
    offsets, total = [], 0
    for size in sizes:
        offsets.append(total)
        total += size
    assert total > 0, "空码流"
    return bytes(min(255, 256 * offsets[i * len(sizes) // TOC_ENTRIES] // total) for i in range(TOC_ENTRIES))


def build_header_frame(first_header, kind, frames, audio_bytes, toc=None):
    if kind not in ("Xing", "Info"):
        raise ValueError("kind 只能是 Xing（VBR）或 Info（CBR）：%r" % (kind,))
    header = bytearray(first_header[:4])
    header[1] |= 0x01  # 无 CRC
    header[2] = (_TEMPLATE_BITRATE_INDEX << 4) | (header[2] & 0x0C)  # 128 kbps、保留采样率、无填充
    size = _frame_length(header)
    mono = (header[3] & 0xC0) == 0xC0
    offset = 21 if mono else 36
    flags = FLAG_FRAMES | FLAG_BYTES | (FLAG_TOC if toc is not None else 0)
    body = kind.encode("ascii") + struct.pack(">III", flags, frames, audio_bytes) + (toc or b"")
    assert offset + len(body) <= size
    frame = bytearray(size)
    frame[0:4] = header
    frame[offset:offset + len(body)] = body
    return bytes(frame)


def add_header(stream, kind, with_toc):
    """在纯音频码流前加 Xing（VBR）或 Info（CBR）头帧；with_toc 为真时带 100 项 TOC。"""
    frames = scan_frames(stream)
    toc = toc_from_sizes([size for _, size in frames]) if with_toc else None
    return build_header_frame(stream[:4], kind, len(frames), len(stream), toc) + stream
