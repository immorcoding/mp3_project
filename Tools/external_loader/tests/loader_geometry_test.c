/**
 * @file loader_geometry_test.c
 * @brief 验证外部烧录算法对 CubeProgrammer 外部地址范围的转换规则。
 */

#include <assert.h>
#include <stdint.h>

#include "loader_geometry.h"

/**
 * @brief 验证地址必须属于 W25Q256 的完整 32 MiB 映射窗口。
 * @return 成功时返回 0。
 */
int main(void)
{
    uint32_t offset = 0U;

    assert(Loader_GeometryToOffset(LOADER_FLASH_BASE_ADDRESS, 1U, &offset) != 0);
    assert(Loader_GeometryToOffset(LOADER_FLASH_BASE_ADDRESS + LOADER_FLASH_SIZE_BYTES - 1U,
                                   1U,
                                   &offset) != 0);
    assert(Loader_GeometryToOffset(LOADER_FLASH_BASE_ADDRESS - 1U, 1U, &offset) == 0);
    assert(Loader_GeometryToOffset(LOADER_FLASH_BASE_ADDRESS + LOADER_FLASH_SIZE_BYTES,
                                   1U,
                                   &offset) == 0);
    assert(Loader_GeometryToOffset(LOADER_FLASH_BASE_ADDRESS + LOADER_FLASH_SIZE_BYTES - 8U,
                                   9U,
                                   &offset) == 0);

    return 0;
}
