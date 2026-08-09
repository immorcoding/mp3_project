/**
  ******************************************************************************
  * @file    sd_stm32_hal_adapter.c
  * @brief   STM32H743 SDMMC1 到 SD Card Device Interface 的 Adapter。
  *
  * @details
  *          本文件只了解 STM32 HAL SD/GPIO 类型和状态，不固定使用 hsd1
  *          或某个卡检测引脚。具体 HAL Handle、检测 GPIO 和有效电平由
  *          Platform 通过 SDCard_STM32HALAdapterTypeDef 注入。
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "Adapters/sd/sd_stm32_hal_adapter.h"

#include <stddef.h>

/* Private functions ---------------------------------------------------------*/
/**
  * @brief  根据 Context 中的 GPIO 和有效电平读取卡检测状态。
  * @param  context 必须指向 SDCard_STM32HALAdapterTypeDef。
  * @retval true 检测电平与 PresentState 相同。
  * @retval false Context 无效或当前没有检测到介质。
  */
static bool sd_stm32_hal_is_present(const void *context)
{
    const SDCard_STM32HALAdapterTypeDef *adapter = context;

    if ((adapter == NULL) || (adapter->DetectPort == NULL))
    {
        return false;
    }

    return HAL_GPIO_ReadPin(adapter->DetectPort,
                            adapter->DetectPin) == adapter->PresentState;
}

/**
  * @brief  将 HAL 返回值转换为 Device 可理解的 Port 状态。
  * @param  native_status 当前 SDK 函数的立即返回状态。
  * @return 与 HAL_OK/BUSY/TIMEOUT/ERROR 对应的归一化 Port 状态。
  * @note   HAL 原始 ErrorCode 继续保留在 hsd1 中，不跨越 Port Seam。
  */
static SDCard_PortStatusTypeDef sd_stm32_hal_status(int32_t native_status)
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
  * @param  context 必须指向包含 SD Handle 和卡检测 GPIO 的 Adapter Context。
  * @retval SDCARD_PORT_OK 控制器和介质初始化成功。
  * @retval SDCARD_PORT_NOT_PRESENT 初始化前或失败后检测到介质不存在。
  * @retval SDCARD_PORT_ERROR Context 无效或 HAL 初始化失败。
  * @note   参数与当前 CubeMX SDMMC1 配置保持一致。以后在 CubeMX 修改总线
  *         宽度、时钟沿或分频时，必须同步检查本函数。
  */
static SDCard_PortStatusTypeDef sd_stm32_hal_init(void *context)
{
    SDCard_STM32HALAdapterTypeDef *adapter = context;

    if ((adapter == NULL) || (adapter->Handle == NULL))
    {
        return SDCARD_PORT_ERROR;
    }

    SD_HandleTypeDef *hal_sd = adapter->Handle;

    if (!sd_stm32_hal_is_present(context))
    {
        return SDCARD_PORT_NOT_PRESENT;
    }

    /*
     * 不调用 CubeMX 的 void MX_SDMMC1_SD_Init()：该函数在失败时直接进入
     * Error_Handler()，不适合可移除介质。这里保留同一份硬件参数，但让
     * HAL_StatusTypeDef 能沿 Port -> Device -> Platform 正常返回。
     */
    hal_sd->Instance = SDMMC1;
    hal_sd->Init.ClockEdge = SDMMC_CLOCK_EDGE_RISING;
    hal_sd->Init.ClockPowerSave = SDMMC_CLOCK_POWER_SAVE_DISABLE;
    hal_sd->Init.BusWide = SDMMC_BUS_WIDE_4B;
    hal_sd->Init.HardwareFlowControl = SDMMC_HARDWARE_FLOW_CONTROL_ENABLE;
    hal_sd->Init.ClockDiv = 0u;

    HAL_StatusTypeDef hal_status = HAL_SD_Init(hal_sd);
    SDCard_PortStatusTypeDef status = sd_stm32_hal_status((int32_t)hal_status);

    /* 初始化期间被拔卡时，优先报告介质不存在而不是泛化 HAL_ERROR。 */
    if ((hal_status != HAL_OK) && (!sd_stm32_hal_is_present(context)))
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

/**
  * @brief  反初始化当前 Context 指向的 SD Handle 和对应 MSP 资源。
  * @param  context 必须指向有效 SDCard_STM32HALAdapterTypeDef。
  * @return HAL_SD_DeInit() 转换后的归一化 Port 状态。
  */
static SDCard_PortStatusTypeDef sd_stm32_hal_deinit(void *context)
{
    SDCard_STM32HALAdapterTypeDef *adapter = context;

    if ((adapter == NULL) || (adapter->Handle == NULL))
    {
        return SDCARD_PORT_ERROR;
    }

    return sd_stm32_hal_status((int32_t)HAL_SD_DeInit(adapter->Handle));
}

/**
  * @brief  将 HAL 卡信息转换为 Device 的逻辑块信息。
  * @param  context 必须指向有效 SDCard_STM32HALAdapterTypeDef。
  * @param  info 接收归一化容量、块大小、卡类型和版本信息。
  * @retval SDCARD_PORT_OK 信息读取并转换成功。
  * @retval SDCARD_PORT_NOT_PRESENT 当前没有检测到介质。
  * @retval SDCARD_PORT_ERROR 参数或 HAL 查询失败。
  */
static SDCard_PortStatusTypeDef sd_stm32_hal_get_info(
    void *context,
    SDCard_InfoTypeDef *info)
{
    SDCard_STM32HALAdapterTypeDef *adapter = context;
    HAL_SD_CardInfoTypeDef hal_info;

    if ((adapter == NULL) || (adapter->Handle == NULL) || (info == NULL))
    {
        return SDCARD_PORT_ERROR;
    }

    SD_HandleTypeDef *hal_sd = adapter->Handle;

    if (!sd_stm32_hal_is_present(context))
    {
        return SDCARD_PORT_NOT_PRESENT;
    }

    HAL_StatusTypeDef hal_status = HAL_SD_GetCardInfo(hal_sd, &hal_info);
    SDCard_PortStatusTypeDef status = sd_stm32_hal_status((int32_t)hal_status);

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

/**
  * @brief  使用 HAL 轮询接口读取一个或多个逻辑块。
  * @param  context 必须指向有效 SDCard_STM32HALAdapterTypeDef。
  * @param  data 接收数据的缓冲区。
  * @param  start_block 起始逻辑块编号。
  * @param  block_count 连续读取的逻辑块数量。
  * @param  timeout_ms HAL 数据阶段超时时间。
  * @return HAL 结果转换后的 Port 状态；传输中拔卡优先返回 NOT_PRESENT。
  */
static SDCard_PortStatusTypeDef sd_stm32_hal_read_blocks(
    void *context,
    uint8_t *data,
    uint32_t start_block,
    uint32_t block_count,
    uint32_t timeout_ms)
{
    SDCard_STM32HALAdapterTypeDef *adapter = context;

    if ((adapter == NULL) ||
        (adapter->Handle == NULL) ||
        (data == NULL) ||
        (block_count == 0u))
    {
        return SDCARD_PORT_ERROR;
    }

    SD_HandleTypeDef *hal_sd = adapter->Handle;

    if (!sd_stm32_hal_is_present(context))
    {
        return SDCARD_PORT_NOT_PRESENT;
    }

    HAL_StatusTypeDef hal_status = HAL_SD_ReadBlocks(hal_sd,
                                                     data,
                                                     start_block,
                                                     block_count,
                                                     timeout_ms);
    SDCard_PortStatusTypeDef status = sd_stm32_hal_status((int32_t)hal_status);

    if ((status != SDCARD_PORT_OK) &&
        (!sd_stm32_hal_is_present(context)))
    {
        status = SDCARD_PORT_NOT_PRESENT;
    }

    return status;
}

/**
  * @brief  使用 HAL 轮询接口写入一个或多个逻辑块。
  * @param  context 必须指向有效 SDCard_STM32HALAdapterTypeDef。
  * @param  data 提供数据的只读缓冲区。
  * @param  start_block 起始逻辑块编号。
  * @param  block_count 连续写入的逻辑块数量。
  * @param  timeout_ms HAL 数据阶段超时时间。
  * @return HAL 结果转换后的 Port 状态；传输中拔卡优先返回 NOT_PRESENT。
  */
static SDCard_PortStatusTypeDef sd_stm32_hal_write_blocks(
    void *context,
    const uint8_t *data,
    uint32_t start_block,
    uint32_t block_count,
    uint32_t timeout_ms)
{
    SDCard_STM32HALAdapterTypeDef *adapter = context;

    if ((adapter == NULL) ||
        (adapter->Handle == NULL) ||
        (data == NULL) ||
        (block_count == 0u))
    {
        return SDCARD_PORT_ERROR;
    }

    SD_HandleTypeDef *hal_sd = adapter->Handle;

    if (!sd_stm32_hal_is_present(context))
    {
        return SDCARD_PORT_NOT_PRESENT;
    }

    HAL_StatusTypeDef hal_status = HAL_SD_WriteBlocks(hal_sd,
                                                      data,
                                                      start_block,
                                                      block_count,
                                                      timeout_ms);
    SDCard_PortStatusTypeDef status = sd_stm32_hal_status((int32_t)hal_status);

    if ((status != SDCARD_PORT_OK) &&
        (!sd_stm32_hal_is_present(context)))
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
  * @param  context 必须指向有效 SDCard_STM32HALAdapterTypeDef。
  * @param  timeout_ms 等待介质回到 TRANSFER 状态的最长时间。
  * @retval SDCARD_PORT_OK 介质已经回到可传输状态。
  * @retval SDCARD_PORT_NOT_PRESENT 等待期间介质被拔出。
  * @retval SDCARD_PORT_ERROR HAL 报告错误或断开状态。
  * @retval SDCARD_PORT_TIMEOUT 等待时间达到上限。
  */
static SDCard_PortStatusTypeDef sd_stm32_hal_sync(
    void *context,
    uint32_t timeout_ms)
{
    SDCard_STM32HALAdapterTypeDef *adapter = context;
    uint32_t start_tick;

    if ((adapter == NULL) || (adapter->Handle == NULL))
    {
        return SDCARD_PORT_ERROR;
    }

    SD_HandleTypeDef *hal_sd = adapter->Handle;
    start_tick = HAL_GetTick();

    while (true)
    {
        if (!sd_stm32_hal_is_present(context))
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
/**
  * @brief SD Card Device 使用的 STM32 HAL SDMMC Port 操作表。
  * @note  具体 SD Handle、卡检测 GPIO 和极性通过 PortContext 注入。
  */
static const SDCard_PortOpsTypeDef sd_stm32_hal_ops = {
    .IsPresent = sd_stm32_hal_is_present,
    .Init = sd_stm32_hal_init,
    .DeInit = sd_stm32_hal_deinit,
    .GetInfo = sd_stm32_hal_get_info,
    .ReadBlocks = sd_stm32_hal_read_blocks,
    .WriteBlocks = sd_stm32_hal_write_blocks,
    .Sync = sd_stm32_hal_sync
};

/* Exported functions --------------------------------------------------------*/
/**
  * @brief  将 STM32 HAL Adapter 的 Ops 和具体实例安装到 Device Handle。
  * @param  hsdcard 待绑定的 SD Card Device Handle。
  * @param  adapter Platform 层装配的 HAL Handle 和卡检测配置。
  * @retval SDCARD_OK 绑定成功。
  * @retval SDCARD_ERROR Device Handle、Adapter Context 或必需资源无效。
  * @note   本函数只完成依赖装配，不初始化 SDMMC1，也不访问 SD 卡。
  */
SDCard_StatusTypeDef SDCard_STM32HALAdapter_Bind(
    SDCard_HandleTypeDef *hsdcard,
    SDCard_STM32HALAdapterTypeDef *adapter)
{
    if ((hsdcard == NULL) ||
        (adapter == NULL) ||
        (adapter->Handle == NULL) ||
        (adapter->DetectPort == NULL) ||
        (adapter->DetectPin == 0u))
    {
        return SDCARD_ERROR;
    }

    hsdcard->PortOps = &sd_stm32_hal_ops;
    hsdcard->PortContext = adapter;
    return SDCARD_OK;
}
