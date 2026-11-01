/**
 * @file filesystem_ff_patch.h
 * @brief Filesystem Module 私有：本仓库对 FatFs `ff.c` 打的补丁所增函数的原型。
 *
 * @details
 *          `f_open_dirent` 由 `patches/fatfs/0001-open-by-dirent.patch` 加到 `ff.c`，
 *          原型只在这里声明，不进 `ff.h`（Vendor 头不改）。补丁未生效时链接失败，
 *          不会静默退回慢路径。见 ADR-0022、ARC-19、HAR-15。
 */

#ifndef FILESYSTEM_FF_PATCH_H
#define FILESYSTEM_FF_PATCH_H

#include "Middlewares/Third_Party/FatFs/src/ff.h"

/**
 * @brief 按目录项位置只读打开已有文件，不按名称搜索目录。
 * @param[out] fp 空白文件对象；失败时 `obj.fs` 被清零。
 * @param[in] path 卷盘符前缀，如 "0:/"，其后部分被忽略。
 * @param[in] dclst 所在文件夹的起始簇，根目录为 0。
 * @param[in] dofs 主（短名）目录项在文件夹内的字节偏移。
 * @param[in] sclust 该目录项应有的文件起始簇。
 * @param[in] fsize 该目录项应有的文件长度。
 * @return FR_OK 已打开并登记锁；FR_NO_FILE 记录与介质不符；其余为 FatFs 错误。
 */
FRESULT f_open_dirent(FIL *fp, const TCHAR *path, DWORD dclst, DWORD dofs, DWORD sclust, DWORD fsize);

#endif /* FILESYSTEM_FF_PATCH_H */
