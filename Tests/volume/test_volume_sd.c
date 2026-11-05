/**
 ******************************************************************************
 * @file    test_volume_sd.c
 * @brief   卷任务 SD 部分的主机测试：启动、消抖、关闸、5 行对账表的每条路径、挂载失败不发事件、
 *          每次 PauseSD 都配 ResumeSD（所有路径）、卷路径日志、假抖动钩子。
 *
 * @details
 *          链接真实的 `volume_sd.c`、`volume_reconcile.c`、`volume_event.c`、共用 FreeRTOS 桩和 Service Log 替身；
 *          Filesystem Service 的七个入口（StartSD、RefreshSD、PauseSD、ResumeSD、InitSD、MountSD、UnmountSD）
 *          在本文件里用脚本化的假实现替换，并记录调用顺序、闸的开合以及调用那一刻的卷状态字。
 *          同一份源文件编两次：默认配置，以及打开假抖动钩子和 SD 基准的配置（`VOLUME_HOOK_FAKE_BOUNCE=1`、
 *          `STORAGE_SD_BENCHMARK_ENABLE=1`），后者验证钩子的节拍与基准在发卷就绪之前运行。
 *
 *          主机测试是单线程的：这里证明每一条路径上调用的顺序、卷状态字的更新时机、闸的开合是对的，
 *          不证明真实任务之间的交错、优先级继承、等锁时长和真实 ISR 时序（第 3 次上板）。
 ******************************************************************************
 */

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "freertos_stub.h"
#include "lock_env_stub.h"

#include "APP/app_config.h"
#include "APP/tasks/volume/sd/volume_sd.h"
#include "APP/tasks/volume/volume_task.h"
#include "Service/filesystem/filesystem_card.h"
#include "Service/filesystem/filesystem_service.h"
#include "Service/log/log_service.h"

#if STORAGE_SD_BENCHMARK_ENABLE
#include "APP/tasks/volume/benchmark/storage_sd_benchmark.h"
#endif

#define CALLS_MAX 48U

/** @brief 假 Filesystem Service 的脚本与观察值。 */
typedef struct
{
    /* 脚本 */
    Service_StatusTypeDef start_status;
    Service_Filesystem_CardStatusTypeDef start_card;
    Service_StatusTypeDef init_status;
    Service_StatusTypeDef refresh_status;
    Service_Filesystem_CardStatusTypeDef refresh_card;  /**< 第一次刷新的结果；之后的刷新返回“与卷现状一致”。 */
    Service_StatusTypeDef mount_status;
    Service_StatusTypeDef unmount_status;
    bool edge_during_refresh;                          /**< 第一次刷新期间来了一个新边沿。 */
    bool edge_during_unmount;
    bool edge_during_mount;
    uint32_t refresh_advance_ticks;                    /**< 第一次刷新“花”了多少 tick。 */

    /* 观察 */
    bool service_mounted;                              /**< 假 Service 眼里卷是否挂着（与卷状态字对照）。 */
    bool gate_closed;
    uint32_t pause_calls;
    uint32_t resume_calls;
    uint32_t start_calls;
    uint32_t init_calls;
    uint32_t refresh_calls;
    uint32_t mount_calls;
    uint32_t unmount_calls;
    bool gate_closed_at_refresh;
    bool gate_closed_at_unmount;
    bool gate_closed_at_mount;
    Service_Filesystem_CardDetectCallback_t detect_callback;
    void *detect_context;
    char calls[CALLS_MAX];                             /**< 调用顺序：S 启动 R 刷新 U 卸载 I 初始化 M 挂载 P 关闸 O 开闸 B 基准。 */
    uint32_t calls_count;
    bool unmount_saw_mounted;                          /**< 卸载被调用那一刻，卷状态字里卷是否还挂着。 */
    uint32_t seq_at_unmount;
    bool mount_saw_mounted;
    uint32_t seq_at_mount;
    uint32_t seq_at_resume;                            /**< 开闸被调用那一刻的卷序号。 */
    bool mounted_at_resume;
    uint32_t wakes_with_gate_open;                     /**< 订阅者被叫醒时闸已经开着的次数（应为 0）。 */
    uint32_t wakes;
    uint32_t benchmark_calls;
    uint32_t benchmark_saw_sequence;
    uint32_t benchmark_saw_mount_calls;
} Fake;

static Fake fake;
static TaskHandle_t subscriber;
static bool subscriber_registered;

/**
 * @brief 记一次调用的顺序标记。
 * @param[in] mark 标记字符。
 */
static void note_call(char mark)
{
    assert(fake.calls_count + 1U < CALLS_MAX);
    fake.calls[fake.calls_count++] = mark;
    fake.calls[fake.calls_count] = '\0';
}

/**
 * @brief 在卷任务自己的通知槽里放一个卡检测边沿，模拟对账期间又抖了一下（ISR 回调的效果）。
 */
static void inject_edge(void)
{
    assert(xTaskNotifyIndexed(xTaskGetCurrentTaskHandle(),
                              VOLUME_NOTIFY_EVENT,
                              VOLUME_NOTIFY_FLAG_SD_DETECT,
                              eSetBits) == pdPASS);
}

/**
 * @brief 组一份卡状态。
 * @param[in] present 卡检测脚。
 * @param[in] ready 设备就绪。
 * @param[in] reinitialized 这次调用把卡重新初始化过。
 * @return 卡状态。
 */
static Service_Filesystem_CardStatusTypeDef card(bool present, bool ready, bool reinitialized)
{
    Service_Filesystem_CardStatusTypeDef value;

    memset(&value, 0, sizeof(value));
    value.Present = present;
    value.Ready = ready;
    value.Reinitialized = reinitialized;
    if (ready)
    {
        value.CapacityBytes = 64ULL * 1024ULL * 1024ULL * 1024ULL;
        value.BlockSize = 512U;
    }
    return value;
}

/* ------------------------------------------------------------------------- */
/* 假 Filesystem Service                                                      */
/* ------------------------------------------------------------------------- */

/**
 * @brief 假启动：记下回调与上下文，按脚本返回。
 * @param[in] detect_callback 卡检测回调。
 * @param[in] detect_context 回调上下文。
 * @param[out] status 卡状态。
 * @return 脚本里的结果。
 */
Service_StatusTypeDef Service_Filesystem_StartSD(Service_Filesystem_CardDetectCallback_t detect_callback,
                                                 void *detect_context,
                                                 Service_Filesystem_CardStatusTypeDef *status)
{
    fake.start_calls++;
    note_call('S');
    fake.detect_callback = detect_callback;
    fake.detect_context = detect_context;
    *status = fake.start_card;
    return fake.start_status;
}

/**
 * @brief 假刷新：记下闸是否关着；第一次按脚本返回，之后返回与卷现状一致的“没变化”。
 * @param[out] status 卡状态。
 * @return 脚本里的结果。
 */
Service_StatusTypeDef Service_Filesystem_RefreshSD(Service_Filesystem_CardStatusTypeDef *status)
{
    fake.refresh_calls++;
    note_call('R');
    fake.gate_closed_at_refresh = fake.gate_closed;

    if (fake.refresh_calls == 1U)
    {
        if (fake.refresh_advance_ticks != 0U)
        {
            freertos_stub_set_tick(xTaskGetTickCount() + fake.refresh_advance_ticks);
        }
        if (fake.edge_during_refresh)
        {
            inject_edge();
        }
        *status = fake.refresh_card;
        return fake.refresh_status;
    }

    *status = fake.service_mounted ? card(true, true, false) : card(false, false, false);
    return SERVICE_OK;
}

/**
 * @brief 假关闸。
 */
void Service_Filesystem_PauseSD(void)
{
    fake.gate_closed = true;
    fake.pause_calls++;
    note_call('P');
}

/**
 * @brief 假开闸：记下此刻的卷状态字，证明“先更新、叫醒，最后开闸”。
 */
void Service_Filesystem_ResumeSD(void)
{
    const Volume_StatusTypeDef status = volume_status(VOLUME_ID_SD);

    fake.gate_closed = false;
    fake.resume_calls++;
    note_call('O');
    fake.seq_at_resume = status.Sequence;
    fake.mounted_at_resume = status.Mounted;
}

/**
 * @brief 假 InitSD。
 * @return 脚本里的结果。
 */
Service_StatusTypeDef Service_Filesystem_InitSD(void)
{
    fake.init_calls++;
    note_call('I');
    return fake.init_status;
}

/**
 * @brief 假挂载：记下闸与卷状态字；成功才让假 Service 眼里的卷挂着。
 * @return 脚本里的结果。
 */
Service_StatusTypeDef Service_Filesystem_MountSD(void)
{
    const Volume_StatusTypeDef status = volume_status(VOLUME_ID_SD);

    fake.mount_calls++;
    note_call('M');
    fake.gate_closed_at_mount = fake.gate_closed;
    fake.mount_saw_mounted = status.Mounted;
    fake.seq_at_mount = status.Sequence;
    if (fake.edge_during_mount && (fake.mount_calls == 1U))
    {
        inject_edge();
    }
    fake.service_mounted = (fake.mount_status == SERVICE_OK);
    return fake.mount_status;
}

/**
 * @brief 假卸载：记下闸与卷状态字；不论返回什么，假 Service 眼里的卷都已卸载（真实实现的拔出路径同样如此）。
 * @return 脚本里的结果。
 */
Service_StatusTypeDef Service_Filesystem_UnmountSD(void)
{
    const Volume_StatusTypeDef status = volume_status(VOLUME_ID_SD);

    fake.unmount_calls++;
    note_call('U');
    fake.gate_closed_at_unmount = fake.gate_closed;
    fake.unmount_saw_mounted = status.Mounted;
    fake.seq_at_unmount = status.Sequence;
    if (fake.edge_during_unmount && (fake.unmount_calls == 1U))
    {
        inject_edge();
    }
    fake.service_mounted = false;
    return fake.unmount_status;
}

#if STORAGE_SD_BENCHMARK_ENABLE
/**
 * @brief 假 SD 基准：记下被调用那一刻已经挂载了几次、卷序号是多少。
 * @return 恒为 true。
 */
bool storage_sd_benchmark_run(void)
{
    fake.benchmark_calls++;
    note_call('B');
    fake.benchmark_saw_sequence = volume_status(VOLUME_ID_SD).Sequence;
    fake.benchmark_saw_mount_calls = fake.mount_calls;
    return true;
}
#endif

/* ------------------------------------------------------------------------- */
/* 场景工具                                                                   */
/* ------------------------------------------------------------------------- */

/**
 * @brief 通知钩子：订阅者被叫醒的那一刻，记下闸是否已经开着。
 * @param[in] task 目标任务。
 * @param[in] index 槽号。
 * @param[in] value 通知值。
 * @param[in,out] context 未使用。
 */
static void record_wake(TaskHandle_t task, UBaseType_t index, uint32_t value, void *context)
{
    (void)index;
    (void)value;
    (void)context;
    if (task == subscriber)
    {
        fake.wakes++;
        if (!fake.gate_closed)
        {
            fake.wakes_with_gate_open++;
        }
    }
}

/**
 * @brief 开始一个场景：清空脚本与观察值、日志、桩；把 SD 卷的状态字调到指定的挂载状态。
 * @param[in] mounted 场景开始时卷是否挂着（卷状态字与假 Service 都一致）。
 */
static void begin_scenario(bool mounted)
{
    memset(&fake, 0, sizeof(fake));
    fake.start_status = SERVICE_OK;
    fake.init_status = SERVICE_OK;
    fake.refresh_status = SERVICE_OK;
    fake.mount_status = SERVICE_OK;
    fake.unmount_status = SERVICE_OK;

    freertos_stub_reset();
    lock_env_log_reset();
    subscriber = freertos_stub_task_create();
    if (!subscriber_registered)
    {
        assert(volume_subscribe(subscriber, 3U, 0x1U));
        subscriber_registered = true;
    }
    freertos_stub_set_notify_hook(record_wake, NULL);
    freertos_stub_set_tick(1000U);

    while (volume_status(VOLUME_ID_SD).Mounted != mounted)
    {
        if (mounted)
        {
            (void)volume_publish_ready(VOLUME_ID_SD);
        }
        else
        {
            (void)volume_publish_gone(VOLUME_ID_SD);
        }
    }
    fake.service_mounted = mounted;
    fake.wakes = 0U;
    fake.wakes_with_gate_open = 0U;
}

#if !VOLUME_HOOK_FAKE_BOUNCE
/**
 * @brief 在 t0 来一个边沿，之后按函数返回的等待时间推进时间并继续调用，直到卷任务回到空闲（返回无限等待）。
 * @param[in] t0 边沿的 tick。
 * @return 空闲时的 tick。
 */
static TickType_t run_episode(TickType_t t0)
{
    TickType_t now = t0;
    TickType_t wait;
    uint32_t guard = 0U;

    freertos_stub_set_tick(now);
    wait = volume_sd_step(now, true);
    while (wait != portMAX_DELAY)
    {
        assert(guard++ < 12U);
        assert(fake.gate_closed); /* 还在消抖或重新消抖：闸必须关着。 */
        now = xTaskGetTickCount();
        now += wait;
        freertos_stub_set_tick(now);
        wait = volume_sd_step(now, false);
    }
    return now;
}

#endif

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
 * @brief 数标签与子串都匹配的日志条数。
 * @param[in] tag 标签。
 * @param[in] needle 子串。
 * @return 条数。
 */
static uint32_t count_logs(const char *tag, const char *needle)
{
    uint32_t count = 0U;

    for (uint32_t index = 0U; index < lock_env_log_count(); index++)
    {
        if ((strcmp(lock_env_log_tag(index), tag) == 0) && (strstr(lock_env_log_text(index), needle) != NULL))
        {
            count++;
        }
    }
    return count;
}

/* ------------------------------------------------------------------------- */
/* 启动                                                                       */
/* ------------------------------------------------------------------------- */

/**
 * @brief 开机时卡在且就绪：注册卡检测回调，初始化、挂载，成功后才发卷就绪（序号 0 到 1），记一行 boot 路径日志。
 */
static void test_boot_with_card_mounts_then_publishes_ready(void)
{
    begin_scenario(false);
    fake.start_card = card(true, true, true);

    assert(volume_sd_start(subscriber));

    assert(strcmp(fake.calls, (STORAGE_SD_BENCHMARK_ENABLE != 0) ? "SIMB" : "SIM") == 0);
    assert(fake.detect_callback == volume_sd_detect_callback);
    assert(fake.detect_context == subscriber);
    assert(volume_status(VOLUME_ID_SD).Mounted);
    assert(has_log("VOLUME", "sd boot path=mount card=pri mount=ok seq=0->"));
    assert(fake.pause_calls == 0U); /* 启动路径没有闸。 */
}

#if !VOLUME_HOOK_FAKE_BOUNCE
/**
 * @brief 开机无卡：什么事件都不发，序号保持不变，只记日志；不挂载。
 */
static void test_boot_without_card_publishes_nothing(void)
{
    uint32_t sequence;

    begin_scenario(false);
    sequence = volume_status(VOLUME_ID_SD).Sequence;
    fake.start_card = card(false, false, false);

    assert(volume_sd_start(subscriber));

    assert(strcmp(fake.calls, "S") == 0);
    assert(volume_status(VOLUME_ID_SD).Sequence == sequence);
    assert(!volume_status(VOLUME_ID_SD).Mounted);
    assert(has_log("SD", "No card inserted."));
    assert(has_log("VOLUME", "sd boot path=idle card=---"));
}

/**
 * @brief 开机挂载失败（卡上没有 FAT，或挂载出错）：不发卷就绪，只记日志，等下一次边沿再试。
 */
static void test_boot_mount_failure_publishes_nothing(void)
{
    static const Service_StatusTypeDef failures[] = {SERVICE_NO_FILESYSTEM, SERVICE_ERROR, SERVICE_BUSY};

    for (size_t index = 0U; index < sizeof(failures) / sizeof(failures[0]); index++)
    {
        uint32_t sequence;

        begin_scenario(false);
        sequence = volume_status(VOLUME_ID_SD).Sequence;
        fake.start_card = card(true, true, true);
        fake.mount_status = failures[index];

        assert(!volume_sd_start(subscriber));

        assert(volume_status(VOLUME_ID_SD).Sequence == sequence);
        assert(!volume_status(VOLUME_ID_SD).Mounted);
        assert(fake.wakes == 0U);
        assert(has_log("VOLUME", "mount=fail"));
    }
}

/**
 * @brief 开机 InitSD 失败、卡在但设备没就绪、Platform 启动失败：都不挂载、不发事件。
 */
static void test_boot_failures_publish_nothing(void)
{
    uint32_t sequence;

    begin_scenario(false);
    sequence = volume_status(VOLUME_ID_SD).Sequence;
    fake.start_card = card(true, true, true);
    fake.init_status = SERVICE_NOT_READY;
    assert(!volume_sd_start(subscriber));
    assert(fake.mount_calls == 0U);
    assert(volume_status(VOLUME_ID_SD).Sequence == sequence);

    begin_scenario(false);
    fake.start_card = card(true, false, false);
    assert(!volume_sd_start(subscriber));
    assert(fake.init_calls == 0U);
    assert(fake.mount_calls == 0U);
    assert(volume_status(VOLUME_ID_SD).Sequence == sequence);

    begin_scenario(false);
    fake.start_status = SERVICE_ERROR;
    fake.start_card = card(true, true, true);
    assert(!volume_sd_start(subscriber));
    assert(fake.init_calls == 0U);
    assert(fake.mount_calls == 0U);
    assert(volume_status(VOLUME_ID_SD).Sequence == sequence);
}

/**
 * @brief SD 基准（默认关闭）在发卷就绪之前运行：此时卷已挂载（挂载次数为 1），但卷序号还没变；默认配置里不运行。
 */
static void test_sd_benchmark_runs_after_mount_and_before_ready(void)
{
    uint32_t sequence;

    begin_scenario(false);
    sequence = volume_status(VOLUME_ID_SD).Sequence;
    fake.start_card = card(true, true, true);

    assert(volume_sd_start(subscriber));

    (void)sequence; /* 只有基准打开的配置才比较它。 */
    if (STORAGE_SD_BENCHMARK_ENABLE != 0)
    {
#if STORAGE_SD_BENCHMARK_ENABLE
        assert(fake.benchmark_calls == 1U);
        assert(fake.benchmark_saw_mount_calls == 1U);
        assert(fake.benchmark_saw_sequence == sequence);
        assert(volume_status(VOLUME_ID_SD).Sequence == sequence + 1U);
#endif
    }
    else
    {
        assert(fake.benchmark_calls == 0U);
    }
}

/* ------------------------------------------------------------------------- */
/* 对账路径                                                                   */
/* ------------------------------------------------------------------------- */

/**
 * @brief 挂着，卡不在：关闸、刷新、卸载、发卷消失（先卸载后发）、开闸（最后）。订阅者被叫醒时闸还关着。
 */
static void test_removal_unmounts_publishes_gone_then_opens_the_gate(void)
{
    uint32_t before;

    begin_scenario(true);
    before = volume_status(VOLUME_ID_SD).Sequence;
    fake.refresh_card = card(false, false, false);
    fake.refresh_advance_ticks = 4U;

    (void)run_episode(2000U);

    assert(strcmp(fake.calls, "PRUO") == 0);
    assert(fake.unmount_saw_mounted);                 /* 卸载时卷状态字还没改：先卸载，后发卷消失。 */
    assert(fake.seq_at_unmount == before);
    assert(fake.seq_at_resume == before + 1U);        /* 开闸时卷消失已经发布。 */
    assert(!fake.mounted_at_resume);
    assert(!volume_status(VOLUME_ID_SD).Mounted);
    assert(fake.wakes == 1U);
    assert(fake.wakes_with_gate_open == 0U);          /* 先叫醒订阅者，最后才开闸；也不等它们。 */
    assert(fake.gate_closed_at_refresh && fake.gate_closed_at_unmount);
    assert(has_log("VOLUME", "sd settle path=unmount refresh=ok card=--- mount=-"));
    assert(has_log("SD", "Card removed."));
}

/**
 * @brief 挂着，卡在，重新初始化过（换卡，或被吞的拔出之后卡又回来）：卸载、发卷消失、挂载、发卷就绪，最后才开闸；序号跳 2。
 */
static void test_swap_unmounts_then_mounts_and_jumps_the_sequence_by_two(void)
{
    uint32_t before;

    begin_scenario(true);
    before = volume_status(VOLUME_ID_SD).Sequence;
    fake.refresh_card = card(true, true, true);

    (void)run_episode(2000U);

    assert(strcmp(fake.calls, "PRUIMO") == 0);
    assert(fake.seq_at_unmount == before);
    assert(!fake.mount_saw_mounted);                  /* 挂载时卷消失已经发布。 */
    assert(fake.seq_at_mount == before + 1U);
    assert(fake.seq_at_resume == before + 2U);        /* 开闸时卷就绪也已经发布。 */
    assert(fake.mounted_at_resume);
    assert(fake.wakes == 2U);
    assert(fake.wakes_with_gate_open == 0U);
    assert(fake.gate_closed_at_mount);
    assert(has_log("VOLUME", "sd settle path=remount refresh=ok card=pri mount=ok"));
    assert(has_log("VOLUME", "seq="));
}

/**
 * @brief 没挂，卡在且就绪（插卡）：挂载，成功后发卷就绪。
 */
static void test_insertion_mounts_and_publishes_ready(void)
{
    uint32_t before;

    begin_scenario(false);
    before = volume_status(VOLUME_ID_SD).Sequence;
    fake.refresh_card = card(true, true, true);

    (void)run_episode(2000U);

    assert(strcmp(fake.calls, "PRIMO") == 0);
    assert(fake.seq_at_mount == before);
    assert(fake.seq_at_resume == before + 1U);
    assert(fake.mounted_at_resume);
    assert(has_log("VOLUME", "sd settle path=mount refresh=ok card=pri mount=ok"));
    assert(has_log("SD", "Card inserted"));
}

/**
 * @brief 没挂，卡不在：无动作，只开闸，不发事件。
 */
static void test_unmounted_and_absent_does_nothing_but_open_the_gate(void)
{
    uint32_t before;

    begin_scenario(false);
    before = volume_status(VOLUME_ID_SD).Sequence;
    fake.refresh_card = card(false, false, false);

    (void)run_episode(2000U);

    assert(strcmp(fake.calls, "PRO") == 0);
    assert(volume_status(VOLUME_ID_SD).Sequence == before);
    assert(fake.wakes == 0U);
    assert(has_log("VOLUME", "sd settle path=idle"));
}

/**
 * @brief 假抖动：挂着、卡在、没变化，只开闸；不卸载、不挂载、不发事件，订阅者一次都不被叫醒。
 */
static void test_false_bounce_only_opens_the_gate(void)
{
    uint32_t before;

    begin_scenario(true);
    before = volume_status(VOLUME_ID_SD).Sequence;
    fake.refresh_card = card(true, true, false);

    (void)run_episode(2000U);

    assert(strcmp(fake.calls, "PRO") == 0);
    assert(volume_status(VOLUME_ID_SD).Sequence == before);
    assert(volume_status(VOLUME_ID_SD).Mounted);
    assert(fake.wakes == 0U);
    assert(has_log("VOLUME", "sd settle path=gate_only refresh=ok card=pr- mount=-"));
}

/**
 * @brief 换卡路径上挂载失败：卷消失照发（旧卷已卸载），卷就绪不发；闸照样打开。没挂时挂载失败同样不发事件。
 */
static void test_mount_failure_never_publishes_ready_but_still_opens_the_gate(void)
{
    uint32_t before;

    begin_scenario(true);
    before = volume_status(VOLUME_ID_SD).Sequence;
    fake.refresh_card = card(true, true, true);
    fake.mount_status = SERVICE_NO_FILESYSTEM;

    (void)run_episode(2000U);

    assert(strcmp(fake.calls, "PRUIMO") == 0);
    assert(volume_status(VOLUME_ID_SD).Sequence == before + 1U);
    assert(!volume_status(VOLUME_ID_SD).Mounted);
    assert(fake.wakes == 1U);
    assert(has_log("SD", "No FAT filesystem found."));
    assert(has_log("VOLUME", "path=remount"));
    assert(has_log("VOLUME", "mount=fail"));

    begin_scenario(false);
    before = volume_status(VOLUME_ID_SD).Sequence;
    fake.refresh_card = card(true, true, true);
    fake.mount_status = SERVICE_ERROR;

    (void)run_episode(2000U);

    assert(strcmp(fake.calls, "PRIMO") == 0);
    assert(volume_status(VOLUME_ID_SD).Sequence == before);
    assert(fake.wakes == 0U);
    assert(has_log("SD", "Filesystem mount failed."));
}

/**
 * @brief 刷新失败但卡不在，卷挂着：同样卸载；刷新失败而卡在、设备进了 ERROR，旧卷也卸载；
 *        刷新失败时卷没挂：什么都不做，不会因为结构里写着“就绪”就去挂载。
 */
static void test_refresh_failure_unmounts_a_mounted_volume_and_never_mounts(void)
{
    uint32_t before;

    begin_scenario(true);
    before = volume_status(VOLUME_ID_SD).Sequence;
    fake.refresh_status = SERVICE_ERROR;
    fake.refresh_card = card(false, false, false);
    (void)run_episode(2000U);
    assert(strcmp(fake.calls, "PRUO") == 0);
    assert(volume_status(VOLUME_ID_SD).Sequence == before + 1U);
    assert(has_log("VOLUME", "path=unmount refresh=fail"));
    assert(has_log("SD", "Hotplug refresh failed"));

    begin_scenario(true);
    before = volume_status(VOLUME_ID_SD).Sequence;
    fake.refresh_status = SERVICE_ERROR;
    fake.refresh_card = card(true, false, false);
    (void)run_episode(2000U);
    assert(strcmp(fake.calls, "PRUO") == 0);
    assert(volume_status(VOLUME_ID_SD).Sequence == before + 1U);

    begin_scenario(false);
    before = volume_status(VOLUME_ID_SD).Sequence;
    fake.refresh_status = SERVICE_ERROR;
    fake.refresh_card = card(true, true, true);
    (void)run_episode(2000U);
    assert(strcmp(fake.calls, "PRO") == 0);
    assert(volume_status(VOLUME_ID_SD).Sequence == before);
}

/**
 * @brief 卸载返回错误（真实实现的拔出路径也会这样）：Service 已使令牌失效，卷消失照发，闸照开。
 */
static void test_unmount_error_still_publishes_gone(void)
{
    uint32_t before;

    begin_scenario(true);
    before = volume_status(VOLUME_ID_SD).Sequence;
    fake.refresh_card = card(false, false, false);
    fake.unmount_status = SERVICE_NOT_READY;

    (void)run_episode(2000U);

    assert(volume_status(VOLUME_ID_SD).Sequence == before + 1U);
    assert(!volume_status(VOLUME_ID_SD).Mounted);
    assert(!fake.gate_closed);
    assert(has_log("SD", "Filesystem unmount failed."));
}

/* ------------------------------------------------------------------------- */
/* 消抖与闸                                                                   */
/* ------------------------------------------------------------------------- */

/**
 * @brief 没有边沿就没有闸：空闲时调用 step 不关闸、不刷新，返回无限等待。
 */
static void test_idle_step_does_not_touch_the_gate(void)
{
    begin_scenario(true);

    freertos_stub_set_tick(5000U);
    assert(volume_sd_step(5000U, false) == portMAX_DELAY);
    assert(fake.pause_calls == 0U);
    assert(fake.refresh_calls == 0U);
}

/**
 * @brief 触点抖动：每个新边沿都重新计 30 ms 静默，闸只关一次，边沿日志只记一行，对账日志记边沿数。
 */
static void test_chatter_restarts_the_quiet_time_and_closes_the_gate_once(void)
{
    begin_scenario(true);
    fake.refresh_card = card(true, true, false);

    freertos_stub_set_tick(100U);
    assert(volume_sd_step(100U, true) == pdMS_TO_TICKS(VOLUME_SD_DEBOUNCE_MS));
    freertos_stub_set_tick(110U);
    assert(volume_sd_step(110U, true) == pdMS_TO_TICKS(VOLUME_SD_DEBOUNCE_MS));
    freertos_stub_set_tick(125U);
    assert(volume_sd_step(125U, true) == pdMS_TO_TICKS(VOLUME_SD_DEBOUNCE_MS));
    freertos_stub_set_tick(140U);
    assert(volume_sd_step(140U, false) == 15U);
    assert(fake.refresh_calls == 0U);                 /* 最后一个边沿后 15 ms，还没静默够。 */
    freertos_stub_set_tick(154U);
    assert(volume_sd_step(154U, false) == 1U);
    assert(fake.refresh_calls == 0U);
    freertos_stub_set_tick(155U);
    assert(volume_sd_step(155U, false) == portMAX_DELAY);

    assert(fake.pause_calls == 1U);
    assert(fake.resume_calls == 1U);
    assert(fake.refresh_calls == 1U);
    assert(!fake.gate_closed);
    assert(count_logs("VOLUME", "sd edge") == 1U);
    assert(has_log("VOLUME", "edges=3"));
    assert(has_log("VOLUME", "end=55ms")); /* 第一个边沿在 100，开闸在 155。 */
}

/**
 * @brief 收尾时刻：刷新花的时间（含重新初始化约 186 ms）和从第一个边沿到开闸的时间都写进日志，毫秒。
 */
static void test_settle_log_reports_refresh_and_end_times_in_milliseconds(void)
{
    begin_scenario(true);
    fake.refresh_card = card(true, true, true);
    fake.refresh_advance_ticks = 186U;

    (void)run_episode(3000U);

    assert(has_log("VOLUME", "ref=186ms"));
    assert(has_log("VOLUME", "end=216ms")); /* 静默 30 + 刷新 186。 */
}

/**
 * @brief 刷新期间又来了新边沿：闸不开，重新消抖；第二次对账看到的是已经稳定的现状，之后才开闸。
 *        第一次对账该做的事照做（这里是卸载），第二次不重复。
 */
static void test_edge_during_refresh_keeps_the_gate_closed_and_debounces_again(void)
{
    uint32_t before;
    TickType_t wait;

    begin_scenario(true);
    before = volume_status(VOLUME_ID_SD).Sequence;
    fake.refresh_card = card(false, false, false);
    fake.edge_during_refresh = true;

    freertos_stub_set_tick(2000U);
    wait = volume_sd_step(2000U, true);
    assert(wait == 30U);
    freertos_stub_set_tick(2030U);
    wait = volume_sd_step(2030U, false);

    /* 第一次对账做完了，但闸保持关闭，重新计 30 ms。 */
    assert(wait == 30U);
    assert(fake.gate_closed);
    assert(fake.resume_calls == 0U);
    assert(volume_status(VOLUME_ID_SD).Sequence == before + 1U);
    assert(has_log("VOLUME", "gate=kept"));
    assert(has_log("VOLUME", "sd edge during settle"));

    freertos_stub_set_tick(2060U);
    wait = volume_sd_step(2060U, false);
    assert(wait == portMAX_DELAY);
    assert(!fake.gate_closed);
    assert(fake.pause_calls == 1U);
    assert(fake.resume_calls == 1U);
    assert(fake.refresh_calls == 2U);
    assert(fake.unmount_calls == 1U);
    assert(volume_status(VOLUME_ID_SD).Sequence == before + 1U);
}

/**
 * @brief 卸载期间、挂载期间来了新边沿：同样闸保持关闭，直到再次静默并对账之后才开。
 */
static void test_edge_during_unmount_or_mount_keeps_the_gate_closed(void)
{
    TickType_t wait;

    begin_scenario(true);
    fake.refresh_card = card(true, true, true);
    fake.edge_during_mount = true;

    freertos_stub_set_tick(2000U);
    (void)volume_sd_step(2000U, true);
    freertos_stub_set_tick(2030U);
    wait = volume_sd_step(2030U, false);
    assert(wait == 30U);
    assert(fake.gate_closed);
    assert(fake.resume_calls == 0U);
    assert(volume_status(VOLUME_ID_SD).Mounted);      /* 换卡已经做完，卷就绪已发布，但闸还不能开。 */

    freertos_stub_set_tick(2060U);
    wait = volume_sd_step(2060U, false);
    assert(wait == portMAX_DELAY);
    assert(!fake.gate_closed);
    assert(fake.resume_calls == 1U);

    begin_scenario(true);
    fake.refresh_card = card(false, false, false);
    fake.edge_during_unmount = true;
    freertos_stub_set_tick(2000U);
    (void)volume_sd_step(2000U, true);
    freertos_stub_set_tick(2030U);
    assert(volume_sd_step(2030U, false) == 30U);
    assert(fake.gate_closed);
    freertos_stub_set_tick(2060U);
    assert(volume_sd_step(2060U, false) == portMAX_DELAY);
    assert(!fake.gate_closed);
    assert(fake.pause_calls == fake.resume_calls);
}

/**
 * @brief 没有路径漏开闸（穷举）：卷挂没挂、卡的四种现状、刷新成败、挂载三种结果、卸载成败、对账期间在刷新、卸载、挂载处
 *        来新边沿或不来，每一种组合的收尾都是：闸开着，PauseSD 与 ResumeSD 各一次，
 *        卷状态字与假 Service 眼里的挂载状态一致，卷序号的变化与 #195 的表一致，订阅者被叫醒时闸始终关着。
 *        空闲时（step 返回无限等待）闸一定是开的由 run_episode 里的断言保证。
 */
static void test_every_path_pairs_pause_with_resume(void)
{
    static const Service_StatusTypeDef mount_results[] = {SERVICE_OK, SERVICE_NO_FILESYSTEM, SERVICE_ERROR};
    uint32_t episodes = 0U;

    for (int mounted = 0; mounted <= 1; mounted++)
    {
        for (int card_kind = 0; card_kind < 4; card_kind++)
        {
            for (int refresh_ok = 0; refresh_ok <= 1; refresh_ok++)
            {
                for (size_t mount_index = 0U; mount_index < 3U; mount_index++)
                {
                    for (int unmount_ok = 0; unmount_ok <= 1; unmount_ok++)
                    {
                        for (int edge_kind = 0; edge_kind < 4; edge_kind++)
                        {
                            const bool present = (card_kind != 0);
                            const bool ready = (card_kind >= 2);
                            const bool reinit = (card_kind == 2);
                            const bool mount_ok = (mount_results[mount_index] == SERVICE_OK);
                            uint32_t before;
                            uint32_t expected_delta;
                            bool edge_fired;

                            begin_scenario(mounted != 0);
                            before = volume_status(VOLUME_ID_SD).Sequence;
                            fake.refresh_status = refresh_ok ? SERVICE_OK : SERVICE_ERROR;
                            fake.refresh_card = card(present, ready, reinit);
                            fake.mount_status = mount_results[mount_index];
                            fake.unmount_status = unmount_ok ? SERVICE_OK : SERVICE_NOT_READY;
                            fake.edge_during_refresh = (edge_kind == 1);
                            fake.edge_during_unmount = (edge_kind == 2);
                            fake.edge_during_mount = (edge_kind == 3);

                            /* 期望：#195 的表，刷新失败时卡状态不可信。 */
                            if (mounted != 0)
                            {
                                if (!refresh_ok || !present || !ready)
                                {
                                    expected_delta = 1U;
                                }
                                else if (reinit)
                                {
                                    expected_delta = mount_ok ? 2U : 1U;
                                }
                                else
                                {
                                    expected_delta = 0U;
                                }
                            }
                            else
                            {
                                expected_delta = (refresh_ok && present && ready && mount_ok) ? 1U : 0U;
                            }

                            (void)run_episode(10000U);

                            assert(!fake.gate_closed);
                            assert(fake.pause_calls == 1U);
                            assert(fake.resume_calls == 1U);
                            assert(volume_status(VOLUME_ID_SD).Sequence == before + expected_delta);
                            assert(volume_status(VOLUME_ID_SD).Mounted == fake.service_mounted);
                            assert(fake.wakes_with_gate_open == 0U);
                            assert(fake.wakes == expected_delta);
                            assert(fake.gate_closed_at_refresh);
                            /* 对账期间的新边沿只有在那一步真的执行时才会落下；落下了就再消抖、再刷新一次。 */
                            edge_fired = (edge_kind == 1) ||
                                         ((edge_kind == 2) && (fake.unmount_calls >= 1U)) ||
                                         ((edge_kind == 3) && (fake.mount_calls >= 1U));
                            assert(fake.refresh_calls == (edge_fired ? 2U : 1U));
                            episodes++;
                        }
                    }
                }
            }
        }
    }
    assert(episodes == 2U * 4U * 2U * 3U * 2U * 4U);
}

/**
 * @brief 日志行不超出 Service Log 单条正文的容量（127 字符）：数字很大（几万个边沿、时钟回绕附近、刷新花了十几万毫秒）
 *        时各字段被截在 9999，仍放得下。
 */
static void test_log_lines_fit_the_service_log_message_size(void)
{
    begin_scenario(true);
    fake.refresh_card = card(true, true, true);
    fake.refresh_advance_ticks = 123456U;
    fake.mount_status = SERVICE_NO_FILESYSTEM;

    freertos_stub_set_tick(0xFFFFFF00U);
    for (uint32_t edge = 0U; edge < 12345U; edge++)
    {
        (void)volume_sd_step(0xFFFFFF00U, true);
    }
    freertos_stub_set_tick(0xFFFFFF00U + 30U);
    (void)volume_sd_step(0xFFFFFF00U + 30U, false);

    assert(lock_env_log_longest_text() <= 127U);
    assert(has_log("VOLUME", "edges=9999"));
    assert(has_log("VOLUME", "ref=9999ms"));
    assert(has_log("VOLUME", "end=9999ms"));
}

/* ------------------------------------------------------------------------- */
/* 卡检测回调与假抖动钩子                                                     */
/* ------------------------------------------------------------------------- */

/**
 * @brief 卡检测回调在 ISR 里只发通知：对卷任务的通知槽置检测位，把唤醒标志交给 portYIELD_FROM_ISR；空上下文什么都不做。
 */
static void test_detect_callback_only_notifies_the_task_from_isr(void)
{
    TaskHandle_t task;

    begin_scenario(true);
    task = freertos_stub_task_create();
    freertos_stub_set_give_from_isr_woken(pdFALSE);

    volume_sd_detect_callback(task);
    assert(freertos_stub_notify_calls(task, VOLUME_NOTIFY_EVENT) == 1U);
    assert(freertos_stub_notification_value(task, VOLUME_NOTIFY_EVENT) == VOLUME_NOTIFY_FLAG_SD_DETECT);
    assert(freertos_stub_isr_yield_requests() == 0U);
    assert(fake.pause_calls == 0U);
    assert(fake.refresh_calls == 0U);

    volume_sd_detect_callback(NULL);
    assert(freertos_stub_notify_calls(task, VOLUME_NOTIFY_EVENT) == 1U);
}

#endif

#if !VOLUME_HOOK_FAKE_BOUNCE
/**
 * @brief 假抖动钩子默认关闭：没有 HOOK 行，时间过多久（这里 10 小时）卷任务都不自己造边沿，也不动闸。
 */
static void test_fake_bounce_hook_is_off_by_default(void)
{
    begin_scenario(false);
    fake.start_card = card(true, true, true);
    assert(volume_sd_start(subscriber));

    assert(!has_log("HOOK", "fake_bounce"));
    assert(lock_env_log_count() >= 1U);
    freertos_stub_set_tick(1000U + 3600U * 10U * 1000U);
    assert(volume_sd_step(1000U + 3600U * 10U * 1000U, false) == portMAX_DELAY);
    assert(fake.pause_calls == 0U);
    assert(fake.refresh_calls == 0U);
}
#else
/**
 * @brief 第一条标签与子串都匹配的日志的序号。
 * @param[in] tag 标签。
 * @param[in] needle 子串。
 * @return 序号；没有为 UINT32_MAX。
 */
static uint32_t find_log(const char *tag, const char *needle)
{
    for (uint32_t index = 0U; index < lock_env_log_count(); index++)
    {
        if ((strcmp(lock_env_log_tag(index), tag) == 0) && (strstr(lock_env_log_text(index), needle) != NULL))
        {
            return index;
        }
    }
    return UINT32_MAX;
}

/**
 * @brief 假抖动钩子打开：启动日志一行 WARN `fake_bounce ON period=5000ms`；空闲时等待被钩子的下一次到期限制；
 *        到期后卷任务自己调一次边沿入口，卡其实没动，走“只开闸”，不发事件，日志写明边沿来自钩子；
 *        之后下一次人造边沿排在一个周期之后。
 */
static void test_fake_bounce_hook_injects_edges_when_on(void)
{
    uint32_t after_start;
    uint32_t hook_line;

    begin_scenario(false);
    fake.start_card = card(true, true, true);
    assert(volume_sd_start(subscriber));
    after_start = volume_status(VOLUME_ID_SD).Sequence;
    assert(volume_status(VOLUME_ID_SD).Mounted);

    assert(count_logs("HOOK", "fake_bounce ON period=5000ms") == 1U);
    hook_line = find_log("HOOK", "fake_bounce ON");
    assert(lock_env_log_level(hook_line) == (uint32_t)SERVICE_LOG_LEVEL_WARN);
    lock_env_log_reset();
    fake.wakes = 0U; /* 启动时发的卷就绪不算。 */

    freertos_stub_set_tick(5999U);
    assert(volume_sd_step(5999U, false) == 1U);       /* 启动在 1000：下一次人造边沿在 6000。 */
    assert(fake.pause_calls == 0U);

    fake.refresh_card = card(true, true, false);
    freertos_stub_set_tick(6000U);
    assert(volume_sd_step(6000U, false) == pdMS_TO_TICKS(VOLUME_SD_DEBOUNCE_MS));
    assert(fake.pause_calls == 1U);
    assert(has_log("VOLUME", "sd edge src=hook"));

    freertos_stub_set_tick(6030U);
    assert(volume_sd_step(6030U, false) == 4970U);    /* 对账完成并开闸；下一次人造边沿在 11000。 */
    assert(fake.resume_calls == 1U);
    assert(strcmp(fake.calls + fake.calls_count - 3U, "PRO") == 0);
    assert(volume_status(VOLUME_ID_SD).Sequence == after_start);
    assert(volume_status(VOLUME_ID_SD).Mounted);
    assert(fake.wakes == 0U);
    assert(has_log("VOLUME", "sd settle path=gate_only"));
    assert(!fake.gate_closed);
}
#endif

/**
 * @brief 依次运行用例。
 * @return 全部通过时为 0，断言失败时进程中止。
 */
int main(void)
{
#if VOLUME_HOOK_FAKE_BOUNCE
    /* 钩子配置：钩子的节拍锚在 volume_sd_start()，别的用例使用的假时间会被它当成到期，所以只跑钩子相关的用例。 */
    test_boot_with_card_mounts_then_publishes_ready();
    test_fake_bounce_hook_injects_edges_when_on();
#else
    test_boot_with_card_mounts_then_publishes_ready();
    test_boot_without_card_publishes_nothing();
    test_boot_mount_failure_publishes_nothing();
    test_boot_failures_publish_nothing();
    test_sd_benchmark_runs_after_mount_and_before_ready();
    test_removal_unmounts_publishes_gone_then_opens_the_gate();
    test_swap_unmounts_then_mounts_and_jumps_the_sequence_by_two();
    test_insertion_mounts_and_publishes_ready();
    test_unmounted_and_absent_does_nothing_but_open_the_gate();
    test_false_bounce_only_opens_the_gate();
    test_mount_failure_never_publishes_ready_but_still_opens_the_gate();
    test_refresh_failure_unmounts_a_mounted_volume_and_never_mounts();
    test_unmount_error_still_publishes_gone();
    test_idle_step_does_not_touch_the_gate();
    test_chatter_restarts_the_quiet_time_and_closes_the_gate_once();
    test_settle_log_reports_refresh_and_end_times_in_milliseconds();
    test_edge_during_refresh_keeps_the_gate_closed_and_debounces_again();
    test_edge_during_unmount_or_mount_keeps_the_gate_closed();
    test_every_path_pairs_pause_with_resume();
    test_log_lines_fit_the_service_log_message_size();
    test_detect_callback_only_notifies_the_task_from_isr();
    test_fake_bounce_hook_is_off_by_default();
#endif
    puts("volume_sd: all passed");
    return 0;
}
