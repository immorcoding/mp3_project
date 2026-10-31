/* 主机用 RAM 稀疏磁盘：实现 FatFs 的 diskio 接口，并统计 disk_read 调用。
 * 整个 FAT32 镜像都在内存里，等价于“所有目录扇区都命中扇区缓存”。 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "diskio.h"
#include "ramdisk.h"

#define PAGE_SECT 128u /* 64 KiB 一页，按需分配；未分配的页读出全 0 */

static unsigned char **g_pages;
static unsigned long g_total;
static unsigned long long g_calls, g_sectors;

/**
 * @brief 分配稀疏镜像
 * @param[in] total_sectors 总扇区数
 */
void ramdisk_init(unsigned long total_sectors)
{
    unsigned long i, pages = (g_total + PAGE_SECT - 1) / PAGE_SECT;
    for (i = 0; g_pages && i < pages; i++) free(g_pages[i]);
    free(g_pages);
    g_total = total_sectors;
    g_pages = calloc((total_sectors + PAGE_SECT - 1) / PAGE_SECT, sizeof(*g_pages));
    g_calls = g_sectors = 0;
}

/**
 * @brief 已分配页的字节数
 * @return 字节数
 */
unsigned long ramdisk_allocated_bytes(void)
{
    unsigned long n = 0, i, pages = (g_total + PAGE_SECT - 1) / PAGE_SECT;
    for (i = 0; i < pages; i++) if (g_pages[i]) n++;
    return n * PAGE_SECT * 512ul;
}

/**
 * @brief 清零计数
 */
void ramdisk_reset_counters(void) { g_calls = g_sectors = 0; }
/**
 * @brief disk_read 调用数
 * @return 次数
 */
unsigned long long ramdisk_calls(void) { return g_calls; }
/**
 * @brief disk_read 扇区数
 * @return 扇区数
 */
unsigned long long ramdisk_sectors(void) { return g_sectors; }

/**
 * @brief 驱动初始化，恒成功
 * @param[in] pdrv 物理驱动号（忽略）
 * @return 0 表示就绪
 */
DSTATUS disk_initialize(BYTE pdrv) { (void)pdrv; return 0; }
/**
 * @brief 驱动状态，恒就绪
 * @param[in] pdrv 物理驱动号（忽略）
 * @return 0 表示就绪
 */
DSTATUS disk_status(BYTE pdrv) { (void)pdrv; return 0; }

/**
 * @brief RAM 镜像读扇区并计数
 * @param[in] pdrv 物理驱动号（忽略）
 * @param[in] buff 输出缓冲
 * @param[in] sector 起始扇区
 * @param[in] count 扇区数
 * @return RES_OK
 */
DRESULT disk_read(BYTE pdrv, BYTE *buff, DWORD sector, UINT count)
{
    (void)pdrv;
    g_calls++;
    g_sectors += count;
    while (count) {
        unsigned char *p = g_pages[sector / PAGE_SECT];
        if (p) memcpy(buff, p + (sector % PAGE_SECT) * 512u, 512u);
        else memset(buff, 0, 512u);
        buff += 512; sector++; count--;
    }
    return RES_OK;
}

/**
 * @brief 写扇区
 * @param[in] pdrv 物理驱动号（忽略）
 * @param[in] buff 输入数据
 * @param[in] sector 起始扇区
 * @param[in] count 扇区数
 * @return RES_OK
 */
DRESULT disk_write(BYTE pdrv, const BYTE *buff, DWORD sector, UINT count)
{
    (void)pdrv;
    while (count) {
        unsigned char **pp = &g_pages[sector / PAGE_SECT];
        if (!*pp) *pp = calloc(PAGE_SECT, 512u);
        memcpy(*pp + (sector % PAGE_SECT) * 512u, buff, 512u);
        buff += 512; sector++; count--;
    }
    return RES_OK;
}

/**
 * @brief 控制命令，只实现 FatFs 格式化所需的几个
 * @param[in] pdrv 物理驱动号（忽略）
 * @param[in] cmd 命令
 * @param[in] buff 命令参数
 * @return RES_OK 或 RES_PARERR
 */
DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    (void)pdrv;
    switch (cmd) {
    case CTRL_SYNC: return RES_OK;
    case GET_SECTOR_COUNT: *(DWORD *)buff = g_total; return RES_OK;
    case GET_SECTOR_SIZE: *(WORD *)buff = 512; return RES_OK;
    case GET_BLOCK_SIZE: *(DWORD *)buff = 1; return RES_OK;
    default: return RES_PARERR;
    }
}

/* 导出非空页：[uint32 页号][64 KiB]，供 arm_emu/ 里的 Cortex-M7 指令级仿真读取。 */
/**
 * @brief 导出非空页
 * @param[in] file 输出文件名
 * @return 0 成功，-1 失败
 */
int ramdisk_dump(const char *file)
{
    unsigned long i, pages = (g_total + PAGE_SECT - 1) / PAGE_SECT;
    FILE *f = fopen(file, "wb");
    if (!f) return -1;
    for (i = 0; i < pages; i++) {
        if (!g_pages[i]) continue;
        fwrite(&i, 4, 1, f);
        fwrite(g_pages[i], PAGE_SECT * 512u, 1, f);
    }
    fclose(f);
    return 0;
}
