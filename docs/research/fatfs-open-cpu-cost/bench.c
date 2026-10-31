/* 主机实测：内存扇区下 f_open 的 CPU 耗时。见同目录 run.ps1 与 ../fatfs-open-cpu-cost.md。
 * 以 -DUNICODE 编译：FatFs 的 integer.h 在 _WIN32 下引入 tchar.h，否则 TCHAR 为 char，
 * 与固件的 _LFN_UNICODE=1（TCHAR=WCHAR=UTF-16）不符。 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ff.h"
#include "diskio.h"
#include "ramdisk.h"
#ifdef FAST_UPPER
int fastupper_selfcheck(void);
#endif

/* 镜像参数：约 30 GB 卡（#143 的 30436 MB），32 KiB 簇。 */
#define IMG_SECTORS (30436ul * 2048ul)
#define CLUSTER_BYTES 32768ul

static int g_n = 12000;
static const char *g_dump; /* 第 3 个参数：导出前缀（目录/前缀），供 ARM 仿真用 */
static FATFS g_fs;
static FIL g_fil;

typedef struct {
    WCHAR name[80];   /* 文件名（UTF-16） */
    WCHAR alt[13];    /* 8.3 别名（readdir 取得） */
    int entries;      /* 该文件占用的目录项数 */
    unsigned long long entries_before; /* Music 目录里它之前的目录项总数 */
} track_t;
static track_t *g_tr;

static unsigned long long g_rng = 0x9E3779B97F4A7C15ull;
/**
 * @brief xorshift 伪随机数，固定种子保证镜像可复现
 * @return 伪随机数
 */
static unsigned rnd(void)
{
    g_rng ^= g_rng << 13; g_rng ^= g_rng >> 7; g_rng ^= g_rng << 17;
    return (unsigned)(g_rng >> 16);
}

#include "hostclock.h"
#define now_us hostclock_us

static WCHAR g_pool[600];
static int g_pool_n;
/**
 * @brief 构建可转换为 GBK 的 CJK 字符池
 */
static void build_pool(void)
{
    WCHAR c;
    for (c = 0x4E00; c < 0x9FA5 && g_pool_n < 600; c++)
        if (ff_convert(c, 0) != 0 && (rnd() % 17) == 0) g_pool[g_pool_n++] = c;
}

static const char ASCII_SET[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

/**
 * @brief 取一个随机字符
 * @param[in] cjk_pct CJK 字符所占百分比
 * @return UTF-16 字符
 */
static WCHAR rch(int cjk_pct)
{
    if ((int)(rnd() % 100) < cjk_pct) return g_pool[rnd() % (unsigned)g_pool_n];
    return (WCHAR)ASCII_SET[rnd() % (sizeof ASCII_SET - 1)];
}

/* 形如“歌手名 - 曲名.mp3”：总长（含 " - " 与 ".mp3"）在 [22,59] 均匀，
 * 平均约 40.5 码元，LFN 项数 ceil(L/13) 平均约 3.4，即约 4.4 项/文件。 */
/**
 * @brief 生成形如“歌手名 - 曲名.mp3”的长文件名
 * @param[in] o 输出的 UTF-16 名字
 */
static void gen_lfn_name(WCHAR *o)
{
    int total = 22 + (int)(rnd() % 38);
    int body = total - 7, artist = 2 + (int)(rnd() % 6), i, n = 0;
    if (artist > body - 2) artist = body - 2;
    for (i = 0; i < artist; i++) o[n++] = rch(75);
    o[n++] = ' '; o[n++] = '-'; o[n++] = ' ';
    for (i = 0; i < body - artist; i++) {
        WCHAR c = rch(70);
        if (c < 0x80 && (rnd() % 6) == 0 && i > 0 && i < body - artist - 1) c = ' ';
        o[n++] = c;
    }
    o[n++] = '.'; o[n++] = 'm'; o[n++] = 'p'; o[n++] = '3'; o[n] = 0;
}

/**
 * @brief 生成全大写 8.3 名字
 * @param[in] o 输出的 UTF-16 名字
 * @param[in] i 序号
 */
static void gen_sfn_name(WCHAR *o, int i)
{
    char b[32]; int k;
    sprintf(b, "TR%06d.MP3", i);
    for (k = 0; b[k]; k++) o[k] = (WCHAR)b[k];
    o[k] = 0;
}

/**
 * @brief UTF-16 串长度
 * @param[in] s 以 0 结尾的串
 * @return 码元数
 */
static int wlen(const WCHAR *s) { int n = 0; while (s[n]) n++; return n; }
/**
 * @brief 拼接 ASCII 前缀与 UTF-16 串
 * @param[in] d 输出
 * @param[in] prefix ASCII 前缀
 * @param[in] s UTF-16 串
 */
static void wcat(WCHAR *d, const char *prefix, const WCHAR *s)
{
    int n = 0, k;
    for (k = 0; prefix[k]; k++) d[n++] = (WCHAR)prefix[k];
    for (k = 0; s[k]; k++) d[n++] = s[k];
    d[n] = 0;
}

/**
 * @brief 创建 FAT32 镜像并按顺序创建全部文件
 * @param[in] lfn_mode 1 为长名，0 为 8.3
 * @return 0 成功，-1 失败
 */
static int make_image(int lfn_mode)
{
    static BYTE work[32768];
    FRESULT r;
    int i;
    ramdisk_init(IMG_SECTORS);
    r = f_mount(&g_fs, L"0:", 0);
    if (r) { printf("mount %d\n", r); return -1; }
    r = f_mkfs(L"0:", FM_FAT32 | FM_SFD, CLUSTER_BYTES, work, sizeof work);
    if (r) { printf("mkfs %d\n", r); return -1; }
    r = f_mount(&g_fs, L"0:", 1);
    if (r) { printf("mount1 %d\n", r); return -1; }
    r = f_mkdir(L"0:/Music");
    if (r) { printf("mkdir %d\n", r); return -1; }
    g_rng = 0x9E3779B97F4A7C15ull;
    for (i = 0; i < g_n; i++) {
        WCHAR nm[80], path[100];
        for (;;) {
            if (lfn_mode) gen_lfn_name(nm); else gen_sfn_name(nm, i);
            wcat(path, "0:/Music/", nm);
            r = f_open(&g_fil, path, FA_CREATE_NEW | FA_WRITE);
            if (r == FR_OK) break;
            if (r != FR_EXIST) { printf("create %d at %d\n", r, i); return -1; }
        }
        f_close(&g_fil);
        memcpy(g_tr[i].name, nm, (size_t)(wlen(nm) + 1) * sizeof(WCHAR));
    }
    return 0;
}

/* 用 f_readdir 按目录顺序取出名字与 altname，并校验顺序 = 创建顺序。 */
/**
 * @brief 用 f_readdir 校验目录顺序并取出 altname 与目录项数
 * @param[in] lfn_mode 1 为长名，0 为 8.3
 * @return 0 成功，-1 失败
 */
static int scan_image(int lfn_mode)
{
    DIR d; FILINFO fi; FRESULT r; int i = 0;
    unsigned long long eb = 0;
    r = f_opendir(&d, L"0:/Music");
    if (r) return -1;
    for (;;) {
        r = f_readdir(&d, &fi);
        if (r || !fi.fname[0]) break;
        if (i >= g_n) { printf("多余项\n"); return -1; }
        if (wlen(fi.fname) != wlen(g_tr[i].name) ||
            memcmp(fi.fname, g_tr[i].name, (size_t)wlen(g_tr[i].name) * sizeof(WCHAR))) {
            printf("顺序与创建不一致 @%d\n", i); return -1;
        }
        memcpy(g_tr[i].alt, fi.altname, sizeof g_tr[i].alt);
        g_tr[i].entries = lfn_mode ? ((wlen(g_tr[i].name) + 12) / 13 + 1) : 1;
        g_tr[i].entries_before = eb;
        eb += (unsigned long long)g_tr[i].entries;
        i++;
    }
    if (i != g_n) { printf("计数 %d != %d\n", i, g_n); return -1; }
    printf("- 目录项总数 %llu（%.3f 项/文件），目录 %.2f MiB = %llu 扇区 = %llu 个簇\n",
           eb, (double)eb / g_n, (double)eb * 32 / 1048576.0, eb / 16,
           (eb * 32 + CLUSTER_BYTES - 1) / CLUSTER_BYTES);
    printf("- 镜像实际占用内存 %.1f MiB（稀疏页）\n", ramdisk_allocated_bytes() / 1048576.0);
    return 0;
}

/**
 * @brief qsort 的 double 比较函数
 * @param[in] a 左值
 * @param[in] b 右值
 * @return 比较结果
 */
static int cmpd(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

static int g_reps = 101;
static double g_hook_us; /* 每次 disk_read 自身（memcpy 512B + 计数）的实测均值 */

/**
 * @brief 测量 disk_read 钩子自身开销
 */
static void measure_hook(void)
{
    static BYTE buf[512]; int i; double t0;
    ramdisk_init(IMG_SECTORS);
    for (i = 0; i < 256; i++) disk_write(0, buf, 3000 + (DWORD)i, 1); /* 让这些页真实存在，测的是 memcpy */
    for (i = 0; i < 1000; i++) disk_read(0, buf, 3000 + (DWORD)i, 1);
    t0 = now_us();
    for (i = 0; i < 200000; i++) disk_read(0, buf, 3000 + (DWORD)(i & 255), 1);
    g_hook_us = (now_us() - t0) / 200000.0;
    ramdisk_reset_counters();
}

/* 返回中位数（µs）；calls/sects 为单次 f_open 的 disk_read 次数/扇区数。 */
/**
 * @brief 测一条路径的 f_open 耗时中位数
 * @param[in] path UTF-16 路径
 * @param[in] calls 输出单次 disk_read 调用数
 * @param[in] sects 输出单次扇区数
 * @param[in] minv 输出最小值
 * @return 中位数，微秒
 */
static double time_open(const WCHAR *path, unsigned long long *calls, unsigned long long *sects, double *minv)
{
    double *v = malloc(sizeof(double) * (size_t)g_reps), med; int i;
    FRESULT r;
    for (i = 0; i < 5; i++) { f_open(&g_fil, path, FA_READ); f_close(&g_fil); }
    ramdisk_reset_counters();
    r = f_open(&g_fil, path, FA_READ);
    if (r) { printf("open 失败 %d\n", r); exit(1); }
    f_close(&g_fil);
    *calls = ramdisk_calls(); *sects = ramdisk_sectors();
    for (i = 0; i < g_reps; i++) {
        double t0 = now_us(), t1;
        f_open(&g_fil, path, FA_READ);
        t1 = now_us();
        f_close(&g_fil);
        v[i] = t1 - t0;
    }
    qsort(v, (size_t)g_reps, sizeof(double), cmpd);
    med = v[g_reps / 2]; *minv = v[0];
    free(v);
    return med;
}

static const int POS[] = {1, 10, 100, 500, 1000, 2000, 3000, 4000, 6000, 8000, 10000, 12000};
/* 仿真用位置较少（逐指令仿真慢）。 */
static const int EPOS[] = {1, 100, 1000, 6000, 12000};
#define WIN_HOST 40 /* 每个位置取“该位置及其前 K-1 个文件”共 K 个目标求均值，避免单个名字的字符构成偏差 */
#define WIN_EMU 8

/**
 * @brief 导出镜像页与路径文件，供 ARM 仿真使用
 * @param[in] tag 镜像标签
 * @param[in] lfn_mode 1 为长名镜像
 */
static void dump_paths(const char *tag, int lfn_mode)
{
    char fn[512]; FILE *f; size_t k; int j, t;
    if (!g_dump) return;
    sprintf(fn, "%s_%s.bin", g_dump, tag); ramdisk_dump(fn);
    sprintf(fn, "%s_%s.paths", g_dump, tag); f = fopen(fn, "w");
    for (k = 0; k < sizeof EPOS / sizeof EPOS[0]; k++) {
        int p = EPOS[k], lo = p - WIN_EMU + 1;
        if (p > g_n) continue;
        if (lo < 1) lo = 1;
        for (t = lo; t <= p; t++) {
            WCHAR path[120]; int pass;
            for (pass = 0; pass < (lfn_mode ? 2 : 1); pass++) {
                wcat(path, "0:/Music/", pass ? g_tr[t - 1].alt : g_tr[t - 1].name);
                fprintf(f, "%d %d %s", p, t, pass ? "alt" : (lfn_mode ? "lfn" : "sfn"));
                for (j = 0; path[j]; j++) fprintf(f, " %04x", path[j]);
                fprintf(f, "\n");
            }
        }
    }
    fclose(f);
}

/**
 * @brief 测一个位置的 K 个目标并打印一行
 * @param[in] p 位置（1 起）
 * @param[in] label 路径类别
 * @param[in] alt 1 表示用 altname 路径
 */
static void run_one(int p, const char *label, int alt)
{
    int lo = p - WIN_HOST + 1, t, n = 0;
    double sum = 0, mn = 1e30, mx = 0, sumhook = 0, sumc = 0, sumeb = 0, sums = 0;
    if (lo < 1) lo = 1;
    for (t = lo; t <= p; t++) {
        WCHAR path[120]; unsigned long long c, s; double minv, med;
        wcat(path, "0:/Music/", alt ? g_tr[t - 1].alt : g_tr[t - 1].name);
        med = time_open(path, &c, &s, &minv);
        sum += med; sumhook += med - (double)c * g_hook_us; sumc += (double)c; sums += (double)s;
        sumeb += (double)g_tr[t - 1].entries_before;
        if (med < mn) mn = med;
        if (med > mx) mx = med;
        n++;
    }
    printf("| %d | %s | %d | %.0f | %.1f | %.1f | %.1f | %.0f | %.0f | %.1f |\n", p, label, n, sumeb / n,
           sum / n, mn, mx, sumc / n, sums / n, sumhook / n);
}

/**
 * @brief 测一个镜像的全部位置并打印表格
 * @param[in] title 标题
 * @param[in] lfn_mode 1 为长名镜像
 */
static void run_image(const char *title, int lfn_mode)
{
    size_t k;
    printf("\n### %s\n\n", title);
    printf("| 位置(1起) | 路径 | 目标数 | 其前目录项数(均值) | 均值 us | 最小 us | 最大 us | disk_read 次数(均值) | 扇区数(均值) | 扣钩子后均值 us |\n|---|---|---|---|---|---|---|---|---|---|\n");
    for (k = 0; k < sizeof POS / sizeof POS[0]; k++) {
        if (POS[k] > g_n) continue;
        run_one(POS[k], lfn_mode ? "长名" : "8.3 名", 0);
        if (lfn_mode) run_one(POS[k], "altname", 1);
    }
}

/**
 * @brief 入口
 * @param[in] argc 参数个数
 * @param[in] argv 参数：N、重复次数、导出前缀
 * @return 0 成功
 */
int main(int argc, char **argv)
{
    int i, k;
    if (argc > 1) g_n = atoi(argv[1]);
    if (argc > 2) g_reps = atoi(argv[2]);
    if (argc > 3) g_dump = argv[3];
    setvbuf(stdout, NULL, _IOLBF, 0);
    hostclock_pin();
    g_tr = calloc((size_t)g_n, sizeof(track_t));
    build_pool();
#ifdef FAST_UPPER
    printf("fastupper 自检（0..65535 与原 ff_wtoupper 逐值比较）: 不一致 %d 个\n", fastupper_selfcheck());
#endif
    measure_hook();
    printf("## 参数\n\n- N=%d，簇 %lu B，镜像 %lu 扇区，每个目标重复 %d 次取中位，每位置取其前 K=%d 个文件（含自身）求均值\n- disk_read 钩子单次 %.3f us（RAM memcpy 512B + 页表查找 + 计数）\n",
           g_n, CLUSTER_BYTES, IMG_SECTORS, g_reps, WIN_HOST, g_hook_us);

    printf("\n## 镜像 A：长文件名\n\n");
    if (make_image(1) || scan_image(1)) return 1;
    for (i = 0; i < 3; i++) {
        printf("- 样例 %d：长名 %d 码元，altname =", i, wlen(g_tr[i].name));
        for (k = 0; g_tr[i].alt[k]; k++) printf(" %04X", g_tr[i].alt[k]);
        printf("\n");
    }
    dump_paths("A", 1);
    run_image("镜像 A 计量（长名，约 4.4 项/文件）", 1);

    printf("\n## 镜像 B：全 8.3\n\n");
    if (make_image(0) || scan_image(0)) return 1;
    dump_paths("B", 0);
    run_image("镜像 B 计量（8.3，1 项/文件）", 0);
    return 0;
}
