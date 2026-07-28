/**
  ******************************************************************************
  * @file    platform_sd.c
  * @brief   本板唯一 SD 卡槽的 Device 装配和 Platform Interface 实现。
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "Platform/sd/platform_sd.h"

#include <stddef.h>

#include "Platform/platform.h"
#include "Components/sd/sd.h"
#include "Adapters/irq/irq_stm32_hal_adapter.h"
#include "Adapters/sd/sd_stm32_hal_adapter.h"
#include "main.h"
#include "sdmmc.h"

/**
  * @brief Platform SD 热插拔非阻塞消抖时间。
  */
#define PLATFORM_SD_DEBOUNCE_MS 30U

/* Private types -------------------------------------------------------------*/
/**
  * @brief 本板唯一 SD 卡槽的私有 Handle。
  * @note  Device 保存通用 SD Card Device 状态；其余字段记录卡检测通知和
  *        非阻塞消抖状态。
  */
typedef struct
{
    SDCard_HandleTypeDef Device; /**< 通用 SD Card Device 状态机和诊断。 */
    IRQ_STM32HALAdapter_CallbackTypeDef DetectIRQCallback; /**< 调用者持有的卡检测 IRQ 链表节点。 */
    volatile bool DetectPending; /**< ISR 发布的二值通知；FreeRTOS 下由任务通知替代。 */
    uint32_t DebounceStartMs;     /**< 最近一次检测边沿开始消抖的 HAL tick。 */
    bool Debouncing;              /**< 当前是否正在等待检测输入保持稳定。 */
    bool IRQRegistered;           /**< DetectIRQCallback 是否已注册到 IRQ Adapter。 */
} Platform_SD_HandleTypeDef;

/* Private variables ---------------------------------------------------------*/
/** @brief 本板唯一 SD 卡槽对应的私有实例。 */
static Platform_SD_HandleTypeDef hplatform_sd;

/**
  * @brief 本板 SDMMC1 Handle 和低有效卡检测信号的 Adapter Context。
  * @note  Platform 拥有本装配关系；hsd1 和 GPIO Port 由 CubeMX/Vendor
  *        代码持有，本对象只保存借用引用。
  */
static SDCard_STM32HALAdapterTypeDef hplatform_sd_adapter = {
    .Handle = &hsd1,
    .DetectPort = SD_CD_GPIO_Port,
    .DetectPin = SD_CD_Pin,
    .PresentState = GPIO_PIN_RESET
};

/* Private functions ---------------------------------------------------------*/
/**
  * @brief  将 Device 返回值转换为 Platform 层统一状态码。
  * @param  status SD Card Device 的立即返回状态。
  * @retval PLATFORM_OK Device 调用成功。
  * @retval PLATFORM_SD_ERROR Device 调用失败。
  */
static Platform_StatusTypeDef platform_sd_status(SDCard_StatusTypeDef status)
{
    return (status == SDCARD_OK) ? PLATFORM_OK : PLATFORM_SD_ERROR;
}

/**
  * @brief  将 Device 的持续状态转换为 Platform 对外状态。
  * @param  state SD Card Device 当前生命周期状态。
  * @return 与输入语义对应的 Platform SD 状态；未知值归为 ERROR。
  */
static Platform_SD_StateTypeDef platform_sd_state(SDCard_StateTypeDef state)
{
    switch (state)
    {
        case SDCARD_STATE_RESET:
            return PLATFORM_SD_STATE_RESET;

        case SDCARD_STATE_NOT_PRESENT:
            return PLATFORM_SD_STATE_NOT_PRESENT;

        case SDCARD_STATE_READY:
            return PLATFORM_SD_STATE_READY;

        case SDCARD_STATE_BUSY:
            return PLATFORM_SD_STATE_BUSY;

        case SDCARD_STATE_ERROR:
        default:
            return PLATFORM_SD_STATE_ERROR;
    }
}

/******************** FreeRTOS 移植替换区：开始 *******************************
 * FreeRTOS 下由任务通知替代本函数和 DetectPending。
 *****************************************************************************/
/**
  * @brief  在普通上下文原子地取出并清除一个检测通知。
  * @param  hsd Platform SD 私有 Handle。
  * @retval true  本次成功取得一个待处理通知。
  * @retval false 当前没有待处理通知。
  */
static bool platform_sd_take_detect_event(Platform_SD_HandleTypeDef *hsd)
{
    uint32_t primask;
    bool pending;

    /*
     * 无通知是主循环中的常见路径。先做一次快速检查，避免每次调用
     * Platform_SD_Process() 都短暂关闭全局中断；若检查后才到达中断，
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
  * @param  gpio_pin 触发回调的 STM32 HAL GPIO_Pin 位掩码。
  * @param  context 注册时绑定的 Platform SD 私有 Handle。
  * @note   本函数只发布二值通知，不执行消抖、SDMMC 操作或日志输出。
  */
static void platform_sd_detect_irq_cb(uint16_t gpio_pin, void *context)
{
    Platform_SD_HandleTypeDef *hsd = (Platform_SD_HandleTypeDef *)context;

    if ((hsd != NULL) && ((gpio_pin & SD_CD_Pin) != 0U))
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
  * @retval PLATFORM_OK
  *         Platform SD 正常完成状态同步；没有插卡时也返回成功，此时状态为
  *         PLATFORM_SD_STATE_NOT_PRESENT。
  * @retval PLATFORM_SD_ERROR Adapter 绑定或介质初始化失败。
  */
Platform_StatusTypeDef Platform_SD_Init(void)
{
    Platform_StatusTypeDef status;

    if (hplatform_sd.IRQRegistered)
    {
        return PLATFORM_SD_ERROR;
    }

    hplatform_sd.DetectPending = false;
    hplatform_sd.DebounceStartMs = 0U;
    hplatform_sd.Debouncing = false;

    if (SDCard_STM32HALAdapter_Bind(
            &hplatform_sd.Device,
            &hplatform_sd_adapter) != SDCARD_OK)
    {
        return PLATFORM_SD_ERROR;
    }

    if (IRQ_STM32HALAdapter_Register(&hplatform_sd.DetectIRQCallback,
                                    SD_CD_Pin,
                                    platform_sd_detect_irq_cb,
                                    &hplatform_sd) != IRQ_STM32_HAL_ADAPTER_OK)
    {
        return PLATFORM_SD_ERROR;
    }

    hplatform_sd.IRQRegistered = true;
    status = platform_sd_status(SDCard_Init(&hplatform_sd.Device));

    if (status != PLATFORM_OK)
    {
        (void)IRQ_STM32HALAdapter_Unregister(&hplatform_sd.DetectIRQCallback);
        hplatform_sd.IRQRegistered = false;
    }

    return status;
}

/**
  * @brief  解除卡检测 IRQ 绑定、释放 SDMMC 资源并恢复为 RESET。
  * @retval PLATFORM_OK    反初始化成功或原本已经处于 RESET。
  * @retval PLATFORM_SD_ERROR 底层反初始化失败。
  */
Platform_StatusTypeDef Platform_SD_DeInit(void)
{
    Platform_StatusTypeDef status = PLATFORM_OK;

    if (hplatform_sd.IRQRegistered)
    {
        if (IRQ_STM32HALAdapter_Unregister(
                &hplatform_sd.DetectIRQCallback) != IRQ_STM32_HAL_ADAPTER_OK)
        {
            status = PLATFORM_SD_ERROR;
        }
        else
        {
            hplatform_sd.IRQRegistered = false;
        }
    }

    if (SDCard_DeInit(&hplatform_sd.Device) != SDCARD_OK)
    {
        status = PLATFORM_SD_ERROR;
    }

    hplatform_sd.DetectPending = false;
    hplatform_sd.DebounceStartMs = 0U;
    hplatform_sd.Debouncing = false;
    return status;
}

/**
  * @brief  根据当前 SD_CD 电平刷新插拔状态。
  * @retval PLATFORM_OK 检测状态与 Device 生命周期刷新成功。
  * @retval PLATFORM_SD_ERROR Device 刷新失败。
  * @note   本函数不实现去抖且不得从 EXTI ISR 调用。正常热插拔路径应通过
  *         Platform_SD_Process() 在检测电平稳定后间接调用。
  */
Platform_StatusTypeDef Platform_SD_Refresh(void)
{
    return platform_sd_status(SDCard_Refresh(&hplatform_sd.Device));
}

/**
  * @brief  推进一次 Platform SD 热插拔事件处理。
  * @param  event 接收本次稳定状态变化。
  * @retval PLATFORM_OK 本次处理正常完成。
  * @retval PLATFORM_SD_ERROR 参数无效、Platform SD 未初始化或刷新失败。
  * @note   每次取到新的检测通知都会重新开始 30 ms 消抖。只有连续稳定满
  *         30 ms 后才刷新 Device；状态未变化时仍返回 EVENT_NONE。
  */
Platform_StatusTypeDef Platform_SD_Process(Platform_SD_EventTypeDef *event)
{
    Platform_SD_StateTypeDef previous_state;
    Platform_SD_StateTypeDef current_state;
    Platform_StatusTypeDef status;

    if (event == NULL)
    {
        return PLATFORM_SD_ERROR;
    }

    *event = PLATFORM_SD_EVENT_NONE;

    if (!hplatform_sd.IRQRegistered)
    {
        return PLATFORM_SD_ERROR;
    }

    /**************** FreeRTOS 移植替换区：开始 *******************************
     * Storage Task 使用 ulTaskNotifyTake() 等待通知，并在最后一次通知后等待
     * 30 ms；超时后继续执行下面的 Platform_SD_Refresh()。
     *************************************************************************/
    if (platform_sd_take_detect_event(&hplatform_sd))
    {
        hplatform_sd.DebounceStartMs = HAL_GetTick();
        hplatform_sd.Debouncing = true;
        return PLATFORM_OK;
    }

    if (!hplatform_sd.Debouncing)
    {
        return PLATFORM_OK;
    }

    if ((uint32_t)(HAL_GetTick() - hplatform_sd.DebounceStartMs) < PLATFORM_SD_DEBOUNCE_MS)
    {
        return PLATFORM_OK;
    }

    hplatform_sd.Debouncing = false;
    /**************** FreeRTOS 移植替换区：结束 *******************************/

    previous_state = Platform_SD_GetState();
    status = Platform_SD_Refresh();

    if (status != PLATFORM_OK)
    {
        return status;
    }

    current_state = Platform_SD_GetState();

    if ((previous_state != current_state) &&
        (current_state == PLATFORM_SD_STATE_READY))
    {
        *event = PLATFORM_SD_EVENT_INSERTED;
    }
    else if ((previous_state != current_state) &&
             (current_state == PLATFORM_SD_STATE_NOT_PRESENT))
    {
        *event = PLATFORM_SD_EVENT_REMOVED;
    }

    return PLATFORM_OK;
}

/**
  * @brief  返回 Platform SD 当前持续状态。
  * @return 从私有 SD Card Device 状态转换得到的 Platform 状态。
  */
Platform_SD_StateTypeDef Platform_SD_GetState(void)
{
    return platform_sd_state(SDCard_GetState(&hplatform_sd.Device));
}

/**
  * @brief  直接读取当前卡检测电平。
  * @retval true 卡检测输入当前表示已插卡。
  * @retval false 卡检测输入当前表示未插卡，或 Port 尚未正确绑定。
  * @note   本函数不更新 Platform SD 状态；状态转换必须通过 Init/Refresh 完成。
  */
bool Platform_SD_IsPresent(void)
{
    return SDCard_IsPresent(&hplatform_sd.Device);
}

/**
  * @brief  将 Device 缓存的介质信息复制到调用者对象。
  * @param  info 接收 Platform SD 信息快照的指针。
  * @retval PLATFORM_OK    信息有效且复制成功。
  * @retval PLATFORM_SD_ERROR 参数为空或介质未就绪。
  */
Platform_StatusTypeDef Platform_SD_GetInfo(Platform_SD_InfoTypeDef *info)
{
    SDCard_InfoTypeDef device_info;

    if (info == NULL)
    {
        return PLATFORM_SD_ERROR;
    }

    if (SDCard_GetInfo(&hplatform_sd.Device, &device_info) != SDCARD_OK)
    {
        return PLATFORM_SD_ERROR;
    }

    info->CapacityBytes = device_info.CapacityBytes;
    info->BlockCount = device_info.BlockCount;
    info->BlockSize = device_info.BlockSize;
    info->CardType = device_info.CardType;
    info->CardVersion = device_info.CardVersion;
    return PLATFORM_OK;
}

/**
  * @brief  复制最近一次 Device/Port 错误诊断。
  * @param  diagnostics 接收诊断快照的指针。
  * @retval PLATFORM_OK 快照复制成功。
  * @retval PLATFORM_SD_ERROR diagnostics 为空。
  */
Platform_StatusTypeDef Platform_SD_GetDiagnostics(Platform_SD_DiagnosticsTypeDef *diagnostics)
{
    if (diagnostics == NULL)
    {
        return PLATFORM_SD_ERROR;
    }

    diagnostics->DeviceError = (uint32_t)hplatform_sd.Device.ErrorCode;
    diagnostics->PortStatus = (uint32_t)hplatform_sd.Device.LastPortStatus;
    return PLATFORM_OK;
}

/**
  * @brief  从本板 SD 卡读取连续逻辑块。
  * @param  data 接收块数据的缓冲区。
  * @param  start_block 第一个逻辑块编号。
  * @param  block_count 连续读取的逻辑块数量。
  * @retval PLATFORM_OK 读取和同步完成。
  * @retval PLATFORM_SD_ERROR 参数、状态、范围或底层读取失败。
  */
Platform_StatusTypeDef Platform_SD_ReadBlocks(uint8_t *data,
                                        uint32_t start_block,
                                        uint32_t block_count)
{
    return platform_sd_status(SDCard_ReadBlocks(&hplatform_sd.Device,
                                              data,
                                              start_block,
                                              block_count));
}

/**
  * @brief  向本板 SD 卡写入连续逻辑块。
  * @param  data 提供块数据的只读缓冲区。
  * @param  start_block 第一个逻辑块编号。
  * @param  block_count 连续写入的逻辑块数量。
  * @retval PLATFORM_OK 写入和同步完成。
  * @retval PLATFORM_SD_ERROR 参数、状态、范围或底层写入失败。
  */
Platform_StatusTypeDef Platform_SD_WriteBlocks(const uint8_t *data,
                                         uint32_t start_block,
                                         uint32_t block_count)
{
    return platform_sd_status(SDCard_WriteBlocks(&hplatform_sd.Device,
                                               data,
                                               start_block,
                                               block_count));
}

/**
  * @brief  等待本板 SD 卡完成内部编程并回到可传输状态。
  * @retval PLATFORM_OK 介质已经可以接受下一条命令。
  * @retval PLATFORM_SD_ERROR 无卡、状态非法、底层错误或等待超时。
  */
Platform_StatusTypeDef Platform_SD_Sync(void)
{
    return platform_sd_status(SDCard_Sync(&hplatform_sd.Device));
}
