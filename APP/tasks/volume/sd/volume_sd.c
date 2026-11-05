/**
 ******************************************************************************
 * @file    volume_sd.c
 * @brief   卷任务的 SD 部分：启动、消抖、关闸、对账、卸载与挂载、发布卷事件、卷路径日志（见 volume_sd.h）。
 *
 * @details
 *          只通过 Filesystem Service 的公开入口访问 SD（`StartSD`、`RefreshSD`、`PauseSD`/`ResumeSD`、`InitSD`、
 *          `MountSD`、`UnmountSD`），不包含 `platform_sd.h`：Device 状态由 SD 卷锁保护，APP 不绕过。
 *          判断（对账表、静默时间）在 `volume_reconcile.c`，卷事件在 `volume_event.c`；本文件把它们接到 Service 上。
 *
 *          卷路径日志（常开，标签 `VOLUME`，DIAG-6）。串口里每行的前缀是 `<时间> <I|W|E> (<tick>) <标签>: `，
 *          标签后面有冒号；下面只写正文：
 *          - `sd edge src=<irq|hook>`：一次消抖过程的第一个边沿（之后的边沿只计数，不再记行，抖动不会刷屏）。
 *          - `sd settle path=<路径> refresh=<ok|fail> card=<pri> mount=<ok|fail|-> seq=<前>-><后> edges=<n>
 *            ref=<ms>ms end=<ms>ms gate=<open|kept>`：对账结果。`path` 取 idle、gate_only、mount、unmount、remount；
 *            `card` 三位依次是 present、ready、reinit，成立写 p、r、i，不成立写 -；`seq` 是这次对账前后的卷序号；
 *            `ref` 是 `RefreshSD` 花的时间（含等在途调用退出和可能的重新初始化，约 186 ms）；`end` 是从第一个边沿
 *            到闸重新打开的时间，即**收尾时刻**；`gate=kept` 表示对账期间又来了新边沿，闸保持关闭、重新消抖。
 *            `edges`、`ref`、`end` 超过 9999 都写 9999。
 *          - `sd edge during settle, gate kept`：对账期间又来了一个边沿。
 *          - `sd boot path=<路径> card=<pri> mount=<ok|fail|-> seq=<前>-><后>`：开机时的启动结果。
 *          另外保留原有的 `SD` 标签的人读日志（Card ready、Card inserted、Card removed.、Filesystem mounted. 等）。
 ******************************************************************************
 */

#include "APP/tasks/volume/sd/volume_sd.h"
#include "APP/tasks/volume/volume_event.h"
#include "APP/tasks/volume/volume_reconcile.h"
#include "APP/tasks/volume/volume_task.h"
#include "APP/app_config.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "Service/filesystem/filesystem_card.h"
#include "Service/filesystem/filesystem_service.h"
#include "Service/log/log_service.h"

#if STORAGE_SD_BENCHMARK_ENABLE
#include "APP/tasks/volume/benchmark/storage_sd_benchmark.h"
#endif

/** @brief 人读日志沿用的标签。 */
static const char volume_sd_log_tag[] = "SD";

/** @brief 卷路径日志的标签（R3.1 至 R3.5 靠它判读）。 */
static const char volume_sd_path_tag[] = "VOLUME";

/** @brief 日志里数字字段的上限；超过写这个数，保证单行放得进 Service Log 的 127 个字符。 */
#define VOLUME_SD_LOG_NUMBER_MAX 9999U

/** @brief 一次对账的过程与结果，供写卷路径日志。 */
typedef struct
{
    Volume_PathTypeDef Path;
    bool RefreshOk;
    Service_Filesystem_CardStatusTypeDef Card;
    bool MountAttempted;
    bool MountOk;
    uint32_t SequenceBefore;
    uint32_t SequenceAfter;
    uint32_t RefreshMs;
} Volume_SdSettleTypeDef;

/* 以下只由卷任务访问。 */
static bool volume_sd_debouncing;       /* 一次消抖过程进行中；此时闸是关着的（反过来也成立）。 */
static TickType_t volume_sd_first_edge; /* 本次过程第一个边沿的 tick。 */
static TickType_t volume_sd_last_edge;  /* 最后一个边沿的 tick，静默时间从它起算。 */
static uint32_t volume_sd_edges;        /* 本次过程里收到的边沿唤醒数（同时到的边沿会合并，所以是下限）。 */
static TickType_t volume_sd_hook_last;  /* 假抖动钩子上一次造边沿（或启动）的 tick。 */

/**
 * @brief  把 tick 差换算成毫秒。
 * @param[in] ticks tick 数。
 * @return 毫秒数。
 */
static uint32_t volume_sd_ticks_to_ms(TickType_t ticks)
{
    return (uint32_t)(((uint64_t)ticks * 1000ULL) / (uint64_t)configTICK_RATE_HZ);
}

/**
 * @brief  日志数字字段截在上限。
 * @param[in] value 原值。
 * @return 不超过 `VOLUME_SD_LOG_NUMBER_MAX` 的值。
 */
static unsigned long volume_sd_clamp(uint32_t value)
{
    return (unsigned long)((value > VOLUME_SD_LOG_NUMBER_MAX) ? VOLUME_SD_LOG_NUMBER_MAX : value);
}

/**
 * @brief  把卡状态写成三位标志：p 卡在、r 就绪、i 重新初始化过，不成立写 -。
 * @param[in] card 卡状态。
 * @param[out] flags 至少 4 字节。
 */
static void volume_sd_card_flags(const Service_Filesystem_CardStatusTypeDef *card, char *flags)
{
    flags[0] = card->Present ? 'p' : '-';
    flags[1] = card->Ready ? 'r' : '-';
    flags[2] = card->Reinitialized ? 'i' : '-';
    flags[3] = '\0';
}

/**
 * @brief  用设备诊断写一条错误日志（来自刷新或启动返回的卡状态，不再直接问 Platform）。
 * @param[in] operation 失败操作的固定说明，例如 "Initialization"。
 * @param[in] card 失败时尽力填写的卡状态。
 */
static void volume_sd_post_diagnostics(const char *operation, const Service_Filesystem_CardStatusTypeDef *card)
{
    char text[128];

    (void)snprintf(text,
                   sizeof(text),
                   "%s failed: device=%lu, port=%lu.",
                   operation,
                   (unsigned long)card->DeviceError,
                   (unsigned long)card->PortStatus);
    (void)Service_Log_Post(SERVICE_LOG_LEVEL_ERROR, volume_sd_log_tag, text);
}

/**
 * @brief  记录当前已经 READY 的 SD 卡的逻辑块信息。
 * @param[in] prefix 描述本次 READY 原因的固定字符串。
 * @param[in] card 卡状态，`Ready` 为真时容量有效。
 */
static void volume_sd_log_card_ready(const char *prefix, const Service_Filesystem_CardStatusTypeDef *card)
{
    char text[128];

    (void)snprintf(text,
                   sizeof(text),
                   "%s: %lu MB, block size: %lu.",
                   prefix,
                   (unsigned long)(card->CapacityBytes / (1024ULL * 1024ULL)),
                   (unsigned long)card->BlockSize);
    (void)Service_Log_Post(SERVICE_LOG_LEVEL_INFO, volume_sd_log_tag, text);
}

/**
 * @brief  在 SD 卡检测 GPIO 的 EXTI 中断里通知卷任务。
 * @param[in] context 启动时登记的卷任务句柄。
 * @note   ISR 上下文：只发一个任务通知，不碰别的（ARC-10）。EXTI9_5 的 NVIC 抢占优先级当前为 10，
 *         满足 FreeRTOS FromISR 调用条件。
 */
void volume_sd_detect_callback(void *context)
{
    TaskHandle_t task = (TaskHandle_t)context;
    BaseType_t higher_priority_task_woken = pdFALSE;

    if (task != NULL)
    {
        (void)xTaskNotifyIndexedFromISR(task,
                                        VOLUME_NOTIFY_EVENT,
                                        VOLUME_NOTIFY_FLAG_SD_DETECT,
                                        eSetBits,
                                        &higher_priority_task_woken);
        portYIELD_FROM_ISR(higher_priority_task_woken);
    }
}

/**
 * @brief  初始化文件系统驱动并挂载 SD 卷（卡已经由 Platform 初始化就绪）。
 * @return 挂载成功为真；驱动没就绪、卡上没有 FAT 或挂载出错为假（都已记日志）。
 * @note   `StartSD` 之后仍要先 `InitSD()` 再 `MountSD()`：前者建立完成信号量并订阅传输事件。
 */
static bool volume_sd_mount(void)
{
    Service_StatusTypeDef result = Service_Filesystem_InitSD();

    if (result != SERVICE_OK)
    {
        (void)Service_Log_Post(SERVICE_LOG_LEVEL_ERROR, volume_sd_log_tag, "FatFs driver is not ready.");
        return false;
    }

    result = Service_Filesystem_MountSD();
    if (result == SERVICE_OK)
    {
        (void)Service_Log_Post(SERVICE_LOG_LEVEL_INFO, volume_sd_log_tag, "Filesystem mounted.");
        return true;
    }

    if (result == SERVICE_NO_FILESYSTEM)
    {
        (void)Service_Log_Post(SERVICE_LOG_LEVEL_WARN, volume_sd_log_tag, "No FAT filesystem found.");
    }
    else
    {
        (void)Service_Log_Post(SERVICE_LOG_LEVEL_ERROR, volume_sd_log_tag, "Filesystem mount failed.");
    }
    return false;
}

/**
 * @brief  卸载 SD 卷：该卷全部令牌失效、槽释放。
 * @note   不访问已拔走的卡。返回错误时 Service 也已使句柄失效（拔出路径），所以调用者照常发卷消失。
 */
static void volume_sd_unmount(void)
{
    if (Service_Filesystem_UnmountSD() != SERVICE_OK)
    {
        (void)Service_Log_Post(SERVICE_LOG_LEVEL_ERROR, volume_sd_log_tag, "Filesystem unmount failed.");
    }
}

/**
 * @brief  写一行开机路径日志。
 * @param[in] path 路径。
 * @param[in] card 开机时的卡状态。
 * @param[in] mount 挂载结果文字：ok、fail 或 -。
 * @param[in] before 启动前的卷序号。
 * @param[in] after 启动后的卷序号。
 */
static void volume_sd_post_boot_line(Volume_PathTypeDef path,
                                     const Service_Filesystem_CardStatusTypeDef *card,
                                     const char *mount,
                                     uint32_t before,
                                     uint32_t after)
{
    char flags[4];
    char text[96];

    volume_sd_card_flags(card, flags);
    (void)snprintf(text,
                   sizeof(text),
                   "sd boot path=%s card=%s mount=%s seq=%lu->%lu",
                   volume_path_name(path),
                   flags,
                   mount,
                   (unsigned long)before,
                   (unsigned long)after);
    (void)Service_Log_Post(SERVICE_LOG_LEVEL_INFO, volume_sd_path_tag, text);
}

/**
 * @brief  启动 SD：注册卡检测回调并初始化设备；卡在就挂载（SD 基准在发卷就绪之前跑），成功后发卷就绪。
 * @param[in] task 卷任务句柄，作为卡检测回调的上下文。
 * @return 无卡或已完成挂载为真；Platform 启动失败、驱动未就绪、卡在但没就绪、挂载失败为假（都已记日志，
 *         且都不发卷事件）。
 * @note   无卡不发事件，卷序号保持不变。挂载失败、卡上没有 FAT 时不发事件，等下一次边沿再试。
 *         假抖动钩子打开时在这里投一行 `HOOK` 日志并给钩子的节拍定起点。
 */
bool volume_sd_start(TaskHandle_t task)
{
    Service_Filesystem_CardStatusTypeDef card;
    const uint32_t before = volume_status(VOLUME_ID_SD).Sequence;
    Service_StatusTypeDef status;

    (void)memset(&card, 0, sizeof(card));

    if (VOLUME_HOOK_FAKE_BOUNCE != 0U)
    {
        char text[64];

        (void)snprintf(text, sizeof(text), "fake_bounce ON period=%lums", (unsigned long)VOLUME_HOOK_FAKE_BOUNCE_PERIOD_MS);
        (void)Service_Log_Post(SERVICE_LOG_LEVEL_WARN, "HOOK", text);
        volume_sd_hook_last = xTaskGetTickCount();
    }

    status = Service_Filesystem_StartSD(volume_sd_detect_callback, task, &card);
    if (status != SERVICE_OK)
    {
        volume_sd_post_diagnostics("Initialization", &card);
        return false;
    }

    if (!card.Present)
    {
        (void)Service_Log_Post(SERVICE_LOG_LEVEL_INFO, volume_sd_log_tag, "No card inserted.");
        volume_sd_post_boot_line(VOLUME_PATH_IDLE, &card, "-", before, before);
        return true;
    }

    if (!card.Ready)
    {
        (void)Service_Log_Post(SERVICE_LOG_LEVEL_ERROR,
                               volume_sd_log_tag,
                               "Initialization returned an unexpected SD state.");
        volume_sd_post_boot_line(VOLUME_PATH_IDLE, &card, "-", before, before);
        return false;
    }

    volume_sd_log_card_ready("Card ready", &card);
    if (!volume_sd_mount())
    {
        volume_sd_post_boot_line(VOLUME_PATH_MOUNT, &card, "fail", before, before);
        return false;
    }

#if STORAGE_SD_BENCHMARK_ENABLE /* SD 读写基准测试：在发卷就绪之前运行，别的任务还不会碰这个卷。 */
    (void)storage_sd_benchmark_run();
#endif

    volume_sd_post_boot_line(VOLUME_PATH_MOUNT, &card, "ok", before, volume_publish_ready(VOLUME_ID_SD));
    return true;
}

/**
 * @brief  假抖动钩子到期了吗；到期就把起点移到现在。
 * @param[in] now 当前 tick。
 * @return 钩子打开且到期为真；钩子关闭永远为假。
 */
static bool volume_sd_hook_due(TickType_t now)
{
    if ((VOLUME_HOOK_FAKE_BOUNCE != 0U) &&
        (volume_debounce_remaining(now, volume_sd_hook_last, pdMS_TO_TICKS(VOLUME_HOOK_FAKE_BOUNCE_PERIOD_MS)) == 0U))
    {
        volume_sd_hook_last = now;
        return true;
    }
    return false;
}

/**
 * @brief  把等待时间限制在假抖动钩子的下一次到期之内（钩子关闭时原样返回）。
 * @param[in] wait 原本想等的 tick 数。
 * @param[in] now 当前 tick。
 * @return 实际等待的 tick 数。
 */
static TickType_t volume_sd_limit_wait_by_hook(TickType_t wait, TickType_t now)
{
    if (VOLUME_HOOK_FAKE_BOUNCE != 0U)
    {
        const TickType_t hook_wait =
            volume_debounce_remaining(now, volume_sd_hook_last, pdMS_TO_TICKS(VOLUME_HOOK_FAKE_BOUNCE_PERIOD_MS));

        if (hook_wait < wait)
        {
            return hook_wait;
        }
    }
    return wait;
}

/**
 * @brief  记一个卡检测边沿：第一个边沿关闸并记一行日志，之后的边沿只重新计时。
 * @param[in] now 当前 tick。
 * @param[in] from_hook 这个边沿是假抖动钩子造的，不是真实中断。
 */
static void volume_sd_note_edge(TickType_t now, bool from_hook)
{
    if (!volume_sd_debouncing)
    {
        char text[32];

        volume_sd_debouncing = true;
        volume_sd_first_edge = now;
        volume_sd_edges = 0U;
        Service_Filesystem_PauseSD(); /* 先关闸再记日志：从这一刻起新的文件调用在 Filesystem 入口等着。 */
        (void)snprintf(text, sizeof(text), "sd edge src=%s", from_hook ? "hook" : "irq");
        (void)Service_Log_Post(SERVICE_LOG_LEVEL_INFO, volume_sd_path_tag, text);
    }

    volume_sd_last_edge = now;
    if (volume_sd_edges < UINT32_MAX)
    {
        volume_sd_edges++;
    }
}

/**
 * @brief  取走卷任务通知槽里已经到达的卡检测边沿（不等待）。
 * @return 对账期间或之前到达了新边沿为真。
 */
static bool volume_sd_poll_edge(void)
{
    uint32_t flags = 0U;

    if (xTaskNotifyWaitIndexed((UBaseType_t)VOLUME_NOTIFY_EVENT,
                               0U,
                               VOLUME_NOTIFY_FLAG_EVENT_MASK,
                               &flags,
                               0U) != pdTRUE)
    {
        return false;
    }
    return (flags & VOLUME_NOTIFY_FLAG_SD_DETECT) != 0U;
}

/**
 * @brief  静默够了：刷新、对账，按路径卸载与挂载并发布卷事件。**不碰闸**，开不开闸由调用者按有没有新边沿决定。
 * @param[out] result 过程与结果，供写日志。
 * @note   顺序是 #195 第 7 条：刷新（`RefreshSD` 在 SD 卷锁里，拿锁就是等在途调用退出）→ 卸载（令牌失效、槽释放）
 *         → 发卷消失 → 挂载 → 发卷就绪。每一步都在闸关着时进行；卷任务不等订阅者，叫醒只是置通知位。
 */
static void volume_sd_settle(Volume_SdSettleTypeDef *result)
{
    Volume_ReconcileInputTypeDef input;
    Service_StatusTypeDef refresh;
    const TickType_t refresh_start = xTaskGetTickCount();
    const Volume_StatusTypeDef before = volume_status(VOLUME_ID_SD);

    (void)memset(result, 0, sizeof(*result));
    result->SequenceBefore = before.Sequence;

    refresh = Service_Filesystem_RefreshSD(&result->Card);
    result->RefreshMs = volume_sd_ticks_to_ms(xTaskGetTickCount() - refresh_start);
    result->RefreshOk = (refresh == SERVICE_OK);
    if (!result->RefreshOk)
    {
        volume_sd_post_diagnostics("Hotplug refresh", &result->Card);
    }

    input.Mounted = before.Mounted;
    input.RefreshOk = result->RefreshOk;
    input.Present = result->Card.Present;
    input.Ready = result->Card.Ready;
    input.Reinitialized = result->Card.Reinitialized;
    result->Path = volume_reconcile(&input);

    if (volume_path_unmounts(result->Path))
    {
        volume_sd_unmount();
        (void)volume_publish_gone(VOLUME_ID_SD);
        if (result->Path == VOLUME_PATH_UNMOUNT)
        {
            (void)Service_Log_Post(SERVICE_LOG_LEVEL_INFO, volume_sd_log_tag, "Card removed.");
        }
    }

    if (volume_path_mounts(result->Path))
    {
        result->MountAttempted = true;
        volume_sd_log_card_ready("Card inserted", &result->Card);
        result->MountOk = volume_sd_mount();
        if (result->MountOk)
        {
            (void)volume_publish_ready(VOLUME_ID_SD);
        }
    }

    result->SequenceAfter = volume_status(VOLUME_ID_SD).Sequence;
}

/**
 * @brief  写一行对账结果日志（格式见文件头）。
 * @param[in] result 对账的过程与结果。
 * @param[in] end_ms 从第一个边沿到现在的毫秒数。
 * @param[in] gate_open 闸已经重新打开为真，对账期间又来了新边沿而保持关闭为假。
 */
static void volume_sd_post_settle_line(const Volume_SdSettleTypeDef *result, uint32_t end_ms, bool gate_open)
{
    char flags[4];
    char text[128];
    const char *mount = !result->MountAttempted ? "-" : (result->MountOk ? "ok" : "fail");

    volume_sd_card_flags(&result->Card, flags);
    (void)snprintf(text,
                   sizeof(text),
                   "sd settle path=%s refresh=%s card=%s mount=%s seq=%lu->%lu edges=%lu ref=%lums end=%lums gate=%s",
                   volume_path_name(result->Path),
                   result->RefreshOk ? "ok" : "fail",
                   flags,
                   mount,
                   (unsigned long)result->SequenceBefore,
                   (unsigned long)result->SequenceAfter,
                   volume_sd_clamp(volume_sd_edges),
                   volume_sd_clamp(result->RefreshMs),
                   volume_sd_clamp(end_ms),
                   gate_open ? "open" : "kept");
    (void)Service_Log_Post(SERVICE_LOG_LEVEL_INFO, volume_sd_path_tag, text);
}

/**
 * @brief  卷任务循环的一步：处理这一圈收到的边沿，静默够了就对账并开闸，返回下一次最多可以等多久。
 * @param[in] now 当前 tick。
 * @param[in] edge 这一圈的通知里带了卡检测边沿。
 * @return 下一次等待的 tick 数；`portMAX_DELAY` 表示没有消抖在进行（闸一定是开的），等下一个边沿。
 *         假抖动钩子打开时，等待被钩子的下一次到期限制。
 * @note   **闸的开合不变量**：消抖进行中闸关着，函数返回有限等待；`PauseSD` 只在一次过程的第一个边沿调用，
 *         每次过程在这里以一次 `ResumeSD` 结束。对账期间又来了新边沿（通知槽里有检测位）时闸保持关闭，
 *         从对账结束的时刻起重新计 30 ms，直到一次对账期间没有新边沿才开闸。
 */
TickType_t volume_sd_step(TickType_t now, bool edge)
{
    const TickType_t quiet = pdMS_TO_TICKS(VOLUME_SD_DEBOUNCE_MS);
    Volume_SdSettleTypeDef result;
    TickType_t remaining;
    TickType_t after;
    bool hook_edge = false;

    if (volume_sd_hook_due(now))
    {
        hook_edge = !edge;
        edge = true;
    }
    if (edge)
    {
        volume_sd_note_edge(now, hook_edge);
    }

    if (!volume_sd_debouncing)
    {
        return volume_sd_limit_wait_by_hook(portMAX_DELAY, now);
    }

    remaining = volume_debounce_remaining(now, volume_sd_last_edge, quiet);
    if (remaining != 0U)
    {
        return volume_sd_limit_wait_by_hook(remaining, now);
    }

    volume_sd_settle(&result);
    after = xTaskGetTickCount();

    if (volume_sd_poll_edge())
    {
        /* 对账期间又抖了一下：闸不开，从现在起重新消抖。 */
        volume_sd_last_edge = after;
        if (volume_sd_edges < UINT32_MAX)
        {
            volume_sd_edges++;
        }
        (void)Service_Log_Post(SERVICE_LOG_LEVEL_INFO, volume_sd_path_tag, "sd edge during settle, gate kept");
        volume_sd_post_settle_line(&result, volume_sd_ticks_to_ms(after - volume_sd_first_edge), false);
        return volume_sd_limit_wait_by_hook(quiet, after);
    }

    Service_Filesystem_ResumeSD();
    volume_sd_debouncing = false;
    after = xTaskGetTickCount();
    volume_sd_post_settle_line(&result, volume_sd_ticks_to_ms(after - volume_sd_first_edge), true);
    return volume_sd_limit_wait_by_hook(portMAX_DELAY, after);
}
