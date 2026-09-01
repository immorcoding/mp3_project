/**
 * @file loader_geometry.h
 * @brief W25Q256 的 CubeProgrammer 地址窗口与物理偏移转换。
 */

#ifndef LOADER_GEOMETRY_H
#define LOADER_GEOMETRY_H

#include <stdint.h>

#define LOADER_FLASH_BASE_ADDRESS 0x90000000UL
#define LOADER_FLASH_SIZE_BYTES   0x02000000UL

int Loader_GeometryToOffset(uint32_t address, uint32_t size, uint32_t *offset);

#endif /* LOADER_GEOMETRY_H */
