/* 交叉编译（-mcpu=cortex-m7 -mthumb，与固件同一份 ff.c / cc936.c / ffconf.h）的最小驱动，
 * 由 emu.py 在 Unicorn 的 Cortex-M7 模型里逐指令执行，用来数 f_open 的真实 Thumb-2 指令数。
 * disk_read 的函数入口被 emu.py 截获（模拟“扇区缓存命中”），此处只放占位实现。 */
#include "ff.h"
#include "diskio.h"

static FATFS g_fs;
static FIL g_fil;

/**
 * @brief RAM 镜像读扇区并计数
 * @param[in] pdrv 物理驱动号（忽略）
 * @param[in] buff 输出缓冲
 * @param[in] sector 起始扇区
 * @param[in] count 扇区数
 * @return RES_OK
 */
__attribute__((noinline)) DRESULT disk_read(BYTE pdrv, BYTE *buff, DWORD sector, UINT count)
{
    (void)pdrv; (void)buff; (void)sector; (void)count;
    return RES_OK;
}
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
 * @brief 写扇区（占位）
 * @param[in] pdrv 物理驱动号（忽略）
 * @param[in] buff 输入数据（忽略）
 * @param[in] sector 起始扇区（忽略）
 * @param[in] count 扇区数（忽略）
 * @return RES_OK
 */
DRESULT disk_write(BYTE pdrv, const BYTE *buff, DWORD sector, UINT count)
{ (void)pdrv; (void)buff; (void)sector; (void)count; return RES_OK; }
/**
 * @brief 控制命令，只实现 FatFs 格式化所需的几个
 * @param[in] pdrv 物理驱动号（忽略）
 * @param[in] cmd 命令
 * @param[in] buff 命令参数
 * @return RES_OK 或 RES_PARERR
 */
DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff) { (void)pdrv; (void)cmd; (void)buff; return RES_OK; }

/**
 * @brief 挂载卷
 * @param[in] p 卷路径
 * @return FRESULT
 */
int h_mount(const TCHAR *p) { return f_mount(&g_fs, p, 1); }
/**
 * @brief 只读打开文件，供仿真统计 f_open
 * @param[in] p UTF-16 路径
 * @return FRESULT
 */
int h_open(const TCHAR *p) { return f_open(&g_fil, p, FA_READ); }
/**
 * @brief 关闭 h_open 打开的文件
 * @return FRESULT
 */
int h_close(void) { return f_close(&g_fil); }
