/**
 * @file test_sd_removal_chain.c
 * @brief 产品里 SD 读路径的整条链：FatFs 的 `SD_read` → Service 的 BSP 与 DMA 执行器 → Platform SD → SD Card Component，
 *        以及读时超时、拔卡、停不住之后 Storage 的挂载与卸载（#209）。
 *
 * 全部用真实代码：`FATFS/Target/sd_diskio.c`（CubeMX 生成的 FatFs 驱动，原样传 `SD_TIMEOUT`）、`filesystem_sd_bsp.c`、
 * `filesystem_sd_transfer.c`、Filesystem Service 与 FatFs、`platform_sd.c`、`Components/sd/sd.c`、`storage_sd.c`。只有硬件一侧用替身：
 * Port（假 SD 卡：一块 64 KiB 的内存 FAT12 镜像，可脚本化“不完成”“读到一半拔卡”“中止停不住”）、两个 IRQ Adapter 的注册函数、
 * HAL 头与 CubeMX 句柄、Flash 传输层、Storage 的曲库与音频流。SDMMC 中断由 FreeRTOS 桩的阻塞钩子在“等待将要发生”时替它发出。
 *
 * 主机单线程，桩不切换任务；通过只证明这一串调用的分支与状态对，不证明真实 ISR 时序、DMA 与 Cache、HAL 寄存器序列。
 */

#include "APP/tasks/storage/catalog/storage_catalog.h"
#include "APP/tasks/storage/sd/storage_sd.h"
#include "APP/tasks/storage/stream/storage_audio_stream.h"
#include "Adapters/stm32_hal/irq/stm32_gpio_exti_irq.h"
#include "Adapters/stm32_hal/irq/stm32_sdmmc_irq.h"
#include "Adapters/stm32_hal/sd/sd_stm32_hal_adapter.h"
#include "Components/sd/sd.h"
#include "FATFS/App/fatfs.h"
#include "Platform/flash/platform_flash.h"
#include "Platform/sd/platform_sd.h"
#include "Service/filesystem/filesystem_file.h"
#include "Service/filesystem/filesystem_service.h"
#include "Service/log/log_service.h"

#include "freertos_stub.h"
#include "lock_env_stub.h"
#include "sd_diskio.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

char SDPath[4];
char USERPath[4];
uint8_t retSD;
uint8_t retUSER;
FATFS SDFatFS;
FATFS USERFatFS;
SD_HandleTypeDef hsd1;
GPIO_TypeDef host_stub_sd_cd_port;

/* ------------------------------------------------------------------------- */
/* 假 SD 卡：内存里的 FAT12 镜像                                              */
/* ------------------------------------------------------------------------- */

#define IMAGE_SECTORS 128U
#define IMAGE_SECTOR_BYTES 512U
#define IMAGE_FILE_NAME "TEST.TXT"
#define IMAGE_FILE_TEXT "0123456789"

static uint8_t image[IMAGE_SECTORS][IMAGE_SECTOR_BYTES];

/**
 * @brief 往字节串的小端偏移处写 16 位值。
 * @param[in,out] bytes 目标。
 * @param[in] offset 偏移。
 * @param[in] value 值。
 */
static void put16(uint8_t *bytes, uint32_t offset, uint16_t value)
{
    bytes[offset] = (uint8_t)(value & 0xFFU);
    bytes[offset + 1U] = (uint8_t)(value >> 8U);
}

/**
 * @brief 往字节串的小端偏移处写 32 位值。
 * @param[in,out] bytes 目标。
 * @param[in] offset 偏移。
 * @param[in] value 值。
 */
static void put32(uint8_t *bytes, uint32_t offset, uint32_t value)
{
    put16(bytes, offset, (uint16_t)(value & 0xFFFFU));
    put16(bytes, offset + 2U, (uint16_t)(value >> 16U));
}

/**
 * @brief 生成最小的 FAT12 镜像：128 个 512 B 扇区，每簇一个扇区，两份 FAT，16 项根目录，根目录里一个 10 字节的文件。
 */
static void build_image(void)
{
    uint8_t *boot = image[0];
    uint8_t *entry = &image[3][0];
    static const uint8_t fat[6] = {0xF8U, 0xFFU, 0xFFU, 0xFFU, 0x0FU, 0x00U};

    memset(image, 0, sizeof(image));
    boot[0] = 0xEBU;
    boot[1] = 0x3CU;
    boot[2] = 0x90U;
    memcpy(&boot[3], "MSDOS5.0", 8U);
    put16(boot, 11U, IMAGE_SECTOR_BYTES);
    boot[13] = 1U;
    put16(boot, 14U, 1U);
    boot[16] = 2U;
    put16(boot, 17U, 16U);
    put16(boot, 19U, IMAGE_SECTORS);
    boot[21] = 0xF8U;
    put16(boot, 22U, 1U);
    put16(boot, 24U, 32U);
    put16(boot, 26U, 2U);
    boot[36] = 0x80U;
    boot[38] = 0x29U;
    put32(boot, 39U, 0x12345678U);
    memcpy(&boot[43], "NO NAME    ", 11U);
    memcpy(&boot[54], "FAT12   ", 8U);
    boot[510] = 0x55U;
    boot[511] = 0xAAU;

    memcpy(image[1], fat, sizeof(fat));
    memcpy(image[2], fat, sizeof(fat));

    memcpy(entry, "TEST    TXT", 11U);
    entry[11] = 0x20U;
    put16(entry, 26U, 2U);
    put32(entry, 28U, (uint32_t)strlen(IMAGE_FILE_TEXT));
    memcpy(image[4], IMAGE_FILE_TEXT, strlen(IMAGE_FILE_TEXT));
}

/* ------------------------------------------------------------------------- */
/* 假 Port 与两个 IRQ Adapter                                                 */
/* ------------------------------------------------------------------------- */

/** @brief 假 Port 的脚本与观察值。 */
typedef struct
{
    bool present;                      /**< 卡检测。 */
    bool inflight;                     /**< 有一笔 DMA 读在飞。 */
    bool hang_next_read;               /**< 下一笔读受理后永远不完成（等待超时）；只作用于一笔读。 */
    bool hang_current_read;            /**< 在飞的这笔读永远不完成。 */
    bool pull_card_during_wait;        /**< 下一次等待期间卡被拔出（不发任何中断）。 */
    bool error_event_during_wait;      /**< 下一次等待期间发 ERROR 中断；若同时 pull_card_during_wait，卡先被拔出。 */
    bool abort_use_override;           /**< 中止结果用 abort_override 而不是按在位与否。 */
    SDCard_PortStatusTypeDef abort_override; /**< 中止结果的脚本。 */
    uint32_t start_read_calls;         /**< StartReadBlocks 次数。 */
    uint32_t abort_calls;              /**< AbortTransfer 次数。 */
    uint32_t last_abort_timeout_ms;    /**< 最近一次中止收到的上界。 */
    uint32_t init_calls;               /**< Init 次数。 */
    uint32_t deinit_calls;             /**< DeInit 次数。 */
} FakePort;

static FakePort fake;
static SDCard_PortOpsTypeDef fake_ops;
static STM32SDMMCIRQ_CallbackTypeDef *irq_node;
static uint32_t irq_register_calls;
static uint32_t irq_unregister_calls;
static SemaphoreHandle_t sd_done;
static TickType_t last_sd_wait_ticks;

/**
 * @brief 假 Port：卡检测。
 * @param[in] context 与 PortOps 成对绑定的对象（未使用）。
 * @return 脚本里的在位与否。
 */
static bool fake_is_present(const void *context)
{
    (void)context;
    return fake.present;
}

/**
 * @brief 假 Port：初始化。
 * @param[in,out] context 与 PortOps 成对绑定的对象（未使用）。
 * @retval SDCARD_PORT_OK 卡在位。
 * @retval SDCARD_PORT_NOT_PRESENT 卡不在位。
 */
static SDCard_PortStatusTypeDef fake_init(void *context)
{
    (void)context;
    fake.init_calls++;
    return fake.present ? SDCARD_PORT_OK : SDCARD_PORT_NOT_PRESENT;
}

/**
 * @brief 假 Port：反初始化，只计数。
 * @param[in,out] context 与 PortOps 成对绑定的对象（未使用）。
 * @return 固定 SDCARD_PORT_OK。
 */
static SDCard_PortStatusTypeDef fake_deinit(void *context)
{
    (void)context;
    fake.deinit_calls++;
    return SDCARD_PORT_OK;
}

/**
 * @brief 假 Port：镜像的容量。
 * @param[in,out] context 与 PortOps 成对绑定的对象（未使用）。
 * @param[out] info 接收介质信息。
 * @return 固定 SDCARD_PORT_OK。
 */
static SDCard_PortStatusTypeDef fake_get_info(void *context, SDCard_InfoTypeDef *info)
{
    (void)context;
    info->BlockCount = IMAGE_SECTORS;
    info->BlockSize = IMAGE_SECTOR_BYTES;
    info->CapacityBytes = (uint64_t)IMAGE_SECTORS * IMAGE_SECTOR_BYTES;
    return SDCARD_PORT_OK;
}

/**
 * @brief 假 Port：阻塞读，本测试不使用。
 * @param[in,out] context 与 PortOps 成对绑定的对象（未使用）。
 * @param[out] data 接收缓冲（未使用）。
 * @param[in] start 起始块（未使用）。
 * @param[in] count 块数（未使用）。
 * @param[in] timeout_ms 上界（未使用）。
 * @return 固定 SDCARD_PORT_ERROR。
 */
static SDCard_PortStatusTypeDef fake_read(void *context, uint8_t *data, uint32_t start, uint32_t count,
                                          uint32_t timeout_ms)
{
    (void)context;
    (void)data;
    (void)start;
    (void)count;
    (void)timeout_ms;
    return SDCARD_PORT_ERROR;
}

/**
 * @brief 假 Port：阻塞写，本测试不使用。
 * @param[in,out] context 与 PortOps 成对绑定的对象（未使用）。
 * @param[in] data 数据（未使用）。
 * @param[in] start 起始块（未使用）。
 * @param[in] count 块数（未使用）。
 * @param[in] timeout_ms 上界（未使用）。
 * @return 固定 SDCARD_PORT_ERROR。
 */
static SDCard_PortStatusTypeDef fake_write(void *context, const uint8_t *data, uint32_t start, uint32_t count,
                                           uint32_t timeout_ms)
{
    (void)context;
    (void)data;
    (void)start;
    (void)count;
    (void)timeout_ms;
    return SDCARD_PORT_ERROR;
}

/**
 * @brief 假 Port：启动 DMA 读，把镜像的块复制进缓冲，等“中断”。
 * @param[in,out] context 与 PortOps 成对绑定的对象（未使用）。
 * @param[out] data 接收缓冲。
 * @param[in] start 起始块。
 * @param[in] count 块数。
 * @retval SDCARD_PORT_OK 受理。
 * @retval SDCARD_PORT_NOT_PRESENT 卡不在位。
 * @retval SDCARD_PORT_ERROR 范围越界。
 */
static SDCard_PortStatusTypeDef fake_start_read(void *context, uint8_t *data, uint32_t start, uint32_t count)
{
    (void)context;
    fake.start_read_calls++;
    if (sd_done == NULL)
    {
        /* Service 的完成信号量在第一次 InitSD 时创建，晚于三把锁；第一笔 DMA 启动时它就是最后创建的那个。 */
        sd_done = freertos_stub_last_created_semaphore();
    }
    if (!fake.present)
    {
        return SDCARD_PORT_NOT_PRESENT;
    }
    if ((start + count) > IMAGE_SECTORS)
    {
        return SDCARD_PORT_ERROR;
    }
    memcpy(data, image[start], count * IMAGE_SECTOR_BYTES);
    fake.inflight = true;
    fake.hang_current_read = fake.hang_next_read;
    fake.hang_next_read = false;
    return SDCARD_PORT_OK;
}

/**
 * @brief 假 Port：启动 DMA 写，本测试不使用。
 * @param[in,out] context 与 PortOps 成对绑定的对象（未使用）。
 * @param[in] data 数据（未使用）。
 * @param[in] start 起始块（未使用）。
 * @param[in] count 块数（未使用）。
 * @return 固定 SDCARD_PORT_ERROR。
 */
static SDCard_PortStatusTypeDef fake_start_write(void *context, const uint8_t *data, uint32_t start, uint32_t count)
{
    (void)context;
    (void)data;
    (void)start;
    (void)count;
    return SDCARD_PORT_ERROR;
}

/**
 * @brief 假 Port：等卡回到 TRANSFER；卡不在位则报告 NOT_PRESENT。
 * @param[in,out] context 与 PortOps 成对绑定的对象（未使用）。
 * @param[in] timeout_ms 上界（未使用）。
 * @retval SDCARD_PORT_OK 卡在位。
 * @retval SDCARD_PORT_NOT_PRESENT 卡不在位。
 */
static SDCard_PortStatusTypeDef fake_sync(void *context, uint32_t timeout_ms)
{
    (void)context;
    (void)timeout_ms;
    return fake.present ? SDCARD_PORT_OK : SDCARD_PORT_NOT_PRESENT;
}

/**
 * @brief 假 Port：显式中止。卡在位且没有脚本覆盖就“停住”；卡不在位报告 NOT_PRESENT；脚本可以让它“停不住”。
 * @param[in,out] context 与 PortOps 成对绑定的对象（未使用）。
 * @param[in] timeout_ms Component 传下来的中止上界。
 * @return 中止结果。
 */
static SDCard_PortStatusTypeDef fake_abort(void *context, uint32_t timeout_ms)
{
    SDCard_PortStatusTypeDef result;

    (void)context;
    fake.abort_calls++;
    fake.last_abort_timeout_ms = timeout_ms;
    fake.inflight = false;
    if (fake.abort_use_override)
    {
        result = fake.abort_override;
    }
    else
    {
        result = fake.present ? SDCARD_PORT_OK : SDCARD_PORT_NOT_PRESENT;
    }
    if (result != SDCARD_PORT_OK)
    {
        fake.deinit_calls++;
    }
    return result;
}

/**
 * @brief 替身：把假 Port 装配到 Device，对应 `SDCard_STM32HALAdapter_Bind`。
 * @param[in,out] hsdcard 待绑定的 Device。
 * @param[in,out] adapter Platform 持有的 Adapter Context（未使用）。
 * @return 固定 SDCARD_OK。
 */
SDCard_StatusTypeDef SDCard_STM32HALAdapter_Bind(SDCard_HandleTypeDef *hsdcard, SDCard_STM32HALAdapterTypeDef *adapter)
{
    (void)adapter;
    hsdcard->PortOps = &fake_ops;
    hsdcard->PortContext = &fake;
    return SDCARD_OK;
}

/**
 * @brief 替身：卡检测 EXTI 注册，恒成功。
 * @param[in,out] callback 调用者的节点（未使用）。
 * @param[in] pin_mask 引脚（未使用）。
 * @param[in] handler 处理函数（未使用）。
 * @param[in,out] context 上下文（未使用）。
 * @return 固定 GPIOEXTI_STM32HAL_ADAPTER_OK。
 */
GPIOEXTI_STM32HALAdapter_StatusTypeDef GPIOEXTI_STM32HALAdapter_Register(
    GPIOEXTI_STM32HALAdapter_CallbackTypeDef *callback,
    uint16_t pin_mask,
    GPIOEXTI_STM32HALAdapter_HandlerTypeDef handler,
    void *context)
{
    (void)callback;
    (void)pin_mask;
    (void)handler;
    (void)context;
    return GPIOEXTI_STM32HAL_ADAPTER_OK;
}

/**
 * @brief 替身：卡检测 EXTI 注销，恒成功。
 * @param[in,out] callback 调用者的节点（未使用）。
 * @return 固定 GPIOEXTI_STM32HAL_ADAPTER_OK。
 */
GPIOEXTI_STM32HALAdapter_StatusTypeDef GPIOEXTI_STM32HALAdapter_Unregister(
    GPIOEXTI_STM32HALAdapter_CallbackTypeDef *callback)
{
    (void)callback;
    return GPIOEXTI_STM32HAL_ADAPTER_OK;
}

/**
 * @brief 替身：注册 SDMMC 传输 IRQ 节点；记下节点，测试在阻塞钩子里替“中断”调用它的处理函数。
 * @param[in,out] callback 调用者的节点。
 * @param[in] handle HAL 句柄。
 * @param[in] handler 处理函数。
 * @param[in,out] context 上下文。
 * @retval STM32SDMMCIRQ_OK 已记下。
 * @retval STM32SDMMCIRQ_ERROR 已经注册过。
 */
STM32SDMMCIRQ_StatusTypeDef STM32SDMMCIRQ_Register(STM32SDMMCIRQ_CallbackTypeDef *callback,
                                                   SD_HandleTypeDef *handle,
                                                   STM32SDMMCIRQ_HandlerTypeDef handler,
                                                   void *context)
{
    if (irq_node != NULL)
    {
        return STM32SDMMCIRQ_ERROR;
    }
    callback->Handle = handle;
    callback->Handler = handler;
    callback->Context = context;
    irq_node = callback;
    irq_register_calls++;
    return STM32SDMMCIRQ_OK;
}

/**
 * @brief 替身：注销 SDMMC 传输 IRQ 节点。
 * @param[in,out] callback 调用者的节点。
 * @retval STM32SDMMCIRQ_OK 已注销。
 * @retval STM32SDMMCIRQ_ERROR 节点没有注册。
 */
STM32SDMMCIRQ_StatusTypeDef STM32SDMMCIRQ_Unregister(STM32SDMMCIRQ_CallbackTypeDef *callback)
{
    if (irq_node != callback)
    {
        return STM32SDMMCIRQ_ERROR;
    }
    irq_node = NULL;
    irq_unregister_calls++;
    return STM32SDMMCIRQ_OK;
}

/**
 * @brief 替“SDMMC 中断”调用已注册节点的处理函数。
 * @param[in] event 要发布的传输事件。
 */
static void fire_irq(STM32SDMMCIRQ_EventTypeDef event)
{
    if (irq_node != NULL)
    {
        irq_node->Handler(event, irq_node->Context);
    }
}

/**
 * @brief 阻塞钩子：Service 等 SD 完成信号量的时刻，按脚本替“硬件”发生点什么。
 * @param[in] wait 将要发生的等待。
 * @param[in,out] context 未使用。
 */
static void interrupt_on_wait(const FreertosStub_Wait *wait, void *context)
{
    (void)context;
    if ((wait->kind != FREERTOS_STUB_WAIT_SEMAPHORE) || (sd_done == NULL) || (wait->semaphore != sd_done))
    {
        return;
    }
    last_sd_wait_ticks = wait->ticks;
    if (fake.pull_card_during_wait)
    {
        fake.present = false;
        fake.pull_card_during_wait = false;
    }
    if (fake.error_event_during_wait)
    {
        fake.error_event_during_wait = false;
        fire_irq(STM32SDMMCIRQ_EVENT_ERROR);
        return;
    }
    if (fake.inflight && !fake.hang_current_read && fake.present)
    {
        fake.inflight = false;
        fire_irq(STM32SDMMCIRQ_EVENT_READ_COMPLETE);
    }
}

/* ------------------------------------------------------------------------- */
/* Storage 一侧的替身，以及 Flash 传输层的替身                                */
/* ------------------------------------------------------------------------- */

static uint32_t catalog_init_calls;
static uint32_t catalog_invalidate_calls;
static uint32_t stream_card_removed_calls;
static Service_Filesystem_FileHandleTypeDef stream_file;

/**
 * @brief 曲库替身：初始化恒成功，只计数。
 * @return 固定 STORAGE_OK。
 */
Storage_StatusTypeDef storage_catalog_init(void)
{
    catalog_init_calls++;
    return STORAGE_OK;
}

/**
 * @brief 曲库替身：作废恒成功，只计数。
 * @return 固定 STORAGE_OK。
 */
Storage_StatusTypeDef storage_catalog_invalidate(void)
{
    catalog_invalidate_calls++;
    return STORAGE_OK;
}

/**
 * @brief 音频流替身：计数，并像真实实现那样试着关闭音频流打开的文件（测试登记在 stream_file 里）。
 *        拔卡之后关闭会失败（卡不在位时 FatFs 的 validate 报 NOT_READY），文件槽因此不会在这里释放，
 *        要靠随后的卸载使 Token 失效；换卡（卡已经插回）时关闭成功。
 */
void storage_audio_stream_card_removed(void)
{
    stream_card_removed_calls++;
    if (stream_file.Token != 0U)
    {
        (void)Service_Filesystem_CloseFile(stream_file);
        stream_file.Token = 0U;
    }
}

/**
 * @brief Flash 传输层替身：初始化恒失败，本测试不使用 Flash。
 * @return 恒为 false。
 */
bool filesystem_flash_transfer_init(void)
{
    return false;
}

/**
 * @brief Flash 传输层替身：恒未就绪。
 * @return 恒为 false。
 */
bool filesystem_flash_transfer_is_ready(void)
{
    return false;
}

/**
 * @brief Flash 传输层替身：打开恒未就绪。
 * @return 恒为 SERVICE_NOT_READY。
 */
Service_StatusTypeDef filesystem_flash_transfer_open(void)
{
    return SERVICE_NOT_READY;
}

/**
 * @brief Flash 传输层替身：格式化恒未就绪。
 * @return 恒为 SERVICE_NOT_READY。
 */
Service_StatusTypeDef filesystem_flash_transfer_format(void)
{
    return SERVICE_NOT_READY;
}

/**
 * @brief Flash 传输层替身：恢复恒未就绪。
 * @return 恒为 SERVICE_NOT_READY。
 */
Service_StatusTypeDef filesystem_flash_transfer_recover(void)
{
    return SERVICE_NOT_READY;
}

/**
 * @brief Flash 传输层替身：回收恒未就绪。
 * @return 恒为 SERVICE_NOT_READY。
 */
Service_StatusTypeDef filesystem_flash_transfer_reclaim(void)
{
    return SERVICE_NOT_READY;
}

/**
 * @brief Flash 执行器替身：恒失败。
 * @param[in] started Platform 对启动请求的受理结果（未使用）。
 * @param[in] timeout_ms 预算（未使用）。
 * @return 恒为 SERVICE_NOT_READY。
 */
Service_StatusTypeDef filesystem_flash_transfer_finish(Platform_StatusTypeDef started, uint32_t timeout_ms)
{
    (void)started;
    (void)timeout_ms;
    return SERVICE_NOT_READY;
}

/**
 * @brief Platform Flash 替身：启动数组读恒拒绝。
 * @param[in] address 芯片地址（未使用）。
 * @param[out] data 接收缓冲（未使用）。
 * @param[in] data_length 字节数（未使用）。
 * @return 恒为 PLATFORM_FLASH_ERROR。
 */
Platform_StatusTypeDef Platform_Flash_StartReadArray(uint32_t address, uint8_t *data, uint32_t data_length)
{
    (void)address;
    (void)data;
    (void)data_length;
    return PLATFORM_FLASH_ERROR;
}

/**
 * @brief Platform Flash 替身：启动自检区读恒拒绝。
 * @param[in] region 自检区域（未使用）。
 * @param[out] data 接收缓冲（未使用）。
 * @param[in] data_length 字节数（未使用）。
 * @return 恒为 PLATFORM_FLASH_ERROR。
 */
Platform_StatusTypeDef Platform_Flash_StartDiagnosticRead(Platform_Flash_DiagnosticRegionTypeDef region,
                                                          uint8_t *data,
                                                          uint32_t data_length)
{
    (void)region;
    (void)data;
    (void)data_length;
    return PLATFORM_FLASH_ERROR;
}

/**
 * @brief Platform Flash 替身：启动自检区擦除恒拒绝。
 * @param[in] region 自检区域（未使用）。
 * @return 恒为 PLATFORM_FLASH_ERROR。
 */
Platform_StatusTypeDef Platform_Flash_StartDiagnosticErase(Platform_Flash_DiagnosticRegionTypeDef region)
{
    (void)region;
    return PLATFORM_FLASH_ERROR;
}

/**
 * @brief Platform Flash 替身：启动自检区页编程恒拒绝。
 * @param[in] region 自检区域（未使用）。
 * @param[in] page_offset 页偏移（未使用）。
 * @param[in] data 输入缓冲（未使用）。
 * @param[in] data_length 字节数（未使用）。
 * @return 恒为 PLATFORM_FLASH_ERROR。
 */
Platform_StatusTypeDef Platform_Flash_StartDiagnosticPageProgram(Platform_Flash_DiagnosticRegionTypeDef region,
                                                                 uint32_t page_offset,
                                                                 const uint8_t *data,
                                                                 uint32_t data_length)
{
    (void)region;
    (void)page_offset;
    (void)data;
    (void)data_length;
    return PLATFORM_FLASH_ERROR;
}

/* ------------------------------------------------------------------------- */
/* 夹具                                                                       */
/* ------------------------------------------------------------------------- */

/**
 * @brief 清脚本和计数（不动卡的在位与否、不动 Platform/Service 的状态）。
 */
static void clear_script(void)
{
    fake.hang_next_read = false;
    fake.hang_current_read = false;
    fake.pull_card_during_wait = false;
    fake.error_event_during_wait = false;
    fake.abort_use_override = false;
    fake.start_read_calls = 0U;
    fake.abort_calls = 0U;
    fake.last_abort_timeout_ms = 0U;
    fake.init_calls = 0U;
    fake.deinit_calls = 0U;
    catalog_init_calls = 0U;
    catalog_invalidate_calls = 0U;
    stream_card_removed_calls = 0U;
    stream_file.Token = 0U;
    last_sd_wait_ticks = 0U;
    lock_env_log_reset();
}

/**
 * @brief 开机：卡在位，`storage_sd_init` 初始化 Platform、InitSD 并挂载；之后的用例共用这套状态。
 */
static void boot_with_card(void)
{
    build_image();
    memset(&fake, 0, sizeof(fake));
    fake.present = true;
    memset(&fake_ops, 0, sizeof(fake_ops));
    fake_ops.IsPresent = fake_is_present;
    fake_ops.Init = fake_init;
    fake_ops.DeInit = fake_deinit;
    fake_ops.GetInfo = fake_get_info;
    fake_ops.ReadBlocks = fake_read;
    fake_ops.WriteBlocks = fake_write;
    fake_ops.StartReadBlocks = fake_start_read;
    fake_ops.StartWriteBlocks = fake_start_write;
    fake_ops.Sync = fake_sync;
    fake_ops.AbortTransfer = fake_abort;

    freertos_stub_reset();
    freertos_stub_set_block_hook(interrupt_on_wait, NULL);
    lock_env_counter_reset();
    lock_env_log_reset();
    sd_done = NULL;
    irq_node = NULL;

    strcpy(SDPath, "");
    retSD = FATFS_LinkDriver(&SD_Driver, SDPath);
    assert(retSD == 0U);
    retUSER = 1U;
    USERPath[0] = '\0';

    assert(storage_sd_init(xTaskGetCurrentTaskHandle()) == STORAGE_OK);
    assert(sd_done != NULL);
    assert(storage_sd_volume_is_mounted());
    assert(Platform_SD_GetState() == PLATFORM_SD_STATE_READY);
    assert(irq_node != NULL);
    clear_script();
}

/**
 * @brief 打开镜像里的文件。
 * @param[out] file 接收句柄。
 * @return 打开结果。
 */
static Service_StatusTypeDef open_test_file(Service_Filesystem_FileHandleTypeDef *file)
{
    return Service_Filesystem_OpenFile(SERVICE_FILESYSTEM_VOLUME_SD, IMAGE_FILE_NAME, SERVICE_FILESYSTEM_FILE_MODE_READ, file);
}

/* ------------------------------------------------------------------------- */
/* 用例                                                                       */
/* ------------------------------------------------------------------------- */

/**
 * @brief 产品路径里 FatFs 传下来的超时是 `SDMMC_DATATIMEOUT`（0xFFFFFFFF），BSP 不能原样用来等：真卡死时会持 SD 锁约 49.7 天。
 *        BSP 一律改用 `FILESYSTEM_FATFS_BSP_DMA_TIMEOUT_MS`（500 ms）；超时之后走到显式中止，紧接着再读成功。
 */
static void test_product_read_path_gives_up_after_500_ms_and_aborts(void)
{
    Service_Filesystem_FileHandleTypeDef file = {0U};

    clear_script();
    fake.hang_next_read = true;

    assert(open_test_file(&file) != SERVICE_OK);

    assert(last_sd_wait_ticks == pdMS_TO_TICKS(500U));
    assert(fake.abort_calls == 1U);
    assert(lock_env_log_count() >= 1U);
    assert(strcmp(lock_env_log_tag(0U), "FSABORT") == 0);
    assert(Platform_SD_GetState() == PLATFORM_SD_STATE_READY);

    assert(open_test_file(&file) == SERVICE_OK);
    assert(Service_Filesystem_CloseFile(file) == SERVICE_OK);
}

/**
 * @brief 断言：读时拔卡之后，Storage 的路径恰好看到一次拔出（卸载、释放文件槽），再插卡看到一次插入并重新挂载成功。
 * @param[in] stale_file 拔卡前打开、拔卡后应已失效的文件句柄；Token 为 0 表示拔卡前没有打开文件。
 */
static void expect_removed_once_then_reinsert_and_remount(Service_Filesystem_FileHandleTypeDef stale_file)
{
    Service_Filesystem_FileHandleTypeDef file = {0U};
    uint32_t transferred = 0U;
    uint8_t byte;

    assert(Platform_SD_GetState() != PLATFORM_SD_STATE_READY);
    assert(storage_sd_volume_is_mounted());

    /* 拔出恰好报告一次：音频流收尾、曲库作废、卷卸载。 */
    assert(storage_sd_process() == STORAGE_OK);
    assert(stream_card_removed_calls == 1U);
    assert(catalog_invalidate_calls == 1U);
    assert(!storage_sd_volume_is_mounted());
    assert(Platform_SD_GetState() == PLATFORM_SD_STATE_NOT_PRESENT);
    if (stale_file.Token != 0U)
    {
        assert(Service_Filesystem_ReadFile(stale_file, &byte, 1U, &transferred) == SERVICE_INVALID_HANDLE);
    }
    assert(storage_sd_process() == STORAGE_OK);
    assert(stream_card_removed_calls == 1U);
    assert(catalog_invalidate_calls == 1U);

    /* 重新插卡：恰好一次插入，重新挂载成功（文件槽已经因卸载释放，不会返回 BUSY）。 */
    fake.present = true;
    assert(storage_sd_process() == STORAGE_OK);
    assert(storage_sd_volume_is_mounted());
    assert(Platform_SD_GetState() == PLATFORM_SD_STATE_READY);
    assert(catalog_init_calls == 1U);
    assert(irq_node != NULL);
    assert(storage_sd_process() == STORAGE_OK);
    assert(catalog_init_calls == 1U);
    assert(open_test_file(&file) == SERVICE_OK);
    assert(Service_Filesystem_ReadFile(file, &byte, 1U, &transferred) == SERVICE_OK);
    assert(transferred == 1U);
    assert(byte == (uint8_t)IMAGE_FILE_TEXT[0]);
    assert(Service_Filesystem_CloseFile(file) == SERVICE_OK);
}

/**
 * @brief 读时拔卡：等待超时，显式中止发现卡已不在（no_media），Device 先于 Process 变成 NOT_PRESENT；
 *        下一次 Process 仍要报告一次 REMOVED，Storage 卸载卷、释放被占用的文件槽，重新插卡后挂载成功。
 */
static void test_card_pulled_during_read_is_reported_removed_once(void)
{
    Service_Filesystem_FileHandleTypeDef file = {0U};
    uint8_t byte;
    uint32_t transferred = 0U;

    clear_script();
    assert(open_test_file(&file) == SERVICE_OK);
    stream_file = file;
    fake.hang_next_read = true;
    fake.pull_card_during_wait = true;

    assert(Service_Filesystem_ReadFile(file, &byte, 1U, &transferred) != SERVICE_OK);

    assert(last_sd_wait_ticks == pdMS_TO_TICKS(500U));
    assert(fake.abort_calls == 1U);
    assert(strcmp(lock_env_log_tag(0U), "FSABORT") == 0);
    assert(strncmp(lock_env_log_text(0U), "no_media ", 9U) == 0);
    assert(Platform_SD_GetState() == PLATFORM_SD_STATE_NOT_PRESENT);
    assert(irq_node == NULL);

    expect_removed_once_then_reinsert_and_remount(file);
}

/**
 * @brief 读时拔卡且 HAL 先发了错误事件：Service 以错误事件收尾（不中止），`SDCard_FailTransfer` 看到卡不在，
 *        同样先于 Process 把 Device 写成 NOT_PRESENT；拔出仍要恰好报告一次。
 */
static void test_card_pulled_with_error_event_is_reported_removed_once(void)
{
    Service_Filesystem_FileHandleTypeDef file = {0U};
    uint8_t byte;
    uint32_t transferred = 0U;

    clear_script();
    assert(open_test_file(&file) == SERVICE_OK);
    stream_file = file;
    fake.pull_card_during_wait = true;
    fake.error_event_during_wait = true;

    assert(Service_Filesystem_ReadFile(file, &byte, 1U, &transferred) != SERVICE_OK);

    assert(fake.abort_calls == 0U);
    assert(Platform_SD_GetState() == PLATFORM_SD_STATE_NOT_PRESENT);

    expect_removed_once_then_reinsert_and_remount(file);
}

/**
 * @brief 中止在上界内没停住（stuck）：卷保持 ERROR，Process 不报事件也不重试；之后拔卡仍恰好报告一次拔出，
 *        重新插卡后重新初始化并挂载成功（Port 在中止里已经反初始化，不会被重复反初始化）。
 */
static void test_abort_that_does_not_stop_keeps_error_until_card_is_replugged(void)
{
    Service_Filesystem_FileHandleTypeDef file = {0U};
    uint8_t byte;
    uint32_t transferred = 0U;

    clear_script();
    assert(open_test_file(&file) == SERVICE_OK);
    stream_file = file;
    fake.hang_next_read = true;
    fake.abort_use_override = true;
    fake.abort_override = SDCARD_PORT_TIMEOUT;

    assert(Service_Filesystem_ReadFile(file, &byte, 1U, &transferred) != SERVICE_OK);

    assert(strncmp(lock_env_log_text(0U), "stuck ", 6U) == 0);
    assert(Platform_SD_GetState() == PLATFORM_SD_STATE_ERROR);
    assert(irq_node == NULL);
    /* 卡还在位：Process 不自动重试，也没有事件。 */
    assert(storage_sd_process() == STORAGE_ERROR);
    assert(stream_card_removed_calls == 0U);
    assert(fake.init_calls == 0U);
    /* 卷保持 ERROR：新的读被拒绝，不等待。 */
    assert(open_test_file(&(Service_Filesystem_FileHandleTypeDef){0U}) != SERVICE_OK);
    assert(Platform_SD_GetState() == PLATFORM_SD_STATE_ERROR);

    fake.present = false;
    expect_removed_once_then_reinsert_and_remount(file);
    assert(fake.init_calls == 1U);
}

/**
 * @brief 静默的拔出之后卡又在 Process 之前插回：Process 看到的是 NOT_PRESENT → READY，报告一次插入（换卡视同拔卡，
 *        音频流先收尾），挂载成功；不会既不报拔出又不报插入。
 */
static void test_silent_removal_and_quick_reinsertion_is_reported_as_insertion(void)
{
    Service_Filesystem_FileHandleTypeDef file = {0U};
    uint8_t byte;
    uint32_t transferred = 0U;

    clear_script();
    assert(open_test_file(&file) == SERVICE_OK);
    stream_file = file;
    fake.hang_next_read = true;
    fake.pull_card_during_wait = true;
    assert(Service_Filesystem_ReadFile(file, &byte, 1U, &transferred) != SERVICE_OK);
    assert(Platform_SD_GetState() == PLATFORM_SD_STATE_NOT_PRESENT);

    fake.present = true;
    assert(storage_sd_process() == STORAGE_OK);

    assert(stream_card_removed_calls == 1U);
    assert(catalog_init_calls == 1U);
    assert(storage_sd_volume_is_mounted());
    assert(Platform_SD_GetState() == PLATFORM_SD_STATE_READY);
    assert(storage_sd_process() == STORAGE_OK);
    assert(catalog_init_calls == 1U);
}

/**
 * @brief 依次运行用例。
 * @return 全部通过时为 0，断言失败时进程中止。
 */
int main(void)
{
    boot_with_card();
    test_product_read_path_gives_up_after_500_ms_and_aborts();
    test_card_pulled_during_read_is_reported_removed_once();
    test_card_pulled_with_error_event_is_reported_removed_once();
    test_abort_that_does_not_stop_keeps_error_until_card_is_replugged();
    test_silent_removal_and_quick_reinsertion_is_reported_as_insertion();
    puts("sd_removal_chain: all passed");
    return 0;
}
