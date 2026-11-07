"""大目录卡（--set bigdir，#180、#214；规则见 #172 J 节、#201 样本卡）的文件名与写出顺序。

曲库共 12000 首（STOR-17 上限）：文件名 50 字符（约 5 个 FAT 目录项），零填充序号作前缀，
使目录顺序等于曲库顺序；第 1、3000、6000、9000、12000 首是 ≥ 3 分钟的 CBR 探针曲，
其余是约 1 s 的小 MP3。

最后 TAIL_COUNT 首放进创建在所有根目录文件之后的子目录 TAIL_DIR：曲库按深度优先扫描，
子目录在当前目录读完后才处理，所以这两首在曲库末尾；固件按路径（定位符失效时）打开它们，
要先在 11998 个目录项里找到子目录，用来测大目录里按路径打开子目录。曲库上限 12000，
所以根目录只放 11998 首，合计恰为 12000，子目录里的曲目不会被截断。

音频由 make_card.py 提供，本模块只管命名与顺序，写出经注入的 sink，不依赖 lameenc。
"""

COUNT = 12000                      # 曲库总数 = 根目录文件 + 子目录文件
TAIL_COUNT = 2                     # 放进末尾子目录的曲目数（曲库最后两首）
TAIL_DIR = "zz-tail-subdir"        # 字母序晚于任何数字前缀的文件名，创建顺序也在所有根文件之后
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


def relative_path(index, count=COUNT, tail_count=TAIL_COUNT):
    """第 index 首相对 Music/ 的路径；最后 tail_count 首在 TAIL_DIR 下（用 / 分隔）。"""
    if index > count - tail_count:
        return TAIL_DIR + "/" + file_name(index)
    return file_name(index)


def relative_paths(count=COUNT, tail_count=TAIL_COUNT):
    """按创建顺序（= 曲库顺序）的相对路径：先所有根目录文件，再子目录文件。"""
    return [relative_path(i, count, tail_count) for i in range(1, count + 1)]


def is_probe(index, probe_indexes=PROBE_INDEXES):
    return index in probe_indexes


def write_set(sink, small, probe, count=COUNT, probe_indexes=PROBE_INDEXES, tail_count=TAIL_COUNT):
    """按创建顺序调用 sink(相对路径, 字节)；探针位置给 probe，其余给 small。"""
    for index in range(1, count + 1):
        sink(relative_path(index, count, tail_count), probe if is_probe(index, probe_indexes) else small)


def report_lines():
    paths = relative_paths()
    root_count = COUNT - TAIL_COUNT
    lines = ["\n大目录卡（--set bigdir；曲库 %d 首；不含在 all 内；放进卡上的一个文件夹，上板前改名成 Music）：" % COUNT,
             "文件名 %d 字符（约 5 个目录项），前缀为 %d 位零填充序号，目录顺序 = 曲库顺序；其余约 1 s 的小 MP3。" % (NAME_LENGTH, _PREFIX_DIGITS),
             "根目录 %d 首（第 1 ~ %d 首）；最后 %d 首在末尾子目录 %s/（第 %d ~ %d 首，创建在所有根文件之后，"
             "曲库扫描最后才到）：用来测大目录里按路径打开子目录。" % (root_count, root_count, TAIL_COUNT, TAIL_DIR, root_count + 1, COUNT),
             "探针曲（≥ 3 分钟 CBR）："]
    for index in PROBE_INDEXES:
        lines.append("  第 %5d 首  %s" % (index, paths[index - 1]))
    lines.append("\n--set bigdir 复制顺序：%s -> ... -> %s -> ... -> %s（共 %d 个，先 %d 个根文件，再子目录里的 %d 个）" % (
                 paths[0], paths[root_count - 1], paths[-1], COUNT, root_count, TAIL_COUNT))
    return lines
