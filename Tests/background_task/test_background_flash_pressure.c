/**
 ******************************************************************************
 * @file    test_background_flash_pressure.c
 * @brief   Flash 压力钩子作业的主机测试（#213）：写入、回收、删除的步序，卷没就绪时不碰 Flash，
 *          失败一次就停、卷序号变化再开始，日志带空闲块数和真擦除次数，默认构建里没有作业。
 *
 * @details
 *          链接真实的 `background_flash_pressure.c`、`background_jobs.c`、`background_reclaim.c`、`background_loop.c`、`volume_event.c`、
 *          共用 FreeRTOS 桩与 Service Log 替身；Filesystem Service 的五个入口（OpenFile、WriteFile、CloseFile、
 *          RemoveFile、ReclaimFlash）和 `Platform_Flash_GetVolumeDiagnostics` 在本文件里用脚本化的假实现替换，
 *          假 FTL 只有"空闲块、水位以下回收一块并计一次擦除、空闲块耗尽时写入自己擦"这点模型。
 *          作业和模块状态是静态的，每个用例一个进程（命令行参数选用例）。同一份测试源编四个配置：
 *          默认（钩子关闭）、钩子打开、加空闲块下限、关掉本作业的回收。
 *          主机测试是单线程的：证明每一步的调用顺序、返回值、日志和停下的条件，不证明真实的 Flash 时序、
 *          持锁时长、与 SD 的并发、擦除是否真的发生（靠上板）。
 ******************************************************************************
 */

/* 同一份测试源编四个配置，每个配置只用其中一部分用例和辅助函数。 */
#pragma GCC diagnostic ignored "-Wunused-function"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos_stub.h"
#include "lock_env_stub.h"

#include "APP/tasks/background/background_flash_pressure.h"
#include "APP/tasks/background/background_jobs.h"
#include "APP/apps/app_table.h"
#include "APP/tasks/background/background_loop.h"
#include "APP/tasks/background/background_reclaim.h"
#include "APP/tasks/background/background_task_config.h"
#include "APP/tasks/volume/volume_event.h"
#include "Platform/flash/platform_flash.h"
#include "Service/filesystem/filesystem_file.h"
#include "Service/filesystem/filesystem_service.h"
#include "Service/log/log_service.h"

/* 期望值用的字面量（#213 的设计：4 MiB 文件、32 KiB 一块、回收水位 462、100 ms 节拍），不用配置宏重算。 */
#define EXPECT_WRITES_PER_FILE 128U
#define EXPECT_LOW_WATER 462U

#if defined(TEST_VARIANT_NORECLAIM)
/* 钩子自己的回收默认关闭：产品的回收由后台任务的回收作业负责，钩子再调会让回收被叫两次（#220）。 */
_Static_assert(BACKGROUND_HOOK_FLASH_PRESSURE_RECLAIM == 0U, "the hook does not reclaim by default (#220)");
#endif

enum { KIND_REMOVE = 0, KIND_OPEN, KIND_WRITE, KIND_CLOSE, KIND_RECLAIM, KIND_COUNT };

/** @brief 假 Filesystem Service 与假 FTL 的脚本与观察值。 */
typedef struct
{
    char calls[2048];                      /**< 调用顺序：R 删除 O 新建 W 写 C 关闭 G 回收。 */
    uint32_t calls_count;
    uint32_t seen[KIND_COUNT];             /**< 每类调用的累计次数。 */
    uint32_t fail_at[KIND_COUNT];          /**< 第几次调用返回 fail_status（1 起，0 不注入）。 */
    Service_StatusTypeDef fail_status[KIND_COUNT];
    bool fail_sticky[KIND_COUNT];          /**< 注入之后的每次调用都失败。 */
    bool file_exists;
    bool handle_open;
    uint32_t token;
    uint64_t file_size;
    uint64_t capacity;                     /**< 卷最多容纳的字节数。 */
    uint32_t last_write_requested;
    const void *last_write_source;
    uint32_t write_ticks;                  /**< 每次写在假时间里花多少 tick。 */
    uint32_t free_blocks;
    uint32_t erases;
    uint32_t min_free_seen;
    uint32_t reported_free;                /**< 最近一次诊断读取返回的值，用来核对日志里的数。 */
    uint32_t reported_erases;
    bool diag_fails;
    uint32_t valid_groups;                 /**< 诊断里的有效块数（默认 0：模拟 FTL 故障后不就绪）。 */
    uint32_t stale_blocks;                 /**< 诊断里的失效块数。 */
    Platform_Flash_VolumeDiagnosticsTypeDef failure; /**< 只用其中的失败诊断字段：Platform 记下的最近一次 FTL 失败。 */
} Fake;

static Fake fake;
static uint32_t erase_baseline_at_start;

/**
 * @brief 记一次调用的顺序标记，并判断是否被注入失败。
 * @param[in] kind 调用类别。
 * @param[in] mark 顺序标记字符。
 * @param[out] status 被注入时的失败状态。
 * @return 被注入失败为真。
 */
static bool note_call(int kind, char mark, Service_StatusTypeDef *status)
{
    assert(fake.calls_count + 1U < sizeof(fake.calls));
    fake.calls[fake.calls_count++] = mark;
    fake.calls[fake.calls_count] = '\0';
    fake.seen[kind]++;
    if ((fake.fail_at[kind] != 0U) &&
        (fake.fail_sticky[kind] ? (fake.seen[kind] >= fake.fail_at[kind]) : (fake.seen[kind] == fake.fail_at[kind])))
    {
        *status = fake.fail_status[kind];
        return true;
    }
    return false;
}

/**
 * @brief  假 RemoveFile：文件在则删除，不在返回 ERROR（FatFs 的 FR_NO_FILE 归入 ERROR）。
 * @param[in] volume 卷，必须是 Flash。
 * @param[in] path 路径。
 * @return 脚本里的结果或按文件是否存在得到的结果。
 */
Service_StatusTypeDef Service_Filesystem_RemoveFile(Service_Filesystem_VolumeTypeDef volume, const char *path)
{
    Service_StatusTypeDef status;

    assert(volume == SERVICE_FILESYSTEM_VOLUME_FLASH);
    assert(path != NULL);
    if (note_call(KIND_REMOVE, 'R', &status))
    {
        return status;
    }
    if (!fake.file_exists)
    {
        return SERVICE_ERROR;
    }
    fake.file_exists = false;
    fake.file_size = 0U;
    return SERVICE_OK;
}

/**
 * @brief  假 OpenFile：只接受 CREATE_NEW；已存在返回 BUSY（FR_EXIST）。
 * @param[in] volume 卷，必须是 Flash。
 * @param[in] path 路径。
 * @param[in] mode 打开方式，必须是 CREATE_NEW。
 * @param[out] file 成功时得到句柄。
 * @return 脚本里的结果或按文件是否存在得到的结果。
 */
Service_StatusTypeDef Service_Filesystem_OpenFile(Service_Filesystem_VolumeTypeDef volume,
                                                  const char *path,
                                                  Service_Filesystem_FileModeTypeDef mode,
                                                  Service_Filesystem_FileHandleTypeDef *file)
{
    Service_StatusTypeDef status;

    assert(volume == SERVICE_FILESYSTEM_VOLUME_FLASH);
    assert(path != NULL);
    assert(mode == SERVICE_FILESYSTEM_FILE_MODE_CREATE_NEW);
    assert(file != NULL);
    if (note_call(KIND_OPEN, 'O', &status))
    {
        return status;
    }
    if (fake.file_exists)
    {
        return SERVICE_BUSY;
    }
    fake.file_exists = true;
    fake.file_size = 0U;
    fake.handle_open = true;
    file->Token = ++fake.token;
    return SERVICE_OK;
}

/**
 * @brief  假 WriteFile：卷容量用尽时只写进去一部分（FatFs 的行为），每个块消耗空闲块。
 * @param[in] file 句柄。
 * @param[in] buffer 数据来源（只记录，不读取）。
 * @param[in] requested 要求写入的字节数。
 * @param[out] transferred 实际写入的字节数。
 * @return 脚本里的结果；句柄无效为 INVALID_HANDLE。
 */
Service_StatusTypeDef Service_Filesystem_WriteFile(Service_Filesystem_FileHandleTypeDef file,
                                                   const void *buffer,
                                                   uint32_t requested,
                                                   uint32_t *transferred)
{
    Service_StatusTypeDef status;
    uint64_t room;
    uint32_t done;
    uint32_t blocks;

    assert(requested <= SERVICE_FILESYSTEM_IO_MAX_BYTES);
    assert(transferred != NULL);
    *transferred = 0U;
    fake.last_write_requested = requested;
    fake.last_write_source = buffer;
    if (note_call(KIND_WRITE, 'W', &status))
    {
        return status;
    }
    if (!fake.handle_open || (file.Token != fake.token))
    {
        return SERVICE_INVALID_HANDLE;
    }
    room = (fake.capacity > fake.file_size) ? (fake.capacity - fake.file_size) : 0U;
    done = (room < requested) ? (uint32_t)room : requested;
    fake.file_size += done;
    blocks = (done + 3583U) / 3584U;
    for (uint32_t index = 0U; index < blocks; index++)
    {
        if (fake.free_blocks <= 2U)
        {
            fake.erases++; /* 空闲块耗尽：写入自己擦一块（FTL 的前台回收）。 */
        }
        else
        {
            fake.free_blocks--;
        }
        if (fake.free_blocks < fake.min_free_seen)
        {
            fake.min_free_seen = fake.free_blocks;
        }
    }
    freertos_stub_set_tick(xTaskGetTickCount() + fake.write_ticks);
    *transferred = done;
    return SERVICE_OK;
}

/**
 * @brief  假 CloseFile。
 * @param[in] file 句柄。
 * @return 脚本里的结果；句柄无效为 INVALID_HANDLE。
 */
Service_StatusTypeDef Service_Filesystem_CloseFile(Service_Filesystem_FileHandleTypeDef file)
{
    Service_StatusTypeDef status;

    if (note_call(KIND_CLOSE, 'C', &status))
    {
        return status;
    }
    if (!fake.handle_open || (file.Token != fake.token))
    {
        return SERVICE_INVALID_HANDLE;
    }
    fake.handle_open = false;
    return SERVICE_OK;
}

/**
 * @brief  假 ReclaimFlash：空闲块不高于水位时擦一块（空闲块加 1，擦除计数加 1），否则空转。
 * @return 脚本里的结果，否则 OK。
 */
Service_StatusTypeDef Service_Filesystem_ReclaimFlash(void)
{
    Service_StatusTypeDef status;

    if (note_call(KIND_RECLAIM, 'G', &status))
    {
        return status;
    }
    if (fake.free_blocks <= EXPECT_LOW_WATER)
    {
        fake.free_blocks++;
        fake.erases++;
    }
    return SERVICE_OK;
}

/**
 * @brief  假 FTL 诊断：空闲块和擦除累计（含"开机以来"的基数，日志里应只显示钩子开始之后的增量）。
 * @param[out] diagnostics 接收快照。
 * @return 脚本要求失败时 PLATFORM_FLASH_ERROR，否则 PLATFORM_OK。
 */
Platform_StatusTypeDef Platform_Flash_GetVolumeDiagnostics(Platform_Flash_VolumeDiagnosticsTypeDef *diagnostics)
{
    if (fake.diag_fails)
    {
        return PLATFORM_FLASH_ERROR;
    }
    memset(diagnostics, 0, sizeof(*diagnostics));
    diagnostics->FreeBlocks = fake.free_blocks;
    diagnostics->SessionErases = fake.erases;
    diagnostics->ValidGroups = fake.valid_groups;
    diagnostics->StaleBlocks = fake.stale_blocks;
    diagnostics->LastFailure = fake.failure.LastFailure;
    diagnostics->LastFailureOperation = fake.failure.LastFailureOperation;
    diagnostics->LastFailureRawStatus = fake.failure.LastFailureRawStatus;
    diagnostics->LastFailureDeviceError = fake.failure.LastFailureDeviceError;
    diagnostics->LastFailurePortStatus = fake.failure.LastFailurePortStatus;
    diagnostics->FailureCount = fake.failure.FailureCount;
    diagnostics->LastRejection = fake.failure.LastRejection;
    fake.reported_free = fake.free_blocks;
    fake.reported_erases = fake.erases;
    return PLATFORM_OK;
}

/**
 * @brief 开始一个场景：假实现与日志清空，桩复位，Flash 卷置为就绪（序号 0 到 1）。
 * @param[in] mounted 是否发 Flash 卷就绪。
 */
static void begin_scenario(bool mounted)
{
    memset(&fake, 0, sizeof(fake));
    fake.capacity = 1ULL << 40;
    fake.free_blocks = 1000U;
    fake.erases = 4094U; /* 开机以来已有的擦除（格式化），日志应只算钩子开始之后的。 */
    erase_baseline_at_start = fake.erases;
    fake.min_free_seen = fake.free_blocks;
    fake.write_ticks = 250U;
    freertos_stub_reset();
    freertos_stub_set_tick(500U);
    lock_env_log_reset();
    if (mounted)
    {
        (void)volume_publish_ready(VOLUME_ID_FLASH);
    }
}

/**
 * @brief 跑一步作业，核对"一步最多一次文件调用"，把假时间推进它要求的等待。
 * @param[in] allow_forever 是否允许返回 BACKGROUND_WAIT_FOREVER（只有钩子关闭的配置会）。
 * @return 这一步的返回值（毫秒）。
 */
static uint32_t drive_one(bool allow_forever)
{
    const uint32_t calls_before = fake.calls_count;
    const uint32_t wait_ms = background_flash_pressure_step();

    assert(fake.calls_count - calls_before <= 1U);
    if (wait_ms == BACKGROUND_WAIT_FOREVER)
    {
        assert(allow_forever);
    }
    else
    {
        freertos_stub_set_tick(xTaskGetTickCount() + wait_ms);
    }
    return wait_ms;
}

/**
 * @brief 连续跑若干步。
 * @param[in] steps 步数。
 * @return 最后一步的返回值。
 */
static uint32_t drive(uint32_t steps)
{
    uint32_t last = 0U;

    for (uint32_t index = 0U; index < steps; index++)
    {
        last = drive_one(false);
    }
    return last;
}

/**
 * @brief 是否有一条日志，标签是 tag 且正文包含 needle。
 * @param[in] tag 标签。
 * @param[in] needle 子串。
 * @return 有为真。
 */
static bool has_log(const char *tag, const char *needle)
{
    for (uint32_t index = 0U; index < lock_env_log_count(); index++)
    {
        if ((strcmp(lock_env_log_tag(index), tag) == 0) && (strstr(lock_env_log_text(index), needle) != NULL))
        {
            return true;
        }
    }
    return false;
}

/**
 * @brief 某个标签的日志条数。
 * @param[in] tag 标签。
 * @return 条数。
 */
static uint32_t count_logs(const char *tag)
{
    uint32_t count = 0U;

    for (uint32_t index = 0U; index < lock_env_log_count(); index++)
    {
        if (strcmp(lock_env_log_tag(index), tag) == 0)
        {
            count++;
        }
    }
    return count;
}

/**
 * @brief 把"重复 n 次的 unit"追加到 text。
 * @param[in,out] text 目标缓冲。
 * @param[in] unit 单元。
 * @param[in] times 次数。
 */
static void append_repeated(char *text, const char *unit, uint32_t times)
{
    for (uint32_t index = 0U; index < times; index++)
    {
        strcat(text, unit);
    }
}

/**
 * @brief 模拟 Flash 卷消失再就绪（卸载再挂载，序号跳 2）。
 */
static void remount_flash(void)
{
    (void)volume_publish_gone(VOLUME_ID_FLASH);
    (void)volume_publish_ready(VOLUME_ID_FLASH);
}

/**
 * @brief 当前这一步之后，模拟下一次调用的期望调用串（用于"一个文件的一圈"）。
 * @param[out] expected 期望的调用串。
 * @param[in] with_reclaim 本配置是否在每次写后回收。
 */
static void expected_first_cycle(char *expected, bool with_reclaim)
{
    expected[0] = '\0';
    strcat(expected, "RO");
    append_repeated(expected, with_reclaim ? "WG" : "W", EXPECT_WRITES_PER_FILE);
    strcat(expected, "CRO");
}

/**
 * @brief 假应用 a 的后台作业：只用来在作业表里认出自己。
 * @return 1。
 */
static uint32_t app_a_step(void)
{
    return 1U;
}

/**
 * @brief 假应用 c 的后台作业：只用来在作业表里认出自己。
 * @return 2。
 */
static uint32_t app_c_step(void)
{
    return 2U;
}

/** @brief 注册表各应用的 `background_step` 按表的顺序接在内置作业之后，空字段不占位。 */
static void test_registry_background_steps_follow_the_builtin_jobs(void)
{
    const AppTable_EntryTypeDef rows[] = {
        {"a", NULL, NULL, NULL, NULL, app_a_step},
        {"b", NULL, NULL, NULL, NULL, NULL},
        {"c", NULL, NULL, NULL, NULL, app_c_step},
    };
    background_job_step_t jobs[BACKGROUND_JOB_MAX];
    const size_t builtin = (BACKGROUND_HOOK_FLASH_PRESSURE != 0U) ? 2U : 1U;
    const size_t count = background_jobs_build(rows, 3U, jobs, BACKGROUND_JOB_MAX);

    assert(count == (builtin + 2U));
    assert(jobs[0] == background_reclaim_step);
    assert(jobs[builtin] == app_a_step);
    assert(jobs[builtin + 1U] == app_c_step);
}

/** @brief 没有应用登记后台作业时只剩内置作业；回收作业无论钩子开不开都在表里，不进注册表。 */
static void test_registry_without_background_steps_adds_nothing(void)
{
    const AppTable_EntryTypeDef rows[] = {{"a", NULL, NULL, NULL, NULL, NULL}};
    background_job_step_t jobs[BACKGROUND_JOB_MAX];
    const size_t builtin = (BACKGROUND_HOOK_FLASH_PRESSURE != 0U) ? 2U : 1U;

    assert(background_jobs_build(rows, 1U, jobs, BACKGROUND_JOB_MAX) == builtin);
    assert(jobs[0] == background_reclaim_step);
    assert(background_jobs_build(NULL, 0U, jobs, BACKGROUND_JOB_MAX) == builtin);
}

#if (BACKGROUND_HOOK_FLASH_PRESSURE == 0U)

/**
 * @brief 默认构建：作业表里只有内置的回收作业，没有压力钩子，也没有文件调用和日志；启动不投 HOOK 行。
 *        Flash 卷还没发过事件（序号 0）时整圈都是等通知。
 */
static void test_default_build_has_no_pressure_job(void)
{
    background_job_step_t jobs[BACKGROUND_JOB_MAX];
    size_t count;

    begin_scenario(false);
    count = background_jobs_build(NULL, 0U, jobs, BACKGROUND_JOB_MAX);
    assert(count == 1U);
    assert(jobs[0] == background_reclaim_step);
    assert(background_loop_run_jobs(jobs, count) == BACKGROUND_WAIT_FOREVER);

    /* 就算直接调作业函数（表里不会有它），关闭时也什么都不做。 */
    assert(background_flash_pressure_step() == BACKGROUND_WAIT_FOREVER);
    background_jobs_announce();
    assert(fake.calls_count == 0U);
    assert(lock_env_log_count() == 0U);
}

#else

/** @brief 钩子打开：启动日志投一行 WARN HOOK，写明开了什么和调参（没有这一行就是没开）。 */
static void test_announce_posts_one_hook_line(void)
{
#if defined(TEST_VARIANT_FLOOR)
    const char *const expected = "flash_pressure ON chunk=32768 file=4194304 pace=100ms floor=300 reclaim=1 log=10000ms";
#elif defined(TEST_VARIANT_NORECLAIM)
    const char *const expected = "flash_pressure ON chunk=32768 file=4194304 pace=100ms floor=0 reclaim=0 log=10000ms";
#else
    const char *const expected = "flash_pressure ON chunk=32768 file=4194304 pace=100ms floor=0 reclaim=1 log=10000ms";
#endif

    begin_scenario(true);
    background_jobs_announce();
    assert(lock_env_log_count() == 1U);
    assert(strcmp(lock_env_log_tag(0U), "HOOK") == 0);
    assert(strcmp(lock_env_log_text(0U), expected) == 0);
    assert(lock_env_log_level(0U) == (uint32_t)SERVICE_LOG_LEVEL_WARN);
    assert(lock_env_log_longest_text() <= 127U);
}

/** @brief 钩子打开：作业表里是回收作业在前、压力钩子在后。 */
static void test_table_registers_the_job(void)
{
    background_job_step_t jobs[BACKGROUND_JOB_MAX];
    const size_t count = background_jobs_build(NULL, 0U, jobs, BACKGROUND_JOB_MAX);

    assert(count == 2U);
    assert(jobs[0] == background_reclaim_step);
    assert(jobs[1] == background_flash_pressure_step);
}

/** @brief Flash 卷没就绪（含 Flash 启动任务挂载期间）：不碰 Flash 卷、不投日志，只按轮询间隔回头看；就绪后才开始。 */
static void test_waits_for_the_flash_volume_without_touching_it(void)
{
    begin_scenario(false);

    for (uint32_t index = 0U; index < 20U; index++)
    {
        assert(drive_one(false) == 500U);
    }
    assert(fake.calls_count == 0U);
    assert(lock_env_log_count() == 0U);

    (void)volume_publish_ready(VOLUME_ID_FLASH);
    assert(drive_one(false) == 0U);
    assert(strcmp(fake.calls, "R") == 0);

    /* 卷消失：马上停手，不再调文件接口。 */
    (void)volume_publish_gone(VOLUME_ID_FLASH);
    assert(drive_one(false) == 500U);
    assert(drive_one(false) == 500U);
    assert(strcmp(fake.calls, "R") == 0);
}

/** @brief 一个文件的一圈：先删上次遗留，新建，写 128 块，每块后回收一次，关闭，删除，再新建。 */
static void test_one_file_cycle_writes_reclaims_closes_and_removes(void)
{
    char expected[1024];
    const bool with_reclaim = (BACKGROUND_HOOK_FLASH_PRESSURE_RECLAIM != 0U);

    begin_scenario(true);
    fake.file_exists = true; /* 上次测试中途断电留下的文件。 */
    fake.file_size = 123456U;

    expected_first_cycle(expected, with_reclaim);
    (void)drive((uint32_t)(2U + (with_reclaim ? 2U : 1U) * EXPECT_WRITES_PER_FILE + 3U));
    assert(strcmp(fake.calls, expected) == 0);
    assert(fake.file_exists && fake.handle_open); /* 第二个文件已经新建。 */
    assert(fake.file_size == 0U);
}

/** @brief 各步返回的等待：删除遗留、新建、写 0（紧接着回收）；回收后让出节拍 100 ms；删除后同样让出。 */
static void test_step_return_values(void)
{
    begin_scenario(true);

    assert(drive_one(false) == 0U);   /* 删除遗留 */
    assert(drive_one(false) == 0U);   /* 新建 */
#if (BACKGROUND_HOOK_FLASH_PRESSURE_RECLAIM != 0U)
    assert(drive_one(false) == 0U);   /* 写：紧接着回收 */
    assert(drive_one(false) == 100U); /* 回收：让出节拍 */
#else
    assert(drive_one(false) == 100U); /* 写：没有回收，直接让出节拍 */
#endif
}

/** @brief 写入的内容取自片内 Flash 映像开头，每块恰好 32 KiB（等于单次写上限），一个文件正好 128 块。 */
static void test_write_source_and_chunk_size(void)
{
    begin_scenario(true);
    (void)drive(2U);
    for (uint32_t index = 0U; index < EXPECT_WRITES_PER_FILE; index++)
    {
        while (fake.seen[KIND_WRITE] == index)
        {
            (void)drive_one(false);
        }
        assert(fake.last_write_requested == 32768U);
        assert(fake.last_write_source == (const void *)0x08000000UL);
    }
    assert(fake.file_size == 4194304U);
}

/** @brief 日志格式：启动那一行带空闲块数、擦除次数（只算钩子开始之后）；之后每 10 s 一行；每行不超过 127 字符。 */
static void test_stats_lines_carry_free_blocks_and_erase_count(void)
{
    char expected_start[160];
    uint32_t run_lines = 0U;

    begin_scenario(true);
    (void)drive_one(false);
    assert(count_logs("FLASHHOOK") == 1U);
    (void)snprintf(expected_start, sizeof(expected_start),
                   "start free=1000 erases=0 wr_kib=0 files=0 wmax=0ms rmax=0ms dmax=0ms");
    assert(strcmp(lock_env_log_text(0U), expected_start) == 0);
    assert(lock_env_log_level(0U) == (uint32_t)SERVICE_LOG_LEVEL_INFO);

    /* 假时间约 60 s：每 10 s 一行，不是每步一行。 */
    while (xTaskGetTickCount() < 500U + 60000U)
    {
        (void)drive_one(false);
    }
    run_lines = count_logs("FLASHHOOK") - 1U;
    assert(run_lines >= 5U);
    assert(run_lines <= 6U);
    assert(lock_env_log_longest_text() <= 127U);
    assert(has_log("FLASHHOOK", "run free="));
    assert(has_log("FLASHHOOK", "wmax=250ms"));
}

/** @brief 把空闲块压到水位以下之后出现真擦除；日志里的空闲块数和擦除次数与此刻 FTL 的读数一致。 */
static void test_pressure_goes_below_the_low_water_mark_and_erases(void)
{
    unsigned long free_blocks = 0UL;
    unsigned long erases = 0UL;
    bool found = false;

    begin_scenario(true);
    while (xTaskGetTickCount() < 500U + 120000U)
    {
        (void)drive_one(false);
    }

    assert(fake.min_free_seen < EXPECT_LOW_WATER);
    assert(fake.erases > erase_baseline_at_start);

    /* 最后一条统计行：空闲块数在水位以下，擦除次数是增量，大于 0。 */
    for (uint32_t index = lock_env_log_count(); index > 0U; index--)
    {
        if ((strcmp(lock_env_log_tag(index - 1U), "FLASHHOOK") == 0) &&
            (sscanf(lock_env_log_text(index - 1U), "run free=%lu erases=%lu", &free_blocks, &erases) == 2))
        {
            found = true;
            break;
        }
    }
    assert(found);
    assert(free_blocks < EXPECT_LOW_WATER);
    assert(erases > 0UL);
    assert(erases <= (unsigned long)(fake.erases - erase_baseline_at_start));
    assert(free_blocks <= (unsigned long)fake.reported_free + 20UL);
}

/** @brief 卷满：写入返回的字节数不足就关闭并删除这个文件，下一个文件照常开始；"满"只告警一次。 */
static void test_full_volume_closes_and_removes_the_file(void)
{
    begin_scenario(true);
    fake.capacity = 100U * 1024U; /* 三块半。 */

    (void)drive(2U + 2U * 4U + 1U); /* R O + (W G)×4 + C，前三块整块、第四块只写进 4 KiB */
    assert(strcmp(fake.calls, "ROWGWGWGWGC") == 0);
    (void)drive(2U);
    assert(strcmp(fake.calls, "ROWGWGWGWGCRO") == 0);
    assert(!fake.file_exists || fake.file_size == 0U);
    assert(has_log("FLASHHOOK", "flash full"));

    /* 再来一圈：不重复告警。 */
    (void)drive(2U * 4U + 3U);
    {
        uint32_t full_lines = 0U;

        for (uint32_t index = 0U; index < lock_env_log_count(); index++)
        {
            if (strstr(lock_env_log_text(index), "flash full") != NULL)
            {
                full_lines++;
            }
        }
        assert(full_lines == 1U);
    }
}

/** @brief 写入返回 NO_SPACE 同样当"卷满"处理，不当故障停下。 */
static void test_no_space_status_is_treated_as_full(void)
{
    begin_scenario(true);
    fake.fail_at[KIND_WRITE] = 2U;
    fake.fail_status[KIND_WRITE] = SERVICE_NO_SPACE;

    (void)drive(2U + 2U + 2U + 1U + 1U + 1U);
    assert(strcmp(fake.calls, "ROWGWGCRO") == 0);
    assert(has_log("FLASHHOOK", "flash full"));
    assert(count_logs("FLASHHOOK") >= 2U);
}

/** @brief 文件调用返回"卷不可用"（NOT_READY）不是故障：不报错、不关旧句柄；卷序号变化后重新从删遗留开始。 */
static void test_volume_unavailable_mid_file_restarts_after_remount(void)
{
    begin_scenario(true);
    fake.fail_at[KIND_WRITE] = 3U;
    fake.fail_status[KIND_WRITE] = SERVICE_NOT_READY;

    (void)drive(2U + 2U + 2U); /* R O WG WG */
    assert(strcmp(fake.calls, "ROWGWG") == 0);
    assert(drive_one(false) == 500U); /* W 返回 NOT_READY */
    assert(strcmp(fake.calls, "ROWGWGW") == 0);
    assert(count_logs("FLASHHOOK") == 1U); /* 只有开始那一行，没有停止告警。 */

    remount_flash();
    fake.handle_open = false; /* 卸载使旧句柄失效。 */
    (void)drive(2U);
    assert(strcmp(fake.calls, "ROWGWGWRO") == 0); /* 没有对旧句柄的 C。 */
    assert(has_log("FLASHHOOK", "start free="));
    assert(count_logs("FLASHHOOK") == 2U);
}

/** @brief 卷序号在两步之间变了（卸载再挂载，Mounted 看起来一直为真）：同样重新开始，不对旧句柄调关闭。 */
static void test_sequence_change_between_steps_restarts_the_cycle(void)
{
    begin_scenario(true);
    (void)drive(4U); /* R O W G */
    assert(strcmp(fake.calls, "ROWG") == 0);

    remount_flash();
    fake.handle_open = false;
    (void)drive(2U);
    assert(strcmp(fake.calls, "ROWGRO") == 0);
}

/** @brief 写入失败一次就停：记一条 ERROR，关掉文件，之后不再调文件接口，直到 Flash 卷序号变化。 */
static void test_write_failure_stops_until_the_volume_sequence_changes(void)
{
    begin_scenario(true);
    fake.fail_at[KIND_WRITE] = 2U;
    fake.fail_status[KIND_WRITE] = SERVICE_ERROR;

    (void)drive(2U + 2U + 1U); /* R O WG W(失败) */
    assert(strcmp(fake.calls, "ROWGW") == 0);
    assert(has_log("FLASHHOOK", "stopped at=write status=1"));
    {
        uint32_t level = 0U;

        for (uint32_t index = 0U; index < lock_env_log_count(); index++)
        {
            if (strstr(lock_env_log_text(index), "stopped") != NULL)
            {
                level = lock_env_log_level(index);
            }
        }
        assert(level == (uint32_t)SERVICE_LOG_LEVEL_ERROR);
    }

    (void)drive(1U); /* C */
    assert(strcmp(fake.calls, "ROWGWC") == 0);
    assert(!fake.handle_open);
    for (uint32_t index = 0U; index < 50U; index++)
    {
        assert(drive_one(false) == 500U);
    }
    assert(strcmp(fake.calls, "ROWGWC") == 0);
    assert(count_logs("FLASHHOOK") <= 3U); /* start、stopped；停下之后不再投统计行。 */

    remount_flash();
    (void)drive(2U);
    assert(strcmp(fake.calls, "ROWGWCRO") == 0);
}

/**
 * @brief 日志里第一条正文以 "stopped" 开头的 FLASHHOOK 行。
 * @return 正文；没有为 NULL。
 */
static const char *stopped_line(void)
{
    for (uint32_t index = 0U; index < lock_env_log_count(); index++)
    {
        if ((strcmp(lock_env_log_tag(index), "FLASHHOOK") == 0) && (strncmp(lock_env_log_text(index), "stopped", 7U) == 0))
        {
            return lock_env_log_text(index);
        }
    }
    return NULL;
}

/**
 * @brief 第 6 次上板的复现：第二次写失败，FTL 因此不就绪（有效/失效块数读作 0）。停下行带 FTL 最近一次失败的具体结果、
 *        请求类别、原始访问结果、失败次数、失败后的空闲块、失败那次写之前的快照、写请求字节数与文件已写字节。
 */
static void test_stop_line_carries_ftl_diagnostics_after_a_fault(void)
{
    begin_scenario(true);
    fake.fail_at[KIND_WRITE] = 2U;
    fake.fail_status[KIND_WRITE] = SERVICE_ERROR;
    fake.failure.LastFailure = PLATFORM_FLASH_FAILURE_CORRUPT;
    fake.failure.LastFailureOperation = PLATFORM_FLASH_FAILED_OP_WRITE;
    fake.failure.LastFailureRawStatus = 0U;
    fake.failure.FailureCount = 1U;

    (void)drive(2U + 2U + 1U); /* R O WG W(失败) */
    assert(strcmp(fake.calls, "ROWGW") == 0);
    /* 第一次写 32768 B = 10 个 3584 B 组，空闲块 1000 到 990；第二次写前快照是 990/0/0，请求 32768，文件已写 32768。 */
    assert(stopped_line() != NULL);
    assert(strcmp(stopped_line(),
                  "stopped at=write status=1 ftl=CORRUPT op=write raw=ok n=1 free=990 pre=990/0/0 req=32768 file=32768") == 0);
    assert(lock_env_log_longest_text() <= 127U);
}

/** @brief FTL 仍就绪（有效/失效块数非零）而写失败：这两个数既在失败后的读数里，也在写之前的快照里。 */
static void test_stop_line_with_a_ready_volume(void)
{
    begin_scenario(true);
    fake.fail_at[KIND_WRITE] = 2U;
    fake.fail_status[KIND_WRITE] = SERVICE_ERROR;
    fake.valid_groups = 100U;
    fake.stale_blocks = 7U;
    fake.failure.LastFailure = PLATFORM_FLASH_FAILURE_IO_ERROR;
    fake.failure.LastFailureOperation = PLATFORM_FLASH_FAILED_OP_WRITE;
    fake.failure.LastFailureRawStatus = 2U;
    fake.failure.FailureCount = 2U;

    (void)drive(2U + 2U + 1U);
    assert(strcmp(stopped_line(),
                  "stopped at=write status=1 ftl=IO_ERROR op=write raw=error n=2 free=990 valid=100 stale=7 pre=990/100/7 req=32768 file=32768") == 0);
}

/** @brief 文件接口报错而 FTL 从没失败过（错误不是 FTL 来的）：停下行写 `ftl=none`。 */
static void test_stop_line_when_the_ftl_never_failed(void)
{
    begin_scenario(true);
    fake.fail_at[KIND_WRITE] = 1U;
    fake.fail_status[KIND_WRITE] = SERVICE_ERROR;

    (void)drive(2U + 1U); /* R O W(失败) */
    assert(strcmp(stopped_line(), "stopped at=write status=1 ftl=none n=0 free=1000 pre=1000/0/0 req=32768 file=0") == 0);
}

/** @brief 别的步骤失败（这里是新建）：没有写请求，所以不带 pre、req、file，只带 FTL 诊断。 */
static void test_stop_line_of_a_non_write_step_has_no_write_fields(void)
{
    begin_scenario(true);
    fake.fail_at[KIND_OPEN] = 1U;
    fake.fail_status[KIND_OPEN] = SERVICE_ERROR;
    fake.failure.LastFailure = PLATFORM_FLASH_FAILURE_NOT_READY;
    fake.failure.LastFailureOperation = PLATFORM_FLASH_FAILED_OP_OPEN;
    fake.failure.FailureCount = 1U;
    fake.failure.LastRejection = PLATFORM_FLASH_FAILURE_NOT_READY;

    (void)drive(2U); /* R O(失败) */
    assert(strcmp(stopped_line(), "stopped at=open status=1 ftl=NOT_READY op=open raw=ok n=1 free=1000 rej=NOT_READY") == 0);
}

/** @brief 诊断读不到（FTL 没绑定）：停下行仍然记，写 `diag=n/a`；写请求字节数与文件已写字节照带（写之前的快照也读不到）。 */
static void test_stop_line_without_diagnostics(void)
{
    begin_scenario(true);
    fake.fail_at[KIND_WRITE] = 2U;
    fake.fail_status[KIND_WRITE] = SERVICE_ERROR;

    (void)drive(2U + 2U - 1U); /* R O WG（第一次写成功之后的回收）之前一步：下一步是回收，再下一步是失败的写 */
    (void)drive(1U);
    fake.diag_fails = true;
    (void)drive(1U);
    assert(strcmp(stopped_line(), "stopped at=write status=1 diag=n/a req=32768 file=32768") == 0);
}

/**
 * @brief 最坏取值（四位数的块数、失败次数封顶、拒绝与硬件快照都在）：行不超过 127 字符；写请求字节数和文件已写字节
 *        不被截断（`rej`、`hw` 在最后，先被截断）。
 */
static void test_stop_line_worst_case_keeps_the_write_fields(void)
{
    const char *line;

    begin_scenario(true);
    fake.free_blocks = 4094U;
    fake.fail_at[KIND_WRITE] = 120U;
    fake.fail_status[KIND_WRITE] = SERVICE_ERROR;
    fake.failure.LastFailure = PLATFORM_FLASH_FAILURE_NOT_READY;
    fake.failure.LastFailureOperation = PLATFORM_FLASH_FAILED_OP_RECLAIM;
    fake.failure.LastFailureRawStatus = 3U;
    fake.failure.FailureCount = 100000U;
    fake.failure.LastRejection = PLATFORM_FLASH_FAILURE_UNFORMATTED;
    fake.failure.LastFailureDeviceError = 0xffU;
    fake.failure.LastFailurePortStatus = 0xffU;

    for (uint32_t guard = 0U; (stopped_line() == NULL) && (guard < 1000U); guard++)
    {
        (void)drive_one(false);
    }
    line = stopped_line();
    assert(line != NULL);
    assert(lock_env_log_longest_text() <= 127U);
    assert(strstr(line, " req=32768 file=3899392") != NULL); /* 119 次写之后：119 * 32768 B */
    assert(strstr(line, "n=999") != NULL);
}

/** @brief 回收失败一次也停（失败一次就停，F 节）：记 ERROR、关文件、不再调。 */
static void test_reclaim_failure_stops(void)
{
    begin_scenario(true);
    fake.fail_at[KIND_RECLAIM] = 2U;
    fake.fail_status[KIND_RECLAIM] = SERVICE_TIMEOUT;

    (void)drive(2U + 2U + 2U); /* R O WG WG(回收失败) */
    assert(strcmp(fake.calls, "ROWGWG") == 0);
    assert(has_log("FLASHHOOK", "stopped at=reclaim status=5"));
    (void)drive(1U);
    assert(strcmp(fake.calls, "ROWGWGC") == 0);
    for (uint32_t index = 0U; index < 20U; index++)
    {
        (void)drive_one(false);
    }
    assert(strcmp(fake.calls, "ROWGWGC") == 0);
}

/** @brief 新建遇到 BUSY（文件已存在、槽用尽）不算故障：约 10 ms 后先删遗留再重试。 */
static void test_open_busy_is_retried_after_cleanup(void)
{
    begin_scenario(true);
    fake.fail_at[KIND_OPEN] = 1U;
    fake.fail_status[KIND_OPEN] = SERVICE_BUSY;

    (void)drive(2U); /* R O(BUSY) */
    assert(strncmp(fake.calls, "RO", 2U) == 0);
    (void)drive(2U);
    assert(strncmp(fake.calls, "RORO", 4U) == 0);
    assert(fake.handle_open);
    assert(count_logs("FLASHHOOK") == 1U);
}

/** @brief BUSY 一直不消失：重试 3 次后停下并记 ERROR，不无限重试。 */
static void test_open_busy_gives_up_after_the_retry_limit(void)
{
    begin_scenario(true);
    fake.fail_at[KIND_OPEN] = 1U;
    fake.fail_status[KIND_OPEN] = SERVICE_BUSY;
    fake.fail_sticky[KIND_OPEN] = true;

    (void)drive(20U);
    assert(strcmp(fake.calls, "RORORORO") == 0); /* 第一次加 3 次重试，共 4 次新建。 */
    assert(has_log("FLASHHOOK", "stopped at=open status=4"));
    assert(fake.seen[KIND_OPEN] == 4U);
}

/** @brief 新建遇到别的失败（例如目录满）直接停下，不重试。 */
static void test_open_hard_failure_stops(void)
{
    begin_scenario(true);
    fake.fail_at[KIND_OPEN] = 1U;
    fake.fail_status[KIND_OPEN] = SERVICE_NO_SPACE;

    (void)drive(10U);
    assert(strcmp(fake.calls, "RO") == 0);
    assert(has_log("FLASHHOOK", "stopped at=open status=9"));
}

/** @brief 关闭失败：停下，不再删除（卷可能有问题）。 */
static void test_close_failure_stops_without_removing(void)
{
    char expected[1024];
    const bool with_reclaim = (BACKGROUND_HOOK_FLASH_PRESSURE_RECLAIM != 0U);
    const uint32_t steps_to_close = 2U + (with_reclaim ? 2U : 1U) * EXPECT_WRITES_PER_FILE;

    begin_scenario(true);
    fake.fail_at[KIND_CLOSE] = 1U;
    fake.fail_status[KIND_CLOSE] = SERVICE_ERROR;
    (void)drive(steps_to_close + 1U + 10U);
    expected[0] = '\0';
    strcat(expected, "RO");
    append_repeated(expected, with_reclaim ? "WG" : "W", EXPECT_WRITES_PER_FILE);
    strcat(expected, "C");
    assert(strcmp(fake.calls, expected) == 0);
    assert(has_log("FLASHHOOK", "stopped at=close status=1"));
}

/** @brief 删除失败：停下，不再新建。 */
static void test_remove_failure_stops(void)
{
    char expected[1024];
    const bool with_reclaim = (BACKGROUND_HOOK_FLASH_PRESSURE_RECLAIM != 0U);
    const uint32_t steps_to_close = 2U + (with_reclaim ? 2U : 1U) * EXPECT_WRITES_PER_FILE;

    begin_scenario(true);
    fake.fail_at[KIND_REMOVE] = 2U; /* 第一次是开头删遗留 */
    fake.fail_status[KIND_REMOVE] = SERVICE_ERROR;
    (void)drive(steps_to_close + 2U + 10U);
    expected[0] = '\0';
    strcat(expected, "RO");
    append_repeated(expected, with_reclaim ? "WG" : "W", EXPECT_WRITES_PER_FILE);
    strcat(expected, "CR");
    assert(strcmp(fake.calls, expected) == 0);
    assert(has_log("FLASHHOOK", "stopped at=remove status=1"));
}

/** @brief 诊断读取失败（FTL 没绑定）不影响作业本身：不投统计行，其余照常。 */
static void test_diagnostics_failure_does_not_stop_the_job(void)
{
    begin_scenario(true);
    fake.diag_fails = true;

    (void)drive(20U);
    assert(strncmp(fake.calls, "ROWG", 4U) == 0);
    assert(count_logs("FLASHHOOK") == 0U);
}

#endif /* BACKGROUND_HOOK_FLASH_PRESSURE */

#if defined(TEST_VARIANT_FLOOR)

/** @brief 空闲块下限：低于下限暂停写入，只靠回收（每步一次文件调用）把空闲块抬回来；写入随后继续，不死等。 */
static void test_free_floor_pauses_writes_until_reclaim_restores_the_blocks(void)
{
    uint32_t writes_after_floor = 0U;

    begin_scenario(true);
    fake.free_blocks = 320U;
    fake.min_free_seen = 320U;
    fake.erases = 4094U;

    for (uint32_t index = 0U; index < 600U; index++)
    {
        (void)drive_one(false);
    }
    writes_after_floor = fake.seen[KIND_WRITE];

    assert(fake.min_free_seen >= 300U - 10U); /* 低于下限（300）就不再写；一块写最多吃 10 个块。 */
    assert(fake.erases > 4094U);              /* 靠回收擦出了空闲块。 */
    assert(writes_after_floor > 20U);         /* 暂停后继续写，没有卡死。 */
    assert(fake.seen[KIND_RECLAIM] > fake.seen[KIND_WRITE] - 1U);
}

#endif /* TEST_VARIANT_FLOOR */

#if defined(TEST_VARIANT_NORECLAIM)

/** @brief 关掉本作业的回收（#220 之后由后台任务自己的回收作业负责）：写完一块就让出节拍，一次回收都不调。 */
static void test_job_never_reclaims_when_disabled(void)
{
    begin_scenario(true);
    (void)drive(400U);
    assert(fake.seen[KIND_WRITE] > 50U);
    assert(fake.seen[KIND_RECLAIM] == 0U);
    assert(strchr(fake.calls, 'G') == NULL);
}

#endif /* TEST_VARIANT_NORECLAIM */

typedef struct
{
    const char *name;
    void (*run)(void);
} Case;

static const Case cases[] = {
    {"registry_steps_follow_builtin", test_registry_background_steps_follow_the_builtin_jobs},
    {"registry_without_steps", test_registry_without_background_steps_adds_nothing},
#if (BACKGROUND_HOOK_FLASH_PRESSURE == 0U)
    {"default_build_has_no_job", test_default_build_has_no_pressure_job},
#else
    {"announce", test_announce_posts_one_hook_line},
    {"table_registers_the_job", test_table_registers_the_job},
    {"waits_for_volume", test_waits_for_the_flash_volume_without_touching_it},
    {"one_file_cycle", test_one_file_cycle_writes_reclaims_closes_and_removes},
    {"step_return_values", test_step_return_values},
    {"write_source_and_chunk", test_write_source_and_chunk_size},
    {"stats_lines", test_stats_lines_carry_free_blocks_and_erase_count},
    {"goes_below_low_water", test_pressure_goes_below_the_low_water_mark_and_erases},
    {"diagnostics_failure", test_diagnostics_failure_does_not_stop_the_job},
    {"open_hard_failure", test_open_hard_failure_stops},
    {"close_failure", test_close_failure_stops_without_removing},
    {"remove_failure", test_remove_failure_stops},
#if (BACKGROUND_HOOK_FLASH_PRESSURE_RECLAIM != 0U) && !defined(TEST_VARIANT_FLOOR)
    {"full_volume", test_full_volume_closes_and_removes_the_file},
    {"no_space_is_full", test_no_space_status_is_treated_as_full},
    {"volume_unavailable", test_volume_unavailable_mid_file_restarts_after_remount},
    {"sequence_change", test_sequence_change_between_steps_restarts_the_cycle},
    {"write_failure_stops", test_write_failure_stops_until_the_volume_sequence_changes},
    {"reclaim_failure_stops", test_reclaim_failure_stops},
    {"stop_line_after_a_fault", test_stop_line_carries_ftl_diagnostics_after_a_fault},
    {"stop_line_ready_volume", test_stop_line_with_a_ready_volume},
    {"stop_line_ftl_never_failed", test_stop_line_when_the_ftl_never_failed},
    {"stop_line_non_write_step", test_stop_line_of_a_non_write_step_has_no_write_fields},
    {"stop_line_without_diagnostics", test_stop_line_without_diagnostics},
    {"stop_line_worst_case", test_stop_line_worst_case_keeps_the_write_fields},
    {"open_busy_retry", test_open_busy_is_retried_after_cleanup},
    {"open_busy_gives_up", test_open_busy_gives_up_after_the_retry_limit},
#endif
#endif
#if defined(TEST_VARIANT_FLOOR)
    {"free_floor", test_free_floor_pauses_writes_until_reclaim_restores_the_blocks},
#endif
#if defined(TEST_VARIANT_NORECLAIM)
    {"never_reclaims", test_job_never_reclaims_when_disabled},
#endif
};

/**
 * @brief 运行命令行指定的用例（模块状态是静态的，每个用例一个进程）。
 * @param[in] argc 参数个数。
 * @param[in] argv 参数。
 * @return 0 表示通过（失败由 assert 中止）。
 */
int main(int argc, char **argv)
{
    if (argc != 2)
    {
        (void)fprintf(stderr, "usage: %s <case>\n", argv[0]);
        return 2;
    }

    for (size_t index = 0U; index < sizeof(cases) / sizeof(cases[0]); index++)
    {
        if (strcmp(cases[index].name, argv[1]) == 0)
        {
            cases[index].run();
            (void)printf("background_flash_pressure %s: OK\n", argv[1]);
            return 0;
        }
    }

    (void)fprintf(stderr, "unknown case: %s\n", argv[1]);
    return 2;
}
