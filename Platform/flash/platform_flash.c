/**
  ******************************************************************************
  * @file    platform_flash.c
  * @brief   当前 PCB W25Q256 与 CubeMX QSPI 的 Platform 装配实现。
  *
  * @details
  *          本 Module 长期持有 W25Qxx Device 和 STM32 HAL QSPI Adapter Context，
 *          并把 CubeMX 管理的 hqspi 和本板预期 JEDEC ID 注入其中。当前对上只
 *          公开启动校验后的 JEDEC ID；FTL、逻辑扇区、写入、擦除与内存映射均
 *          尚未接入。
  ******************************************************************************
  */

#include "Platform/flash/platform_flash.h"
#include "Platform/flash/platform_flash_config.h"

#include <stddef.h>

#include "Adapters/stm32_hal/w25qxx_qspi/w25qxx_qspi_stm32_hal_adapter.h"
#include "Components/w25qxx/w25qxx.h"
#include "quadspi.h"

/** @brief 当前 PCB W25Q256 对应的最小 JEDEC 兼容性要求。 */
static const W25Qxx_ExpectedJedecIDTypeDef platform_flash_expected_jedec_id = {
    .ManufacturerID = PLATFORM_FLASH_EXPECTED_MANUFACTURER_ID,
    .CapacityID = PLATFORM_FLASH_EXPECTED_CAPACITY_ID
};

/** @brief 当前 PCB 唯一 W25Q256 对应的 W25Qxx Device 实例。 */
static W25Qxx_HandleTypeDef hplatform_flash = {
    .ExpectedJedecID = &platform_flash_expected_jedec_id
};

/**
 * @brief 当前 PCB QSPI 外设的 STM32 HAL Adapter Context。
 * @note  hqspi 的创建、GPIO、时钟和 NVIC 均由 CubeMX 管理；Platform 只借用
 *        它并决定它服务于哪一个板级 Device 实例。
 */
static W25Qxx_QSPI_STM32HALAdapterTypeDef hplatform_flash_adapter = {
    .Handle = &hqspi,
    .TimeoutMs = PLATFORM_FLASH_QSPI_TIMEOUT_MS
};

/**
 * @brief  绑定当前 PCB QSPI Adapter 并校验 W25Q256 的 JEDEC ID。
 * @retval PLATFORM_OK W25Qxx Device 已进入 READY，JEDEC ID 已校验并缓存。
 * @retval PLATFORM_FLASH_ERROR Adapter 绑定或芯片识别失败。
 * @note   本函数仅用于启动阶段的一次同步硬件识别，必须在 CubeMX 已完成
 *         MX_QUADSPI_Init() 后调用。当前不注册 QSPI 完成回调，也不使用已启用
 *         的 QUADSPI IRQ；后续异步页编程与自动状态轮询会另行定义状态机。
 */
Platform_StatusTypeDef Platform_Flash_Init(void)
{
    if (W25Qxx_QSPI_STM32HALAdapter_Bind(
            &hplatform_flash,
            &hplatform_flash_adapter) != W25QXX_OK)
    {
        return PLATFORM_FLASH_ERROR;
    }

    return (W25Qxx_Init(&hplatform_flash) == W25QXX_OK) ?
               PLATFORM_OK :
               PLATFORM_FLASH_ERROR;
}

/**
 * @brief  获取当前 PCB W25Q256 在初始化阶段缓存的 JEDEC ID。
 * @param  jedec_id 接收三字节芯片标识的有效地址。
 * @retval PLATFORM_OK 已返回缓存的 JEDEC ID。
 * @retval PLATFORM_FLASH_ERROR 参数无效，或 Flash 尚未成功初始化。
 * @note   本函数不会重新访问 QSPI；它只把 W25Qxx Device 的可移植标识类型转换
 *         成 Platform 对上的板级表达，避免上层包含 Component 头文件。
 */
Platform_StatusTypeDef Platform_Flash_GetJedecID(
    Platform_Flash_JedecIDTypeDef *jedec_id)
{
    W25Qxx_JedecIDTypeDef device_id;

    if (jedec_id == NULL)
    {
        return PLATFORM_FLASH_ERROR;
    }

    if (W25Qxx_GetJedecID(&hplatform_flash, &device_id) != W25QXX_OK)
    {
        return PLATFORM_FLASH_ERROR;
    }

    jedec_id->ManufacturerID = device_id.ManufacturerID;
    jedec_id->MemoryType = device_id.MemoryType;
    jedec_id->CapacityID = device_id.CapacityID;
    return PLATFORM_OK;
}
