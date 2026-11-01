/**
 * @file storage_track_diag.c
 * @brief 标签读取诊断：把一次读取的结果归入六类回退原因，累加计数器，按 DIAG-4 记日志。
 */

#include "storage_track_diag.h"

#include <stdbool.h>
#include <stdio.h>

#include "Adapters/cortex/cycle_counter/cortex_m7_cycle_counter_adapter.h"
#include "Service/log/log_service.h"

/** @brief 一次标签读取的结果归类：取到加六类回退原因（#134 更正）。 */
typedef enum
{
    STORAGE_TRACK_DIAG_TAGGED = 0U, /**< 取到：TIT2 可用。 */
    STORAGE_TRACK_DIAG_NO_ID3V2,    /**< 无 ID3v2：正常情形，只计数。 */
    STORAGE_TRACK_DIAG_NO_FRAME,    /**< 帧缺失：正常情形，只计数。 */
    STORAGE_TRACK_DIAG_DECODE,      /**< 解码失败：名称槽 WARN。 */
    STORAGE_TRACK_DIAG_LIMIT,       /**< 超读取上限：名称槽 WARN。 */
    STORAGE_TRACK_DIAG_OPEN,        /**< 打开失败：名称槽 WARN。 */
    STORAGE_TRACK_DIAG_READ,        /**< 读取错误：名称槽 WARN。 */
    STORAGE_TRACK_DIAG_COUNT
} StorageTrackDiag_ReasonTypeDef;

/** @brief 自启动累加的计数器；只由 Storage Task 读写，供调试器与日志汇总使用。 */
typedef struct
{
    uint32_t Reads;                                     /**< 读回调调用次数合计（含首块，打开失败为 0），与名称槽日志 reads= 同口径。 */
    uint32_t ByReason[STORAGE_TRACK_DIAG_COUNT];        /**< 按结果归类的次数；[TAGGED] 为取到数，其余为六类回退数。 */
    uint32_t MaxUs;                                     /**< 单行耗时（含打开）最长值，单位 µs。 */
    uint64_t TotalUs;                                   /**< 单行耗时（含打开）累计值，单位 µs。 */
    uint32_t MaxOpenUs;                                 /**< 单次打开耗时最长值，单位 µs。 */
    uint32_t LocatorMisses;                             /**< 文件定位符核对不符、退回按路径打开的次数（板上预期为 0）。 */
} StorageTrackDiag_CountersTypeDef;

/** @brief 一个 Queue 窗口的累加，窗口开始时清零。 */
typedef struct
{
    uint16_t Rows;                                      /**< 已填行数。 */
    uint16_t ByReason[STORAGE_TRACK_DIAG_COUNT];        /**< 本窗按结果归类的行数。 */
    uint32_t Reads;                                     /**< 本窗读回调调用次数合计，与自启动 Reads 同口径。 */
    uint32_t TotalUs;                                   /**< 本窗总耗时（含打开），单位 µs。 */
    uint32_t MaxUs;                                     /**< 本窗最长单行耗时（含打开），单位 µs。 */
    uint32_t MaxOpenUs;                                 /**< 本窗最长单次打开耗时，单位 µs。 */
} StorageTrackDiag_WindowTypeDef;

static const char storage_track_diag_tag[] = "STORAGE";

static const char *const storage_track_diag_reason_name[STORAGE_TRACK_DIAG_COUNT] =
{
    "tagged", "no_id3v2", "frame_missing", "decode_failed", "read_limit", "open_failed", "read_error"
};

static StorageTrackDiag_CountersTypeDef storage_track_diag_counters;
static StorageTrackDiag_WindowTypeDef storage_track_diag_window;

/**
 * @brief 把读取结果归入取到或六类回退原因。
 * @param[in] track 读取结果，非 NULL。
 * @return 归类；打开失败优先于分类字段。
 */
static StorageTrackDiag_ReasonTypeDef storage_track_diag_classify(const StorageTrackText_ResultTypeDef *track)
{
    if (track->OpenFailed)
    {
        return STORAGE_TRACK_DIAG_OPEN;
    }

    switch (track->Text.Result)
    {
        case STORAGE_CATALOG_TEXT_TAGGED:
            return STORAGE_TRACK_DIAG_TAGGED;
        case STORAGE_CATALOG_TEXT_NO_ID3V2:
            return STORAGE_TRACK_DIAG_NO_ID3V2;
        case STORAGE_CATALOG_TEXT_FRAME_MISSING:
            return STORAGE_TRACK_DIAG_NO_FRAME;
        case STORAGE_CATALOG_TEXT_DECODE_FAILED:
            return STORAGE_TRACK_DIAG_DECODE;
        case STORAGE_CATALOG_TEXT_READ_LIMIT:
            return STORAGE_TRACK_DIAG_LIMIT;
        case STORAGE_CATALOG_TEXT_READ_ERROR:
        default:
            return STORAGE_TRACK_DIAG_READ;
    }
}

/**
 * @brief 该归类是否属 WARN 级：解码失败、超读取上限、打开失败、读取错误。
 * @param[in] reason 归类。
 * @return true 为 WARN 级；无 ID3v2、帧缺失与取到为 false。
 */
static bool storage_track_diag_is_warn(StorageTrackDiag_ReasonTypeDef reason)
{
    return (reason == STORAGE_TRACK_DIAG_DECODE) || (reason == STORAGE_TRACK_DIAG_LIMIT) ||
           (reason == STORAGE_TRACK_DIAG_OPEN) || (reason == STORAGE_TRACK_DIAG_READ);
}

/**
 * @brief 把起点之后经过的周期数换算成微秒；不复位计数器，无符号减法处理回绕。
 * @param[in] start_cycles storage_track_diag_begin() 的返回值。
 * @return 经过的微秒数；核心频率不可用时为 0。
 */
uint32_t storage_track_diag_elapsed_us(uint32_t start_cycles)
{
    const uint32_t cycles = CortexM7CycleCounter_Read() - start_cycles;
    const uint32_t hz = CortexM7CycleCounter_GetFrequencyHz();
    const uint64_t us = (hz == 0U) ? 0U : (((uint64_t)cycles * 1000000ULL) / hz);

    return (us > UINT32_MAX) ? UINT32_MAX : (uint32_t)us;
}

/**
 * @brief 累加自启动计数器。
 * @param[in] reason 归类。
 * @param[in] reads 本行读回调调用次数。
 * @param[in] us 单行耗时（含打开）。
 * @param[in] open_us 其中的打开耗时。
 */
static void storage_track_diag_count(StorageTrackDiag_ReasonTypeDef reason, uint32_t reads, uint32_t us, uint32_t open_us)
{
    storage_track_diag_counters.Reads += reads;
    storage_track_diag_counters.ByReason[reason]++;
    storage_track_diag_counters.TotalUs += us;
    if (us > storage_track_diag_counters.MaxUs)
    {
        storage_track_diag_counters.MaxUs = us;
    }
    if (open_us > storage_track_diag_counters.MaxOpenUs)
    {
        storage_track_diag_counters.MaxOpenUs = open_us;
    }
}

/**
 * @brief 记下一次标签读取的起点周期数。
 * @return 起点周期数，交给 *_done 计算耗时。只读计数器，不复位。
 */
uint32_t storage_track_diag_begin(void)
{
    return CortexM7CycleCounter_Read();
}

/**
 * @brief 名称槽一次 load 读完标签后：累加计数器，记一行 INFO，WARN 级原因另记一行 WARN。
 * @param[in] index 播放列表位置。
 * @param[in] start_cycles storage_track_diag_begin() 的返回值。
 * @param[in] track 读取结果；NULL（文件主名提取失败，防御分支）不计数，由调用方记原有 WARN。
 * @note  无 ID3v2 与帧缺失只出现在 INFO 的来源里，不记 WARN。INFO 的 `open=` 是打开耗时，
 *        `us=` 是标签读取耗时（整行耗时减去打开耗时），两者分开。
 */
void storage_track_diag_name_done(uint16_t index, uint32_t start_cycles, const StorageTrackText_ResultTypeDef *track)
{
    StorageTrackDiag_ReasonTypeDef reason;
    uint32_t us;
    uint32_t read_us;
    char text[STORAGE_CATALOG_DIAG_TAG_LINE_BYTES];

    if (track == NULL)
    {
        return;
    }

    reason = storage_track_diag_classify(track);
    us = storage_track_diag_elapsed_us(start_cycles);
    storage_track_diag_count(reason, track->Text.Reads, us, track->OpenUs);
    read_us = (us > track->OpenUs) ? (us - track->OpenUs) : 0U;

    if (reason == STORAGE_TRACK_DIAG_TAGGED)
    {
        (void)snprintf(text, sizeof(text), "Tag idx=%u src=title reads=%u open=%lu us=%lu",
                       (unsigned)index, (unsigned)track->Text.Reads,
                       (unsigned long)track->OpenUs, (unsigned long)read_us);
    }
    else
    {
        (void)snprintf(text, sizeof(text), "Tag idx=%u src=file why=%s reads=%u open=%lu us=%lu",
                       (unsigned)index, storage_track_diag_reason_name[reason],
                       (unsigned)track->Text.Reads, (unsigned long)track->OpenUs, (unsigned long)read_us);
    }
    (void)Service_Log_Post(SERVICE_LOG_LEVEL_INFO, storage_track_diag_tag, text);

    if (storage_track_diag_is_warn(reason))
    {
        (void)snprintf(text, sizeof(text), "Tag warn idx=%u why=%s",
                       (unsigned)index, storage_track_diag_reason_name[reason]);
        (void)Service_Log_Post(SERVICE_LOG_LEVEL_WARN, storage_track_diag_tag, text);
    }
}

/**
 * @brief 一个 Queue 窗口开始填写：清零窗口累加。
 */
void storage_track_diag_window_begin(void)
{
    storage_track_diag_window = (StorageTrackDiag_WindowTypeDef){0};
}

/**
 * @brief 窗口一行读完后：累加自启动计数器与窗口累加，不记日志。
 * @param[in] start_cycles storage_track_diag_begin() 的返回值。
 * @param[in] track 读取结果；NULL（文件主名提取失败）只计入行数。
 */
void storage_track_diag_row_done(uint32_t start_cycles, const StorageTrackText_ResultTypeDef *track)
{
    StorageTrackDiag_ReasonTypeDef reason;
    uint32_t us;

    storage_track_diag_window.Rows++;
    if (track == NULL)
    {
        return;
    }

    reason = storage_track_diag_classify(track);
    us = storage_track_diag_elapsed_us(start_cycles);
    storage_track_diag_count(reason, track->Text.Reads, us, track->OpenUs);

    storage_track_diag_window.ByReason[reason]++;
    storage_track_diag_window.Reads += track->Text.Reads;
    storage_track_diag_window.TotalUs += us;
    if (us > storage_track_diag_window.MaxUs)
    {
        storage_track_diag_window.MaxUs = us;
    }
    if (track->OpenUs > storage_track_diag_window.MaxOpenUs)
    {
        storage_track_diag_window.MaxOpenUs = track->OpenUs;
    }
}

/**
 * @brief 窗口加载完成（打 READY 前）：本窗有 WARN 级原因，或单行打开超过 30 ms，或整窗超过 0.5 s，
 *        或单行读取超过 T_data/2（防御阈值）时记一行汇总。
 * @note  有 WARN 级原因时为 WARN，否则 INFO；其余窗口只累加计数器，滚动不刷屏（DIAG-4）。
 *        `max=` 与 `us=` 含打开，`omax=` 是本窗最长单次打开。
 */
void storage_track_diag_window_end(void)
{
    const StorageTrackDiag_WindowTypeDef *window = &storage_track_diag_window;
    bool warn;
    bool over_budget;
    char text[STORAGE_CATALOG_DIAG_WINDOW_LINE_BYTES];

    warn = (window->ByReason[STORAGE_TRACK_DIAG_DECODE] != 0U) ||
           (window->ByReason[STORAGE_TRACK_DIAG_LIMIT] != 0U) ||
           (window->ByReason[STORAGE_TRACK_DIAG_OPEN] != 0U) ||
           (window->ByReason[STORAGE_TRACK_DIAG_READ] != 0U);
    over_budget = (window->MaxOpenUs > STORAGE_CATALOG_OPEN_BUDGET_US) ||
                  (window->TotalUs > STORAGE_CATALOG_WINDOW_BUDGET_US) ||
                  (window->MaxUs > STORAGE_CATALOG_TRACK_BUDGET_US);
    if (!warn && !over_budget)
    {
        return;
    }

    (void)snprintf(text, sizeof(text),
                   "Tag win rows=%u max=%lu omax=%lu top=%lu us=%lu rd=%lu ok=%u nid3=%u nofr=%u dec=%u lim=%u open=%u rerr=%u",
                   (unsigned)window->Rows,
                   (unsigned long)window->MaxUs,
                   (unsigned long)window->MaxOpenUs,
                   (unsigned long)storage_track_diag_counters.MaxUs,
                   (unsigned long)window->TotalUs,
                   (unsigned long)window->Reads,
                   (unsigned)window->ByReason[STORAGE_TRACK_DIAG_TAGGED],
                   (unsigned)window->ByReason[STORAGE_TRACK_DIAG_NO_ID3V2],
                   (unsigned)window->ByReason[STORAGE_TRACK_DIAG_NO_FRAME],
                   (unsigned)window->ByReason[STORAGE_TRACK_DIAG_DECODE],
                   (unsigned)window->ByReason[STORAGE_TRACK_DIAG_LIMIT],
                   (unsigned)window->ByReason[STORAGE_TRACK_DIAG_OPEN],
                   (unsigned)window->ByReason[STORAGE_TRACK_DIAG_READ]);
    (void)Service_Log_Post(warn ? SERVICE_LOG_LEVEL_WARN : SERVICE_LOG_LEVEL_INFO,
                           storage_track_diag_tag, text);
}

/**
 * @brief 文件定位符核对不符、退回按路径打开：累加计数器，记一行 WARN（DIAG-5）。
 * @param[in] position 播放列表位置。
 * @note  文本只含位置，不含文件名；板上预期为 0。
 */
void storage_track_diag_locator_miss(uint16_t position)
{
    char text[STORAGE_CATALOG_DIAG_LOCATOR_LINE_BYTES];

    storage_track_diag_counters.LocatorMisses++;
    (void)snprintf(text, sizeof(text), "Locator miss pos=%u", (unsigned)position);
    (void)Service_Log_Post(SERVICE_LOG_LEVEL_WARN, storage_track_diag_tag, text);
}

/**
 * @brief 一次开流或重开流成功后记一行 `Stream` INFO（DIAG-5）。
 * @param[in] position 播放列表位置。
 * @param[in] audio_offset 请求里的音频区间偏移（字节，未钳位）。
 * @param[in] open_us 打开耗时，单位 µs。
 * @param[in] lseek_us 定位到音频区间起点加偏移的 lseek 耗时，单位 µs。
 * @note  只在曲目或跳转边界调用，不进每块读取路径。
 */
void storage_track_diag_stream_done(uint16_t position, uint32_t audio_offset, uint32_t open_us, uint32_t lseek_us)
{
    char text[STORAGE_CATALOG_DIAG_STREAM_LINE_BYTES];

    (void)snprintf(text, sizeof(text), "Stream pos=%u off=%lu open=%lu lseek=%lu",
                   (unsigned)position, (unsigned long)audio_offset,
                   (unsigned long)open_us, (unsigned long)lseek_us);
    (void)Service_Log_Post(SERVICE_LOG_LEVEL_INFO, storage_track_diag_tag, text);
}
