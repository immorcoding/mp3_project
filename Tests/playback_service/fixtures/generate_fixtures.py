#!/usr/bin/env python3
"""生成 Tests/playback_service 使用的小型 MP3 夹具。

夹具内容全部由本脚本合成，不含任何第三方录音：

* 音频是确定性的正弦和弦加幅度包络，由本脚本逐采样计算；
* 编码用 LAME（Python 包 lameenc 1.9.0 内嵌的 LAME），输出的 MP3 码流归本仓库所有；
* Xing 与 VBRI 元数据帧由本脚本按文件格式手工拼出（LAME 的流式编码接口不写它们）。

用法（需要 ``pip install lameenc``，不属于固件或主机测试的运行时依赖）：

    python Tests/playback_service/fixtures/generate_fixtures.py

脚本末尾打印每个夹具的字节数与帧数，测试里的期望值以此为准；
重新生成后若数字变化，须同步 ``Tests/playback_service`` 里的常量。
"""

import math
import os
import struct

import lameenc

OUT_DIR = os.path.dirname(os.path.abspath(__file__))

# MPEG-1 与 MPEG-2/2.5 Layer III 的比特率表（kbps），下标为帧头中的比特率索引。
BITRATE_MPEG1_L3 = [0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320]
BITRATE_MPEG2_L3 = [0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160]
SAMPLE_RATE_MPEG1 = [44100, 48000, 32000]
SAMPLE_RATE_MPEG2 = [22050, 24000, 16000]


def synth_pcm(sample_rate, channels, seconds, tones):
    """合成 16 位交错 PCM：每个声道一组 (频率 Hz, 幅度) 正弦，叠加缓慢的幅度包络。"""
    total = int(sample_rate * seconds)
    out = bytearray()
    for n in range(total):
        t = n / sample_rate
        envelope = 0.55 + 0.45 * math.sin(2.0 * math.pi * 3.0 * t)
        for ch in range(channels):
            value = 0.0
            for freq, amp in tones[ch % len(tones)]:
                value += amp * math.sin(2.0 * math.pi * freq * t)
            sample = int(max(-1.0, min(1.0, value * envelope)) * 12000)
            out += struct.pack("<h", sample)
    return bytes(out)


def encode(pcm, sample_rate, channels, bitrate=None, vbr_quality=None):
    enc = lameenc.Encoder()
    enc.set_in_sample_rate(sample_rate)
    enc.set_out_sample_rate(sample_rate)
    enc.set_channels(channels)
    enc.set_quality(2)
    if vbr_quality is None:
        enc.set_bit_rate(bitrate)
    else:
        enc.set_vbr(4)
        enc.set_vbr_quality(vbr_quality)
    return bytes(enc.encode(pcm)) + bytes(enc.flush())


def frame_length(header):
    """返回一个 Layer III 帧的字节数（含填充）。"""
    version_mpeg1 = (header[1] & 0x08) != 0
    bitrate_index = header[2] >> 4
    sample_rate_index = (header[2] >> 2) & 3
    padding = (header[2] >> 1) & 1
    if version_mpeg1:
        return 144 * BITRATE_MPEG1_L3[bitrate_index] * 1000 // SAMPLE_RATE_MPEG1[sample_rate_index] + padding
    rate = SAMPLE_RATE_MPEG2[sample_rate_index]
    return 72 * BITRATE_MPEG2_L3[bitrate_index] * 1000 // rate + padding


def split_frames(stream):
    """把纯 MP3 帧流切成帧列表；码流必须从帧头开始且不含标签。"""
    frames = []
    pos = 0
    while pos < len(stream):
        header = stream[pos:pos + 4]
        assert header[0] == 0xFF and (header[1] & 0xE0) == 0xE0, "帧同步丢失，偏移 %d" % pos
        size = frame_length(header)
        assert pos + size <= len(stream), "码流末尾有残缺帧"
        frames.append(stream[pos:pos + size])
        pos += size
    return frames


def metadata_frame_header(first_header):
    """以首帧为模板造一个 128 kbps（MPEG-1）或 64 kbps（MPEG-2）、无填充的元数据帧头。"""
    header = bytearray(first_header)
    header[1] |= 0x01  # 不带 CRC
    mpeg1 = (header[1] & 0x08) != 0
    header[2] = ((9 if mpeg1 else 8) << 4) | (header[2] & 0x0C)  # 比特率索引 + 原采样率，无填充
    return bytes(header)


def build_xing_frame(first_header, audio_frames, audio_bytes):
    header = metadata_frame_header(first_header)
    size = frame_length(header)
    mpeg1 = (header[1] & 0x08) != 0
    mono = (header[3] & 0xC0) == 0xC0
    offset = (21 if mono else 36) if mpeg1 else (13 if mono else 21)
    frame = bytearray(size)
    frame[0:4] = header
    frame[offset:offset + 4] = b"Xing"
    frame[offset + 4:offset + 8] = struct.pack(">I", 0x00000003)  # 帧数 + 字节数
    frame[offset + 8:offset + 12] = struct.pack(">I", audio_frames)
    frame[offset + 12:offset + 16] = struct.pack(">I", audio_bytes)
    return bytes(frame)


def build_vbri_frame(first_header, audio_frames, audio_bytes):
    header = metadata_frame_header(first_header)
    size = frame_length(header)
    frame = bytearray(size)
    frame[0:4] = header
    # VBRI 固定位于帧头后 32 字节处（偏移 36）。
    frame[36:40] = b"VBRI"
    frame[40:50] = struct.pack(">HHHI", 1, 0, 75, audio_bytes)  # 版本、延迟、质量、字节数
    frame[50:54] = struct.pack(">I", audio_frames)
    frame[54:62] = struct.pack(">HHHH", 0, 1, 1, 1)  # 无 TOC 条目
    return bytes(frame)


def write(name, data, frames, note):
    path = os.path.join(OUT_DIR, name)
    with open(path, "wb") as handle:
        handle.write(data)
    print("%-28s %6d 字节  音频帧 %3d  %s" % (name, len(data), frames, note))


def main():
    chord_a = [[(440.0, 0.5), (660.0, 0.3)], [(554.4, 0.5), (880.0, 0.3)]]
    chord_b = [[(330.0, 0.5), (495.0, 0.3)], [(392.0, 0.5), (784.0, 0.3)]]
    chord_c = [[(523.3, 0.5), (784.0, 0.3), (1046.5, 0.2)]]

    # 1. CBR 128 kbps，44.1 kHz 立体声，约 0.75 s。
    stream = encode(synth_pcm(44100, 2, 0.75, chord_a), 44100, 2, bitrate=128)
    frames = split_frames(stream)
    write("cbr_44100_stereo.mp3", stream, len(frames), "CBR 128 kbps，无元数据帧")

    # 2. VBR（LAME -V4），44.1 kHz 立体声，约 0.75 s，前置 Xing 帧。
    stream = encode(synth_pcm(44100, 2, 0.75, chord_b), 44100, 2, vbr_quality=4)
    frames = split_frames(stream)
    bitrates = sorted({BITRATE_MPEG1_L3[f[2] >> 4] for f in frames})
    xing = build_xing_frame(frames[0][:4], len(frames), len(stream))
    write("vbr_xing_44100_stereo.mp3", xing + stream, len(frames), "VBR，比特率 %s kbps，Xing 帧数 %d" % (bitrates, len(frames)))

    # 3. CBR 112 kbps，44.1 kHz 立体声，约 0.6 s，前置 VBRI 帧。
    stream = encode(synth_pcm(44100, 2, 0.6, chord_a[::-1]), 44100, 2, bitrate=112)
    frames = split_frames(stream)
    vbri = build_vbri_frame(frames[0][:4], len(frames), len(stream))
    write("vbri_44100_stereo.mp3", vbri + stream, len(frames), "CBR 112 kbps，VBRI 帧数 %d" % len(frames))

    # 4. MPEG-2 Layer III 单声道，22.05 kHz，32 kbps，约 1.0 s（每帧 576 个采样）。
    stream = encode(synth_pcm(22050, 1, 1.0, chord_c), 22050, 1, bitrate=32)
    frames = split_frames(stream)
    write("mpeg2_22050_mono.mp3", stream, len(frames), "MPEG-2 单声道 32 kbps，无元数据帧")


if __name__ == "__main__":
    main()
