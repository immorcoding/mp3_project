/**
  ******************************************************************************
  * @file    platform_sd.c
  * @brief   本板唯一 SD 卡槽的 Device 装配和 Platform Interface 实现。
  *
  * @details
  *          本文件装配 hsd1、低有效 SD_CD 引脚、SD Card Device 与 STM32 HAL
  *          GPIO EXTI Adapter。它持有 GPIO EXTI 链表节点，并把原始检测边沿
  *          转交给调用者提供的通用通知回调。
  *
  *          Platform 不包含 FreeRTOS 头文件，也不认识 TaskHandle_t。这样同一
  *          个 Platform SD 能力可被裸机轮询、FreeRTOS Task 通知或其他运行时
  *          机制使用；调用者只需在检测回调内完成自身的轻量唤醒。
  ******************************************************************************
  */

#include "Platform/sd/platform_sd.h"

#include <stddef.h>

#include "Adapters/gpio_exti/gpio_exti_stm32_hal_adapter.h"
#include "Adapters/sd/sd_stm32_hal_adapter.h"
#include "Components/sd/sd.h"
#include "main.h"
#include "sdmmc.h"

/**
  * @brief 本板唯一 SD 卡槽的私有 Handle。
  * @note  DetectIRQCallback 是 GPIO EXTI Adapter 中的侵入式节点；其余两个
  *        Detect 字段组成 Platform 与调用者之间的调度接缝。
  */
typedef struct
{
    SDCard_HandleTypeDef Device; /**< 通用 SD Card Device 状态机和诊断。 */
    GPIOEXTI_STM32HALAdapter_CallbackTypeDef DetectIRQCallback; /**< 卡检测 EXTI 节点。 */
    Platform_SD_DetectCallback_t DetectCallback; /**< ISR 中调用的上层轻量通知。 */
    void *DetectContext; /**< 原样传给 DetectCallback 的调用者上下文。 */
    bool IRQRegistered; /**< DetectIRQCallback 是否已经注册到 GPIO EXTI Adapter。 */
} Platform_SD_HandleTypeDef;

/** @brief 本板唯一 SD 卡槽对应的私有实例。 */
static Platform_SD_HandleTypeDef hplatform_sd;

/**
  * @brief 本板 SDMMC1 Handle 和低有效卡检测信号的 STM32 HAL Adapter Context。
  * @note  Platform 拥有这份 PCB 装配关系；hsd1 和 GPIO Port 仍由 CubeMX
  *        代码拥有，本对象只保存借用引用。
  */
static SDCard_STM32HALAdapterTypeDef hplatform_sd_adapter = {
    .Handle = &hsd1,
    .DetectPort = SD_CD_GPIO_Port,
    .DetectPin = SD_CD_Pin,
    .PresentState = GPIO_PIN_RESET
};

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

/**
  * @brief  接收 SD_CD GPIO EXTI 事件并转交调用者的检测通知。
  * @param  gpio_pin GPIO EXTI Adapter 传入的 HAL GPIO_Pin 位掩码。
  * @param  context 注册时绑定的 Platform SD 私有 Handle。
  * @note   本函数运行在 ISR 上下文。它不做消抖、SDMMC、日志或文件系统操作；
  *         其唯一职责是把已经匹配的硬件边沿通知给 Platform 的调用者。
  */
static void platform_sd_detect_irq_cb(uint16_t gpio_pin, void *context)
{
    Platform_SD_HandleTypeDef *hsd = (Platform_SD_HandleTypeDef *)context;

    if ((hsd != NULL) &&
        ((gpio_pin & SD_CD_Pin) != 0U) &&
        (hsd->DetectCallback != NULL))
    {
        hsd->DetectCallback(hsd->DetectContext);
    }
}

/**
  * @brief  绑定本板 SDMMC Adapter、注册卡检测 EXTI 并初始化当前介质。
  * @param  detect_callback 在 SD_CD 边沿到达时从 ISR 调用的轻量通知回调。
  * @param  detect_context 原样传给 detect_callback 的调用者上下文，可为 NULL。
  * @retval PLATFORM_OK 介质状态已同步；未插卡同样成功，此时状态为 NOT_PRESENT。
  * @retval PLATFORM_SD_ERROR Adapter 绑定、GPIO EXTI 注册或介质初始化失败。
  * @note   即使 SDCard_Init() 因当前介质异常而失败，GPIO EXTI 注册仍会保留，
  *         以便后续拔卡后恢复到 NOT_PRESENT，并允许下次稳定插卡重新初始化。
  *         本函数不可重复调用；调用者应先执行 Platform_SD_DeInit()。
  */
Platform_StatusTypeDef Platform_SD_Init(Platform_SD_DetectCallback_t detect_callback,
                                        void *detect_context)
{
    Platform_StatusTypeDef status;

    if ((detect_callback == NULL) || hplatform_sd.IRQRegistered)
    {
        return PLATFORM_SD_ERROR;
    }

    if (SDCard_STM32HALAdapter_Bind(&hplatform_sd.Device,
                                    &hplatform_sd_adapter) != SDCARD_OK)
    {
        return PLATFORM_SD_ERROR;
    }

    hplatform_sd.DetectCallback = detect_callback;
    hplatform_sd.DetectContext = detect_context;

    if (GPIOEXTI_STM32HALAdapter_Register(&hplatform_sd.DetectIRQCallback,
                                           SD_CD_Pin,
                                           platform_sd_detect_irq_cb,
                                           &hplatform_sd) != GPIOEXTI_STM32HAL_ADAPTER_OK)
    {
        hplatform_sd.DetectCallback = NULL;
        hplatform_sd.DetectContext = NULL;
        return PLATFORM_SD_ERROR;
    }

    hplatform_sd.IRQRegistered = true;
    status = platform_sd_status(SDCard_Init(&hplatform_sd.Device));

    return status;
}

/**
  * @brief  注销卡检测 EXTI、释放 SDMMC 资源并恢复为 RESET。
  * @retval PLATFORM_OK 反初始化成功。
  * @retval PLATFORM_SD_ERROR GPIO EXTI 注销或底层反初始化失败。
  * @note   若 GPIO EXTI 注销失败，本函数不会清空回调指针或继续释放 SD Device，
  *         避免 ISR 仍可达时破坏 Platform 私有实例的一致性。
  */
Platform_StatusTypeDef Platform_SD_DeInit(void)
{
    if (hplatform_sd.IRQRegistered)
    {
        if (GPIOEXTI_STM32HALAdapter_Unregister(
                &hplatform_sd.DetectIRQCallback) != GPIOEXTI_STM32HAL_ADAPTER_OK)
        {
            return PLATFORM_SD_ERROR;
        }

        hplatform_sd.IRQRegistered = false;
    }

    hplatform_sd.DetectCallback = NULL;
    hplatform_sd.DetectContext = NULL;

    return platform_sd_status(SDCard_DeInit(&hplatform_sd.Device));
}

/**
  * @brief  根据当前稳定的 SD_CD 电平刷新介质生命周期。
  * @retval PLATFORM_OK 检测状态与 Device 生命周期刷新成功。
  * @retval PLATFORM_SD_ERROR Device 刷新失败。
  * @note   本函数不实现机械触点消抖，且不得从 EXTI ISR 调用。当前 Storage
  *         Task 在最后一个检测通知后的 30 ms 静默期结束后调用本函数。
  */
Platform_StatusTypeDef Platform_SD_Refresh(void)
{
    return platform_sd_status(SDCard_Refresh(&hplatform_sd.Device));
}

/**
  * @brief  处理一次已经完成消抖的 SD 卡状态刷新并生成稳定事件。
  * @param  event 接收本次产生的稳定状态变化，不能为空。
  * @retval PLATFORM_OK 刷新成功；无状态变化时 *event 为 EVENT_NONE。
  * @retval PLATFORM_SD_ERROR 参数无效、Platform 尚未初始化或刷新失败。
  * @note   本函数不等待也不消抖。调用者必须确保其在普通执行上下文调用，并且
  *         SD_CD 输入已稳定；Storage Task 的任务通知循环负责这一时序。
  */
Platform_StatusTypeDef Platform_SD_Process(Platform_SD_EventTypeDef *event)
{
    Platform_SD_StateTypeDef previous_state;
    Platform_SD_StateTypeDef current_state;
    Platform_StatusTypeDef status;

    if ((event == NULL) || !hplatform_sd.IRQRegistered)
    {
        return PLATFORM_SD_ERROR;
    }

    *event = PLATFORM_SD_EVENT_NONE;
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
  * @note   本函数不更新 Platform SD 持续状态；状态转换必须通过 Init、Refresh
  *         或 Process 完成。
  */
bool Platform_SD_IsPresent(void)
{
    return SDCard_IsPresent(&hplatform_sd.Device);
}

/**
  * @brief  将 Device 缓存的介质信息复制到调用者对象。
  * @param  info 接收 Platform SD 信息快照的指针。
  * @retval PLATFORM_OK 信息有效且复制成功。
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
