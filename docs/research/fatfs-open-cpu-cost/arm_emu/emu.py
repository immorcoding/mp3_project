"""在 Unicorn 的 Cortex-M7 模型里逐指令执行真实 ARM 版 ff.c，数 f_open 的 Thumb-2 指令数。

输入：run_arm.ps1 交叉编译出的 harness.elf/.bin/.nm，以及 bench.exe 导出的镜像页文件 <prefix>_A.bin / _B.bin
与路径文件 <prefix>_A.paths / _B.paths。disk_read 入口被截获并从镜像页里供数（模拟扇区缓存全命中）。
只数指令与已取分支，不建模流水线、缓存、存储器等待；周期换算见报告第 4 节。
用法：python -I emu.py <build_dir> <dump_prefix>
"""
import bisect
import struct
import sys

from unicorn import (UC_ARCH_ARM, UC_HOOK_CODE, UC_MODE_MCLASS, UC_MODE_THUMB, Uc)
from unicorn.arm_const import (UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_R0, UC_ARM_REG_R1,
                               UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_SP, UC_CPU_ARM_CORTEX_M7)

CODE_BASE = 0x08000000
CODE_SIZE = 0x00200000
DTCM_BASE = 0x20000000
DTCM_SIZE = 0x00020000
PATH_BUF = DTCM_BASE + 0x10000
STACK_TOP = DTCM_BASE + 0xF000
SENTINEL = CODE_BASE + CODE_SIZE - 0x100


def load_symbols(nm_file):
    syms = []
    for line in open(nm_file, encoding='utf-8'):
        p = line.split()
        if len(p) == 3 and p[1] in 'tTwW':
            syms.append((int(p[0], 16) & ~1, p[2]))
    syms.sort()
    return syms


def load_pages(fn):
    pages = {}
    data = open(fn, 'rb').read()
    rec = 4 + 65536
    for off in range(0, len(data), rec):
        idx = struct.unpack_from('<I', data, off)[0]
        pages[idx] = data[off + 4:off + rec]
    return pages


def run_image(build, prefix, tag):
    syms = load_symbols(f'{build}/harness.nm')
    addrs = [a for a, _ in syms]
    sym = {n: a for a, n in syms}
    pages = load_pages(f'{prefix}_{tag}.bin')
    uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
    uc.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_M7)
    uc.mem_map(CODE_BASE, CODE_SIZE)
    uc.mem_map(DTCM_BASE, DTCM_SIZE)
    uc.mem_write(CODE_BASE, open(f'{build}/harness.bin', 'rb').read())

    st = {'on': False, 'cnt': {}, 'taken': 0, 'nxt': 0, 'reads': 0, 'sectors': 0}

    def on_code(u, addr, size, _):
        if not st['on']:
            return
        c = st['cnt']
        c[addr] = c.get(addr, 0) + 1
        if addr != st['nxt']:
            st['taken'] += 1
        st['nxt'] = addr + size

    def on_read(u, addr, size, _):
        buff = u.reg_read(UC_ARM_REG_R1)
        sector = u.reg_read(UC_ARM_REG_R2)
        count = u.reg_read(UC_ARM_REG_R3)
        if st['on']:
            st['reads'] += 1
            st['sectors'] += count
        for i in range(count):
            s = sector + i
            pg = pages.get(s // 128)
            sec = pg[(s % 128) * 512:(s % 128) * 512 + 512] if pg else bytes(512)
            u.mem_write(buff + i * 512, sec)
        u.reg_write(UC_ARM_REG_R0, 0)
        u.reg_write(UC_ARM_REG_PC, u.reg_read(UC_ARM_REG_LR))

    uc.hook_add(UC_HOOK_CODE, on_code, begin=CODE_BASE, end=CODE_BASE + 0x40000)
    dr = sym['disk_read']
    uc.hook_add(UC_HOOK_CODE, on_read, begin=dr, end=dr)

    def call(name, arg=None, count=False):
        if arg is not None:
            uc.reg_write(UC_ARM_REG_R0, arg)
        uc.reg_write(UC_ARM_REG_SP, STACK_TOP)
        uc.reg_write(UC_ARM_REG_LR, SENTINEL | 1)
        st.update(on=count, cnt={}, taken=0, nxt=0, reads=0, sectors=0)
        uc.emu_start(sym[name] | 1, SENTINEL)
        st['on'] = False
        return uc.reg_read(UC_ARM_REG_R0)

    def put_path(units):
        b = b''.join(struct.pack('<H', u) for u in units) + b'\0\0'
        uc.mem_write(PATH_BUF, b)

    put_path([ord('0'), ord(':')])
    assert call('h_mount', PATH_BUF) == 0, 'mount 失败'
    out = []
    for line in open(f'{prefix}_{tag}.paths'):
        p = line.split()
        pos, tgt, kind, units = int(p[0]), int(p[1]), p[2], [int(x, 16) for x in p[3:]]
        put_path(units)
        r = call('h_open', PATH_BUF, count=True)
        assert r == 0, f'h_open={r} pos={pos}'
        total = sum(st['cnt'].values())
        by = {}
        for a, n in st['cnt'].items():
            name = syms[bisect.bisect_right(addrs, a) - 1][1]
            by[name] = by.get(name, 0) + n
        out.append((pos, tgt, kind, total, st['taken'], st['reads'], st['sectors'], by))
        call('h_close')
    return out


def main():
    build, prefix = sys.argv[1], sys.argv[2]
    for tag, title in (('A', '镜像 A（长名）'), ('B', '镜像 B（8.3）')):
        res = run_image(build, prefix, tag)
        print(f'\n### {title}\n')
        print('| 位置 | 路径 | 目标数 | f_open 指令数(均值) | 最小 | 最大 | 已取分支数(均值) | disk_read 次数(均值) | 主要函数（均值指令占比）|')
        print('|---|---|---|---|---|---|---|---|---|')
        groups = {}
        for r in res:
            groups.setdefault((r[0], r[2]), []).append(r)
        for (pos, kind), rs in groups.items():
            n = len(rs)
            tots = [r[3] for r in rs]
            by = {}
            for r in rs:
                for k, v in r[7].items():
                    by[k] = by.get(k, 0) + v
            top = sorted(by.items(), key=lambda kv: -kv[1])[:4]
            tops = '，'.join(f'{k} {100 * v / sum(tots):.0f}%' for k, v in top)
            print(f'| {pos} | {kind} | {n} | {sum(tots) / n:.0f} | {min(tots)} | {max(tots)} | '
                  f'{sum(r[4] for r in rs) / n:.0f} | {sum(r[5] for r in rs) / n:.0f} | {tops} |')


main()
