/**
  ******************************************************************************
  * @file    board_sd.c
  * @brief   本板唯一 SD 卡槽的 Device 装配和 Board Interface 实现。
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "System/Log/log.h"
#include "BSP/Board/sd/board_sd.h"

#include <stddef.h>

#include "BSP/Board/board.h"
#include "BSP/Devices/sd/sd.h"
#include "BSP/Devices/sd/port/sd_port.h"

/* Private variables ---------------------------------------------------------*/
/**
  * @brief 本板唯一 SD 卡槽对应的私有 Device 实例。
  * @note  static 只限制 C 代码链接可见性；调试构建仍可在调试器中观察其
  *        State、ErrorCode 和 LastPortStatus。
  */
static SDCard_HandleTypeDef hboard_sd;

/* Private functions ---------------------------------------------------------*/
/** @brief 将 Device 返回值转换为 Board 层统一状态码。 */
static Board_StatusTypeDef board_sd_status(SDCard_StatusTypeDef status)
{
    return (status == SDCARD_OK) ? BOARD_OK : BOARD_SD_ERROR;
}

/** @brief 将 Device 的持续状态转换为 Board 对外状态。 */
static Board_SD_StateTypeDef board_sd_state(SDCard_StateTypeDef state)
{
    switch (state)
    {
        case SDCARD_STATE_RESET:
            return BOARD_SD_STATE_RESET;

        case SDCARD_STATE_NOT_PRESENT:
            return BOARD_SD_STATE_NOT_PRESENT;

        case SDCARD_STATE_READY:
            return BOARD_SD_STATE_READY;

        case SDCARD_STATE_BUSY:
            return BOARD_SD_STATE_BUSY;

        case SDCARD_STATE_ERROR:
        default:
            return BOARD_SD_STATE_ERROR;
    }
}

/* Exported functions --------------------------------------------------------*/
/**
  * @brief  绑定本板 SDMMC Adapter 并初始化当前介质。
  * @retval BOARD_OK
  *         Board SD 正常完成状态同步；没有插卡时也返回成功，此时状态为
  *         BOARD_SD_STATE_NOT_PRESENT。
  * @retval BOARD_SD_ERROR Adapter 绑定或介质初始化失败。
  */
Board_StatusTypeDef Board_SD_Init(void)
{
    if (SDCard_Port_Bind(&hboard_sd) != SDCARD_OK)
    {
        return BOARD_SD_ERROR;
    }

    return board_sd_status(SDCard_Init(&hboard_sd));
}

/**
  * @brief  释放 SDMMC 资源并把 Board SD 恢复为 RESET。
  * @retval BOARD_OK    反初始化成功或原本已经处于 RESET。
  * @retval BOARD_SD_ERROR 底层反初始化失败。
  */
Board_StatusTypeDef Board_SD_DeInit(void)
{
    return board_sd_status(SDCard_DeInit(&hboard_sd));
}

/**
  * @brief  根据当前 SD_CD 电平刷新插拔状态。
  * @note   本函数不实现去抖，不得从 EXTI ISR 调用。APP 或未来 Storage task
  *         应在确认检测电平稳定后调用。
  */
Board_StatusTypeDef Board_SD_Refresh(void)
{
    Board_SD_StateTypeDef previous_state = Board_SD_GetState();
    Board_StatusTypeDef status = board_sd_status(SDCard_Refresh(&hboard_sd));
    Board_SD_StateTypeDef current_state = Board_SD_GetState();

    if ((status != BOARD_OK) || (current_state == previous_state))
    {
        return status;
    }

    /*
     * Polling only discovers state transitions. Logging on the edge prevents
     * every Refresh call from printing the same READY message.
     */
    if (current_state == BOARD_SD_STATE_READY)
    {
        Board_SD_InfoTypeDef info;

        if (Board_SD_GetInfo(&info) == BOARD_OK)
        {
            uint32_t capacity_mb = (uint32_t)(info.CapacityBytes / (1024ULL * 1024ULL));

            (void)LOG_Printf(LOG_LEVEL_INFO,
                             "SD",
                             "card ready: %lu MB, block size: %lu",
                             (unsigned long)capacity_mb,
                             (unsigned long)info.BlockSize);
        }
    }
    else if (current_state == BOARD_SD_STATE_NOT_PRESENT)
    {
        (void)LOG_Printf(LOG_LEVEL_INFO, "SD", "card removed");
    }

    return status;
}

/** @brief 返回 Board SD 当前持续状态。 */
Board_SD_StateTypeDef Board_SD_GetState(void)
{
    return board_sd_state(SDCard_GetState(&hboard_sd));
}

/**
  * @brief  直接读取当前卡检测电平。
  * @note   本函数不更新 Board SD 状态；状态转换必须通过 Init/Refresh 完成。
  */
bool Board_SD_IsPresent(void)
{
    return SDCard_IsPresent(&hboard_sd);
}

/**
  * @brief  将 Device 缓存的介质信息复制到调用者对象。
  * @param  info 接收 Board SD 信息快照的指针。
  * @retval BOARD_OK    信息有效且复制成功。
  * @retval BOARD_SD_ERROR 参数为空或介质未就绪。
  */
Board_StatusTypeDef Board_SD_GetInfo(Board_SD_InfoTypeDef *info)
{
    SDCard_InfoTypeDef device_info;

    if (info == NULL)
    {
        return BOARD_SD_ERROR;
    }

    if (SDCard_GetInfo(&hboard_sd, &device_info) != SDCARD_OK)
    {
        return BOARD_SD_ERROR;
    }

    info->CapacityBytes = device_info.CapacityBytes;
    info->BlockCount = device_info.BlockCount;
    info->BlockSize = device_info.BlockSize;
    info->CardType = device_info.CardType;
    info->CardVersion = device_info.CardVersion;
    return BOARD_OK;
}

/**
  * @brief  复制最近一次 Device/Port 错误诊断。
  * @param  diagnostics 接收诊断快照的指针。
  */
Board_StatusTypeDef Board_SD_GetDiagnostics(Board_SD_DiagnosticsTypeDef *diagnostics)
{
    if (diagnostics == NULL)
    {
        return BOARD_SD_ERROR;
    }

    diagnostics->DeviceError = (uint32_t)hboard_sd.ErrorCode;
    diagnostics->PortStatus = (uint32_t)hboard_sd.LastPortStatus;
    return BOARD_OK;
}

/** @brief 从本板 SD 卡读取连续逻辑块。 */
Board_StatusTypeDef Board_SD_ReadBlocks(uint8_t *data,
                                        uint32_t start_block,
                                        uint32_t block_count)
{
    return board_sd_status(SDCard_ReadBlocks(&hboard_sd,
                                              data,
                                              start_block,
                                              block_count));
}

/** @brief 向本板 SD 卡写入连续逻辑块。 */
Board_StatusTypeDef Board_SD_WriteBlocks(const uint8_t *data,
                                         uint32_t start_block,
                                         uint32_t block_count)
{
    return board_sd_status(SDCard_WriteBlocks(&hboard_sd,
                                               data,
                                               start_block,
                                               block_count));
}

/** @brief 等待本板 SD 卡完成内部操作。 */
Board_StatusTypeDef Board_SD_Sync(void)
{
    return board_sd_status(SDCard_Sync(&hboard_sd));
}
