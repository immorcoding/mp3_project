/**
 * @file bsp_driver_user_diskio.c
 * @brief 无后端时安全失败的弱默认定义；Service 同名强定义接管。
 */

#include "FATFS/Target/bsp_driver_user_diskio.h"

/**
 * @brief 无后端时保持未初始化，不访问介质。
 * @param lun 驱动内编号，默认实现不解释此值。
 * @retval STA_NOINIT 固定返回未初始化。
 * @note Service 以同名强定义接管；弱属性只在此定义处出现。
 */
__attribute__((weak)) DSTATUS BSP_USER_DISKIO_Init(BYTE lun)
{
    (void)lun;
    return STA_NOINIT;
}

/**
 * @brief 无后端时报告未就绪，不伪造设备存在。
 * @param lun 驱动内编号，默认实现不解释此值。
 * @retval STA_NOINIT 固定返回未初始化。
 */
__attribute__((weak)) DSTATUS BSP_USER_DISKIO_GetStatus(BYTE lun)
{
    (void)lun;
    return STA_NOINIT;
}

/**
 * @brief 缺少后端时安全拒绝读取，不修改输出。
 * @param lun 驱动内编号。
 * @param data 调用者输出缓冲，默认实现不访问。
 * @param sector 起始逻辑扇区，默认实现不访问介质。
 * @param count 请求扇区数。
 * @retval RES_NOTRDY 固定返回未就绪，不伪报数据有效。
 */
__attribute__((weak)) DRESULT BSP_USER_DISKIO_ReadBlocks(BYTE lun,
                                                         BYTE *data,
                                                         DWORD sector,
                                                         UINT count)
{
    (void)lun;
    (void)data;
    (void)sector;
    (void)count;
    return RES_NOTRDY;
}

/**
 * @brief 缺少后端时安全拒绝写入，不伪报落盘。
 * @param lun 驱动内编号。
 * @param data 调用者输入缓冲，默认实现不访问。
 * @param sector 起始逻辑扇区，默认实现不访问介质。
 * @param count 请求扇区数。
 * @retval RES_NOTRDY 固定返回未就绪，不产生任何擦写。
 */
__attribute__((weak)) DRESULT BSP_USER_DISKIO_WriteBlocks(BYTE lun,
                                                          const BYTE *data,
                                                          DWORD sector,
                                                          UINT count)
{
    (void)lun;
    (void)data;
    (void)sector;
    (void)count;
    return RES_NOTRDY;
}

/**
 * @brief 缺少后端时拒绝控制请求。
 * @param lun 驱动内编号。
 * @param command 控制命令，默认实现不执行。
 * @param data 命令缓冲，默认实现不访问。
 * @retval RES_NOTRDY 固定返回未就绪，CTRL_SYNC 也不能伪报成功。
 */
__attribute__((weak)) DRESULT BSP_USER_DISKIO_Ioctl(BYTE lun, BYTE command, void *data)
{
    (void)lun;
    (void)command;
    (void)data;
    return RES_NOTRDY;
}
