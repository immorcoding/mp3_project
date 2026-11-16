"""长跑样本（--set soak，#214；规则见 #198 第 6 条）：三首 320 kbps，供 2 h 纯播放长跑。

32、44.1、48 kHz 各一首，按此顺序入库，末曲环回首曲。内容是 make_card.py 的确定性和弦，
纯数据模块，不依赖 lameenc；编码在 make_card.py 里完成。
"""

from collections import namedtuple

DURATION_SECONDS = 210  # 3:30，≥ 3 分钟，与 card-01..03 同长
BITRATE_KBPS = 320

SoakSample = namedtuple("SoakSample", "filename rate bitrate note")

SOAK_SAMPLES = [
    SoakSample("soak-1-32k-320k.mp3", 32000, BITRATE_KBPS, "MPEG-1 L3 32 kHz 立体声 320 kbps，3:30"),
    SoakSample("soak-2-44k1-320k.mp3", 44100, BITRATE_KBPS, "MPEG-1 L3 44.1 kHz 立体声 320 kbps，3:30"),
    SoakSample("soak-3-48k-320k.mp3", 48000, BITRATE_KBPS, "MPEG-1 L3 48 kHz 立体声 320 kbps，3:30"),
]
