/**
 * @file audio_reader_stream.h
 * @brief 音频流：播放任务经请求信箱开启/关闭，音频读取任务经流记录信箱与块池交出音频块。
 * @note  FIL 归音频读取任务（ARC-12）；播放任务只持流序号。信箱是“最新值”：后写整份覆盖先写。
 *        FLAG 只用于唤醒，醒来后以信箱与队列内容为准。播放侧合同（请求信箱、流记录信箱、块池、唤醒回调）的字段、
 *        枚举和语义与 0.9.0 的 `storage_audio_*` 完全相同，只改了名字（#194 第 1 条）。
 */

#ifndef AUDIO_READER_STREAM_H
#define AUDIO_READER_STREAM_H

#include <stdbool.h>
#include <stdint.h>

#include "APP/tasks/audio_reader/audio_reader_block.h"
#include "APP/tasks/audio_reader/audio_reader_pace.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/task.h"

/** @brief 请求信箱内容（播放任务 → 音频读取任务）。StreamSeq 为 0 表示“无流”（关闭/取消）。 */
typedef struct
{
    uint32_t StreamSeq;   /**< 播放任务分配的单调流序号；0 = 无流。 */
    uint32_t Generation;  /**< 播放列表代次；0 表示“当前”。 */
    uint16_t Position;    /**< 播放列表下标。 */
    uint32_t AudioOffset; /**< 相对音频区间起点的字节数：切歌与普通开流为 0，跳转重开流为播放模块映射给出的偏移。音频读取任务只把它钳到区间内，不认识音频格式；应答的 AudioBytes 仍是整段区间长度。 */
} AudioReader_RequestTypeDef;

/** @brief 开启结果。 */
typedef enum
{
    AUDIO_READER_OPEN_PENDING = 0U, /**< 尚无应答。 */
    AUDIO_READER_OPEN_OK,           /**< 已打开，参数有效。 */
    AUDIO_READER_OPEN_NOT_READY,    /**< SD 未就绪或曲库不可用（曲库代次 0：扫描中、卷变了、没发布）。 */
    AUDIO_READER_OPEN_STALE,        /**< 请求的代次已过期，或位置超出列表。 */
    AUDIO_READER_OPEN_NO_TRACK,     /**< 曲库没有曲目。 */
    AUDIO_READER_OPEN_BAD_FILE,     /**< 打不开、标签长度越界或音频区间为空。 */
    AUDIO_READER_OPEN_IO_ERROR      /**< 读取标签时的 I/O 错误。 */
} AudioReader_OpenResultTypeDef;

/** @brief 终止状态。 */
typedef enum
{
    AUDIO_READER_END_NONE = 0U,     /**< 未终止。 */
    AUDIO_READER_END_ENDED,         /**< 最后一块已入队，读尽。 */
    AUDIO_READER_END_READ_ERROR,    /**< 读取失败。 */
    AUDIO_READER_END_CARD_REMOVED   /**< 拔卡，FIL 已先关闭。 */
} AudioReader_EndTypeDef;

/** @brief 流记录信箱内容（音频读取任务 → 播放任务）；开启参数写入后一直保留。 */
typedef struct
{
    uint32_t StreamSeq;                       /**< 这份记录属于哪个流。 */
    AudioReader_OpenResultTypeDef OpenResult; /**< 开启结果。 */
    uint32_t Generation;                      /**< OK 时的实际代次。 */
    uint16_t TrackCount;                      /**< OK 时的曲目数。 */
    uint32_t AudioBytes;                      /**< OK 时的音频字节数（区间长度）。 */
    AudioReader_EndTypeDef End;               /**< 终止状态。 */
} AudioReader_RecordTypeDef;

/** @brief 唤醒原因，供播放任务把它映射到自己的事件槽 FLAG。 */
typedef enum
{
    AUDIO_READER_WAKE_STREAM_RECORD = 0U, /**< 流记录信箱变更。 */
    AUDIO_READER_WAKE_BLOCK_READY         /**< 就绪队列有新块。 */
} AudioReader_WakeTypeDef;

/** @brief 唤醒播放任务的回调；在音频读取任务上下文调用，不得阻塞。 */
typedef void (*AudioReader_Wake_Callback_t)(AudioReader_WakeTypeDef reason);

/* Boot 任务调用一次：创建请求信箱和流记录信箱（静态）。块池另由 audio_reader_block_init() 初始化。须早于创建播放任务和音频读取任务。 */
void audio_reader_stream_boot_init(void);

/* 音频读取任务内部：入口记下自己的句柄（供播放任务通知），之后每圈 卷事件 → 请求信箱 → 读一块 → 决定等多久。 */
void audio_reader_stream_bind(TaskHandle_t reader_task);
void audio_reader_stream_apply_volume(void);
void audio_reader_stream_process_request(void);
void audio_reader_stream_read_block(void);
void audio_reader_stream_pace_input(AudioReader_PaceInputTypeDef *input);

/* 播放任务调用。 */
void audio_reader_stream_set_wake_callback(AudioReader_Wake_Callback_t callback);
bool audio_reader_stream_request(const AudioReader_RequestTypeDef *request);
bool audio_reader_stream_get_record(AudioReader_RecordTypeDef *record);
bool audio_reader_stream_return_block(AudioReader_BlockTypeDef *block);

#endif /* AUDIO_READER_STREAM_H */
