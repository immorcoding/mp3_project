/**
 * @file loader_geometry.c
 * @brief W25Q256 的 CubeProgrammer 地址范围检查实现。
 */

#include <stddef.h>

#include "loader_geometry.h"

/**
 * @brief 将映射窗口内的地址和长度转换成 NOR 物理偏移。
 * @param[in] address CubeProgrammer 传入的映射地址。
 * @param[in] size 本次操作的字节数，必须非零。
 * @param[out] offset 成功时写入相对 W25Q256 地址 0 的偏移。
 * @retval 1 地址范围完全位于 32 MiB Flash 内。
 * @retval 0 参数为空、长度为零或范围越界。
 */
int Loader_GeometryToOffset(uint32_t address, uint32_t size, uint32_t *offset)
{
    uint64_t end_address;

    if ((offset == NULL) || (size == 0U) || (address < LOADER_FLASH_BASE_ADDRESS))
    {
        return 0;
    }

    end_address = (uint64_t)address + (uint64_t)size;
    if (end_address > ((uint64_t)LOADER_FLASH_BASE_ADDRESS + LOADER_FLASH_SIZE_BYTES))
    {
        return 0;
    }

    *offset = address - LOADER_FLASH_BASE_ADDRESS;
    return 1;
}
