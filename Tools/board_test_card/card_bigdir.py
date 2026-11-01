"""大目录卡（--set bigdir，#180；规则见 #172 J 节）的文件名与写出顺序。

12000 首同文件夹：文件名 50 字符（约 5 个 FAT 目录项），零填充序号作前缀，
使目录顺序等于曲库顺序；第 1、3000、6000、9000、12000 首是 ≥ 3 分钟的 CBR 探针曲，
其余是约 1 s 的小 MP3。音频由 make_card.py 提供，本模块只管命名与顺序，写出经注入的 sink，不依赖 lameenc。
"""

COUNT = 12000
PROBE_INDEXES = (1, 3000, 6000, 9000, 12000)
NAME_LENGTH = 50
_PREFIX_DIGITS = 5
_STEM = "-bigdir-open-time-card-long-file-name-padding-"


def file_name(index):
    """第 index 首（从 1 起）的文件名，恒为 NAME_LENGTH 个 ASCII 字符。"""
    head = "%0*d" % (_PREFIX_DIGITS, index)
    tail = ".mp3"
    pad = NAME_LENGTH - len(head) - len(tail)
    return head + _STEM[:pad].ljust(pad, "x") + tail


def file_names(count=COUNT):
    return [file_name(i) for i in range(1, count + 1)]


def is_probe(index, probe_indexes=PROBE_INDEXES):
    return index in probe_indexes


def write_set(sink, small, probe, count=COUNT, probe_indexes=PROBE_INDEXES):
    """按目录顺序调用 sink(文件名, 字节)；探针位置给 probe，其余给 small。"""
    for index in range(1, count + 1):
        sink(file_name(index), probe if is_probe(index, probe_indexes) else small)


def report_lines():
    names = file_names()
    lines = ["\n大目录卡（--set bigdir；%d 首同文件夹；不含在 all 内；换一张空卡，复制进卡的 Music/ 目录）：" % COUNT,
             "文件名 %d 字符（约 5 个目录项），前缀为 %d 位零填充序号，目录顺序 = 曲库顺序；其余约 1 s 的小 MP3。" % (NAME_LENGTH, _PREFIX_DIGITS),
             "探针曲（≥ 3 分钟 CBR）："]
    for index in PROBE_INDEXES:
        lines.append("  第 %5d 首  %s" % (index, names[index - 1]))
    lines.append("\n--set bigdir 复制顺序：%s -> ... -> %s（共 %d 个，按文件名升序）" % (names[0], names[-1], COUNT))
    return lines
