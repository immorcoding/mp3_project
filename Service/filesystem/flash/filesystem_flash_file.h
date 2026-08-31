/**
 * @file filesystem_flash_file.h
 * @brief Filesystem Module 私有文件槽与卷生命周期接缝。
 */
#ifndef FILESYSTEM_FLASH_FILE_H
#define FILESYSTEM_FLASH_FILE_H

#include <stdbool.h>

void filesystem_flash_file_set_mounted(bool mounted);
bool filesystem_flash_file_is_open(void);

#endif /* FILESYSTEM_FLASH_FILE_H */
