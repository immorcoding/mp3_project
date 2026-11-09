/**
 * @file test_filesystem_lock.c
 * @brief 经公开 Service Interface 验证 Filesystem 的入口加锁：每卷一把卷锁、窄表锁、调用者拒绝名单、
 *        32 KiB 上限、槽按卷用尽与持锁统计。真实的 Service、FatFs 与共用 FreeRTOS 桩，RAM 盘替代硬件。
 *
 * @details
 *          主机是单线程的，桩不切换任务：这里证明每个分支走对，不证明真实并发。“别的任务占着锁”
 *          用桩的脚本化（直接拿走互斥锁、让接下来几次拿取失败、在将要等待时安装钩子替别的任务放锁）模拟。
 *          断言会让进程 abort 的用例（调用者拒绝名单、锁序违反）各在独立进程里跑：环境变量
 *          FILESYSTEM_LOCK_SCENARIO 选用例，未设置时跑全部不会 abort 的用例。
 */

#include "Service/filesystem/filesystem_config.h"
#include "Service/filesystem/filesystem_directory.h"
#include "Service/filesystem/filesystem_file.h"
#include "Service/filesystem/filesystem_flash_access.h"
#include "Service/filesystem/filesystem_service.h"
#include "Service/filesystem/filesystem_text.h"

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

/** @brief 每次 RAM 盘读写时调用的钩子：测试用它推进假时间，或在持锁时让“下层”回调公开接口。 */
typedef void (*DiskHook)(BYTE pdrv);
static DiskHook disk_hook;
static void (*flash_reclaim_hook)(void);

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
 * @brief 从对应 RAM 盘复制扇区，先调用钩子。
 * @param[in] pdrv 逻辑盘号。
 * @param[out] buff 至少 count 个扇区。
 * @param[in] sector 起始扇区。
 * @param[in] count 扇区数。
 * @return RES_OK 或 RES_PARERR。
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
    memcpy(buff, &ram_media[pdrv][sector * RAM_SECTOR_SIZE], count * RAM_SECTOR_SIZE);
    return RES_OK;
}

/**
 * @brief 把扇区写入对应 RAM 盘，先调用钩子。
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
 * @brief SD 传输层替身：卡检测脚，恒表示有卡（读写失败时不会被改成 NO_MEDIA）。
 * @return 恒为 true。
 */
bool filesystem_sd_transfer_card_present(void)
{
    return true;
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
 * @brief Flash 传输层替身：格式化清空 RAM 盘。
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
 * @brief Flash 传输层替身：回收先调钩子，再成功。
 * @return 恒为 SERVICE_OK。
 */
Service_StatusTypeDef filesystem_flash_transfer_reclaim(void)
{
    if (flash_reclaim_hook != NULL)
    {
        flash_reclaim_hook();
    }
    return SERVICE_OK;
}

/**
 * @brief Flash 执行器替身：把已受理的请求当作立即完成。
 * @param[in] started Platform 对启动请求的受理结果。
 * @param[in] timeout_ms 预算，替身不用。
 * @retval SERVICE_OK 请求已受理。
 * @retval SERVICE_ERROR 请求未被受理。
 */
Service_StatusTypeDef filesystem_flash_transfer_finish(Platform_StatusTypeDef started, uint32_t timeout_ms)
{
    (void)timeout_ms;
    return (started == PLATFORM_OK) ? SERVICE_OK : SERVICE_ERROR;
}

/**
 * @brief Platform Flash 替身：启动数组读恒受理。
 * @param[in] address 芯片地址，替身不用。
 * @param[out] data 接收缓冲，替身不用。
 * @param[in] data_length 字节数，替身不用。
 * @return 恒为 PLATFORM_OK。
 */
Platform_StatusTypeDef Platform_Flash_StartReadArray(uint32_t address, uint8_t *data, uint32_t data_length)
{
    (void)address;
    (void)data;
    (void)data_length;
    return PLATFORM_OK;
}

/**
 * @brief Platform Flash 替身：启动自检区读恒受理。
 * @param[in] region 自检区域，替身不用。
 * @param[out] data 接收缓冲，替身不用。
 * @param[in] data_length 字节数，替身不用。
 * @return 恒为 PLATFORM_OK。
 */
Platform_StatusTypeDef Platform_Flash_StartDiagnosticRead(Platform_Flash_DiagnosticRegionTypeDef region,
                                                          uint8_t *data,
                                                          uint32_t data_length)
{
    (void)region;
    (void)data;
    (void)data_length;
    return PLATFORM_OK;
}

/**
 * @brief Platform Flash 替身：启动自检区擦除恒受理。
 * @param[in] region 自检区域，替身不用。
 * @return 恒为 PLATFORM_OK。
 */
Platform_StatusTypeDef Platform_Flash_StartDiagnosticErase(Platform_Flash_DiagnosticRegionTypeDef region)
{
    (void)region;
    return PLATFORM_OK;
}

/**
 * @brief Platform Flash 替身：启动自检区页编程恒受理。
 * @param[in] region 自检区域，替身不用。
 * @param[in] page_offset 页偏移，替身不用。
 * @param[in] data 输入缓冲，替身不用。
 * @param[in] data_length 字节数，替身不用。
 * @return 恒为 PLATFORM_OK。
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
    return PLATFORM_OK;
}

/* ---------------------------------------------------------------- 锁的观察 */

/** @brief 三把锁的句柄；按“拿了哪把”识别，不依赖创建顺序。 */
typedef struct
{
    SemaphoreHandle_t sd;
    SemaphoreHandle_t flash;
    SemaphoreHandle_t table;
} Locks;

/** @brief 三把锁拿放次数的快照。 */
typedef struct
{
    uint32_t takes[3];
    uint32_t gives[3];
    uint32_t waits[3];
} Counts;

static Locks locks;

/**
 * @brief 读三把锁当前的拿放次数。
 * @return 快照，下标 0 为 SD、1 为 Flash、2 为表锁。
 */
static Counts snapshot(void)
{
    const SemaphoreHandle_t handles[3] = {locks.sd, locks.flash, locks.table};
    Counts counts;

    for (uint32_t i = 0U; i < 3U; i++)
    {
        FreertosStub_SemaphoreStats stats = freertos_stub_semaphore_stats(handles[i]);

        counts.takes[i] = stats.take_successes;
        counts.gives[i] = stats.give_successes;
        counts.waits[i] = stats.waits;
    }
    return counts;
}

/**
 * @brief 断言自 before 以来三把锁各拿放了多少次，且每次拿都配了一次放。
 * @param[in] before 调用前的快照。
 * @param[in] sd_takes SD 卷锁应被拿的次数。
 * @param[in] flash_takes Flash 卷锁应被拿的次数。
 * @param[in] table_takes 表锁应被拿的次数。
 */
static void expect_takes(const Counts *before, uint32_t sd_takes, uint32_t flash_takes, uint32_t table_takes)
{
    const Counts after = snapshot();
    const uint32_t expected[3] = {sd_takes, flash_takes, table_takes};

    for (uint32_t i = 0U; i < 3U; i++)
    {
        assert((after.takes[i] - before->takes[i]) == expected[i]);
        assert((after.gives[i] - before->gives[i]) == expected[i]);
    }
}

/**
 * @brief 断言三把锁此刻都空闲（拿放次数相等）。
 */
static void expect_all_locks_free(void)
{
    const Counts now = snapshot();

    for (uint32_t i = 0U; i < 3U; i++)
    {
        assert(now.takes[i] == now.gives[i]);
    }
}

/**
 * @brief 链接双 RAM 盘、初始化并挂载两个卷，然后按“拿了哪把锁”认出三把锁。
 * @note  必须在第一次调用 Filesystem 之前 reset 桩：锁是第一次用到时才建的。
 */
static void setup_volumes(void)
{
    uint8_t work[4096];
    TCHAR sd_drive[] = {'0', ':', 0};
    TCHAR user_drive[] = {'1', ':', 0};
    Service_Filesystem_FileInfoTypeDef info;
    Counts before;
    uint32_t sd_index = 0U;
    uint32_t flash_index = 0U;
    SemaphoreHandle_t handles[3];

    freertos_stub_reset();
    lock_env_counter_reset();
    lock_env_log_reset();
    memset(ram_media, 0, sizeof(ram_media));
    disk_hook = NULL;
    flash_reclaim_hook = NULL;
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

    /* 一共三把互斥锁：SD 卷锁、Flash 卷锁、表锁；没有第三把卷级的锁，也没有槽表锁。 */
    assert(freertos_stub_created_semaphore_count() == 3U);
    for (uint32_t i = 0U; i < 3U; i++)
    {
        handles[i] = freertos_stub_created_semaphore(i);
        assert(handles[i] != NULL);
    }

    /* GetFileInfo 只拿所属卷的卷锁：哪把被拿，哪把就是那一卷的锁。 */
    for (uint32_t i = 0U; i < 3U; i++)
    {
        locks.sd = handles[i];
        locks.flash = handles[i];
        locks.table = handles[i];
        before = snapshot();
        (void)Service_Filesystem_GetFileInfo(SERVICE_FILESYSTEM_VOLUME_SD, "none", &info);
        if (snapshot().takes[0] != before.takes[0])
        {
            sd_index = i;
        }
        before = snapshot();
        (void)Service_Filesystem_GetFileInfo(SERVICE_FILESYSTEM_VOLUME_FLASH, "none", &info);
        if (snapshot().takes[0] != before.takes[0])
        {
            flash_index = i;
        }
    }
    assert(sd_index != flash_index);
    locks.sd = handles[sd_index];
    locks.flash = handles[flash_index];
    locks.table = handles[3U - sd_index - flash_index];
}

/* ---------------------------------------------------------------- 用例 */

/**
 * @brief 每次公开调用只拿所属卷的一把卷锁一次，返回前放；另一卷的锁不碰。覆盖成功和锁内的失败路径。
 */
static void test_each_call_takes_its_volume_lock_exactly_once(void)
{
    Service_Filesystem_FileHandleTypeDef file = {0};
    Service_Filesystem_DirectoryHandleTypeDef directory = {0};
    Service_Filesystem_DirectoryEntryTypeDef entry;
    Service_Filesystem_FileInfoTypeDef info;
    uint8_t data[4] = {1, 2, 3, 4};
    uint8_t back[4];
    uint32_t transferred = 0U;
    uint64_t value = 0U;
    Counts before;

    /* 每一步都在两个卷上做一遍：SD 的调用只动 SD 锁，Flash 的调用只动 Flash 锁。 */
    for (int v = 0; v < 2; v++)
    {
        const Service_Filesystem_VolumeTypeDef volume =
            (v == 0) ? SERVICE_FILESYSTEM_VOLUME_SD : SERVICE_FILESYSTEM_VOLUME_FLASH;
        const uint32_t sd = (v == 0) ? 1U : 0U;
        const uint32_t flash = (v == 0) ? 0U : 1U;

        before = snapshot();
        assert(Service_Filesystem_CreateDirectory(volume, "Dir") == SERVICE_OK);
        expect_takes(&before, sd, flash, 0U);

        before = snapshot();
        assert(Service_Filesystem_OpenFile(volume, "Dir/a.bin", SERVICE_FILESYSTEM_FILE_MODE_CREATE_NEW, &file) ==
               SERVICE_OK);
        expect_takes(&before, sd, flash, 1U);

        before = snapshot();
        assert(Service_Filesystem_WriteFile(file, data, sizeof(data), &transferred) == SERVICE_OK);
        expect_takes(&before, sd, flash, 0U);

        before = snapshot();
        assert(Service_Filesystem_SyncFile(file) == SERVICE_OK);
        expect_takes(&before, sd, flash, 0U);

        before = snapshot();
        assert(Service_Filesystem_SeekFile(file, 0U) == SERVICE_OK);
        expect_takes(&before, sd, flash, 0U);

        before = snapshot();
        assert(Service_Filesystem_GetFilePosition(file, &value) == SERVICE_OK);
        expect_takes(&before, sd, flash, 0U);

        before = snapshot();
        assert(Service_Filesystem_GetFileSize(file, &value) == SERVICE_OK && value == sizeof(data));
        expect_takes(&before, sd, flash, 0U);

        before = snapshot();
        assert(Service_Filesystem_ReadFile(file, back, sizeof(back), &transferred) == SERVICE_OK);
        assert(transferred == sizeof(back) && memcmp(back, data, sizeof(data)) == 0);
        expect_takes(&before, sd, flash, 0U);

        before = snapshot();
        assert(Service_Filesystem_CloseFile(file) == SERVICE_OK);
        expect_takes(&before, sd, flash, 0U);

        /* 锁内发现句柄已失效：仍然只拿放一次。 */
        before = snapshot();
        assert(Service_Filesystem_ReadFile(file, back, sizeof(back), &transferred) == SERVICE_INVALID_HANDLE);
        expect_takes(&before, sd, flash, 0U);

        before = snapshot();
        assert(Service_Filesystem_GetFileInfo(volume, "Dir/a.bin", &info) == SERVICE_OK);
        expect_takes(&before, sd, flash, 0U);

        before = snapshot();
        assert(Service_Filesystem_RenameFile(volume, "Dir/a.bin", "Dir/b.bin") == SERVICE_OK);
        expect_takes(&before, sd, flash, 0U);

        before = snapshot();
        assert(Service_Filesystem_OpenDirectory(volume, "Dir", &directory) == SERVICE_OK);
        expect_takes(&before, sd, flash, 1U);

        before = snapshot();
        assert(Service_Filesystem_ReadDirectory(directory, &entry) == SERVICE_OK);
        assert(strcmp(entry.Name, "b.bin") == 0);
        expect_takes(&before, sd, flash, 0U);

        before = snapshot();
        assert(Service_Filesystem_RewindDirectory(directory) == SERVICE_OK);
        expect_takes(&before, sd, flash, 0U);

        before = snapshot();
        assert(Service_Filesystem_CloseDirectory(directory) == SERVICE_OK);
        expect_takes(&before, sd, flash, 0U);

        before = snapshot();
        assert(Service_Filesystem_RemoveFile(volume, "Dir/b.bin") == SERVICE_OK);
        expect_takes(&before, sd, flash, 0U);

        before = snapshot();
        assert(Service_Filesystem_RemoveDirectory(volume, "Dir") == SERVICE_OK);
        expect_takes(&before, sd, flash, 0U);

        /* 锁内的失败路径：打开不存在的文件。 */
        before = snapshot();
        assert(Service_Filesystem_OpenFile(volume, "missing.bin", SERVICE_FILESYSTEM_FILE_MODE_READ, &file) !=
               SERVICE_OK);
        expect_takes(&before, sd, flash, 1U);
    }
    expect_all_locks_free();
}

/**
 * @brief 卷生命周期入口同样只拿所属卷的一把锁一次；FormatAndMount、RecoverAndMount 内部分好几步也不例外。
 */
static void test_lifecycle_calls_take_their_volume_lock_once(void)
{
    Counts before;

    before = snapshot();
    assert(Service_Filesystem_UnmountSD() == SERVICE_OK);
    expect_takes(&before, 1U, 0U, 0U);

    before = snapshot();
    assert(Service_Filesystem_MountSD() == SERVICE_OK);
    expect_takes(&before, 1U, 0U, 0U);

    before = snapshot();
    assert(Service_Filesystem_InitSD() == SERVICE_OK);
    expect_takes(&before, 1U, 0U, 0U);

    before = snapshot();
    assert(Service_Filesystem_UnmountFlash() == SERVICE_OK);
    expect_takes(&before, 0U, 1U, 0U);

    before = snapshot();
    assert(Service_Filesystem_MountFlash() == SERVICE_OK);
    expect_takes(&before, 0U, 1U, 0U);

    before = snapshot();
    assert(Service_Filesystem_InitFlash() == SERVICE_OK);
    expect_takes(&before, 0U, 1U, 0U);

    before = snapshot();
    assert(Service_Filesystem_ReclaimFlash() == SERVICE_OK);
    expect_takes(&before, 0U, 1U, 0U);

    before = snapshot();
    assert(Service_Filesystem_FormatAndMountFlash() == SERVICE_OK);
    expect_takes(&before, 0U, 1U, 0U);

    before = snapshot();
    assert(Service_Filesystem_RecoverAndMountFlash() == SERVICE_OK);
    expect_takes(&before, 0U, 1U, 0U);

    expect_all_locks_free();
}

/**
 * @brief 在参数或路径就被拒绝的调用不拿锁（没有共享状态可保护）；槽号越界的句柄在拿锁之前就能判定无效。
 */
static void test_calls_rejected_before_any_shared_state_do_not_take_locks(void)
{
    Service_Filesystem_FileHandleTypeDef bogus_file = {0};
    Service_Filesystem_DirectoryHandleTypeDef bogus_directory = {0};
    Service_Filesystem_FileHandleTypeDef file = {0};
    Service_Filesystem_DirectoryEntryTypeDef entry;
    uint8_t byte = 0U;
    uint32_t transferred = 0U;
    Counts before = snapshot();

    assert(Service_Filesystem_OpenFile(SERVICE_FILESYSTEM_VOLUME_SD, "../x", SERVICE_FILESYSTEM_FILE_MODE_READ, &file) ==
           SERVICE_INVALID_PARAM);
    assert(Service_Filesystem_OpenFile(SERVICE_FILESYSTEM_VOLUME_SD, "x", SERVICE_FILESYSTEM_FILE_MODE_READ, NULL) ==
           SERVICE_INVALID_PARAM);
    assert(Service_Filesystem_ReadFile(bogus_file, &byte, 1U, &transferred) == SERVICE_INVALID_HANDLE);
    assert(Service_Filesystem_CloseFile(bogus_file) == SERVICE_INVALID_HANDLE);
    assert(Service_Filesystem_ReadDirectory(bogus_directory, &entry) == SERVICE_INVALID_HANDLE);
    assert(Service_Filesystem_RemoveFile((Service_Filesystem_VolumeTypeDef)7, "x") == SERVICE_INVALID_PARAM);
    expect_takes(&before, 0U, 0U, 0U);
}

/**
 * @brief 一卷的锁被别的任务占着，不影响另一卷的调用：Flash 调用不等 SD 锁，也不触发 SD 的等待。
 */
static void test_other_volume_calls_do_not_wait_for_a_held_volume_lock(void)
{
    Service_Filesystem_FileInfoTypeDef info;
    Counts before = snapshot();

    assert(xSemaphoreTake(locks.sd, 0U) == pdTRUE); /* 别的任务在 SD 上读。 */
    assert(Service_Filesystem_CreateDirectory(SERVICE_FILESYSTEM_VOLUME_FLASH, "other") == SERVICE_OK);
    assert(Service_Filesystem_GetFileInfo(SERVICE_FILESYSTEM_VOLUME_FLASH, "other", &info) == SERVICE_OK);
    assert(Service_Filesystem_RemoveDirectory(SERVICE_FILESYSTEM_VOLUME_FLASH, "other") == SERVICE_OK);
    assert(snapshot().waits[0] == before.waits[0]); /* 没有谁在等 SD 锁。 */
    assert(xSemaphoreGive(locks.sd) == pdTRUE);
}

/** @brief 等锁时替“别的任务”放锁的钩子的上下文。 */
typedef struct
{
    SemaphoreHandle_t watched;
    uint32_t calls;
} ReleaseOnWait;

/**
 * @brief 阻塞钩子：被测代码在等 watched 时，别的任务放锁。
 * @param[in] wait 将要进行的等待。
 * @param[in,out] context ReleaseOnWait。
 */
static void release_watched_on_wait(const FreertosStub_Wait *wait, void *context)
{
    ReleaseOnWait *state = (ReleaseOnWait *)context;

    if ((wait->kind == FREERTOS_STUB_WAIT_SEMAPHORE) && (wait->semaphore == state->watched))
    {
        state->calls++;
        assert(xSemaphoreGive(state->watched) == pdTRUE);
    }
}

/**
 * @brief 卷锁被占时调用者在入口等到放锁为止，之后正常完成；等的是这一卷的锁。
 */
static void test_call_waits_at_entry_until_the_volume_lock_is_released(void)
{
    ReleaseOnWait state = {locks.sd, 0U};
    Service_Filesystem_FileInfoTypeDef info;
    Counts before = snapshot();

    assert(xSemaphoreTake(locks.sd, 0U) == pdTRUE);
    freertos_stub_set_block_hook(release_watched_on_wait, &state);
    assert(Service_Filesystem_CreateDirectory(SERVICE_FILESYSTEM_VOLUME_SD, "waited") == SERVICE_OK);
    freertos_stub_set_block_hook(NULL, NULL);

    assert(state.calls == 1U);
    assert(snapshot().waits[0] - before.waits[0] == 1U);
    assert(Service_Filesystem_GetFileInfo(SERVICE_FILESYSTEM_VOLUME_SD, "waited", &info) == SERVICE_OK);
    assert(Service_Filesystem_RemoveDirectory(SERVICE_FILESYSTEM_VOLUME_SD, "waited") == SERVICE_OK);
    expect_all_locks_free();
}

/**
 * @brief 内核给的互斥锁拿取失败（超时）时入口继续等，直到拿到：调用不会在没有锁的情况下往下走。
 */
static void test_failed_take_is_retried_until_the_lock_is_obtained(void)
{
    Service_Filesystem_FileInfoTypeDef info;
    Counts before = snapshot();

    freertos_stub_semaphore_fail_next_takes(locks.sd, 3U);
    assert(Service_Filesystem_GetFileInfo(SERVICE_FILESYSTEM_VOLUME_SD, "nothing", &info) != SERVICE_OK);
    assert(snapshot().waits[0] - before.waits[0] == 3U);
    expect_takes(&before, 1U, 0U, 0U);
}

/**
 * @brief Flash 启动诊断的四个同步包装共用 Flash 的完成信号量与 Platform 在飞操作，所以同样拿 Flash 卷锁一次，
 *        不碰 SD 的锁，也不走表锁。
 */
static void test_flash_diagnostics_take_the_flash_volume_lock_once(void)
{
    uint8_t data[16] = {0};
    Counts before = snapshot();

    assert(Service_Filesystem_ReadFlashArray(0U, data, sizeof(data), 100U) == SERVICE_OK);
    expect_takes(&before, 0U, 1U, 0U);

    before = snapshot();
    assert(Service_Filesystem_ReadFlashDiagnostic(PLATFORM_FLASH_DIAGNOSTIC_REGION_HEAD, data, sizeof(data), 100U) ==
           SERVICE_OK);
    expect_takes(&before, 0U, 1U, 0U);

    before = snapshot();
    assert(Service_Filesystem_EraseFlashDiagnostic(PLATFORM_FLASH_DIAGNOSTIC_REGION_HEAD, 100U) == SERVICE_OK);
    expect_takes(&before, 0U, 1U, 0U);

    before = snapshot();
    assert(Service_Filesystem_ProgramFlashDiagnostic(PLATFORM_FLASH_DIAGNOSTIC_REGION_HEAD, 0U, data, sizeof(data),
                                                     100U) == SERVICE_OK);
    expect_takes(&before, 0U, 1U, 0U);

    /* 预算为零是调用错误，仍然在锁内判定，拿放一次。 */
    before = snapshot();
    assert(Service_Filesystem_ReadFlashArray(0U, data, sizeof(data), 0U) == SERVICE_NOT_READY);
    expect_takes(&before, 0U, 1U, 0U);
    expect_all_locks_free();
}

/* ---------------------------------------------------------------- 表锁 */

/** @brief 等表锁时核对“谁持着卷锁”的探针。 */
typedef struct
{
    uint32_t calls;
    uint32_t held_sd;    /**< 探针运行时 SD 卷锁被持有的次数（拿减放）。 */
    uint32_t held_flash; /**< 探针运行时 Flash 卷锁被持有的次数。 */
} TableWaitProbe;

/**
 * @brief 阻塞钩子：被测代码在等表锁时，记下两把卷锁此刻是否被持有，然后替“别的任务”放表锁。
 * @param[in] wait 将要进行的等待。
 * @param[in,out] context TableWaitProbe。
 */
static void probe_table_wait(const FreertosStub_Wait *wait, void *context)
{
    TableWaitProbe *probe = (TableWaitProbe *)context;

    if ((wait->kind == FREERTOS_STUB_WAIT_SEMAPHORE) && (wait->semaphore == locks.table))
    {
        const Counts now = snapshot();

        probe->calls++;
        probe->held_sd = now.takes[0] - now.gives[0];
        probe->held_flash = now.takes[1] - now.gives[1];
        assert(xSemaphoreGive(locks.table) == pdTRUE);
    }
}

/**
 * @brief 打开文件和目录要等的表锁，是在已持所属卷的卷锁之后拿的（卷锁 → 表锁），且不同时持另一卷的锁。
 */
static void test_opens_wait_for_the_table_lock_while_holding_only_their_own_volume_lock(void)
{
    Service_Filesystem_FileHandleTypeDef file = {0};
    Service_Filesystem_DirectoryHandleTypeDef directory = {0};

    for (int v = 0; v < 2; v++)
    {
        const Service_Filesystem_VolumeTypeDef volume =
            (v == 0) ? SERVICE_FILESYSTEM_VOLUME_SD : SERVICE_FILESYSTEM_VOLUME_FLASH;
        TableWaitProbe probe = {0U, 0U, 0U};
        const uint32_t own = 1U;

        assert(Service_Filesystem_CreateDirectory(volume, "ord") == SERVICE_OK);

        assert(xSemaphoreTake(locks.table, 0U) == pdTRUE); /* 别的任务在另一卷上 f_open。 */
        freertos_stub_set_block_hook(probe_table_wait, &probe);
        assert(Service_Filesystem_OpenFile(volume, "ord/a.bin", SERVICE_FILESYSTEM_FILE_MODE_CREATE_NEW, &file) ==
               SERVICE_OK);
        assert(probe.calls == 1U);
        assert(probe.held_sd == ((v == 0) ? own : 0U));
        assert(probe.held_flash == ((v == 1) ? own : 0U));

        probe = (TableWaitProbe){0U, 0U, 0U};
        assert(xSemaphoreTake(locks.table, 0U) == pdTRUE);
        assert(Service_Filesystem_OpenDirectory(volume, "ord", &directory) == SERVICE_OK);
        assert(probe.calls == 1U);
        assert(probe.held_sd == ((v == 0) ? own : 0U));
        assert(probe.held_flash == ((v == 1) ? own : 0U));
        freertos_stub_set_block_hook(NULL, NULL);

        assert(Service_Filesystem_CloseDirectory(directory) == SERVICE_OK);
        assert(Service_Filesystem_CloseFile(file) == SERVICE_OK);
        assert(Service_Filesystem_RemoveFile(volume, "ord/a.bin") == SERVICE_OK);
        assert(Service_Filesystem_RemoveDirectory(volume, "ord") == SERVICE_OK);
    }
    expect_all_locks_free();
}

/**
 * @brief 只有打开会等表锁：表锁被别的任务占着时，读写、关闭、删除、改名、建目录、挂载卸载都不等它。
 */
static void test_only_opens_wait_for_the_table_lock(void)
{
    Service_Filesystem_FileHandleTypeDef sd_file = {0};
    Service_Filesystem_FileHandleTypeDef flash_file = {0};
    Service_Filesystem_DirectoryHandleTypeDef sd_dir = {0};
    Service_Filesystem_DirectoryEntryTypeDef entry;
    Service_Filesystem_FileInfoTypeDef info;
    uint8_t data[2] = {7, 9};
    uint8_t back[2];
    uint32_t transferred = 0U;
    uint64_t value = 0U;
    Counts before;

    assert(Service_Filesystem_CreateDirectory(SERVICE_FILESYSTEM_VOLUME_SD, "nt") == SERVICE_OK);
    assert(Service_Filesystem_OpenFile(SERVICE_FILESYSTEM_VOLUME_SD, "nt/a.bin",
                                       SERVICE_FILESYSTEM_FILE_MODE_CREATE_NEW, &sd_file) == SERVICE_OK);
    assert(Service_Filesystem_OpenFile(SERVICE_FILESYSTEM_VOLUME_FLASH, "b.bin",
                                       SERVICE_FILESYSTEM_FILE_MODE_CREATE_NEW, &flash_file) == SERVICE_OK);
    assert(Service_Filesystem_OpenDirectory(SERVICE_FILESYSTEM_VOLUME_SD, "nt", &sd_dir) == SERVICE_OK);

    before = snapshot();
    assert(xSemaphoreTake(locks.table, 0U) == pdTRUE); /* 没有人会放：真去等就会被桩判成死锁。 */

    assert(Service_Filesystem_WriteFile(sd_file, data, sizeof(data), &transferred) == SERVICE_OK);
    assert(Service_Filesystem_WriteFile(flash_file, data, sizeof(data), &transferred) == SERVICE_OK);
    assert(Service_Filesystem_SeekFile(sd_file, 0U) == SERVICE_OK);
    assert(Service_Filesystem_ReadFile(sd_file, back, sizeof(back), &transferred) == SERVICE_OK);
    assert(Service_Filesystem_GetFilePosition(sd_file, &value) == SERVICE_OK);
    assert(Service_Filesystem_GetFileSize(flash_file, &value) == SERVICE_OK);
    assert(Service_Filesystem_SyncFile(flash_file) == SERVICE_OK);
    assert(Service_Filesystem_GetFileInfo(SERVICE_FILESYSTEM_VOLUME_SD, "nt/a.bin", &info) == SERVICE_OK);
    assert(Service_Filesystem_ReadDirectory(sd_dir, &entry) == SERVICE_OK);
    assert(Service_Filesystem_RewindDirectory(sd_dir) == SERVICE_OK);
    assert(Service_Filesystem_CreateDirectory(SERVICE_FILESYSTEM_VOLUME_FLASH, "nd") == SERVICE_OK);
    assert(Service_Filesystem_RemoveDirectory(SERVICE_FILESYSTEM_VOLUME_FLASH, "nd") == SERVICE_OK);
    assert(Service_Filesystem_ReclaimFlash() == SERVICE_OK);
    assert(Service_Filesystem_CloseFile(flash_file) == SERVICE_OK);
    assert(Service_Filesystem_RenameFile(SERVICE_FILESYSTEM_VOLUME_FLASH, "b.bin", "c.bin") == SERVICE_OK);
    assert(Service_Filesystem_RemoveFile(SERVICE_FILESYSTEM_VOLUME_FLASH, "c.bin") == SERVICE_OK);
    assert(Service_Filesystem_CloseDirectory(sd_dir) == SERVICE_OK);
    assert(Service_Filesystem_CloseFile(sd_file) == SERVICE_OK);
    assert(Service_Filesystem_RemoveFile(SERVICE_FILESYSTEM_VOLUME_SD, "nt/a.bin") == SERVICE_OK);
    assert(Service_Filesystem_RemoveDirectory(SERVICE_FILESYSTEM_VOLUME_SD, "nt") == SERVICE_OK);
    assert(Service_Filesystem_UnmountSD() == SERVICE_OK);
    assert(Service_Filesystem_MountSD() == SERVICE_OK);
    assert(Service_Filesystem_UnmountFlash() == SERVICE_OK);
    assert(Service_Filesystem_MountFlash() == SERVICE_OK);

    assert(snapshot().waits[2] == before.waits[2]); /* 没有谁等过表锁。 */
    assert(snapshot().takes[2] - before.takes[2] == 1U); /* 只有测试自己拿的那一次。 */
    assert(xSemaphoreGive(locks.table) == pdTRUE);
    expect_all_locks_free();
}

/* ---------------------------------------------------------------- 单次读写上限 */

/**
 * @brief 单次读写正好 32 KiB 可以；多一个字节返回 INVALID_PARAM，不截短、不拆分、不动文件，也不拿锁。
 */
static void test_single_transfer_is_limited_to_32_kib(void)
{
    static uint8_t big[SERVICE_FILESYSTEM_IO_MAX_BYTES + 1U];
    Service_Filesystem_FileHandleTypeDef file = {0};
    uint32_t transferred = 99U;
    uint64_t value = 0U;
    Counts before;

    memset(big, 0x5A, sizeof(big));
    assert(Service_Filesystem_OpenFile(SERVICE_FILESYSTEM_VOLUME_SD, "big.bin",
                                       SERVICE_FILESYSTEM_FILE_MODE_CREATE_NEW, &file) == SERVICE_OK);

    assert(Service_Filesystem_WriteFile(file, big, SERVICE_FILESYSTEM_IO_MAX_BYTES, &transferred) == SERVICE_OK);
    assert(transferred == SERVICE_FILESYSTEM_IO_MAX_BYTES);

    before = snapshot();
    assert(Service_Filesystem_WriteFile(file, big, SERVICE_FILESYSTEM_IO_MAX_BYTES + 1U, &transferred) ==
           SERVICE_INVALID_PARAM);
    assert(transferred == 0U);
    assert(Service_Filesystem_WriteFile(file, big, 0xFFFFFFFFU, &transferred) == SERVICE_INVALID_PARAM);
    assert(transferred == 0U);
    expect_takes(&before, 0U, 0U, 0U);
    assert(Service_Filesystem_GetFileSize(file, &value) == SERVICE_OK);
    assert(value == SERVICE_FILESYSTEM_IO_MAX_BYTES); /* 被拒的写没有写进任何字节。 */

    assert(Service_Filesystem_SeekFile(file, 0U) == SERVICE_OK);
    assert(Service_Filesystem_ReadFile(file, big, SERVICE_FILESYSTEM_IO_MAX_BYTES, &transferred) == SERVICE_OK);
    assert(transferred == SERVICE_FILESYSTEM_IO_MAX_BYTES);
    assert(Service_Filesystem_SeekFile(file, 100U) == SERVICE_OK);
    before = snapshot();
    assert(Service_Filesystem_ReadFile(file, big, SERVICE_FILESYSTEM_IO_MAX_BYTES + 1U, &transferred) ==
           SERVICE_INVALID_PARAM);
    assert(transferred == 0U);
    expect_takes(&before, 0U, 0U, 0U);
    assert(Service_Filesystem_GetFilePosition(file, &value) == SERVICE_OK);
    assert(value == 100U); /* 被拒的读没有移动文件指针。 */

    assert(Service_Filesystem_CloseFile(file) == SERVICE_OK);
    assert(Service_Filesystem_RemoveFile(SERVICE_FILESYSTEM_VOLUME_SD, "big.bin") == SERVICE_OK);
}

/* ---------------------------------------------------------------- 槽按卷固定分配 */

/**
 * @brief 槽按卷固定分配（SD 4 文件 2 目录，Flash 2 文件 1 目录）：一卷用尽返回 BUSY，另一卷照常；
 *        九个对象同时打开时 FatFs 的锁表（_FS_LOCK 9）不会先满。
 */
static void test_slots_are_assigned_per_volume_and_exhaustion_returns_busy(void)
{
    static const char *const sd_files[] = {"s0.bin", "s1.bin", "s2.bin", "s3.bin"};
    static const char *const flash_files[] = {"f0.bin", "f1.bin"};
    Service_Filesystem_FileHandleTypeDef sd_open[4] = {{0}};
    Service_Filesystem_FileHandleTypeDef flash_open[2] = {{0}};
    Service_Filesystem_DirectoryHandleTypeDef sd_dirs[2] = {{0}};
    Service_Filesystem_DirectoryHandleTypeDef flash_dir = {0};
    Service_Filesystem_FileHandleTypeDef extra_file = {0};
    Service_Filesystem_DirectoryHandleTypeDef extra_dir = {0};
    Counts before;

    assert(Service_Filesystem_CreateDirectory(SERVICE_FILESYSTEM_VOLUME_SD, "da") == SERVICE_OK);
    assert(Service_Filesystem_CreateDirectory(SERVICE_FILESYSTEM_VOLUME_SD, "db") == SERVICE_OK);
    assert(Service_Filesystem_CreateDirectory(SERVICE_FILESYSTEM_VOLUME_FLASH, "da") == SERVICE_OK);

    for (uint32_t i = 0U; i < 4U; i++)
    {
        assert(Service_Filesystem_OpenFile(SERVICE_FILESYSTEM_VOLUME_SD, sd_files[i],
                                           SERVICE_FILESYSTEM_FILE_MODE_CREATE_NEW, &sd_open[i]) == SERVICE_OK);
    }
    for (uint32_t i = 0U; i < 2U; i++)
    {
        assert(Service_Filesystem_OpenFile(SERVICE_FILESYSTEM_VOLUME_FLASH, flash_files[i],
                                           SERVICE_FILESYSTEM_FILE_MODE_CREATE_NEW, &flash_open[i]) == SERVICE_OK);
    }
    assert(Service_Filesystem_OpenDirectory(SERVICE_FILESYSTEM_VOLUME_SD, "da", &sd_dirs[0]) == SERVICE_OK);
    assert(Service_Filesystem_OpenDirectory(SERVICE_FILESYSTEM_VOLUME_SD, "db", &sd_dirs[1]) == SERVICE_OK);
    assert(Service_Filesystem_OpenDirectory(SERVICE_FILESYSTEM_VOLUME_FLASH, "da", &flash_dir) == SERVICE_OK);

    /* 槽用尽：返回 BUSY（不是锁表满的 ERROR），拿一次卷锁、没有走到表锁，句柄不写。 */
    before = snapshot();
    assert(Service_Filesystem_OpenFile(SERVICE_FILESYSTEM_VOLUME_SD, "s4.bin",
                                       SERVICE_FILESYSTEM_FILE_MODE_CREATE_NEW, &extra_file) == SERVICE_BUSY);
    expect_takes(&before, 1U, 0U, 0U);
    assert(extra_file.Token == 0U);
    before = snapshot();
    assert(Service_Filesystem_OpenFile(SERVICE_FILESYSTEM_VOLUME_FLASH, "f2.bin",
                                       SERVICE_FILESYSTEM_FILE_MODE_CREATE_NEW, &extra_file) == SERVICE_BUSY);
    expect_takes(&before, 0U, 1U, 0U);
    assert(Service_Filesystem_OpenDirectory(SERVICE_FILESYSTEM_VOLUME_SD, "da", &extra_dir) == SERVICE_BUSY);
    assert(Service_Filesystem_OpenDirectory(SERVICE_FILESYSTEM_VOLUME_FLASH, "da", &extra_dir) == SERVICE_BUSY);
    assert(extra_dir.Token == 0U);

    /* 一卷的槽用尽不影响另一卷：关掉 Flash 的一个文件，Flash 能再开，SD 仍然满。 */
    assert(Service_Filesystem_CloseFile(flash_open[0]) == SERVICE_OK);
    assert(Service_Filesystem_OpenFile(SERVICE_FILESYSTEM_VOLUME_FLASH, flash_files[0],
                                       SERVICE_FILESYSTEM_FILE_MODE_READ, &flash_open[0]) == SERVICE_OK);
    assert(Service_Filesystem_OpenFile(SERVICE_FILESYSTEM_VOLUME_SD, "s4.bin",
                                       SERVICE_FILESYSTEM_FILE_MODE_CREATE_NEW, &extra_file) == SERVICE_BUSY);

    /* 关掉一个 SD 文件，SD 的槽又空出来。 */
    assert(Service_Filesystem_CloseFile(sd_open[3]) == SERVICE_OK);
    assert(Service_Filesystem_OpenFile(SERVICE_FILESYSTEM_VOLUME_SD, "s4.bin",
                                       SERVICE_FILESYSTEM_FILE_MODE_CREATE_NEW, &sd_open[3]) == SERVICE_OK);

    assert(Service_Filesystem_CloseDirectory(flash_dir) == SERVICE_OK);
    for (uint32_t i = 0U; i < 2U; i++)
    {
        assert(Service_Filesystem_CloseDirectory(sd_dirs[i]) == SERVICE_OK);
        assert(Service_Filesystem_CloseFile(flash_open[i]) == SERVICE_OK);
    }
    for (uint32_t i = 0U; i < 4U; i++)
    {
        assert(Service_Filesystem_CloseFile(sd_open[i]) == SERVICE_OK);
    }
    expect_all_locks_free();
}

/* ---------------------------------------------------------------- 调用者拒绝名单 */

static Service_Filesystem_FileHandleTypeDef thunk_file;
static Service_Filesystem_DirectoryHandleTypeDef thunk_directory;
static Service_Filesystem_DirectoryEntryTypeDef thunk_entry;
static Service_Filesystem_FileInfoTypeDef thunk_info;
static uint8_t thunk_buffer[8];
static uint32_t thunk_count;
static uint64_t thunk_value;

/** @brief 定义一个调用某个公开入口的无参函数。 */
#define THUNK(name, call)  \
    static void name(void) \
    {                      \
        (void)(call);      \
    }

THUNK(thunk_init_sd, Service_Filesystem_InitSD())
THUNK(thunk_mount_sd, Service_Filesystem_MountSD())
THUNK(thunk_unmount_sd, Service_Filesystem_UnmountSD())
THUNK(thunk_init_flash, Service_Filesystem_InitFlash())
THUNK(thunk_mount_flash, Service_Filesystem_MountFlash())
THUNK(thunk_unmount_flash, Service_Filesystem_UnmountFlash())
THUNK(thunk_format_flash, Service_Filesystem_FormatAndMountFlash())
THUNK(thunk_recover_flash, Service_Filesystem_RecoverAndMountFlash())
THUNK(thunk_reclaim_flash, Service_Filesystem_ReclaimFlash())
THUNK(thunk_open_file, Service_Filesystem_OpenFile(SERVICE_FILESYSTEM_VOLUME_SD, "x", SERVICE_FILESYSTEM_FILE_MODE_READ, &thunk_file))
THUNK(thunk_read_file, Service_Filesystem_ReadFile(thunk_file, thunk_buffer, 1U, &thunk_count))
THUNK(thunk_write_file, Service_Filesystem_WriteFile(thunk_file, thunk_buffer, 1U, &thunk_count))
THUNK(thunk_seek_file, Service_Filesystem_SeekFile(thunk_file, 0U))
THUNK(thunk_tell_file, Service_Filesystem_GetFilePosition(thunk_file, &thunk_value))
THUNK(thunk_size_file, Service_Filesystem_GetFileSize(thunk_file, &thunk_value))
THUNK(thunk_sync_file, Service_Filesystem_SyncFile(thunk_file))
THUNK(thunk_close_file, Service_Filesystem_CloseFile(thunk_file))
THUNK(thunk_remove_file, Service_Filesystem_RemoveFile(SERVICE_FILESYSTEM_VOLUME_SD, "x"))
THUNK(thunk_rename_file, Service_Filesystem_RenameFile(SERVICE_FILESYSTEM_VOLUME_SD, "x", "y"))
THUNK(thunk_info_file, Service_Filesystem_GetFileInfo(SERVICE_FILESYSTEM_VOLUME_SD, "x", &thunk_info))
THUNK(thunk_open_directory, Service_Filesystem_OpenDirectory(SERVICE_FILESYSTEM_VOLUME_SD, "", &thunk_directory))
THUNK(thunk_read_directory, Service_Filesystem_ReadDirectory(thunk_directory, &thunk_entry))
THUNK(thunk_rewind_directory, Service_Filesystem_RewindDirectory(thunk_directory))
THUNK(thunk_close_directory, Service_Filesystem_CloseDirectory(thunk_directory))
THUNK(thunk_create_directory, Service_Filesystem_CreateDirectory(SERVICE_FILESYSTEM_VOLUME_SD, "x"))
THUNK(thunk_remove_directory, Service_Filesystem_RemoveDirectory(SERVICE_FILESYSTEM_VOLUME_SD, "x"))
THUNK(thunk_flash_array, Service_Filesystem_ReadFlashArray(0U, thunk_buffer, 1U, 100U))
THUNK(thunk_flash_read_diagnostic, Service_Filesystem_ReadFlashDiagnostic(PLATFORM_FLASH_DIAGNOSTIC_REGION_HEAD, thunk_buffer, 1U, 100U))
THUNK(thunk_flash_erase_diagnostic, Service_Filesystem_EraseFlashDiagnostic(PLATFORM_FLASH_DIAGNOSTIC_REGION_HEAD, 100U))
THUNK(thunk_flash_program_diagnostic, Service_Filesystem_ProgramFlashDiagnostic(PLATFORM_FLASH_DIAGNOSTIC_REGION_HEAD, 0U, thunk_buffer, 1U, 100U))

/** @brief 一个公开入口：名字和调用它的无参函数。 */
typedef struct
{
    const char *name;
    void (*call)(void);
} EntryPoint;

#define ENTRY(name) {#name, thunk_##name}

static const EntryPoint entry_points[] = {
    ENTRY(init_sd),         ENTRY(mount_sd),         ENTRY(unmount_sd),       ENTRY(init_flash),
    ENTRY(mount_flash),     ENTRY(unmount_flash),    ENTRY(format_flash),     ENTRY(recover_flash),
    ENTRY(reclaim_flash),   ENTRY(open_file),        ENTRY(read_file),        ENTRY(write_file),
    ENTRY(seek_file),       ENTRY(tell_file),        ENTRY(size_file),        ENTRY(sync_file),
    ENTRY(close_file),      ENTRY(remove_file),      ENTRY(rename_file),      ENTRY(info_file),
    ENTRY(open_directory),  ENTRY(read_directory),   ENTRY(rewind_directory), ENTRY(close_directory),
    ENTRY(create_directory), ENTRY(remove_directory),    ENTRY(flash_array),      ENTRY(flash_read_diagnostic),
    ENTRY(flash_erase_diagnostic), ENTRY(flash_program_diagnostic),
};

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

/**
 * @brief 登记在拒绝名单里的任务（GUI、播放）进入任何公开入口都在等锁之前停机；没登记的任务不受影响；
 *        GBK 查表不拿锁，不受限制。
 */
static void test_denied_tasks_stop_at_every_entry_before_waiting_for_a_lock(void)
{
    const TaskHandle_t allowed_task = xTaskGetCurrentTaskHandle();
    const TaskHandle_t gui_task = freertos_stub_task_create();
    const TaskHandle_t playback_task = freertos_stub_task_create();
    const TaskHandle_t other_task = freertos_stub_task_create();
    Service_Filesystem_FileInfoTypeDef info;
    Counts before;
    uint32_t unsatisfied;

    assert((gui_task != NULL) && (playback_task != NULL) && (other_task != NULL));
    Service_Filesystem_DenyCaller(gui_task);
    Service_Filesystem_DenyCaller(playback_task);
    Service_Filesystem_DenyCaller(gui_task); /* 重复登记同一个任务没有副作用。 */

    /* 没登记的任务照常使用，包括登记之前就在用的那个。 */
    freertos_stub_set_current_task(other_task);
    assert(Service_Filesystem_GetFileInfo(SERVICE_FILESYSTEM_VOLUME_SD, "none", &info) != SERVICE_OK);
    freertos_stub_set_current_task(allowed_task);

    /* 三把锁都被“别的任务”占着：入口若先去等锁就会被桩判成死锁；检查在等锁之前，则一次等待都没有。 */
    assert(xSemaphoreTake(locks.sd, 0U) == pdTRUE);
    assert(xSemaphoreTake(locks.flash, 0U) == pdTRUE);
    assert(xSemaphoreTake(locks.table, 0U) == pdTRUE);
    before = snapshot();
    unsatisfied = freertos_stub_unsatisfied_infinite_waits();

    for (uint32_t i = 0U; i < (sizeof(entry_points) / sizeof(entry_points[0])); i++)
    {
        const TaskHandle_t denied = ((i % 2U) == 0U) ? gui_task : playback_task;
        bool stopped;

        freertos_stub_set_current_task(denied);
        stopped = run_expecting_abort(entry_points[i].call);
        freertos_stub_set_current_task(allowed_task);
        if (!stopped)
        {
            (void)fprintf(stderr, "entry %s did not stop a denied caller\n", entry_points[i].name);
        }
        assert(stopped);
    }

    assert(freertos_stub_unsatisfied_infinite_waits() == unsatisfied);
    assert(snapshot().waits[0] == before.waits[0]);
    assert(snapshot().waits[1] == before.waits[1]);
    assert(snapshot().waits[2] == before.waits[2]);
    assert(xSemaphoreGive(locks.sd) == pdTRUE);
    assert(xSemaphoreGive(locks.flash) == pdTRUE);
    assert(xSemaphoreGive(locks.table) == pdTRUE);

    /* GBK 查表是只读的 CP936 表查询，不拿锁，登记过的任务也能调。 */
    freertos_stub_set_current_task(gui_task);
    assert(Service_Filesystem_GbkToUnicode(0xB0A1U) == 0x554AU); /* 啊 */
    freertos_stub_set_current_task(allowed_task);
    expect_all_locks_free();
}

/** @brief 登记超过上限的任务数：断言停机，而不是悄悄丢掉一个。 */
static void register_too_many_tasks(void)
{
    for (uint32_t i = 0U; i < (FILESYSTEM_DENIED_CALLER_MAX + 1U); i++)
    {
        TaskHandle_t task = freertos_stub_task_create();

        assert(task != NULL);
        Service_Filesystem_DenyCaller(task);
    }
}
/* ---------------------------------------------------------------- 各在独立进程里跑的断言用例 */

/** @brief 持 Flash 卷锁时（回收的下层回调里）重入同一卷的公开入口。 */
static void reclaim_hook_reenters_same_volume(void)
{
    Service_Filesystem_FileInfoTypeDef info;

    (void)Service_Filesystem_GetFileInfo(SERVICE_FILESYSTEM_VOLUME_FLASH, "x", &info);
}

/** @brief 持 Flash 卷锁时（回收的下层回调里）进入另一卷的公开入口。 */
static void reclaim_hook_enters_other_volume(void)
{
    Service_Filesystem_FileInfoTypeDef info;

    (void)Service_Filesystem_GetFileInfo(SERVICE_FILESYSTEM_VOLUME_SD, "x", &info);
}

/**
 * @brief 持 SD 卷锁和表锁时（f_open 读盘的下层回调里）进入 Flash 的公开入口。
 * @param[in] pdrv 正在读写的逻辑盘号，只在 SD（0）上触发。
 */
static void disk_hook_enters_flash_volume(BYTE pdrv)
{
    Service_Filesystem_FileInfoTypeDef info;

    if (pdrv == 0U)
    {
        (void)Service_Filesystem_GetFileInfo(SERVICE_FILESYSTEM_VOLUME_FLASH, "x", &info);
    }
}

/** @brief 调用 ReclaimFlash。 */
static void call_reclaim_flash(void)
{
    (void)Service_Filesystem_ReclaimFlash();
}

/** @brief 打开 SD 上的一个文件（会走到表锁）。 */
static void call_open_sd_file(void)
{
    Service_Filesystem_FileHandleTypeDef file = {0};

    (void)Service_Filesystem_OpenFile(SERVICE_FILESYSTEM_VOLUME_SD, "x.bin",
                                      SERVICE_FILESYSTEM_FILE_MODE_CREATE_NEW, &file);
}

/**
 * @brief 同一任务在持有一卷的锁时再进入同一卷的公开入口：断言停机，而不是死锁（互斥量不是递归的）。
 * @return 0 表示通过；失败由断言终止进程。
 */
static int scenario_reentering_the_same_volume_stops(void)
{
    flash_reclaim_hook = reclaim_hook_reenters_same_volume;
    assert(run_expecting_abort(call_reclaim_flash));
    assert(freertos_stub_unsatisfied_infinite_waits() == 0U); /* 没有真的去等那把自己持着的锁。 */
    return 0;
}

/**
 * @brief 持有一卷的锁时进入另一卷的公开入口：断言停机（绝不同时持两把卷锁，跨卷操作由调用者分成多次调用）。
 * @return 0 表示通过；失败由断言终止进程。
 */
static int scenario_holding_one_volume_and_entering_the_other_stops(void)
{
    flash_reclaim_hook = reclaim_hook_enters_other_volume;
    assert(run_expecting_abort(call_reclaim_flash));
    assert(freertos_stub_unsatisfied_infinite_waits() == 0U);
    return 0;
}

/**
 * @brief 持有卷锁和表锁（`f_open` 在读盘）时进入公开入口：断言停机，表锁内不拿任何别的锁。
 * @return 0 表示通过；失败由断言终止进程。
 */
static int scenario_entering_a_volume_while_holding_the_table_lock_stops(void)
{
    Service_Filesystem_FileHandleTypeDef file = {0};
    Service_Filesystem_FileInfoTypeDef info;

    /* 先让 FatFs 的磁盘窗口停在子目录的扇区上：之后在根目录里 f_open 必须读盘，钩子才会在表锁内被调用。 */
    assert(Service_Filesystem_CreateDirectory(SERVICE_FILESYSTEM_VOLUME_SD, "sub") == SERVICE_OK);
    assert(Service_Filesystem_OpenFile(SERVICE_FILESYSTEM_VOLUME_SD, "sub/y.bin",
                                       SERVICE_FILESYSTEM_FILE_MODE_CREATE_NEW, &file) == SERVICE_OK);
    assert(Service_Filesystem_CloseFile(file) == SERVICE_OK);
    assert(Service_Filesystem_GetFileInfo(SERVICE_FILESYSTEM_VOLUME_SD, "sub/y.bin", &info) == SERVICE_OK);

    disk_hook = disk_hook_enters_flash_volume;
    assert(run_expecting_abort(call_open_sd_file));
    assert(freertos_stub_unsatisfied_infinite_waits() == 0U);
    return 0;
}

/** @brief 登记空任务句柄。 */
static void register_null_task(void)
{
    Service_Filesystem_DenyCaller(NULL);
}

/**
 * @brief 拒绝名单有容量上限，超出时停机而不是悄悄丢掉一个；登记空句柄也停机。
 * @return 0 表示通过；失败由断言终止进程。
 */
static int scenario_deny_list_overflow_and_null_stop(void)
{
    uint32_t registered = 0U;

    assert(run_expecting_abort(register_null_task));
    while (registered < FILESYSTEM_DENIED_CALLER_MAX)
    {
        TaskHandle_t task = freertos_stub_task_create();

        assert(task != NULL);
        Service_Filesystem_DenyCaller(task);
        registered++;
    }
    assert(run_expecting_abort(register_too_many_tasks));
    return 0;
}

/* ---------------------------------------------------------------- 持锁统计（Debug 构建；各在独立进程里，因为统计是开机以来的最大值） */

static uint32_t sd_disk_calls;

/**
 * @brief SD 盘每读写一次花 4000 微秒（假时间），并计数。
 * @param[in] pdrv 正在读写的逻辑盘号，只算 SD（0）。
 */
static void disk_hook_costs_4000_us_on_sd(BYTE pdrv)
{
    if (pdrv == 0U)
    {
        sd_disk_calls++;
        lock_env_advance_us(4000U);
    }
}

/** @brief 回收花 7000 微秒（假时间）。 */
static void reclaim_hook_costs_7000_us(void)
{
    lock_env_advance_us(7000U);
}

/**
 * @brief 等 Flash 锁时：别的任务又持了 2500 微秒才放。
 * @param[in] wait 将要进行的等待。
 * @param[in,out] context 放锁次数计数（uint32_t）。
 */
static void wait_hook_holder_releases_after_2500_us(const FreertosStub_Wait *wait, void *context)
{
    if ((wait->kind == FREERTOS_STUB_WAIT_SEMAPHORE) && (wait->semaphore == locks.flash))
    {
        (*(uint32_t *)context)++;
        lock_env_advance_us(2500U);
        assert(xSemaphoreGive(locks.flash) == pdTRUE);
    }
}

/**
 * @brief 汇总日志投递时三把锁都必须已经放掉。
 * @param[in] tag 日志标签，不用。
 * @param[in] text 日志正文，不用。
 */
static void post_hook_requires_all_locks_free(const char *tag, const char *text)
{
    (void)tag;
    (void)text;
    expect_all_locks_free();
}

/** @brief 一行汇总日志解析出的字段。 */
typedef struct
{
    char name[8];
    uint32_t acquisitions;
    uint32_t hold_us;
    char hold_operation[16];
    uint32_t wait_us;
    char wait_operation[16];
} StatsLine;

/**
 * @brief 解析 `<锁> n=<次数> hold=<微秒>us:<操作> wait=<微秒>us:<操作>`。
 * @param[in] text 汇总行正文。
 * @param[out] line 解析结果。
 * @return true 格式正确。
 */
static bool parse_stats_line(const char *text, StatsLine *line)
{
    return sscanf(text, "%7s n=%u hold=%uus:%15s wait=%uus:%15s", line->name, &line->acquisitions, &line->hold_us,
                  line->hold_operation, &line->wait_us, line->wait_operation) == 6;
}

/**
 * @brief 读第 index 行汇总日志并核对标签是 FSLOCK。
 * @param[in] index 日志序号。
 * @return 解析出的字段。
 */
static StatsLine read_stats_line(uint32_t index)
{
    StatsLine line;

    memset(&line, 0, sizeof(line));
    assert(lock_env_log_tag(index) != NULL);
    assert(strcmp(lock_env_log_tag(index), "FSLOCK") == 0);
    assert(parse_stats_line(lock_env_log_text(index), &line));
    return line;
}

/**
 * @brief 每把锁的最长持有、最长等待和对应操作：节拍到了才投一批（每把锁一行，共三行），之后的下一批再等一个节拍；
 *        投递在锁放掉之后；行不会长到被 Log 丢弃。
 * @return 0 表示通过；失败由断言终止进程。
 */
static int scenario_stats_report_per_lock_each_period(void)
{
    Service_Filesystem_FileHandleTypeDef file = {0};
    Service_Filesystem_FileInfoTypeDef info;
    uint32_t releases = 0U;
    uint32_t expected_open_us;
    Counts totals;
    StatsLine sd;
    StatsLine flash;
    StatsLine table;

    lock_env_log_reset();
    lock_env_set_post_hook(post_hook_requires_all_locks_free);

    flash_reclaim_hook = reclaim_hook_costs_7000_us;
    assert(Service_Filesystem_ReclaimFlash() == SERVICE_OK);
    flash_reclaim_hook = NULL;

    assert(xSemaphoreTake(locks.flash, 0U) == pdTRUE); /* 别的任务持着 Flash 锁 2500 微秒。 */
    freertos_stub_set_block_hook(wait_hook_holder_releases_after_2500_us, &releases);
    assert(Service_Filesystem_GetFileInfo(SERVICE_FILESYSTEM_VOLUME_FLASH, "none", &info) != SERVICE_OK);
    freertos_stub_set_block_hook(NULL, NULL);
    assert(releases == 1U);

    /* 先让 FatFs 的磁盘窗口停在子目录的扇区上：之后在根目录里 f_open 必须读盘，钩子才会在表锁内被调用。 */
    assert(Service_Filesystem_CreateDirectory(SERVICE_FILESYSTEM_VOLUME_SD, "sub") == SERVICE_OK);
    assert(Service_Filesystem_GetFileInfo(SERVICE_FILESYSTEM_VOLUME_SD, "sub/none", &info) != SERVICE_OK);

    sd_disk_calls = 0U;
    disk_hook = disk_hook_costs_4000_us_on_sd;
    assert(Service_Filesystem_OpenFile(SERVICE_FILESYSTEM_VOLUME_SD, "stat.bin",
                                       SERVICE_FILESYSTEM_FILE_MODE_CREATE_NEW, &file) == SERVICE_OK);
    disk_hook = NULL;
    assert(sd_disk_calls >= 1U);
    expected_open_us = 4000U * sd_disk_calls;

    assert(lock_env_log_count() == 0U); /* 节拍没到，一行都不投。 */

    freertos_stub_set_tick(xTaskGetTickCount() + 10000U);
    assert(Service_Filesystem_CloseFile(file) == SERVICE_OK); /* 这次放锁时发现节拍到了。 */
    assert(lock_env_log_count() == 3U);

    sd = read_stats_line(0U);
    flash = read_stats_line(1U);
    table = read_stats_line(2U);
    assert(strcmp(sd.name, "sd") == 0);
    assert(strcmp(flash.name, "flash") == 0);
    assert(strcmp(table.name, "table") == 0);

    /* 持有：SD 和表锁是打开文件那次（f_open 的全部读盘都在表锁内），Flash 是回收。 */
    assert(sd.hold_us == expected_open_us);
    assert(strcmp(sd.hold_operation, "Open") == 0);
    assert(table.hold_us == expected_open_us);
    assert(strcmp(table.hold_operation, "Open") == 0);
    assert(flash.hold_us == 7000U);
    assert(strcmp(flash.hold_operation, "Reclaim") == 0);

    /* 等待：只有 Flash 等过 2500 微秒，操作是等锁的那个调用。 */
    assert(flash.wait_us == 2500U);
    assert(strcmp(flash.wait_operation, "Info") == 0);
    assert(sd.wait_us == 0U);
    assert(table.wait_us == 0U);

    /* 次数 = 内核看到的成功拿取次数（Flash 里减去测试自己模拟“别的任务”的那一次）。 */
    totals = snapshot();
    assert(sd.acquisitions == totals.takes[0]);
    assert(flash.acquisitions == (totals.takes[1] - 1U));
    assert(table.acquisitions == totals.takes[2]);

    /* 下一批要再等一个节拍。 */
    assert(Service_Filesystem_GetFileInfo(SERVICE_FILESYSTEM_VOLUME_SD, "none", &info) != SERVICE_OK);
    assert(lock_env_log_count() == 3U);
    freertos_stub_set_tick(xTaskGetTickCount() + 10000U);
    assert(Service_Filesystem_GetFileInfo(SERVICE_FILESYSTEM_VOLUME_FLASH, "none", &info) != SERVICE_OK);
    assert(lock_env_log_count() == 6U);

    assert(lock_env_log_longest_text() < 128U);
    for (uint32_t i = 0U; i < 3U; i++)
    {
        (void)printf("  %s %s\n", lock_env_log_tag(i), lock_env_log_text(i)); /* 给人看的一份样例。 */
    }
    return 0;
}

/** @brief 回收花 9 秒：周期计数器（480 MHz 下约 8.9 秒回绕）量不了，要靠 tick 兜底。 */
static void reclaim_hook_costs_9_seconds(void)
{
    lock_env_advance_us(9000000U);
    freertos_stub_set_tick(xTaskGetTickCount() + 9000U);
}

/**
 * @brief 持锁超过周期计数器的回绕时间（MountFlash 实测 3.3 至 8.3 s）时，改用 tick 量，数字仍然对。
 * @return 0 表示通过；失败由断言终止进程。
 */
static int scenario_stats_long_hold_falls_back_to_ticks(void)
{
    Service_Filesystem_FileInfoTypeDef info;
    StatsLine flash;

    lock_env_log_reset();
    flash_reclaim_hook = reclaim_hook_costs_9_seconds;
    assert(Service_Filesystem_ReclaimFlash() == SERVICE_OK);
    flash_reclaim_hook = NULL;

    freertos_stub_set_tick(xTaskGetTickCount() + 10000U);
    assert(Service_Filesystem_GetFileInfo(SERVICE_FILESYSTEM_VOLUME_SD, "none", &info) != SERVICE_OK);
    assert(lock_env_log_count() == 3U);
    flash = read_stats_line(1U);
    assert(flash.hold_us == 9000000U);
    assert(strcmp(flash.hold_operation, "Reclaim") == 0);
    return 0;
}

/** @brief 回收只推进 tick 20 毫秒，周期计数器没动。 */
static void reclaim_hook_advances_only_ticks(void)
{
    freertos_stub_set_tick(xTaskGetTickCount() + 20U);
}

/**
 * @brief 周期计数器没有启动（频率为 0）时用 tick 量，精度降到毫秒，但不会除零或报 0。
 * @return 0 表示通过；失败由断言终止进程。
 */
static int scenario_stats_without_cycle_counter_uses_ticks(void)
{
    Service_Filesystem_FileInfoTypeDef info;
    StatsLine flash;

    lock_env_log_reset();
    lock_env_set_counter_unavailable(true);
    flash_reclaim_hook = reclaim_hook_advances_only_ticks;
    assert(Service_Filesystem_ReclaimFlash() == SERVICE_OK);
    flash_reclaim_hook = NULL;

    freertos_stub_set_tick(xTaskGetTickCount() + 10000U);
    assert(Service_Filesystem_GetFileInfo(SERVICE_FILESYSTEM_VOLUME_SD, "none", &info) != SERVICE_OK);
    assert(lock_env_log_count() == 3U);
    flash = read_stats_line(1U);
    assert(flash.hold_us == 20000U);
    return 0;
}

#ifdef LOCK_TEST_SERVICE_RELEASE
/**
 * @brief Release 构建（Service 带 -DNDEBUG）：持锁统计整段编译掉，节拍到了也不投一行日志；锁的拿放、
 *        拒绝名单和锁序断言不受影响（同一套用例在本构建里照跑）。
 */
static void test_release_build_posts_no_lock_stats(void)
{
    Service_Filesystem_FileInfoTypeDef info;

    lock_env_log_reset();
    freertos_stub_set_tick(xTaskGetTickCount() + 10000U);
    assert(Service_Filesystem_GetFileInfo(SERVICE_FILESYSTEM_VOLUME_SD, "none", &info) != SERVICE_OK);
    freertos_stub_set_tick(xTaskGetTickCount() + 10000U);
    assert(Service_Filesystem_ReclaimFlash() == SERVICE_OK);
    assert(lock_env_log_count() == 0U);
}
#endif

/** @brief 一个独立进程里跑的用例。 */
typedef struct
{
    const char *name;
    int (*run)(void);
} Scenario;

static const Scenario scenarios[] = {
    {"reenter_same_volume", scenario_reentering_the_same_volume_stops},
    {"hold_one_volume_enter_other", scenario_holding_one_volume_and_entering_the_other_stops},
    {"hold_table_enter_volume", scenario_entering_a_volume_while_holding_the_table_lock_stops},
    {"deny_list_overflow", scenario_deny_list_overflow_and_null_stop},
    {"stats_report", scenario_stats_report_per_lock_each_period},
    {"stats_long_hold", scenario_stats_long_hold_falls_back_to_ticks},
    {"stats_no_counter", scenario_stats_without_cycle_counter_uses_ticks},
};

/**
 * @brief 运行所选用例；未设置环境变量时运行全部不会 abort 的用例。
 * @return 断言全部通过返回 0。
 */
int main(void)
{
    const char *scenario = getenv("FILESYSTEM_LOCK_SCENARIO");

    setup_volumes();
    if (scenario == NULL)
    {
        test_each_call_takes_its_volume_lock_exactly_once();
        test_lifecycle_calls_take_their_volume_lock_once();
        test_calls_rejected_before_any_shared_state_do_not_take_locks();
        test_other_volume_calls_do_not_wait_for_a_held_volume_lock();
        test_call_waits_at_entry_until_the_volume_lock_is_released();
        test_failed_take_is_retried_until_the_lock_is_obtained();
        test_flash_diagnostics_take_the_flash_volume_lock_once();
        test_opens_wait_for_the_table_lock_while_holding_only_their_own_volume_lock();
        test_only_opens_wait_for_the_table_lock();
        test_single_transfer_is_limited_to_32_kib();
        test_slots_are_assigned_per_volume_and_exhaustion_returns_busy();
        test_denied_tasks_stop_at_every_entry_before_waiting_for_a_lock();
#ifdef LOCK_TEST_SERVICE_RELEASE
        test_release_build_posts_no_lock_stats();
#endif
        puts("filesystem_lock: tests passed");
        return 0;
    }

    for (uint32_t i = 0U; i < (sizeof(scenarios) / sizeof(scenarios[0])); i++)
    {
        if (strcmp(scenario, scenarios[i].name) == 0)
        {
            const int result = scenarios[i].run();

            if (result == 0)
            {
                (void)printf("filesystem_lock: scenario %s passed\n", scenario);
            }
            return result;
        }
    }
    (void)fprintf(stderr, "unknown scenario: %s\n", scenario);
    return 2;
}
