/**
  ******************************************************************************
  * @file    sd_port.c
  * @brief   STM32H743 SDMMC1 到 SD Card Device Interface 的 Adapter。
  *
  * @details
  *          本文件是唯一了解 hsd1、SDMMC1、PC7 卡检测极性和 STM32 HAL
  *          状态码的自维护 SD 代码。CubeMX 生成的 HAL_SD_MspInit/DeInit
  *          仍负责时钟和复用引脚配置；本 Adapter 直接调用可返回状态的
  *          HAL_SD_Init()，避免 MX_SDMMC1_SD_Init() 失败后进入 Error_Handler()。
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "BSP/Devices/sd/port/sd_port.h"

#include <stddef.h>

#include "main.h"
#include "sdmmc.h"

/* Private functions ---------------------------------------------------------*/
/**
  * @brief  读取本板低有效 SD_CD 信号。
  * @param  context 当前未使用；保留以满足通用 Port Interface。
  */
static bool sdcard_port_is_present(const void *context)
{
    (void)context;
    return HAL_GPIO_ReadPin(SD_CD_GPIO_Port, SD_CD_Pin) == GPIO_PIN_RESET;
}

/**
  * @brief  将 HAL 返回值转换为 Device 可理解的 Port 状态。
  * @param  native_status 当前 SDK 函数的立即返回状态。
  * @note   HAL 原始 ErrorCode 继续保留在 hsd1 中，不跨越 Port Seam。
  */
static SDCard_PortStatusTypeDef sdcard_port_status(int32_t native_status)
{
    HAL_StatusTypeDef hal_status = (HAL_StatusTypeDef)native_status;

    switch (hal_status)
    {
        case HAL_OK:
            return SDCARD_PORT_OK;

        case HAL_BUSY:
            return SDCARD_PORT_BUSY;

        case HAL_TIMEOUT:
            return SDCARD_PORT_TIMEOUT;

        case HAL_ERROR:
        default:
            return SDCARD_PORT_ERROR;
    }
}

/**
  * @brief  配置 hsd1 并初始化 SDMMC1 和 SD 卡。
  * @note   参数与当前 CubeMX SDMMC1 配置保持一致。以后在 CubeMX 修改总线
  *         宽度、时钟沿或分频时，必须同步检查本函数。
  */
static SDCard_PortStatusTypeDef sdcard_port_init(void *context)
{
    SD_HandleTypeDef *hal_sd = (SD_HandleTypeDef *)context;

    if (hal_sd == NULL)
    {
        return SDCARD_PORT_ERROR;
    }

    if (!sdcard_port_is_present(context))
    {
        return SDCARD_PORT_NOT_PRESENT;
    }

    /*
     * 不调用 CubeMX 的 void MX_SDMMC1_SD_Init()：该函数在失败时直接进入
     * Error_Handler()，不适合可移除介质。这里保留同一份硬件参数，但让
     * HAL_StatusTypeDef 能沿 Port -> Device -> Board 正常返回。
     */
    hal_sd->Instance = SDMMC1;
    hal_sd->Init.ClockEdge = SDMMC_CLOCK_EDGE_RISING;
    hal_sd->Init.ClockPowerSave = SDMMC_CLOCK_POWER_SAVE_DISABLE;
    hal_sd->Init.BusWide = SDMMC_BUS_WIDE_4B;
    hal_sd->Init.HardwareFlowControl = SDMMC_HARDWARE_FLOW_CONTROL_DISABLE;
    hal_sd->Init.ClockDiv = 0u;

    HAL_StatusTypeDef hal_status = HAL_SD_Init(hal_sd);
    SDCard_PortStatusTypeDef status = sdcard_port_status((int32_t)hal_status);

    /* 初始化期间被拔卡时，优先报告介质不存在而不是泛化 HAL_ERROR。 */
    if ((hal_status != HAL_OK) && (!sdcard_port_is_present(context)))
    {
        status = SDCARD_PORT_NOT_PRESENT;
    }

    if (hal_status != HAL_OK)
    {
        /* 尽力清理 MSP 时钟和 GPIO；保留清理前捕获的原始失败信息。 */
        (void)HAL_SD_DeInit(hal_sd);
    }

    return status;
}

/** @brief 反初始化 SDMMC1 和对应 MSP 资源。 */
static SDCard_PortStatusTypeDef sdcard_port_deinit(void *context)
{
    SD_HandleTypeDef *hal_sd = (SD_HandleTypeDef *)context;

    if (hal_sd == NULL)
    {
        return SDCARD_PORT_ERROR;
    }

    return sdcard_port_status((int32_t)HAL_SD_DeInit(hal_sd));
}

/** @brief 将 HAL 卡信息转换为 Device 的逻辑块信息。 */
static SDCard_PortStatusTypeDef sdcard_port_get_info(void *context,
                                                     SDCard_InfoTypeDef *info)
{
    SD_HandleTypeDef *hal_sd = (SD_HandleTypeDef *)context;
    HAL_SD_CardInfoTypeDef hal_info;

    if ((hal_sd == NULL) || (info == NULL))
    {
        return SDCARD_PORT_ERROR;
    }

    if (!sdcard_port_is_present(context))
    {
        return SDCARD_PORT_NOT_PRESENT;
    }

    HAL_StatusTypeDef hal_status = HAL_SD_GetCardInfo(hal_sd, &hal_info);
    SDCard_PortStatusTypeDef status = sdcard_port_status((int32_t)hal_status);

    if (status != SDCARD_PORT_OK)
    {
        return status;
    }

    info->BlockCount = hal_info.LogBlockNbr;
    info->BlockSize = hal_info.LogBlockSize;
    info->CapacityBytes = (uint64_t)hal_info.LogBlockNbr * (uint64_t)hal_info.LogBlockSize;
    info->CardType = hal_info.CardType;
    info->CardVersion = hal_info.CardVersion;

    return status;
}

/** @brief 使用 HAL 轮询接口读取一个或多个逻辑块。 */
static SDCard_PortStatusTypeDef sdcard_port_read_blocks(void *context,
                                                        uint8_t *data,
                                                        uint32_t start_block,
                                                        uint32_t block_count,
                                                        uint32_t timeout_ms)
{
    SD_HandleTypeDef *hal_sd = (SD_HandleTypeDef *)context;

    if ((hal_sd == NULL) || (data == NULL) || (block_count == 0u))
    {
        return SDCARD_PORT_ERROR;
    }

    if (!sdcard_port_is_present(context))
    {
        return SDCARD_PORT_NOT_PRESENT;
    }

    HAL_StatusTypeDef hal_status = HAL_SD_ReadBlocks(hal_sd,
                                                     data,
                                                     start_block,
                                                     block_count,
                                                     timeout_ms);
    SDCard_PortStatusTypeDef status = sdcard_port_status((int32_t)hal_status);

    if ((status != SDCARD_PORT_OK) &&
        (!sdcard_port_is_present(context)))
    {
        status = SDCARD_PORT_NOT_PRESENT;
    }

    return status;
}

/** @brief 使用 HAL 轮询接口写入一个或多个逻辑块。 */
static SDCard_PortStatusTypeDef sdcard_port_write_blocks(void *context,
                                                         const uint8_t *data,
                                                         uint32_t start_block,
                                                         uint32_t block_count,
                                                         uint32_t timeout_ms)
{
    SD_HandleTypeDef *hal_sd = (SD_HandleTypeDef *)context;

    if ((hal_sd == NULL) || (data == NULL) || (block_count == 0u))
    {
        return SDCARD_PORT_ERROR;
    }

    if (!sdcard_port_is_present(context))
    {
        return SDCARD_PORT_NOT_PRESENT;
    }

    HAL_StatusTypeDef hal_status = HAL_SD_WriteBlocks(hal_sd,
                                                      data,
                                                      start_block,
                                                      block_count,
                                                      timeout_ms);
    SDCard_PortStatusTypeDef status = sdcard_port_status((int32_t)hal_status);

    if ((status != SDCARD_PORT_OK) &&
        (!sdcard_port_is_present(context)))
    {
        status = SDCARD_PORT_NOT_PRESENT;
    }

    return status;
}

/**
  * @brief  轮询卡状态，直到回到 HAL_SD_CARD_TRANSFER。
  * @details
  *         HAL 的阻塞读写完成数据搬运后，卡内部仍可能处于 PROGRAMMING。
  *         本函数让 Device 成功返回前确认介质真正可以接受下一条命令。
  */
static SDCard_PortStatusTypeDef sdcard_port_sync(void *context,
                                                 uint32_t timeout_ms)
{
    SD_HandleTypeDef *hal_sd = (SD_HandleTypeDef *)context;
    uint32_t start_tick;

    if (hal_sd == NULL)
    {
        return SDCARD_PORT_ERROR;
    }

    start_tick = HAL_GetTick();

    while (true)
    {
        if (!sdcard_port_is_present(context))
        {
            return SDCARD_PORT_NOT_PRESENT;
        }

        HAL_SD_CardStateTypeDef card_state = HAL_SD_GetCardState(hal_sd);

        if (card_state == HAL_SD_CARD_TRANSFER)
        {
            return SDCARD_PORT_OK;
        }

        if ((card_state == HAL_SD_CARD_ERROR) ||
            (card_state == HAL_SD_CARD_DISCONNECTED))
        {
            return SDCARD_PORT_ERROR;
        }

        /* 无符号时间差可安全跨越 HAL_GetTick() 的 32 位回绕点。 */
        if ((uint32_t)(HAL_GetTick() - start_tick) >= timeout_ms)
        {
            return SDCARD_PORT_TIMEOUT;
        }
    }
}

/* Private variables ---------------------------------------------------------*/
/** @brief 当前 PCB 的 SDMMC1 Port Adapter。 */
static const SDCard_PortOpsTypeDef sdcard_port_ops = {
    .IsPresent = sdcard_port_is_present,
    .Init = sdcard_port_init,
    .DeInit = sdcard_port_deinit,
    .GetInfo = sdcard_port_get_info,
    .ReadBlocks = sdcard_port_read_blocks,
    .WriteBlocks = sdcard_port_write_blocks,
    .Sync = sdcard_port_sync
};

/* Exported functions --------------------------------------------------------*/
SDCard_StatusTypeDef SDCard_Port_Bind(SDCard_HandleTypeDef *hsdcard)
{
    if (hsdcard == NULL)
    {
        return SDCARD_ERROR;
    }

    hsdcard->PortOps = &sdcard_port_ops;
    hsdcard->PortContext = &hsd1;
    return SDCARD_OK;
}
