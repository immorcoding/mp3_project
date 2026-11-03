"""补充提取：每卷固件 P3、P5 各把锁；P0 循环次数的先后；全局固件的 P4 与拔卡；日志大小。"""
import pathlib
import re

tmp = pathlib.Path(r"C:\Users\22137\.claude\jobs\9bbd3cd9\tmp")
ansi = re.compile(r"\x1b\[[0-9;]*m|\[[0-9;]*m")


def clean(path):
    return [ansi.sub("", l) for l in (tmp / path).read_text(encoding="utf-8", errors="replace").splitlines()]


per = clean("board-pervol.log")
boots = [i for i, l in enumerate(per) if "BOOT: FW" in l]
print("pervol boots", boots, "lines", len(per))
head, run = per[: boots[-1]], per[boots[-1]:]

phase = None
keep = {}
for l in run:
    if "PHASE start" in l:
        phase = l.split("PHASE start ")[1].split()[0]
    m = re.search(r"SPIKE: (LOCK (?:SD|FLASH|TABLE) ph.*|LOCKBLK .*)", l)
    if m and phase:
        keep[(phase, m.group(1).split()[0], m.group(1).split()[1])] = m.group(1)
print("--- pervol lock rows by phase")
for k in sorted(keep):
    print(k[0], keep[k][:125])

print("--- GUI P0 order pervol")
for l in run:
    if "SPIKE: GUI P0" in l or "SPIKE: CPU P0" in l:
        print(l[:125])

print("--- global run tail in pervol.log head (P4/P5)")
for l in head:
    if re.search(r"SPIKE: (CELL|CELLGUI|LOCK GLOBAL ph|GUI P5|CPU P5|UNDER P4)", l):
        pass
rows = {}
for l in head:
    m = re.search(r"SPIKE: (CELL P4 write|CELL P4 read|CELLGUI P4)", l)
    if m:
        rows[m.group(1)] = l.split("SPIKE: ")[1]
for v in rows.values():
    print(v[:110])

print("--- global unplug (board-global-3 first gca-less run, bc01237)")
for l in clean("board-global-3.log"):
    if "UNPLUG" in l or "HotplugRefresh" in l:
        print(l[13:135])

print("--- sizes")
for f in ["board-global-2.log", "board-global-3.log", "board-pervol.log"]:
    print(f, (tmp / f).stat().st_size)
