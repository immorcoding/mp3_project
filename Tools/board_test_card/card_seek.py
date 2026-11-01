"""跳转样本（#174，spec #172 J 节）：刻度音内容与四首的头部类型。

内容：静音底，每 10 s 一声短哔，整分钟（60 s 的倍数）双哔，供板测听“跳到刻度前约 2 s 后多久听到哔声”。
纯 Python，不依赖 lameenc；编码与头部在 make_card.py 里完成。
"""

import math
import struct
from collections import namedtuple

RATE = 44100
CHANNELS = 2
DURATION_SECONDS = 210   # 3:30，≥ 3 分钟
TICK_SECONDS = 10
MINUTE_SECONDS = 60
BEEP_MS = 100
BEEP_GAP_MS = 150        # 双哔：第二声起点 = 第一声起点 + 哔长 + 间隔
BEEP_HZ = 1000
BEEP_RAMP_MS = 5
BEEP_PEAK = 12000        # 与其他样本同峰值

# kind: 头部类型；bitrate 为 None 表示 VBR。with_toc 只对带头的样本有意义。
SeekSample = namedtuple("SeekSample", "filename header bitrate with_toc note")

SEEK_SAMPLES = [
    SeekSample("seek-1-cbr320-nohdr.mp3", None, 320, False, "CBR 320，无 Xing/Info：按 AudioBytes 线性映射（lin）"),
    SeekSample("seek-2-cbr320-info.mp3", "Info", 320, False, "CBR 320，Info 头（帧数+字节数，无 TOC）：lin"),
    SeekSample("seek-3-vbr-toc.mp3", "Xing", None, True, "VBR，Xing 头带 100 项 TOC：toc 映射"),
    SeekSample("seek-4-vbr-nohdr.mp3", None, None, False, "VBR，无头：线性映射，误差只记录"),
]


def beeps(duration_seconds=DURATION_SECONDS):
    """返回每声哔的起点秒数列表：每 10 s 一声，60 s 的倍数多一声（第二声在 0.25 s 后）。"""
    out = []
    second = TICK_SECONDS
    while second + (BEEP_MS + BEEP_GAP_MS + BEEP_MS) / 1000.0 < duration_seconds:
        out.append(float(second))
        if second % MINUTE_SECONDS == 0:
            out.append(second + (BEEP_MS + BEEP_GAP_MS) / 1000.0)
        second += TICK_SECONDS
    return out


def tick_pcm(duration_seconds=DURATION_SECONDS):
    """16 位交错立体声 PCM：全零静音底 + 哔声。"""
    total = duration_seconds * RATE
    pcm = bytearray(total * CHANNELS * 2)
    length = RATE * BEEP_MS // 1000
    ramp = RATE * BEEP_RAMP_MS // 1000
    shape = []
    for n in range(length):
        gain = min(1.0, n / ramp, (length - 1 - n) / ramp)
        shape.append(int(BEEP_PEAK * gain * math.sin(2.0 * math.pi * BEEP_HZ * n / RATE)))
    for start in beeps(duration_seconds):
        base = int(round(start * RATE))
        for n, value in enumerate(shape):
            struct.pack_into("<hh", pcm, (base + n) * 4, value, value)
    return bytes(pcm)
