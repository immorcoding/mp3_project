/**
 ******************************************************************************
 * @file    filesystem_text.h
 * @brief   Filesystem Service 的文本编码查表 Interface：GBK 双字节码转 Unicode。
 ******************************************************************************
 */

#ifndef FILESYSTEM_TEXT_H
#define FILESYSTEM_TEXT_H

#include <stdint.h>

uint16_t Service_Filesystem_GbkToUnicode(uint16_t gbk_code);

#endif /* FILESYSTEM_TEXT_H */
