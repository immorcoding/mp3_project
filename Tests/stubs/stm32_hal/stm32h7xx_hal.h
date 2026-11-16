/**
 * @file stm32h7xx_hal.h
 * @brief 主机测试用的 STM32 HAL 最小替身：只提供 SD 相关代码编进主机测试所需的类型与常量。
 *
 * @details
 *          `FATFS/Target/bsp_driver_sd.h`、`Adapters/stm32_hal/sd/sd_stm32_hal_adapter.h` 和两个 IRQ Adapter 头
 *          都包含 `stm32h7xx_hal.h`。主机上没有 HAL，这里只定义它们用到的名字。`SDMMC_DATATIMEOUT` 是 FatFs 的
 *          `sd_diskio.c` 选 `SD_TIMEOUT` 的依据，值取自 `Drivers/STM32H7xx_HAL_Driver/Inc/stm32h7xx_ll_sdmmc.h`
 *          的默认定义；`Tests/flash_ftl/CMakeLists.txt` 在配置时核对两处一致，HAL 升级改了这个值会让配置失败。
 */
#ifndef HOST_STUB_STM32H7XX_HAL_H
#define HOST_STUB_STM32H7XX_HAL_H

#include <stdbool.h>
#include <stdint.h>

/** @brief SD 数据超时的 HAL 默认值（毫秒计时的软件超时与硬件超时共用同一个常量名），即 0xFFFFFFFF。 */
#define SDMMC_DATATIMEOUT ((uint32_t)0xFFFFFFFFU)

/** @brief `BSP_SD_CardInfo` 展开成的 HAL 卡信息类型（只保留 Service 用到的字段）。 */
typedef struct
{
    uint32_t CardType;
    uint32_t CardVersion;
    uint32_t Class;
    uint32_t RelCardAdd;
    uint32_t BlockNbr;
    uint32_t BlockSize;
    uint32_t LogBlockNbr;
    uint32_t LogBlockSize;
} HAL_SD_CardInfoTypeDef;

/** @brief HAL SD 句柄的占位类型：主机测试只传指针，不读字段。 */
typedef struct
{
    int Placeholder;
} SD_HandleTypeDef;

/** @brief GPIO 端口寄存器块的占位类型。 */
typedef struct
{
    int Placeholder;
} GPIO_TypeDef;

/** @brief GPIO 电平。 */
typedef enum
{
    GPIO_PIN_RESET = 0,
    GPIO_PIN_SET
} GPIO_PinState;

#endif /* HOST_STUB_STM32H7XX_HAL_H */
