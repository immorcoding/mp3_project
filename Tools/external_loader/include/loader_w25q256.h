/**
 * @file loader_w25q256.h
 * @brief W25Q256 QSPI 最小擦写与映射接口。
 */

#ifndef LOADER_W25Q256_H
#define LOADER_W25Q256_H

#include <stdint.h>

int Loader_W25Q256_Initialize(void);
int Loader_W25Q256_Program(uint32_t offset, const uint8_t *data, uint32_t size);
int Loader_W25Q256_EraseSector(uint32_t offset);
int Loader_W25Q256_EraseChip(void);
int Loader_W25Q256_EnableMemoryMappedMode(void);
int Loader_W25Q256_DisableMemoryMappedMode(void);

#endif /* LOADER_W25Q256_H */
