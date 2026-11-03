"""按阶段提取板测日志的 SPIKE 终值。用法：extract.py <log> [<boot 匹配串>] ；只看最后一次开机之后。"""
import re
import sys

path = sys.argv[1]
lines = open(path, encoding="utf-8", errors="replace").read().splitlines()
start = 0
for i, line in enumerate(lines):
    if "BOOT: FW" in line:
        start = i
lines = lines[start:]

ansi = re.compile(r"\x1b\[[0-9;]*m|\[[0-9;]*m")
last = {}
order = []
switches = []
for line in lines:
    clean = ansi.sub("", line)
    m = re.search(r"SPIKE: (\S+) (P\d|GLOBAL|SD|FLASH|TABLE)(?: (\S+))?", clean)
    if "Switch idx" in clean:
        switches.append(clean.split("PLAYBACK: ")[1])
    if "SPIKE: " not in clean:
        continue
    body = clean.split("SPIKE: ", 1)[1]
    if body.startswith("PHASE start"):
        phase = body.split()[2]
        order.append(("PHASE", body[:150]))
    if m:
        key = " ".join(x for x in m.groups() if x)
        last[key] = body

for key in sorted(last):
    print(last[key][:170])
print("--- phases")
for _, b in order:
    print(b)
print("--- switches")
for s in switches:
    print(s)
print("--- stacks (last)")
stacks = {}
for line in lines:
    clean = ansi.sub("", line)
    if "MONITOR: " in clean and ("total=" in clean or "Heap" in clean or "GUI max" in clean):
        body = clean.split("MONITOR: ")[1]
        stacks[body.split(":")[0]] = body
for v in stacks.values():
    print(v)
