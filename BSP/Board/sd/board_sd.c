/**
  ******************************************************************************
  * @file    board_sd.c
  * @brief   本板唯一 SD 卡槽的 Device 装配和 Board Interface 实现。
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "BSP/Board/sd/board_sd.h"

#include <stddef.h>

#include "BSP/Board/board.h"
#include "BSP/Board/board_irq.h"
#include "BSP/Devices/sd/sd.h"
#include "BSP/Devices/sd/port/sd_port.h"
#include "main.h"

/**
  * @brief Board SD 热插拔非阻塞消抖时间。
  */
#define BOARD_SD_DEBOUNCE_MS 30U

/* Private types -------------------------------------------------------------*/
/**
  * @brief 本板唯一 SD 卡槽的私有 Handle。
  * @note  Device 保存通用 SD Card Device 状态；其余字段记录卡检测通知和
  *        非阻塞消抖状态。
  */
typedef struct
{
    SDCard_HandleTypeDef Device;
    volatile bool DetectPending; /**< FreeRTOS 下由任务通知替代。 */
    uint32_t DebounceStartMs;
    bool Debouncing;
    bool IRQRegistered;
} Board_SD_HandleTypeDef;

/* Private variables ---------------------------------------------------------*/
/** @brief 本板唯一 SD 卡槽对应的私有实例。 */
static Board_SD_HandleTypeDef hboard_sd;

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

/******************** FreeRTOS 移植替换区：开始 *******************************
 * FreeRTOS 下由任务通知替代本函数和 DetectPending。
 *****************************************************************************/
/**
  * @brief  在普通上下文原子地取出并清除一个检测通知。
  * @param  hsd Board SD 私有 Handle。
  * @retval true  本次成功取得一个待处理通知。
  * @retval false 当前没有待处理通知。
  */
static bool board_sd_take_detect_event(Board_SD_HandleTypeDef *hsd)
{
    uint32_t primask;
    bool pending;

    /*
     * 无通知是主循环中的常见路径。先做一次快速检查，避免每次调用
     * Board_SD_Process() 都短暂关闭全局中断；若检查后才到达中断，
     * 通知仍会保留到下一轮处理。
     */
    if (!hsd->DetectPending)
    {
        return false;
    }

    primask = __get_PRIMASK();
    __disable_irq();

    pending = hsd->DetectPending;
    hsd->DetectPending = false;

    /* 保持调用本函数前的中断总开关状态，不能无条件重新开启中断。 */
    if (primask == 0U)
    {
        __enable_irq();
    }

    return pending;
}
/******************** FreeRTOS 移植替换区：结束 *******************************/

/**
  * @brief  在 ISR 中记录一次 SD 卡检测边沿。
  * @param  context 注册时绑定的 Board SD 私有 Handle。
  * @param  source  触发回调的板级逻辑中断源。
  * @note   本函数只发布二值通知，不执行消抖、SDMMC 操作或日志输出。
  */
static void board_sd_detect_irq_cb(void *context, Board_IRQ_SourceTypeDef source)
{
    Board_SD_HandleTypeDef *hsd = (Board_SD_HandleTypeDef *)context;

    if ((hsd != NULL) && (source == BOARD_IRQ_SOURCE_SD_DETECT))
    {
        /**************** FreeRTOS 移植替换区：开始 ***************************
         * 改为 vTaskNotifyGiveFromISR()，并按返回值决定是否请求任务切换。
         *********************************************************************/
        hsd->DetectPending = true;
        /**************** FreeRTOS 移植替换区：结束 ***************************/
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
    Board_StatusTypeDef status;

    if (hboard_sd.IRQRegistered)
    {
        return BOARD_SD_ERROR;
    }

    hboard_sd.DetectPending = false;
    hboard_sd.DebounceStartMs = 0U;
    hboard_sd.Debouncing = false;

    if (SDCard_Port_Bind(&hboard_sd.Device) != SDCARD_OK)
    {
        return BOARD_SD_ERROR;
    }

    if (Board_IRQ_Register(BOARD_IRQ_SOURCE_SD_DETECT,
                           board_sd_detect_irq_cb,
                           &hboard_sd) != BOARD_OK)
    {
        return BOARD_SD_ERROR;
    }

    hboard_sd.IRQRegistered = true;
    status = board_sd_status(SDCard_Init(&hboard_sd.Device));

    if (status != BOARD_OK)
    {
        (void)Board_IRQ_Unregister(BOARD_IRQ_SOURCE_SD_DETECT);
        hboard_sd.IRQRegistered = false;
    }

    return status;
}

/**
  * @brief  解除卡检测 IRQ 绑定、释放 SDMMC 资源并恢复为 RESET。
  * @retval BOARD_OK    反初始化成功或原本已经处于 RESET。
  * @retval BOARD_SD_ERROR 底层反初始化失败。
  */
Board_StatusTypeDef Board_SD_DeInit(void)
{
    Board_StatusTypeDef status = BOARD_OK;

    if (hboard_sd.IRQRegistered)
    {
        if (Board_IRQ_Unregister(BOARD_IRQ_SOURCE_SD_DETECT) != BOARD_OK)
        {
            status = BOARD_SD_ERROR;
        }
        else
        {
            hboard_sd.IRQRegistered = false;
        }
    }

    if (SDCard_DeInit(&hboard_sd.Device) != SDCARD_OK)
    {
        status = BOARD_SD_ERROR;
    }

    hboard_sd.DetectPending = false;
    hboard_sd.DebounceStartMs = 0U;
    hboard_sd.Debouncing = false;
    return status;
}

/**
  * @brief  根据当前 SD_CD 电平刷新插拔状态。
  * @note   本函数不实现去抖且不得从 EXTI ISR 调用。正常热插拔路径应通过
  *         Board_SD_Process() 在检测电平稳定后间接调用。
  */
Board_StatusTypeDef Board_SD_Refresh(void)
{
    return board_sd_status(SDCard_Refresh(&hboard_sd.Device));
}

/**
  * @brief  推进一次 Board SD 热插拔事件处理。
  * @param  event 接收本次稳定状态变化。
  * @retval BOARD_OK 本次处理正常完成。
  * @retval BOARD_SD_ERROR 参数无效、Board SD 未初始化或刷新失败。
  * @note   每次取到新的检测通知都会重新开始 30 ms 消抖。只有连续稳定满
  *         30 ms 后才刷新 Device；状态未变化时仍返回 EVENT_NONE。
  */
Board_StatusTypeDef Board_SD_Process(Board_SD_EventTypeDef *event)
{
    Board_SD_StateTypeDef previous_state;
    Board_SD_StateTypeDef current_state;
    Board_StatusTypeDef status;

    if (event == NULL)
    {
        return BOARD_SD_ERROR;
    }

    *event = BOARD_SD_EVENT_NONE;

    if (!hboard_sd.IRQRegistered)
    {
        return BOARD_SD_ERROR;
    }

    /**************** FreeRTOS 移植替换区：开始 *******************************
     * Storage Task 使用 ulTaskNotifyTake() 等待通知，并在最后一次通知后等待
     * 30 ms；超时后继续执行下面的 Board_SD_Refresh()。
     *************************************************************************/
    if (board_sd_take_detect_event(&hboard_sd))
    {
        hboard_sd.DebounceStartMs = HAL_GetTick();
        hboard_sd.Debouncing = true;
        return BOARD_OK;
    }

    if (!hboard_sd.Debouncing)
    {
        return BOARD_OK;
    }

    if ((uint32_t)(HAL_GetTick() - hboard_sd.DebounceStartMs) < BOARD_SD_DEBOUNCE_MS)
    {
        return BOARD_OK;
    }

    hboard_sd.Debouncing = false;
    /**************** FreeRTOS 移植替换区：结束 *******************************/

    previous_state = Board_SD_GetState();
    status = Board_SD_Refresh();

    if (status != BOARD_OK)
    {
        return status;
    }

    current_state = Board_SD_GetState();

    if ((previous_state != current_state) &&
        (current_state == BOARD_SD_STATE_READY))
    {
        *event = BOARD_SD_EVENT_INSERTED;
    }
    else if ((previous_state != current_state) &&
             (current_state == BOARD_SD_STATE_NOT_PRESENT))
    {
        *event = BOARD_SD_EVENT_REMOVED;
    }

    return BOARD_OK;
}

/** @brief 返回 Board SD 当前持续状态。 */
Board_SD_StateTypeDef Board_SD_GetState(void)
{
    return board_sd_state(SDCard_GetState(&hboard_sd.Device));
}

/**
  * @brief  直接读取当前卡检测电平。
  * @note   本函数不更新 Board SD 状态；状态转换必须通过 Init/Refresh 完成。
  */
bool Board_SD_IsPresent(void)
{
    return SDCard_IsPresent(&hboard_sd.Device);
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

    if (SDCard_GetInfo(&hboard_sd.Device, &device_info) != SDCARD_OK)
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

    diagnostics->DeviceError = (uint32_t)hboard_sd.Device.ErrorCode;
    diagnostics->PortStatus = (uint32_t)hboard_sd.Device.LastPortStatus;
    return BOARD_OK;
}

/** @brief 从本板 SD 卡读取连续逻辑块。 */
Board_StatusTypeDef Board_SD_ReadBlocks(uint8_t *data,
                                        uint32_t start_block,
                                        uint32_t block_count)
{
    return board_sd_status(SDCard_ReadBlocks(&hboard_sd.Device,
                                              data,
                                              start_block,
                                              block_count));
}

/** @brief 向本板 SD 卡写入连续逻辑块。 */
Board_StatusTypeDef Board_SD_WriteBlocks(const uint8_t *data,
                                         uint32_t start_block,
                                         uint32_t block_count)
{
    return board_sd_status(SDCard_WriteBlocks(&hboard_sd.Device,
                                               data,
                                               start_block,
                                               block_count));
}

/** @brief 等待本板 SD 卡完成内部操作。 */
Board_StatusTypeDef Board_SD_Sync(void)
{
    return board_sd_status(SDCard_Sync(&hboard_sd.Device));
}
