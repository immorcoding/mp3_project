/**
 * @file test_filesystem_card.c
 * @brief 经公开 Service Interface 验证 SD 卷生命周期的新入口（#210）：闸（暂停与恢复）、卷没挂时的拿锁前后检查、
 *        读写失败时的 `NO_MEDIA`、卸载释放槽、启动与刷新入口，以及 `filesystem_types.h` 的失败分类函数。
 *        真实的 Service、FatFs 与共用 FreeRTOS 桩（含事件组），RAM 盘和 Platform SD 替身替代硬件。
 *
 * @details
 *          主机是单线程的，桩不切换任务也不真的阻塞：这里证明每个分支走对，不证明真实并发。“闸关着时调用者在等”
 *          用桩的阻塞钩子脚本化：调用者将要在闸上阻塞时，钩子替“卷任务”做事（开闸、卸载卷）。
 *          断言会让进程 abort 的用例各在独立进程里跑：环境变量 FILESYSTEM_CARD_SCENARIO 选用例，
 *          未设置时跑全部不会 abort 的用例。
 */

#include "Service/filesystem/filesystem_card.h"
#include "Service/filesystem/filesystem_config.h"
#include "Service/filesystem/filesystem_directory.h"
#include "Service/filesystem/filesystem_file.h"
#include "Service/filesystem/filesystem_service.h"
#include "Service/filesystem/filesystem_types.h"

#include "FATFS/App/fatfs.h"
#include "Middlewares/Third_Party/FatFs/src/ff.h"
#include "Middlewares/Third_Party/FatFs/src/ff_gen_drv.h"

#include "freertos_stub.h"
#include "lock_env_stub.h"

#include <assert.h>
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char SDPath[4];
char USERPath[4];
uint8_t retSD;
uint8_t retUSER;
FATFS SDFatFS;
FATFS USERFatFS;

#define RAM_SECTOR_COUNT 2048U
#define RAM_SECTOR_SIZE 512U
static uint8_t ram_media[2][RAM_SECTOR_COUNT * RAM_SECTOR_SIZE];

/** @brief 每次 RAM 盘读写时调用的钩子：测试用它在持锁的读写中途让“别的任务”做事。 */
typedef void (*DiskHook)(BYTE pdrv);
static DiskHook disk_hook;
static bool fail_disk_reads; /**< 为真时 RAM 盘读返回错误，模拟读写失败（FatFs 给出 FR_DISK_ERR）。 */

/** @brief 卡检测脚替身：为真表示槽里有卡。 */
static bool card_pin_present = true;
static uint32_t card_pin_reads;

/**
 * @brief 主机 RAM 盘初始化。
 * @param[in] pdrv 逻辑盘号，仅允许 0 或 1。
 * @return 固定 0 表示就绪。
 */
static DSTATUS ram_initialize(BYTE pdrv)
{
    assert(pdrv < 2U);
    return 0;
}

/**
 * @brief 主机 RAM 盘状态。
 * @param[in] pdrv 逻辑盘号。
 * @return 固定 0 表示就绪。
 */
static DSTATUS ram_status(BYTE pdrv)
{
    return ram_initialize(pdrv);
}

/**
 * @brief 从对应 RAM 盘复制扇区；先调磁盘钩子，`fail_disk_reads` 为真时 SD 盘返回错误。
 * @param[in] pdrv 逻辑盘号。
 * @param[out] buff 至少 count 个扇区。
 * @param[in] sector 起始扇区。
 * @param[in] count 扇区数。
 * @return RES_OK、RES_PARERR 或脚本化的 RES_ERROR。
 */
static DRESULT ram_read(BYTE pdrv, BYTE *buff, DWORD sector, UINT count)
{
    if ((pdrv >= 2U) || (buff == NULL) || ((sector + count) > RAM_SECTOR_COUNT))
    {
        return RES_PARERR;
    }
    if (disk_hook != NULL)
    {
        disk_hook(pdrv);
    }
    if (fail_disk_reads && (pdrv == 0U))
    {
        return RES_ERROR;
    }
    memcpy(buff, &ram_media[pdrv][sector * RAM_SECTOR_SIZE], count * RAM_SECTOR_SIZE);
    return RES_OK;
}

/**
 * @brief 把扇区写入对应 RAM 盘；先调磁盘钩子。
 * @param[in] pdrv 逻辑盘号。
 * @param[in] buff 至少 count 个扇区。
 * @param[in] sector 起始扇区。
 * @param[in] count 扇区数。
 * @return RES_OK 或 RES_PARERR。
 */
static DRESULT ram_write(BYTE pdrv, const BYTE *buff, DWORD sector, UINT count)
{
    if ((pdrv >= 2U) || (buff == NULL) || ((sector + count) > RAM_SECTOR_COUNT))
    {
        return RES_PARERR;
    }
    if (disk_hook != NULL)
    {
        disk_hook(pdrv);
    }
    memcpy(&ram_media[pdrv][sector * RAM_SECTOR_SIZE], buff, count * RAM_SECTOR_SIZE);
    return RES_OK;
}

/**
 * @brief 提供 mkfs 所需的几何与同步命令。
 * @param[in] pdrv 逻辑盘号。
 * @param[in] cmd ioctl 命令。
 * @param[out] buff 几何输出。
 * @return RES_OK 或 RES_PARERR。
 */
static DRESULT ram_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    assert(pdrv < 2U);
    switch (cmd)
    {
        case CTRL_SYNC:
            return RES_OK;
        case GET_SECTOR_COUNT:
            *(DWORD *)buff = RAM_SECTOR_COUNT;
            return RES_OK;
        case GET_BLOCK_SIZE:
            *(DWORD *)buff = 1U;
            return RES_OK;
        case GET_SECTOR_SIZE:
            *(WORD *)buff = RAM_SECTOR_SIZE;
            return RES_OK;
        default:
            return RES_PARERR;
    }
}

static const Diskio_drvTypeDef ram_driver = {ram_initialize, ram_status, ram_read, ram_write, ram_ioctl};

/* ---------------------------------------------------------------- 传输层与 Platform 替身 */

/** @brief 锁的句柄；按“拿了哪把”识别。 */
typedef struct
{
    SemaphoreHandle_t sd;
    SemaphoreHandle_t flash;
    SemaphoreHandle_t table;
} Locks;

static Locks locks;

/**
 * @brief 某把锁此刻是否被持有（互斥锁取走后再取，非阻塞超时 0 失败；这里用统计差判断，不改变状态）。
 * @param[in] lock 锁句柄。
 * @return true 表示拿的次数多于放的次数。
 */
static bool lock_is_held(SemaphoreHandle_t lock)
{
    const FreertosStub_SemaphoreStats stats = freertos_stub_semaphore_stats(lock);

    return stats.take_successes != stats.give_successes;
}

/**
 * @brief SD 传输层替身：初始化恒成功。
 * @return 恒为 true。
 */
bool filesystem_sd_transfer_init(void)
{
    return true;
}

/**
 * @brief SD 传输层替身：恒已就绪。
 * @return 恒为 true。
 */
bool filesystem_sd_transfer_is_ready(void)
{
    return true;
}

/**
 * @brief SD 传输层替身：卡检测脚。调用者须在 SD 卷锁内（读写失败后分类时）。
 * @return 脚本化的卡检测结果。
 */
bool filesystem_sd_transfer_card_present(void)
{
    card_pin_reads++;
    if (locks.sd != NULL)
    {
        assert(lock_is_held(locks.sd)); /* 卡检测脚只在 SD 卷锁内读。 */
    }
    return card_pin_present;
}

/**
 * @brief Flash 传输层替身：初始化恒成功。
 * @return 恒为 true。
 */
bool filesystem_flash_transfer_init(void)
{
    return true;
}

/**
 * @brief Flash 传输层替身：恒已就绪。
 * @return 恒为 true。
 */
bool filesystem_flash_transfer_is_ready(void)
{
    return true;
}

/**
 * @brief Flash 传输层替身：打开恒成功。
 * @return 恒为 SERVICE_OK。
 */
Service_StatusTypeDef filesystem_flash_transfer_open(void)
{
    return SERVICE_OK;
}

/**
 * @brief Flash 传输层替身：格式化清空 Flash 的 RAM 盘。
 * @return 恒为 SERVICE_OK。
 */
Service_StatusTypeDef filesystem_flash_transfer_format(void)
{
    memset(ram_media[1], 0, sizeof(ram_media[1]));
    return SERVICE_OK;
}

/**
 * @brief Flash 传输层替身：恢复恒成功。
 * @return 恒为 SERVICE_OK。
 */
Service_StatusTypeDef filesystem_flash_transfer_recover(void)
{
    return SERVICE_OK;
}

/**
 * @brief Flash 传输层替身：回收恒成功。
 * @return 恒为 SERVICE_OK。
 */
Service_StatusTypeDef filesystem_flash_transfer_reclaim(void)
{
    return SERVICE_OK;
}

/** @brief Platform SD 替身的可编排状态。 */
typedef struct
{
    Platform_SD_StateTypeDef state;      /**< `Platform_SD_GetState` 的返回值。 */
    bool present;                        /**< `Platform_SD_IsPresent` 的返回值。 */
    Platform_StatusTypeDef init_result;  /**< `Platform_SD_Init` 的返回值。 */
    Platform_StatusTypeDef process_result; /**< `Platform_SD_Process` 的返回值。 */
    Platform_SD_EventTypeDef process_event; /**< `Platform_SD_Process` 给出的事件。 */
    Platform_SD_StateTypeDef state_after_process; /**< `Process` 之后 Device 的状态。 */
    Platform_SD_InfoTypeDef info;        /**< `Platform_SD_GetInfo` 给出的信息。 */
    Platform_SD_DiagnosticsTypeDef diagnostics; /**< `Platform_SD_GetDiagnostics` 给出的诊断。 */
    Platform_SD_DetectCallback_t registered_callback; /**< `Platform_SD_Init` 收到的回调。 */
    void *registered_context;            /**< `Platform_SD_Init` 收到的上下文。 */
    uint32_t init_calls;                 /**< `Platform_SD_Init` 次数。 */
    uint32_t process_calls;              /**< `Platform_SD_Process` 次数。 */
    bool sd_lock_held_in_init;           /**< `Init` 被调用时 SD 卷锁是否被持有。 */
    bool sd_lock_held_in_process;        /**< `Process` 被调用时 SD 卷锁是否被持有。 */
} FakePlatformSd;

static FakePlatformSd fake_sd;

/**
 * @brief Platform SD 替身：记下回调、上下文和调用时 SD 卷锁是否被持有，返回脚本化的结果。
 * @param[in] detect_callback 启动入口转交的卡检测回调。
 * @param[in] detect_context 启动入口转交的上下文。
 * @return `fake_sd.init_result`。
 */
Platform_StatusTypeDef Platform_SD_Init(Platform_SD_DetectCallback_t detect_callback, void *detect_context)
{
    fake_sd.init_calls++;
    fake_sd.registered_callback = detect_callback;
    fake_sd.registered_context = detect_context;
    fake_sd.sd_lock_held_in_init = (locks.sd != NULL) && lock_is_held(locks.sd);
    return fake_sd.init_result;
}

/**
 * @brief Platform SD 替身：记下调用时 SD 卷锁是否被持有；成功时给出脚本化的事件并把状态推进到脚本化的刷新后状态。
 * @param[out] event 接收事件，成功时写入。
 * @return `fake_sd.process_result`。
 */
Platform_StatusTypeDef Platform_SD_Process(Platform_SD_EventTypeDef *event)
{
    fake_sd.process_calls++;
    fake_sd.sd_lock_held_in_process = (locks.sd != NULL) && lock_is_held(locks.sd);
    if (fake_sd.process_result != PLATFORM_OK)
    {
        return fake_sd.process_result;
    }
    *event = fake_sd.process_event;
    fake_sd.state = fake_sd.state_after_process;
    return PLATFORM_OK;
}

/**
 * @brief Platform SD 替身：当前设备状态。
 * @return `fake_sd.state`。
 */
Platform_SD_StateTypeDef Platform_SD_GetState(void)
{
    return fake_sd.state;
}

/**
 * @brief Platform SD 替身：卡检测脚。
 * @return `fake_sd.present`。
 */
bool Platform_SD_IsPresent(void)
{
    return fake_sd.present;
}

/**
 * @brief Platform SD 替身：设备就绪时给出脚本化的容量信息。
 * @param[out] info 接收信息。
 * @retval PLATFORM_OK 设备 READY，信息已写入。
 * @retval PLATFORM_SD_ERROR 设备没有 READY。
 */
Platform_StatusTypeDef Platform_SD_GetInfo(Platform_SD_InfoTypeDef *info)
{
    if (fake_sd.state != PLATFORM_SD_STATE_READY)
    {
        return PLATFORM_SD_ERROR;
    }
    *info = fake_sd.info;
    return PLATFORM_OK;
}

/**
 * @brief Platform SD 替身：给出脚本化的诊断。
 * @param[out] diagnostics 接收诊断。
 * @return 恒为 PLATFORM_OK。
 */
Platform_StatusTypeDef Platform_SD_GetDiagnostics(Platform_SD_DiagnosticsTypeDef *diagnostics)
{
    *diagnostics = fake_sd.diagnostics;
    return PLATFORM_OK;
}

/* ---------------------------------------------------------------- 观察辅助 */

/** @brief 一次调用前后某把锁拿放次数与事件组统计的快照。 */
typedef struct
{
    uint32_t sd_takes;
    uint32_t flash_takes;
    uint32_t sd_waits;
    uint32_t gate_waits;
    uint32_t gate_blocked;
} Snapshot;

static EventGroupHandle_t gate_group;

/**
 * @brief 读当前计数。
 * @return 快照。
 */
static Snapshot snapshot(void)
{
    Snapshot now;
    FreertosStub_SemaphoreStats sd = freertos_stub_semaphore_stats(locks.sd);
    FreertosStub_SemaphoreStats flash = freertos_stub_semaphore_stats(locks.flash);
    FreertosStub_EventGroupStats gate = freertos_stub_event_group_stats(gate_group);

    now.sd_takes = sd.take_successes;
    now.flash_takes = flash.take_successes;
    now.sd_waits = sd.waits;
    now.gate_waits = gate.wait_calls;
    now.gate_blocked = gate.blocked_waits;
    return now;
}

/**
 * @brief 链接双 RAM 盘、初始化并挂载两个卷，然后认出两把卷锁和闸的事件组。
 * @note  必须在第一次调用 Filesystem 之前 reset 桩：锁和闸都是第一次用到时才建的。
 */
static void setup_volumes(void)
{
    uint8_t work[4096];
    TCHAR sd_drive[] = {'0', ':', 0};
    TCHAR user_drive[] = {'1', ':', 0};
    Service_Filesystem_FileInfoTypeDef info;
    SemaphoreHandle_t handles[3];
    uint32_t sd_index = 0U;
    uint32_t flash_index = 0U;

    freertos_stub_reset();
    lock_env_counter_reset();
    lock_env_log_reset();
    memset(ram_media, 0, sizeof(ram_media));
    memset(&fake_sd, 0, sizeof(fake_sd));
    fake_sd.state = PLATFORM_SD_STATE_READY;
    fake_sd.present = true;
    disk_hook = NULL;
    fail_disk_reads = false;
    card_pin_present = true;
    locks = (Locks){NULL, NULL, NULL};
    assert(FATFS_LinkDriverEx(&ram_driver, SDPath, 0) == 0);
    assert(FATFS_LinkDriverEx(&ram_driver, USERPath, 1) == 0);
    retSD = 0U;
    retUSER = 0U;
    assert(Service_Filesystem_InitSD() == SERVICE_OK);
    assert(Service_Filesystem_InitFlash() == SERVICE_OK);
    assert(f_mkfs(sd_drive, FM_FAT | FM_SFD, 0, work, sizeof(work)) == FR_OK);
    assert(f_mkfs(user_drive, FM_FAT | FM_SFD, 0, work, sizeof(work)) == FR_OK);
    assert(Service_Filesystem_MountSD() == SERVICE_OK);
    assert(Service_Filesystem_MountFlash() == SERVICE_OK);

    /* 三把互斥锁：SD 卷锁、Flash 卷锁、表锁。闸是事件组，不占信号量。 */
    assert(freertos_stub_created_semaphore_count() == 3U);
    for (uint32_t i = 0U; i < 3U; i++)
    {
        handles[i] = freertos_stub_created_semaphore(i);
    }
    for (uint32_t i = 0U; i < 3U; i++)
    {
        FreertosStub_SemaphoreStats before = freertos_stub_semaphore_stats(handles[i]);

        (void)Service_Filesystem_GetFileInfo(SERVICE_FILESYSTEM_VOLUME_SD, "none", &info);
        if (freertos_stub_semaphore_stats(handles[i]).take_successes != before.take_successes)
        {
            sd_index = i;
        }
        before = freertos_stub_semaphore_stats(handles[i]);
        (void)Service_Filesystem_GetFileInfo(SERVICE_FILESYSTEM_VOLUME_FLASH, "none", &info);
        if (freertos_stub_semaphore_stats(handles[i]).take_successes != before.take_successes)
        {
            flash_index = i;
        }
    }
    assert(sd_index != flash_index);
    locks.sd = handles[sd_index];
    locks.flash = handles[flash_index];
    locks.table = handles[3U - sd_index - flash_index];

    /* 闸在第一次需要时创建；暂停一次再恢复，让它存在并且是开着的，然后取句柄。 */
    Service_Filesystem_PauseSD();
    Service_Filesystem_ResumeSD();
    gate_group = freertos_stub_last_created_event_group();
    assert(gate_group != NULL);
}

/* ---------------------------------------------------------------- 失败分类函数 */

/**
 * @brief `NO_MEDIA`、`NOT_READY`、`INVALID_HANDLE` 不算文件错误；其余状态（含成功）都不是“卷不可用”。
 */
static void test_failure_classifier_marks_only_volume_unavailable_statuses(void)
{
    assert(Service_Filesystem_StatusIsVolumeUnavailable(SERVICE_NO_MEDIA));
    assert(Service_Filesystem_StatusIsVolumeUnavailable(SERVICE_NOT_READY));
    assert(Service_Filesystem_StatusIsVolumeUnavailable(SERVICE_INVALID_HANDLE));

    assert(!Service_Filesystem_StatusIsVolumeUnavailable(SERVICE_OK));
    assert(!Service_Filesystem_StatusIsVolumeUnavailable(SERVICE_ERROR));
    assert(!Service_Filesystem_StatusIsVolumeUnavailable(SERVICE_INVALID_PARAM));
    assert(!Service_Filesystem_StatusIsVolumeUnavailable(SERVICE_BUSY));
    assert(!Service_Filesystem_StatusIsVolumeUnavailable(SERVICE_TIMEOUT));
    assert(!Service_Filesystem_StatusIsVolumeUnavailable(SERVICE_NO_FILESYSTEM));
    assert(!Service_Filesystem_StatusIsVolumeUnavailable(SERVICE_NO_SPACE));
}

/* ---------------------------------------------------------------- 闸 */

/** @brief 闸上等待时替“卷任务”做事的钩子上下文。 */
typedef struct
{
    uint32_t gate_waits_seen;    /**< 钩子看到的“将要在闸上阻塞”的次数。 */
    uint32_t sd_takes_at_wait;   /**< 最近一次闸上等待时，SD 卷锁已被拿的次数。 */
    bool resume_gate;            /**< 为真时钩子开闸。 */
    bool unmount_first;          /**< 为真时钩子先卸载 SD 卷（卷任务在锁内卸载）再开闸。 */
} GateScript;

static GateScript gate_script;

/**
 * @brief 阻塞钩子：调用者将要在闸上阻塞时，卷任务做事。
 * @param[in] wait 将要进行的等待。
 * @param[in,out] context 未使用。
 */
static void gate_hook(const FreertosStub_Wait *wait, void *context)
{
    (void)context;
    if ((wait->kind == FREERTOS_STUB_WAIT_EVENT_GROUP) && (wait->event_group == gate_group))
    {
        gate_script.gate_waits_seen++;
        gate_script.sd_takes_at_wait = snapshot().sd_takes;
        if (gate_script.unmount_first)
        {
            assert(Service_Filesystem_UnmountSD() == SERVICE_OK);
        }
        if (gate_script.resume_gate)
        {
            Service_Filesystem_ResumeSD();
        }
    }
}

/**
 * @brief 关闸、安装钩子，调用一次，撤掉钩子。
 * @param[in] unmount_first 钩子里先卸载 SD 卷再开闸。
 */
static void begin_gated_call(bool unmount_first)
{
    gate_script = (GateScript){0U, 0U, true, unmount_first};
    Service_Filesystem_PauseSD();
    freertos_stub_set_block_hook(gate_hook, NULL);
}

/** @brief 撤掉钩子并确保闸是开的，供下一个用例使用。 */
static void end_gated_call(void)
{
    freertos_stub_set_block_hook(NULL, NULL);
    Service_Filesystem_ResumeSD();
}

/**
 * @brief 关闸时 SD 的文件与目录入口在拿锁之前等闸：等待发生时 SD 卷锁一次都没拿，开闸之后正常完成。
 *        覆盖路径类入口和句柄类入口。
 */
static void test_sd_entries_wait_at_the_gate_before_taking_the_lock(void)
{
    Service_Filesystem_FileHandleTypeDef file = {0};
    Service_Filesystem_DirectoryHandleTypeDef directory = {0};
    Service_Filesystem_DirectoryEntryTypeDef entry;
    Service_Filesystem_FileInfoTypeDef info;
    uint8_t data[4] = {1, 2, 3, 4};
    uint8_t back[4];
    uint32_t transferred = 0U;
    uint64_t value = 0U;
    Snapshot before;

    /* 没有关闸时也先建好文件和目录，后面每个入口各关一次闸。 */
    assert(Service_Filesystem_CreateDirectory(SERVICE_FILESYSTEM_VOLUME_SD, "Gate") == SERVICE_OK);
    assert(Service_Filesystem_OpenFile(SERVICE_FILESYSTEM_VOLUME_SD, "Gate/r.bin",
                                       SERVICE_FILESYSTEM_FILE_MODE_CREATE_NEW, &file) == SERVICE_OK);
    assert(Service_Filesystem_CloseFile(file) == SERVICE_OK);
    assert(Service_Filesystem_OpenFile(SERVICE_FILESYSTEM_VOLUME_SD, "Gate/a.bin",
                                       SERVICE_FILESYSTEM_FILE_MODE_CREATE_NEW, &file) == SERVICE_OK);
    assert(Service_Filesystem_WriteFile(file, data, sizeof(data), &transferred) == SERVICE_OK);
    assert(Service_Filesystem_OpenDirectory(SERVICE_FILESYSTEM_VOLUME_SD, "Gate", &directory) == SERVICE_OK);

#define EXPECT_GATED(call_expression, expected_status)                                         \
    do                                                                                         \
    {                                                                                          \
        begin_gated_call(false);                                                               \
        before = snapshot();                                                                   \
        assert((call_expression) == (expected_status));                                        \
        assert(gate_script.gate_waits_seen == 1U);                                             \
        assert(gate_script.sd_takes_at_wait == before.sd_takes); /* 等闸时还没拿锁。 */        \
        assert(snapshot().sd_takes == (before.sd_takes + 1U));   /* 开闸之后拿了一次，放了一次。 */ \
        assert(!lock_is_held(locks.sd));                                                       \
        end_gated_call();                                                                      \
    } while (0)

    EXPECT_GATED(Service_Filesystem_GetFileInfo(SERVICE_FILESYSTEM_VOLUME_SD, "Gate/a.bin", &info), SERVICE_OK);
    EXPECT_GATED(Service_Filesystem_SeekFile(file, 0U), SERVICE_OK);
    EXPECT_GATED(Service_Filesystem_GetFilePosition(file, &value), SERVICE_OK);
    EXPECT_GATED(Service_Filesystem_GetFileSize(file, &value), SERVICE_OK);
    EXPECT_GATED(Service_Filesystem_ReadFile(file, back, sizeof(back), &transferred), SERVICE_OK);
    EXPECT_GATED(Service_Filesystem_WriteFile(file, data, sizeof(data), &transferred), SERVICE_OK);
    EXPECT_GATED(Service_Filesystem_SyncFile(file), SERVICE_OK);
    EXPECT_GATED(Service_Filesystem_RewindDirectory(directory), SERVICE_OK);
    EXPECT_GATED(Service_Filesystem_ReadDirectory(directory, &entry), SERVICE_OK);
    EXPECT_GATED(Service_Filesystem_CreateDirectory(SERVICE_FILESYSTEM_VOLUME_SD, "Gate2"), SERVICE_OK);
    EXPECT_GATED(Service_Filesystem_RemoveDirectory(SERVICE_FILESYSTEM_VOLUME_SD, "Gate2"), SERVICE_OK);
    EXPECT_GATED(Service_Filesystem_RenameFile(SERVICE_FILESYSTEM_VOLUME_SD, "Gate/r.bin", "Gate/r2.bin"), SERVICE_OK);
    EXPECT_GATED(Service_Filesystem_RemoveFile(SERVICE_FILESYSTEM_VOLUME_SD, "Gate/a.bin"), SERVICE_BUSY); /* 仍打开着。 */
    EXPECT_GATED(Service_Filesystem_CloseDirectory(directory), SERVICE_OK);
    EXPECT_GATED(Service_Filesystem_CloseFile(file), SERVICE_OK);
    EXPECT_GATED(Service_Filesystem_RemoveFile(SERVICE_FILESYSTEM_VOLUME_SD, "Gate/a.bin"), SERVICE_OK);
    EXPECT_GATED(Service_Filesystem_OpenFile(SERVICE_FILESYSTEM_VOLUME_SD, "Gate/c.bin",
                                             SERVICE_FILESYSTEM_FILE_MODE_CREATE_NEW, &file), SERVICE_OK);
    EXPECT_GATED(Service_Filesystem_CloseFile(file), SERVICE_OK);
    EXPECT_GATED(Service_Filesystem_OpenDirectory(SERVICE_FILESYSTEM_VOLUME_SD, "Gate", &directory), SERVICE_OK);
    EXPECT_GATED(Service_Filesystem_CloseDirectory(directory), SERVICE_OK);
#undef EXPECT_GATED

    assert(Service_Filesystem_RemoveFile(SERVICE_FILESYSTEM_VOLUME_SD, "Gate/c.bin") == SERVICE_OK);
    assert(Service_Filesystem_RemoveFile(SERVICE_FILESYSTEM_VOLUME_SD, "Gate/r2.bin") == SERVICE_OK);
    assert(Service_Filesystem_RemoveDirectory(SERVICE_FILESYSTEM_VOLUME_SD, "Gate") == SERVICE_OK);
}

/**
 * @brief 闸开着时入口不阻塞：等闸的位已置，事件组没有进入阻塞。
 */
static void test_open_gate_does_not_block_entries(void)
{
    Service_Filesystem_FileInfoTypeDef info;
    Snapshot before = snapshot();

    assert(Service_Filesystem_GetFileInfo(SERVICE_FILESYSTEM_VOLUME_SD, "none", &info) != SERVICE_OK);
    assert(snapshot().gate_blocked == before.gate_blocked);
}

/**
 * @brief 闸只用于 SD：关闸时 Flash 的文件与目录入口、Flash 生命周期入口都不等闸。
 */
static void test_gate_does_not_block_flash_entries(void)
{
    Service_Filesystem_FileHandleTypeDef file = {0};
    Service_Filesystem_FileInfoTypeDef info;
    Snapshot before = snapshot();

    Service_Filesystem_PauseSD();
    assert(Service_Filesystem_CreateDirectory(SERVICE_FILESYSTEM_VOLUME_FLASH, "F") == SERVICE_OK);
    assert(Service_Filesystem_OpenFile(SERVICE_FILESYSTEM_VOLUME_FLASH, "F/a.bin",
                                       SERVICE_FILESYSTEM_FILE_MODE_CREATE_NEW, &file) == SERVICE_OK);
    assert(Service_Filesystem_CloseFile(file) == SERVICE_OK);
    assert(Service_Filesystem_GetFileInfo(SERVICE_FILESYSTEM_VOLUME_FLASH, "F/a.bin", &info) == SERVICE_OK);
    assert(Service_Filesystem_RemoveFile(SERVICE_FILESYSTEM_VOLUME_FLASH, "F/a.bin") == SERVICE_OK);
    assert(Service_Filesystem_RemoveDirectory(SERVICE_FILESYSTEM_VOLUME_FLASH, "F") == SERVICE_OK);
    assert(Service_Filesystem_ReclaimFlash() == SERVICE_OK);
    assert(snapshot().gate_waits == before.gate_waits); /* Flash 入口连闸都不看。 */
    assert(snapshot().gate_blocked == before.gate_blocked);
    Service_Filesystem_ResumeSD();
}

/**
 * @brief 卡检测回调替身：什么都不做，只供启动入口登记。
 * @param[in] context 未使用。
 */
static void gate_hook_detect(void *context)
{
    (void)context;
}

/**
 * @brief 磁盘钩子：读盘时（调用者正持着 SD 卷锁）卷任务关闸，只触发一次。
 * @param[in] pdrv 正在读写的逻辑盘号，只在 SD（0）上触发。
 */
static void pause_gate_from_disk_hook(BYTE pdrv)
{
    if ((pdrv == 0U) && lock_is_held(locks.sd))
    {
        disk_hook = NULL;
        Service_Filesystem_PauseSD();
    }
}

/** @brief 等 SD 卷锁时替“卷任务”做事的钩子上下文。 */
typedef struct
{
    SemaphoreHandle_t watched; /**< 被等待的锁。 */
    uint32_t calls;            /**< 钩子在这把锁上被调用的次数。 */
    bool unmount;              /**< 为真时：卷任务放锁、卸载卷（拿放一次），然后让调用者继续。 */
} LockScript;

static LockScript lock_script;

/**
 * @brief 阻塞钩子：调用者在等 SD 卷锁时，卷任务放锁并在锁内卸载 SD 卷。
 * @param[in] wait 将要进行的等待。
 * @param[in,out] context 未使用。
 */
static void unmount_when_waiting_for_the_lock(const FreertosStub_Wait *wait, void *context)
{
    (void)context;
    if ((wait->kind == FREERTOS_STUB_WAIT_SEMAPHORE) && (wait->semaphore == lock_script.watched))
    {
        lock_script.calls++;
        assert(xSemaphoreGive(lock_script.watched) == pdTRUE);
        if (lock_script.unmount)
        {
            assert(Service_Filesystem_UnmountSD() == SERVICE_OK);
        }
    }
}

/**
 * @brief 闸不挡卷任务自己的 SD 生命周期入口：关闸时 Init、Unmount、Mount、刷新、启动都照常完成，一次闸上等待都没有。
 */
static void test_gate_does_not_block_the_volume_task_lifecycle_entries(void)
{
    Service_Filesystem_CardStatusTypeDef card;
    Snapshot before = snapshot();

    Service_Filesystem_PauseSD();
    assert(Service_Filesystem_UnmountSD() == SERVICE_OK);
    assert(Service_Filesystem_InitSD() == SERVICE_OK);
    assert(Service_Filesystem_MountSD() == SERVICE_OK);
    fake_sd.process_event = PLATFORM_SD_EVENT_NONE;
    fake_sd.state_after_process = PLATFORM_SD_STATE_READY;
    assert(Service_Filesystem_RefreshSD(&card) == SERVICE_OK);
    assert(Service_Filesystem_StartSD(gate_hook_detect, NULL, &card) == SERVICE_OK);
    assert(snapshot().gate_waits == before.gate_waits);
    assert(snapshot().gate_blocked == before.gate_blocked);
    Service_Filesystem_ResumeSD();
}

/**
 * @brief 已持锁的调用不被打断：读写进行中（SD 卷锁已被持有）卷任务关闸，这次调用照常完成；
 *        关闸之后的下一次调用才等。
 */
static void test_gate_closed_during_a_call_does_not_interrupt_it(void)
{
    Service_Filesystem_FileHandleTypeDef file = {0};
    uint8_t buffer[2048];
    uint8_t data[2048];
    uint32_t transferred = 0U;

    memset(data, 0x3C, sizeof(data));
    assert(Service_Filesystem_OpenFile(SERVICE_FILESYSTEM_VOLUME_SD, "flight.bin",
                                       SERVICE_FILESYSTEM_FILE_MODE_CREATE_NEW, &file) == SERVICE_OK);
    assert(Service_Filesystem_WriteFile(file, data, sizeof(data), &transferred) == SERVICE_OK);
    assert(Service_Filesystem_SyncFile(file) == SERVICE_OK);
    assert(Service_Filesystem_SeekFile(file, 0U) == SERVICE_OK);

    disk_hook = pause_gate_from_disk_hook; /* 读盘时（持 SD 卷锁）卷任务关闸。 */
    gate_script = (GateScript){0U, 0U, true, false};
    assert(Service_Filesystem_ReadFile(file, buffer, sizeof(buffer), &transferred) == SERVICE_OK);
    disk_hook = NULL;
    assert(transferred == sizeof(buffer));
    assert(memcmp(buffer, data, sizeof(data)) == 0);
    assert(gate_script.gate_waits_seen == 0U); /* 在途的调用没有等闸。 */

    /* 闸现在是关着的：下一次调用才等，开闸后完成。 */
    freertos_stub_set_block_hook(gate_hook, NULL);
    assert(Service_Filesystem_SeekFile(file, 0U) == SERVICE_OK);
    assert(gate_script.gate_waits_seen == 1U);
    end_gated_call();
    assert(Service_Filesystem_CloseFile(file) == SERVICE_OK);
    assert(Service_Filesystem_RemoveFile(SERVICE_FILESYSTEM_VOLUME_SD, "flight.bin") == SERVICE_OK);
}

/**
 * @brief 暂停和恢复是标志而不是计数：重复暂停之后一次恢复就够，重复恢复无副作用。
 */
static void test_pause_and_resume_are_idempotent_flags_not_counters(void)
{
    Service_Filesystem_FileInfoTypeDef info;
    Snapshot before;

    Service_Filesystem_PauseSD();
    Service_Filesystem_PauseSD(); /* 消抖期间每个新边沿都会再关一次。 */
    Service_Filesystem_ResumeSD(); /* 一次恢复就够。 */
    before = snapshot();
    assert(Service_Filesystem_GetFileInfo(SERVICE_FILESYSTEM_VOLUME_SD, "none", &info) != SERVICE_OK);
    assert(snapshot().gate_blocked == before.gate_blocked);
    Service_Filesystem_ResumeSD();
    Service_Filesystem_ResumeSD();
    assert(Service_Filesystem_GetFileInfo(SERVICE_FILESYSTEM_VOLUME_SD, "none", &info) != SERVICE_OK);
    assert(snapshot().gate_blocked == before.gate_blocked);
}

/* ---------------------------------------------------------------- 卷没挂 */

/**
 * @brief 卷没挂时按路径的入口在拿锁之前就返回 NOT_READY：SD 卷锁被“别的任务”占着也不等，闸关着也不等。
 */
static void test_unmounted_path_entries_return_not_ready_without_waiting_for_lock_or_gate(void)
{
    Service_Filesystem_FileHandleTypeDef file = {0};
    Service_Filesystem_DirectoryHandleTypeDef directory = {0};
    Service_Filesystem_FileInfoTypeDef info;
    Snapshot before;
    uint32_t unsatisfied;

    assert(Service_Filesystem_UnmountSD() == SERVICE_OK);
    assert(xSemaphoreTake(locks.sd, 0U) == pdTRUE); /* 别的任务（比如卷任务在挂载）占着 SD 卷锁。 */
    Service_Filesystem_PauseSD();
    before = snapshot();
    unsatisfied = freertos_stub_unsatisfied_infinite_waits();

    assert(Service_Filesystem_OpenFile(SERVICE_FILESYSTEM_VOLUME_SD, "x", SERVICE_FILESYSTEM_FILE_MODE_READ, &file) ==
           SERVICE_NOT_READY);
    assert(Service_Filesystem_OpenDirectory(SERVICE_FILESYSTEM_VOLUME_SD, "", &directory) == SERVICE_NOT_READY);
    assert(Service_Filesystem_GetFileInfo(SERVICE_FILESYSTEM_VOLUME_SD, "x", &info) == SERVICE_NOT_READY);
    assert(Service_Filesystem_RemoveFile(SERVICE_FILESYSTEM_VOLUME_SD, "x") == SERVICE_NOT_READY);
    assert(Service_Filesystem_RenameFile(SERVICE_FILESYSTEM_VOLUME_SD, "x", "y") == SERVICE_NOT_READY);
    assert(Service_Filesystem_CreateDirectory(SERVICE_FILESYSTEM_VOLUME_SD, "x") == SERVICE_NOT_READY);
    assert(Service_Filesystem_RemoveDirectory(SERVICE_FILESYSTEM_VOLUME_SD, "x") == SERVICE_NOT_READY);

    assert(freertos_stub_unsatisfied_infinite_waits() == unsatisfied);
    assert(snapshot().sd_waits == before.sd_waits);         /* 没有等锁。 */
    assert(snapshot().gate_blocked == before.gate_blocked); /* 没有等闸。 */
    assert(snapshot().sd_takes == before.sd_takes);
    assert(xSemaphoreGive(locks.sd) == pdTRUE);
    Service_Filesystem_ResumeSD();
    assert(Service_Filesystem_MountSD() == SERVICE_OK);
}

/**
 * @brief 卷没挂时按句柄的入口返回 INVALID_HANDLE（卷没挂则其上所有句柄都已失效），同样不等锁不等闸；
 *        卸载使旧句柄失效，挂回后旧句柄仍然无效，新打开的句柄有效。
 */
static void test_unmounted_handle_entries_return_invalid_handle_without_waiting(void)
{
    Service_Filesystem_FileHandleTypeDef old_file = {0};
    Service_Filesystem_FileHandleTypeDef new_file = {0};
    Service_Filesystem_DirectoryHandleTypeDef old_directory = {0};
    Service_Filesystem_DirectoryEntryTypeDef entry;
    uint8_t byte = 0U;
    uint32_t transferred = 0U;
    uint64_t value = 0U;
    Snapshot before;

    assert(Service_Filesystem_OpenFile(SERVICE_FILESYSTEM_VOLUME_SD, "h.bin", SERVICE_FILESYSTEM_FILE_MODE_CREATE_NEW,
                                       &old_file) == SERVICE_OK);
    assert(Service_Filesystem_CloseFile(old_file) == SERVICE_OK);
    assert(Service_Filesystem_OpenFile(SERVICE_FILESYSTEM_VOLUME_SD, "h.bin", SERVICE_FILESYSTEM_FILE_MODE_READ,
                                       &old_file) == SERVICE_OK);
    assert(Service_Filesystem_OpenDirectory(SERVICE_FILESYSTEM_VOLUME_SD, "", &old_directory) == SERVICE_OK);
    assert(Service_Filesystem_UnmountSD() == SERVICE_OK);
    assert(xSemaphoreTake(locks.sd, 0U) == pdTRUE);
    Service_Filesystem_PauseSD();
    before = snapshot();

    assert(Service_Filesystem_ReadFile(old_file, &byte, 1U, &transferred) == SERVICE_INVALID_HANDLE);
    assert(Service_Filesystem_WriteFile(old_file, &byte, 1U, &transferred) == SERVICE_INVALID_HANDLE);
    assert(Service_Filesystem_SeekFile(old_file, 0U) == SERVICE_INVALID_HANDLE);
    assert(Service_Filesystem_GetFilePosition(old_file, &value) == SERVICE_INVALID_HANDLE);
    assert(Service_Filesystem_GetFileSize(old_file, &value) == SERVICE_INVALID_HANDLE);
    assert(Service_Filesystem_SyncFile(old_file) == SERVICE_INVALID_HANDLE);
    assert(Service_Filesystem_CloseFile(old_file) == SERVICE_INVALID_HANDLE);
    assert(Service_Filesystem_ReadDirectory(old_directory, &entry) == SERVICE_INVALID_HANDLE);
    assert(Service_Filesystem_RewindDirectory(old_directory) == SERVICE_INVALID_HANDLE);
    assert(Service_Filesystem_CloseDirectory(old_directory) == SERVICE_INVALID_HANDLE);
    assert(snapshot().sd_waits == before.sd_waits);
    assert(snapshot().gate_blocked == before.gate_blocked);
    assert(snapshot().sd_takes == before.sd_takes);
    assert(xSemaphoreGive(locks.sd) == pdTRUE);
    Service_Filesystem_ResumeSD();

    assert(Service_Filesystem_MountSD() == SERVICE_OK);
    assert(Service_Filesystem_ReadFile(old_file, &byte, 1U, &transferred) == SERVICE_INVALID_HANDLE);
    assert(Service_Filesystem_OpenFile(SERVICE_FILESYSTEM_VOLUME_SD, "h.bin", SERVICE_FILESYSTEM_FILE_MODE_READ,
                                       &new_file) == SERVICE_OK);
    assert(Service_Filesystem_ReadFile(old_file, &byte, 1U, &transferred) == SERVICE_INVALID_HANDLE);
    assert(Service_Filesystem_CloseFile(new_file) == SERVICE_OK);
    assert(Service_Filesystem_RemoveFile(SERVICE_FILESYSTEM_VOLUME_SD, "h.bin") == SERVICE_OK);
}

/**
 * @brief 第二次检查（拿到锁之后）：第一次检查通过、拿锁等待期间，卷任务在锁内卸载了卷——调用者拿到锁后
 *        发现卷没挂，放锁，返回 NOT_READY（按路径）或 INVALID_HANDLE（按句柄），不碰 FatFs，锁拿放平衡。
 */
static void test_unmount_while_waiting_for_the_lock_is_caught_after_taking_it(void)
{
    Service_Filesystem_FileHandleTypeDef file = {0};
    Service_Filesystem_FileHandleTypeDef opened = {0};
    Service_Filesystem_FileInfoTypeDef info;
    uint32_t transferred = 0U;
    uint8_t byte = 0U;
    Snapshot before;

    assert(Service_Filesystem_OpenFile(SERVICE_FILESYSTEM_VOLUME_SD, "w.bin", SERVICE_FILESYSTEM_FILE_MODE_CREATE_NEW,
                                       &file) == SERVICE_OK);
    assert(Service_Filesystem_CloseFile(file) == SERVICE_OK);

    /* 路径类入口。 */
    assert(xSemaphoreTake(locks.sd, 0U) == pdTRUE);
    lock_script = (LockScript){locks.sd, 0U, true};
    freertos_stub_set_block_hook(unmount_when_waiting_for_the_lock, NULL);
    before = snapshot();
    assert(Service_Filesystem_GetFileInfo(SERVICE_FILESYSTEM_VOLUME_SD, "w.bin", &info) == SERVICE_NOT_READY);
    freertos_stub_set_block_hook(NULL, NULL);
    assert(lock_script.calls == 1U);
    /* 卷任务的卸载拿放一次，调用者自己拿放一次（拿到后发现卷没挂，放掉）。 */
    assert(snapshot().sd_takes == (before.sd_takes + 2U));
    assert(!lock_is_held(locks.sd));
    assert(Service_Filesystem_MountSD() == SERVICE_OK);

    /* 句柄类入口：钩子里先卸载，句柄随之失效。 */
    assert(Service_Filesystem_OpenFile(SERVICE_FILESYSTEM_VOLUME_SD, "w.bin", SERVICE_FILESYSTEM_FILE_MODE_READ,
                                       &opened) == SERVICE_OK);
    assert(xSemaphoreTake(locks.sd, 0U) == pdTRUE);
    lock_script = (LockScript){locks.sd, 0U, true};
    freertos_stub_set_block_hook(unmount_when_waiting_for_the_lock, NULL);
    before = snapshot();
    assert(Service_Filesystem_ReadFile(opened, &byte, 1U, &transferred) == SERVICE_INVALID_HANDLE);
    freertos_stub_set_block_hook(NULL, NULL);
    assert(lock_script.calls == 1U);
    assert(snapshot().sd_takes == (before.sd_takes + 2U));
    assert(!lock_is_held(locks.sd));
    assert(Service_Filesystem_MountSD() == SERVICE_OK);
    assert(Service_Filesystem_RemoveFile(SERVICE_FILESYSTEM_VOLUME_SD, "w.bin") == SERVICE_OK);
}

/**
 * @brief 第一次检查之后、等闸期间卷被卸载：开闸后调用者不去拿锁，直接 NOT_READY；这段时间调用者自己没拿过锁。
 */
static void test_unmount_while_waiting_at_the_gate_is_caught_before_taking_the_lock(void)
{
    Service_Filesystem_FileInfoTypeDef info;
    Snapshot before;

    begin_gated_call(true); /* 钩子里先由卷任务卸载（拿放一次 SD 卷锁），再开闸。 */
    before = snapshot();
    assert(Service_Filesystem_GetFileInfo(SERVICE_FILESYSTEM_VOLUME_SD, "x", &info) == SERVICE_NOT_READY);
    assert(gate_script.gate_waits_seen == 1U);
    assert(snapshot().sd_takes == (before.sd_takes + 1U)); /* 只有卷任务的那一次卸载。 */
    end_gated_call();
    assert(Service_Filesystem_MountSD() == SERVICE_OK);
}

/**
 * @brief 卸载使本卷全部槽释放：SD 的 4 个文件槽和 2 个目录槽占满，卸载再挂载之后又能全部占满；旧句柄无效。
 */
static void test_unmount_releases_all_slots_of_the_volume(void)
{
    Service_Filesystem_FileHandleTypeDef files[4] = {{0}};
    Service_Filesystem_FileHandleTypeDef extra_file = {0};
    Service_Filesystem_DirectoryHandleTypeDef directories[2] = {{0}};
    Service_Filesystem_DirectoryHandleTypeDef extra_directory = {0};
    Service_Filesystem_FileHandleTypeDef flash_file = {0};
    static const char *const names[] = {"u0.bin", "u1.bin", "u2.bin", "u3.bin"};
    uint8_t byte = 0U;
    uint32_t transferred = 0U;

    assert(Service_Filesystem_OpenFile(SERVICE_FILESYSTEM_VOLUME_FLASH, "keep.bin",
                                       SERVICE_FILESYSTEM_FILE_MODE_CREATE_NEW, &flash_file) == SERVICE_OK);
    for (uint32_t i = 0U; i < 4U; i++)
    {
        assert(Service_Filesystem_OpenFile(SERVICE_FILESYSTEM_VOLUME_SD, names[i],
                                           SERVICE_FILESYSTEM_FILE_MODE_CREATE_NEW, &files[i]) == SERVICE_OK);
        assert(Service_Filesystem_CloseFile(files[i]) == SERVICE_OK);
    }
    for (uint32_t round = 0U; round < 2U; round++)
    {
        for (uint32_t i = 0U; i < 4U; i++)
        {
            assert(Service_Filesystem_OpenFile(SERVICE_FILESYSTEM_VOLUME_SD, names[i],
                                               SERVICE_FILESYSTEM_FILE_MODE_READ_WRITE, &files[i]) == SERVICE_OK);
        }
        assert(Service_Filesystem_OpenDirectory(SERVICE_FILESYSTEM_VOLUME_SD, "", &directories[0]) == SERVICE_OK);
        assert(Service_Filesystem_OpenDirectory(SERVICE_FILESYSTEM_VOLUME_SD, "", &directories[1]) == SERVICE_OK);
        assert(Service_Filesystem_OpenFile(SERVICE_FILESYSTEM_VOLUME_SD, "u4.bin",
                                           SERVICE_FILESYSTEM_FILE_MODE_CREATE_NEW, &extra_file) == SERVICE_BUSY);
        assert(Service_Filesystem_OpenDirectory(SERVICE_FILESYSTEM_VOLUME_SD, "", &extra_directory) == SERVICE_BUSY);

        assert(Service_Filesystem_UnmountSD() == SERVICE_OK);
        for (uint32_t i = 0U; i < 4U; i++)
        {
            assert(Service_Filesystem_ReadFile(files[i], &byte, 1U, &transferred) == SERVICE_INVALID_HANDLE);
        }
        assert(Service_Filesystem_MountSD() == SERVICE_OK); /* 有槽还占着的话挂载会返回 BUSY。 */
    }

    /* 另一卷不受影响。 */
    assert(Service_Filesystem_CloseFile(flash_file) == SERVICE_OK);
    assert(Service_Filesystem_RemoveFile(SERVICE_FILESYSTEM_VOLUME_FLASH, "keep.bin") == SERVICE_OK);
    for (uint32_t i = 0U; i < 4U; i++)
    {
        assert(Service_Filesystem_RemoveFile(SERVICE_FILESYSTEM_VOLUME_SD, names[i]) == SERVICE_OK);
    }
}

/* ---------------------------------------------------------------- NO_MEDIA */

/**
 * @brief 读写失败（FatFs 给出磁盘错误，Service 映射为 ERROR）且卡检测脚显示卡不在：返回 NO_MEDIA，
 *        卡检测脚在 SD 卷锁内读（替身里断言）；卡还在则仍是 ERROR，表示是文件或介质自己的问题。
 *        FatFs 的扇区窗口缓存着目录扇区，目录和路径类入口在主机上很难让读盘失败，所以这里用文件数据读；
 *        所有入口的出口是同一段代码（`filesystem_entry_leave`）。
 */
static void test_failed_sd_read_with_the_card_gone_returns_no_media_inside_the_lock(void)
{
    Service_Filesystem_FileHandleTypeDef file = {0};
    uint8_t data[2048];
    uint8_t buffer[2048];
    uint32_t transferred = 0U;
    uint32_t reads_before;

    memset(data, 0x5A, sizeof(data));
    assert(Service_Filesystem_OpenFile(SERVICE_FILESYSTEM_VOLUME_SD, "nm.bin", SERVICE_FILESYSTEM_FILE_MODE_CREATE_NEW,
                                       &file) == SERVICE_OK);
    assert(Service_Filesystem_WriteFile(file, data, sizeof(data), &transferred) == SERVICE_OK);
    assert(Service_Filesystem_SyncFile(file) == SERVICE_OK);
    assert(Service_Filesystem_SeekFile(file, 0U) == SERVICE_OK);

    /* 卡还在，读失败：仍是 ERROR，调用者当文件错误。 */
    fail_disk_reads = true;
    card_pin_present = true;
    assert(Service_Filesystem_ReadFile(file, buffer, sizeof(buffer), &transferred) == SERVICE_ERROR);

    /* 卡已不在：同样的失败返回 NO_MEDIA，不算文件错误。 */
    card_pin_present = false;
    reads_before = card_pin_reads;
    assert(Service_Filesystem_ReadFile(file, buffer, sizeof(buffer), &transferred) == SERVICE_NO_MEDIA);
    assert(card_pin_reads == (reads_before + 1U));
    assert(Service_Filesystem_StatusIsVolumeUnavailable(SERVICE_NO_MEDIA));
    assert(!lock_is_held(locks.sd));

    /* 恢复，清理。FatFs 在磁盘错误后把对象标成出错，关不掉；卷任务的卸载把它们连同槽一起释放。 */
    fail_disk_reads = false;
    card_pin_present = true;
    assert(Service_Filesystem_UnmountSD() == SERVICE_OK);
    assert(Service_Filesystem_MountSD() == SERVICE_OK);
    assert(Service_Filesystem_RemoveFile(SERVICE_FILESYSTEM_VOLUME_SD, "nm.bin") == SERVICE_OK);
}

/**
 * @brief 卡检测脚只在失败时看：成功的调用，以及不是“读写失败”的其他失败（槽用尽、已存在、参数错），
 *        即使卡不在也保持原状态，且一次脚都不读。
 */
static void test_card_pin_is_read_only_for_generic_failures(void)
{
    Service_Filesystem_FileHandleTypeDef file = {0};
    Service_Filesystem_FileHandleTypeDef again = {0};
    Service_Filesystem_FileInfoTypeDef info;
    uint32_t reads_before;

    card_pin_present = false;
    reads_before = card_pin_reads;
    assert(Service_Filesystem_OpenFile(SERVICE_FILESYSTEM_VOLUME_SD, "pin.bin", SERVICE_FILESYSTEM_FILE_MODE_CREATE_NEW,
                                       &file) == SERVICE_OK);
    assert(Service_Filesystem_OpenFile(SERVICE_FILESYSTEM_VOLUME_SD, "pin.bin", SERVICE_FILESYSTEM_FILE_MODE_CREATE_NEW,
                                       &again) == SERVICE_BUSY);                 /* 已存在。 */
    assert(Service_Filesystem_OpenFile(SERVICE_FILESYSTEM_VOLUME_SD, "../x", SERVICE_FILESYSTEM_FILE_MODE_READ,
                                       &again) == SERVICE_INVALID_PARAM);       /* 路径非法。 */
    assert(Service_Filesystem_CloseFile(file) == SERVICE_OK);
    assert(card_pin_reads == reads_before);

    /* Flash 没有卡检测脚：失败照常是 ERROR，一次脚都不读。 */
    assert(Service_Filesystem_GetFileInfo(SERVICE_FILESYSTEM_VOLUME_FLASH, "missing.bin", &info) == SERVICE_ERROR);
    assert(card_pin_reads == reads_before);

    card_pin_present = true;
    assert(Service_Filesystem_RemoveFile(SERVICE_FILESYSTEM_VOLUME_SD, "pin.bin") == SERVICE_OK);
}

/* ---------------------------------------------------------------- 启动与刷新入口 */

/**
 * @brief 启动入口：在 SD 卷锁内把回调和上下文交给 Platform，回报卡的现状（含容量）；无卡同样成功；
 *        Platform 初始化失败返回 ERROR 并带诊断；空指针在拿锁之前被拒。
 */
static void test_start_registers_the_detect_callback_inside_the_sd_lock(void)
{
    Service_Filesystem_CardStatusTypeDef card;
    int context_marker = 0;
    Snapshot before = snapshot();

    fake_sd.init_calls = 0U;
    /* 卡在且就绪。 */
    fake_sd.state = PLATFORM_SD_STATE_READY;
    fake_sd.present = true;
    fake_sd.info = (Platform_SD_InfoTypeDef){.CapacityBytes = 7818182656ULL, .BlockCount = 15269888U, .BlockSize = 512U};
    memset(&card, 0xFF, sizeof(card));
    assert(Service_Filesystem_StartSD(gate_hook_detect, &context_marker, &card) == SERVICE_OK);
    assert(fake_sd.init_calls == 1U);
    assert(fake_sd.registered_callback == gate_hook_detect);
    assert(fake_sd.registered_context == &context_marker);
    assert(fake_sd.sd_lock_held_in_init);
    assert(snapshot().sd_takes == (before.sd_takes + 1U));
    assert(!lock_is_held(locks.sd));
    assert(card.Present && card.Ready && card.Reinitialized);
    assert(card.CapacityBytes == 7818182656ULL);
    assert(card.BlockSize == 512U);

    /* 无卡：成功，Ready 为假，没有容量，没有“重新初始化过”。 */
    fake_sd.state = PLATFORM_SD_STATE_NOT_PRESENT;
    fake_sd.present = false;
    assert(Service_Filesystem_StartSD(gate_hook_detect, NULL, &card) == SERVICE_OK);
    assert(!card.Present && !card.Ready && !card.Reinitialized);
    assert((card.CapacityBytes == 0U) && (card.BlockSize == 0U));

    /* Platform 初始化失败（卡在，初始化出错）：ERROR，诊断带回。 */
    fake_sd.init_result = PLATFORM_SD_ERROR;
    fake_sd.state = PLATFORM_SD_STATE_ERROR;
    fake_sd.present = true;
    fake_sd.diagnostics = (Platform_SD_DiagnosticsTypeDef){.DeviceError = 3U, .PortStatus = 9U};
    assert(Service_Filesystem_StartSD(gate_hook_detect, NULL, &card) == SERVICE_ERROR);
    assert(card.Present && !card.Ready && !card.Reinitialized);
    assert((card.DeviceError == 3U) && (card.PortStatus == 9U));
    fake_sd.init_result = PLATFORM_OK;

    /* 空指针在拿锁之前被拒，Platform 不被调用。 */
    before = snapshot();
    fake_sd.init_calls = 0U;
    assert(Service_Filesystem_StartSD(NULL, NULL, &card) == SERVICE_INVALID_PARAM);
    assert(Service_Filesystem_StartSD(gate_hook_detect, NULL, NULL) == SERVICE_INVALID_PARAM);
    assert(fake_sd.init_calls == 0U);
    assert(snapshot().sd_takes == before.sd_takes);
}

/**
 * @brief 刷新入口：在 SD 卷锁内调 Platform 的刷新，给出在不在、就绪否、是否重新初始化过、容量和诊断；
 *        刷新失败时也回报现状（卡在不在），卷任务据此对账；只拿 SD 卷锁一次，不碰 Flash 锁和表锁。
 */
static void test_refresh_reports_the_card_inside_the_sd_lock(void)
{
    Service_Filesystem_CardStatusTypeDef card;
    Snapshot before;

    fake_sd.info = (Platform_SD_InfoTypeDef){.CapacityBytes = 31914983424ULL, .BlockCount = 62333952U, .BlockSize = 512U};
    fake_sd.diagnostics = (Platform_SD_DiagnosticsTypeDef){0U, 0U};

    /* 假抖动：没变化，卡在、就绪、没有重新初始化。 */
    fake_sd.present = true;
    fake_sd.process_event = PLATFORM_SD_EVENT_NONE;
    fake_sd.state_after_process = PLATFORM_SD_STATE_READY;
    before = snapshot();
    fake_sd.process_calls = 0U;
    assert(Service_Filesystem_RefreshSD(&card) == SERVICE_OK);
    assert(fake_sd.process_calls == 1U);
    assert(fake_sd.sd_lock_held_in_process);
    assert(snapshot().sd_takes == (before.sd_takes + 1U));
    assert(snapshot().flash_takes == before.flash_takes);
    assert(card.Present && card.Ready && !card.Reinitialized);
    assert(card.CapacityBytes == 31914983424ULL);
    assert(card.BlockSize == 512U);

    /* 换卡或被吞的拔出：Platform 报告 INSERTED，重新初始化过。 */
    fake_sd.process_event = PLATFORM_SD_EVENT_INSERTED;
    assert(Service_Filesystem_RefreshSD(&card) == SERVICE_OK);
    assert(card.Present && card.Ready && card.Reinitialized);

    /* 拔卡：REMOVED，卡不在，没有容量。 */
    fake_sd.process_event = PLATFORM_SD_EVENT_REMOVED;
    fake_sd.state_after_process = PLATFORM_SD_STATE_NOT_PRESENT;
    fake_sd.present = false;
    assert(Service_Filesystem_RefreshSD(&card) == SERVICE_OK);
    assert(!card.Present && !card.Ready && !card.Reinitialized);
    assert((card.CapacityBytes == 0U) && (card.BlockSize == 0U));

    /* 卡在但设备 ERROR、又没有边沿：刷新成功，但没就绪，卷任务据此卸载。 */
    fake_sd.process_event = PLATFORM_SD_EVENT_NONE;
    fake_sd.state_after_process = PLATFORM_SD_STATE_ERROR;
    fake_sd.present = true;
    fake_sd.diagnostics = (Platform_SD_DiagnosticsTypeDef){.DeviceError = 2U, .PortStatus = 5U};
    assert(Service_Filesystem_RefreshSD(&card) == SERVICE_OK);
    assert(card.Present && !card.Ready && !card.Reinitialized);
    assert((card.DeviceError == 2U) && (card.PortStatus == 5U));

    /* 刷新失败：ERROR，现状照填；卡不在时卷任务同样卸载。 */
    fake_sd.process_result = PLATFORM_SD_ERROR;
    fake_sd.state = PLATFORM_SD_STATE_ERROR;
    fake_sd.present = false;
    memset(&card, 0xFF, sizeof(card));
    assert(Service_Filesystem_RefreshSD(&card) == SERVICE_ERROR);
    assert(!card.Present && !card.Ready && !card.Reinitialized);
    assert((card.DeviceError == 2U) && (card.PortStatus == 5U));
    fake_sd.process_result = PLATFORM_OK;

    /* 空指针在拿锁之前被拒。 */
    before = snapshot();
    fake_sd.process_calls = 0U;
    assert(Service_Filesystem_RefreshSD(NULL) == SERVICE_INVALID_PARAM);
    assert(fake_sd.process_calls == 0U);
    assert(snapshot().sd_takes == before.sd_takes);
    assert(!lock_is_held(locks.sd));
}

/* ---------------------------------------------------------------- 各在独立进程里跑的断言用例 */

static jmp_buf abort_jump;
static volatile sig_atomic_t abort_seen;

/**
 * @brief SIGABRT 处理：把 configASSERT 停机变成可检查的事件。
 * @param[in] signal_number 信号编号。
 */
static void on_abort(int signal_number)
{
    (void)signal_number;
    abort_seen = 1;
    longjmp(abort_jump, 1);
}

/**
 * @brief 调用 fn，返回它是否让进程 abort（被 SIGABRT 处理函数接住）。
 * @param[in] fn 要调用的函数。
 * @return true 表示 fn 触发了 configASSERT 停机。
 */
static bool run_expecting_abort(void (*fn)(void))
{
    void (*previous)(int);

    abort_seen = 0;
    previous = signal(SIGABRT, on_abort); /* Windows 运行库投递信号时把处理函数复位成默认，所以每次都要装。 */
    assert(previous != SIG_ERR);
    if (setjmp(abort_jump) == 0)
    {
        fn();
    }
    (void)signal(SIGABRT, previous);
    return abort_seen != 0;
}

static Service_Filesystem_FileHandleTypeDef scenario_file;
static uint32_t gate_waits_at_nested_call;

/** @brief 读 scenario_file（读盘钩子会在持 SD 卷锁时进入别的入口）。 */
static void call_read_scenario_file(void)
{
    uint8_t buffer[2048];
    uint32_t transferred = 0U;

    (void)Service_Filesystem_ReadFile(scenario_file, buffer, sizeof(buffer), &transferred);
}

/**
 * @brief 磁盘钩子：持 SD 卷锁读盘时，先关闸，再进入另一个 SD 入口——持着锁去等闸。
 * @param[in] pdrv 正在读写的逻辑盘号，只在 SD（0）上触发。
 */
static void disk_hook_waits_at_the_gate_while_holding_the_lock(BYTE pdrv)
{
    Service_Filesystem_FileInfoTypeDef info;

    if ((pdrv == 0U) && lock_is_held(locks.sd))
    {
        disk_hook = NULL;
        Service_Filesystem_PauseSD();
        gate_waits_at_nested_call = snapshot().gate_waits;
        (void)Service_Filesystem_GetFileInfo(SERVICE_FILESYSTEM_VOLUME_SD, "x", &info);
    }
}

/**
 * @brief 持着 SD 卷锁去等闸：在等之前断言停机（死锁：卷任务要这把锁才能对账和开闸），而不是真的去等。
 * @return 0 表示断言触发且发生在等闸之前。
 */
static int scenario_waiting_at_the_gate_while_holding_a_lock_stops(void)
{
    uint8_t data[2048] = {0};
    uint32_t transferred = 0U;

    assert(Service_Filesystem_OpenFile(SERVICE_FILESYSTEM_VOLUME_SD, "dl.bin", SERVICE_FILESYSTEM_FILE_MODE_CREATE_NEW,
                                       &scenario_file) == SERVICE_OK);
    assert(Service_Filesystem_WriteFile(scenario_file, data, sizeof(data), &transferred) == SERVICE_OK);
    assert(Service_Filesystem_SyncFile(scenario_file) == SERVICE_OK);
    assert(Service_Filesystem_SeekFile(scenario_file, 0U) == SERVICE_OK);

    disk_hook = disk_hook_waits_at_the_gate_while_holding_the_lock;
    if (!run_expecting_abort(call_read_scenario_file))
    {
        (void)fprintf(stderr, "waiting at the gate while holding the SD lock did not stop\n");
        return 1;
    }
    disk_hook = NULL;
    if (snapshot().gate_waits != gate_waits_at_nested_call)
    {
        (void)fprintf(stderr, "the stop happened after the wait on the gate began\n");
        return 1;
    }
    return 0;
}

static Service_Filesystem_CardStatusTypeDef scenario_card;

/** @brief 调用 StartSD。 */
static void call_start_sd(void)
{
    (void)Service_Filesystem_StartSD(gate_hook_detect, NULL, &scenario_card);
}

/** @brief 调用 RefreshSD。 */
static void call_refresh_sd(void)
{
    (void)Service_Filesystem_RefreshSD(&scenario_card);
}

/**
 * @brief 登记在拒绝名单里的任务进入启动或刷新入口，在等锁之前停机（SD 卷锁被占着时一次等待都没有）。
 * @param[in] entry 要调用的入口。
 * @return 0 表示停机且没有等锁。
 */
static int denied_caller_stops_before_waiting(void (*entry)(void))
{
    const TaskHandle_t allowed = xTaskGetCurrentTaskHandle();
    const TaskHandle_t denied = freertos_stub_task_create();
    Snapshot before;
    bool stopped;

    assert(denied != NULL);
    Service_Filesystem_DenyCaller(denied);
    assert(xSemaphoreTake(locks.sd, 0U) == pdTRUE);
    before = snapshot();
    freertos_stub_set_current_task(denied);
    stopped = run_expecting_abort(entry);
    freertos_stub_set_current_task(allowed);
    if (!stopped || (snapshot().sd_waits != before.sd_waits))
    {
        (void)fprintf(stderr, "a denied caller was not stopped before waiting for the lock\n");
        return 1;
    }
    return 0;
}

/** @brief 拒绝名单挡启动入口。 @return 0 表示通过。 */
static int scenario_denied_caller_stops_at_start(void)
{
    return denied_caller_stops_before_waiting(call_start_sd);
}

/** @brief 拒绝名单挡刷新入口。 @return 0 表示通过。 */
static int scenario_denied_caller_stops_at_refresh(void)
{
    return denied_caller_stops_before_waiting(call_refresh_sd);
}

/** @brief 一个独立进程里跑的用例。 */
typedef struct
{
    const char *name;
    int (*run)(void);
} Scenario;

static const Scenario scenarios[] = {
    {"gate_wait_holding_lock", scenario_waiting_at_the_gate_while_holding_a_lock_stops},
    {"denied_start", scenario_denied_caller_stops_at_start},
    {"denied_refresh", scenario_denied_caller_stops_at_refresh},
};

/* ---------------------------------------------------------------- 主函数 */

/**
 * @brief 运行所选用例；未设置环境变量时运行全部不会 abort 的用例。
 * @return 断言全部通过返回 0。
 */
int main(void)
{
    const char *scenario = getenv("FILESYSTEM_CARD_SCENARIO");

    setup_volumes();
    if (scenario != NULL)
    {
        for (uint32_t i = 0U; i < (sizeof(scenarios) / sizeof(scenarios[0])); i++)
        {
            if (strcmp(scenario, scenarios[i].name) == 0)
            {
                const int result = scenarios[i].run();

                if (result == 0)
                {
                    (void)printf("filesystem_card: scenario %s passed\n", scenario);
                }
                return result;
            }
        }
        (void)fprintf(stderr, "unknown scenario: %s\n", scenario);
        return 2;
    }
    test_failure_classifier_marks_only_volume_unavailable_statuses();
    test_sd_entries_wait_at_the_gate_before_taking_the_lock();
    test_open_gate_does_not_block_entries();
    test_gate_does_not_block_flash_entries();
    test_gate_does_not_block_the_volume_task_lifecycle_entries();
    test_gate_closed_during_a_call_does_not_interrupt_it();
    test_pause_and_resume_are_idempotent_flags_not_counters();
    test_unmounted_path_entries_return_not_ready_without_waiting_for_lock_or_gate();
    test_unmounted_handle_entries_return_invalid_handle_without_waiting();
    test_unmount_while_waiting_for_the_lock_is_caught_after_taking_it();
    test_unmount_while_waiting_at_the_gate_is_caught_before_taking_the_lock();
    test_unmount_releases_all_slots_of_the_volume();
    test_failed_sd_read_with_the_card_gone_returns_no_media_inside_the_lock();
    test_card_pin_is_read_only_for_generic_failures();
    test_start_registers_the_detect_callback_inside_the_sd_lock();
    test_refresh_reports_the_card_inside_the_sd_lock();
    puts("filesystem_card: tests passed");
    return 0;
}
