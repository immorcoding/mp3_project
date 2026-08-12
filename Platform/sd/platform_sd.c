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

#include "Adapters/stm32_hal/irq/stm32_gpio_exti_irq.h"
#include "Adapters/stm32_hal/irq/stm32_sdmmc_irq.h"
#include "Adapters/stm32_hal/sd/sd_stm32_hal_adapter.h"
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
    STM32SDMMCIRQ_CallbackTypeDef TransferIRQCallback; /**< SDMMC DMA IRQ 节点。 */
    Platform_SD_DetectCallback_t DetectCallback; /**< ISR 中调用的上层轻量通知。 */
    void *DetectContext; /**< 原样传给 DetectCallback 的调用者上下文。 */
    Platform_SD_TransferCallback_t TransferCallback; /**< ISR 中调用的 DMA 事件通知。 */
    void *TransferContext; /**< 原样传给 TransferCallback 的调用者上下文。 */
    bool IRQRegistered; /**< DetectIRQCallback 是否已经注册到 GPIO EXTI Adapter。 */
    bool TransferIRQRegistered; /**< TransferIRQCallback 是否已经注册到 SDMMC IRQ Adapter。 */
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
  * @brief  把 STM32 SDMMC DMA 生命周期事件转换为 Platform SD 事件。
  * @param  event SDMMC IRQ Adapter 发布的源特定事件。
  * @param  context 注册时绑定的 Platform SD 私有 Handle。
  * @note   本函数运行在 IRQ 上下文；它不修改 Device 状态、不处理 Cache，也不调用
  *         FreeRTOS。Storage Task 的已注入回调负责执行 xxxFromISR() 唤醒任务。
  */
static void platform_sd_transfer_irq_cb(STM32SDMMCIRQ_EventTypeDef event,
                                        void *context)
{
    Platform_SD_HandleTypeDef *hsd = (Platform_SD_HandleTypeDef *)context;
    Platform_SD_TransferEventTypeDef platform_event;

    if ((hsd == NULL) || (hsd->TransferCallback == NULL))
    {
        return;
    }

    switch (event)
    {
        case STM32SDMMCIRQ_EVENT_READ_COMPLETE:
            platform_event = PLATFORM_SD_TRANSFER_EVENT_READ_COMPLETE;
            break;

        case STM32SDMMCIRQ_EVENT_WRITE_COMPLETE:
            platform_event = PLATFORM_SD_TRANSFER_EVENT_WRITE_COMPLETE;
            break;

        case STM32SDMMCIRQ_EVENT_ABORTED:
            platform_event = PLATFORM_SD_TRANSFER_EVENT_ABORTED;
            break;

        case STM32SDMMCIRQ_EVENT_ERROR:
        default:
            platform_event = PLATFORM_SD_TRANSFER_EVENT_ERROR;
            break;
    }

    hsd->TransferCallback(platform_event, hsd->TransferContext);
}

/**
  * @brief  根据当前 Device 状态注册或移除 SDMMC DMA 回调节点。
  * @retval PLATFORM_OK 节点与当前介质生命周期保持一致。
  * @retval PLATFORM_SD_ERROR 回调注册或注销失败。
  * @details
  *          HAL_SD_Init() 在热插卡后会重置其回调指针，因而节点不能只在系统启动时
  *          注册一次。READY 时安装回调；卡移除后的 NOT_PRESENT/RESET 时移除节点，
  *          保证 IRQ Adapter 永远不会保留已反初始化 Device 的 Platform 上下文。
  */
static Platform_StatusTypeDef platform_sd_sync_transfer_irq(void)
{
    SDCard_StateTypeDef state = SDCard_GetState(&hplatform_sd.Device);

    if ((state == SDCARD_STATE_READY) &&
        (hplatform_sd.TransferCallback != NULL))
    {
        if (!hplatform_sd.TransferIRQRegistered)
        {
            if (STM32SDMMCIRQ_Register(&hplatform_sd.TransferIRQCallback,
                                        hplatform_sd_adapter.Handle,
                                        platform_sd_transfer_irq_cb,
                                        &hplatform_sd) != STM32SDMMCIRQ_OK)
            {
                return PLATFORM_SD_ERROR;
            }

            hplatform_sd.TransferIRQRegistered = true;
        }
    }
    else if (hplatform_sd.TransferIRQRegistered)
    {
        if (STM32SDMMCIRQ_Unregister(&hplatform_sd.TransferIRQCallback) != STM32SDMMCIRQ_OK)
        {
            return PLATFORM_SD_ERROR;
        }

        hplatform_sd.TransferIRQRegistered = false;
    }

    return PLATFORM_OK;
}

/**
  * @brief  绑定本板 SDMMC Adapter、注册卡检测 EXTI 并初始化当前介质。
  * @param  detect_callback 在 SD_CD 边沿到达时从 ISR 调用的轻量通知回调。
  * @param  detect_context 原样传给 detect_callback 的调用者上下文，可为 NULL。
  * @retval PLATFORM_OK 介质状态已同步；未插卡同样成功，此时状态为 NOT_PRESENT。
  * @retval PLATFORM_SD_ERROR Adapter 绑定、GPIO EXTI 注册或介质初始化失败。
  * @note   即使 SDCard_Init() 因当前介质异常而失败，GPIO EXTI 注册仍会保留，
  *         以便后续拔卡后恢复到 NOT_PRESENT，并允许下次稳定插卡重新初始化。
  *         Filesystem Service 在本函数成功后通过 Platform_SD_SetTransferCallback()
  *         订阅 SDMMC DMA 事件；只有 Device READY 时才会为 hsd1 安装回调。
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

    if ((status == PLATFORM_OK) &&
        (platform_sd_sync_transfer_irq() != PLATFORM_OK))
    {
        status = PLATFORM_SD_ERROR;
    }

    return status;
}

/**
  * @brief  注销卡检测 EXTI、SDMMC 传输回调，释放 SDMMC 资源并恢复为 RESET。
  * @retval PLATFORM_OK 反初始化成功。
  * @retval PLATFORM_SD_ERROR GPIO EXTI/SDMMC 回调注销或底层反初始化失败。
  * @note   先移除 SDMMC 回调，再注销 GPIO EXTI，最后才释放 SD Device。若任一步
  *         注销失败，本函数不会继续破坏仍可能被 ISR 到达的 Platform 私有实例。
  */
Platform_StatusTypeDef Platform_SD_DeInit(void)
{
    if (hplatform_sd.TransferIRQRegistered)
    {
        if (STM32SDMMCIRQ_Unregister(&hplatform_sd.TransferIRQCallback) != STM32SDMMCIRQ_OK)
        {
            return PLATFORM_SD_ERROR;
        }

        hplatform_sd.TransferIRQRegistered = false;
    }

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
    hplatform_sd.TransferCallback = NULL;
    hplatform_sd.TransferContext = NULL;

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
    Platform_StatusTypeDef status = platform_sd_status(SDCard_Refresh(&hplatform_sd.Device));

    if ((status == PLATFORM_OK) &&
        (platform_sd_sync_transfer_irq() != PLATFORM_OK))
    {
        return PLATFORM_SD_ERROR;
    }

    return status;
}

/**
  * @brief  为本板 SD 卡槽设置唯一的 SDMMC 传输事件订阅者。
  * @param  transfer_callback 在 DMA 完成、错误或中止时调用的 ISR 安全回调。
  * @param  transfer_context 原样传给 transfer_callback 的不透明上下文。
  * @retval PLATFORM_OK 已保存订阅者且已同步 SDMMC IRQ 节点。
  * @retval PLATFORM_SD_ERROR Platform SD 未初始化、回调无效或 IRQ 节点安装失败。
  * @note   这是单订阅者 Interface，不是回调链表。DMA 在飞时不得替换订阅者。回调
  *         运行于 ISR 上下文，只能发布轻量事件；不得访问 FatFs 或提交传输状态。
  */
Platform_StatusTypeDef Platform_SD_SetTransferCallback(
    Platform_SD_TransferCallback_t transfer_callback,
    void *transfer_context)
{
    Platform_SD_TransferCallback_t previous_callback;
    void *previous_context;

    if ((transfer_callback == NULL) ||
        (!hplatform_sd.IRQRegistered) ||
        (Platform_SD_GetState() == PLATFORM_SD_STATE_BUSY))
    {
        return PLATFORM_SD_ERROR;
    }

    previous_callback = hplatform_sd.TransferCallback;
    previous_context = hplatform_sd.TransferContext;
    hplatform_sd.TransferCallback = transfer_callback;
    hplatform_sd.TransferContext = transfer_context;

    if (platform_sd_sync_transfer_irq() != PLATFORM_OK)
    {
        hplatform_sd.TransferCallback = previous_callback;
        hplatform_sd.TransferContext = previous_context;
        return PLATFORM_SD_ERROR;
    }

    return PLATFORM_OK;
}

/**
  * @brief  清除本板 SD 卡槽的 SDMMC 传输事件订阅者。
  * @retval PLATFORM_OK 已移除订阅者及其 SDMMC IRQ 节点。
  * @retval PLATFORM_SD_ERROR Platform SD 未初始化、仍有传输在飞或 IRQ 节点移除失败。
  * @note   调用者必须确保本函数返回后不再有传输可能完成。Platform_SD_DeInit()
  *         会在回收过程中清除订阅者。
  */
Platform_StatusTypeDef Platform_SD_ClearTransferCallback(void)
{
    if ((!hplatform_sd.IRQRegistered) ||
        (Platform_SD_GetState() == PLATFORM_SD_STATE_BUSY))
    {
        return PLATFORM_SD_ERROR;
    }

    if (hplatform_sd.TransferIRQRegistered)
    {
        if (STM32SDMMCIRQ_Unregister(&hplatform_sd.TransferIRQCallback) != STM32SDMMCIRQ_OK)
        {
            return PLATFORM_SD_ERROR;
        }

        hplatform_sd.TransferIRQRegistered = false;
    }

    hplatform_sd.TransferCallback = NULL;
    hplatform_sd.TransferContext = NULL;
    return PLATFORM_OK;
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
  * @brief  启动本板 SD 卡的一次 DMA 块读取。
  * @note   返回 PLATFORM_OK 只表示 DMA 已启动。调用者必须等待先前通过
  *         Platform_SD_SetTransferCallback() 订阅的传输事件，再调用
  *         Platform_SD_CompleteTransfer() 推进 Device 状态。
  */
Platform_StatusTypeDef Platform_SD_StartReadBlocks(uint8_t *data,
                                                   uint32_t start_block,
                                                   uint32_t block_count)
{
    if (!hplatform_sd.TransferIRQRegistered)
    {
        return PLATFORM_SD_ERROR;
    }

    return platform_sd_status(SDCard_StartReadBlocks(&hplatform_sd.Device,
                                                     data,
                                                     start_block,
                                                     block_count));
}

/**
  * @brief  启动本板 SD 卡的一次 DMA 块写入。
  * @note   data 必须保持有效且不可修改，直至接收完成/失败事件的任务完成后续处理。
  */
Platform_StatusTypeDef Platform_SD_StartWriteBlocks(const uint8_t *data,
                                                    uint32_t start_block,
                                                    uint32_t block_count)
{
    if (!hplatform_sd.TransferIRQRegistered)
    {
        return PLATFORM_SD_ERROR;
    }

    return platform_sd_status(SDCard_StartWriteBlocks(&hplatform_sd.Device,
                                                      data,
                                                      start_block,
                                                      block_count));
}

/**
  * @brief  在普通任务上下文处理已到达的 DMA 传输事件。
  * @param  event Storage Task 从 IRQ 通知中取得的 Platform 传输事件。
  * @retval PLATFORM_OK 数据阶段成功且介质已经回到 TRANSFER。
  * @retval PLATFORM_SD_ERROR DMA 失败/中止、事件非法或完成后的同步失败。
  * @note   此函数是 IRQ 与 Device 状态机之间唯一的普通上下文接缝。它不会等待
  *         一个新的 IRQ；成功事件仅在数据阶段结束后检查卡的内部编程是否完成。
  */
Platform_StatusTypeDef Platform_SD_CompleteTransfer(
    Platform_SD_TransferEventTypeDef event)
{
    if ((event == PLATFORM_SD_TRANSFER_EVENT_READ_COMPLETE) ||
        (event == PLATFORM_SD_TRANSFER_EVENT_WRITE_COMPLETE))
    {
        return platform_sd_status(SDCard_CompleteTransfer(&hplatform_sd.Device));
    }

    if ((event == PLATFORM_SD_TRANSFER_EVENT_ERROR) ||
        (event == PLATFORM_SD_TRANSFER_EVENT_ABORTED))
    {
        return platform_sd_status(SDCard_FailTransfer(&hplatform_sd.Device));
    }

    return PLATFORM_SD_ERROR;
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
