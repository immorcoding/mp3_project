/**
 * @file catalog_name.h
 * @brief 曲目显示名纯函数：从曲库 UTF-8 相对路径提取去掉 MP3 扩展名的文件主名。
 * @note  不依赖 FreeRTOS、FatFs 或 HAL，可在主机上直接测试。规则见 STOR-1、STOR-15。
 */

#ifndef CATALOG_NAME_H
#define CATALOG_NAME_H

#include <stdbool.h>
#include <stdint.h>

#define CATALOG_NAME_BYTES 252U /* 显示名缓冲容量，含结尾 '\0'；覆盖全部合法文件主名（<= 251 B）。 */

bool catalog_has_mp3_extension(const char *name, uint32_t length);

bool catalog_display_name(const char *path,
                                  uint32_t path_bytes,
                                  char *out,
                                  uint32_t out_bytes);

#endif /* CATALOG_NAME_H */
