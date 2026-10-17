/**
 * @file storage_audio_stream.h
 * @brief Storage 音频流：播放任务经请求信箱开启/关闭，Storage 经流记录信箱与块池交出音频块。
 * @note  FIL 归 Storage Task（ARC-12）；播放任务只持流序号。信箱是“最新值”：后写整份覆盖先写。
 *        FLAG 只用于唤醒，醒来后以信箱与队列内容为准。
 */

#ifndef STORAGE_AUDIO_STREAM_H
#define STORAGE_AUDIO_STREAM_H

#include <stdbool.h>
#include <stdint.h>

#include "APP/tasks/storage/storage_task.h"
#include "APP/tasks/storage/stream/storage_audio_block.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/task.h"

/** @brief 请求信箱内容（播放任务 → Storage）。StreamSeq 为 0 表示“无流”（关闭/取消）。 */
typedef struct
{
    uint32_t StreamSeq;   /**< 播放任务分配的单调流序号；0 = 无流。 */
    uint32_t Generation;  /**< 播放列表代次；0 表示“当前”。 */
    uint16_t Position;    /**< 播放列表下标。 */
} Storage_AudioRequestTypeDef;

/** @brief 开启结果。 */
typedef enum
{
    STORAGE_AUDIO_OPEN_PENDING = 0U, /**< 尚无应答。 */
    STORAGE_AUDIO_OPEN_OK,           /**< 已打开，参数有效。 */
    STORAGE_AUDIO_OPEN_NOT_READY,    /**< SD 未就绪或曲库不可用。 */
    STORAGE_AUDIO_OPEN_STALE,        /**< 请求的代次已过期，或位置超出列表。 */
    STORAGE_AUDIO_OPEN_NO_TRACK,     /**< 曲库没有曲目。 */
    STORAGE_AUDIO_OPEN_BAD_FILE,     /**< 打不开、标签长度越界或音频区间为空。 */
    STORAGE_AUDIO_OPEN_IO_ERROR      /**< 读取标签时的 I/O 错误。 */
} Storage_AudioOpenResultTypeDef;

/** @brief 终止状态。 */
typedef enum
{
    STORAGE_AUDIO_END_NONE = 0U,     /**< 未终止。 */
    STORAGE_AUDIO_END_ENDED,         /**< 最后一块已入队，读尽。 */
    STORAGE_AUDIO_END_READ_ERROR,    /**< 读取失败。 */
    STORAGE_AUDIO_END_CARD_REMOVED   /**< 拔卡，FIL 已先关闭。 */
} Storage_AudioEndTypeDef;

/** @brief 流记录信箱内容（Storage → 播放任务）；开启参数写入后一直保留。 */
typedef struct
{
    uint32_t StreamSeq;                       /**< 这份记录属于哪个流。 */
    Storage_AudioOpenResultTypeDef OpenResult;/**< 开启结果。 */
    uint32_t Generation;                      /**< OK 时的实际代次。 */
    uint16_t TrackCount;                      /**< OK 时的曲目数。 */
    uint32_t AudioBytes;                      /**< OK 时的音频字节数（区间长度）。 */
    Storage_AudioEndTypeDef End;              /**< 终止状态。 */
} Storage_AudioRecordTypeDef;

/** @brief Storage 本圈看到的介质状态，由 storage_task 传入。 */
typedef enum
{
    STORAGE_AUDIO_MEDIA_READY = 0U,   /**< SD 已挂载且不在消抖中。 */
    STORAGE_AUDIO_MEDIA_DEBOUNCING,   /**< 消抖中：暂缓流请求与补块，不应答。 */
    STORAGE_AUDIO_MEDIA_ABSENT        /**< 未挂载。 */
} Storage_AudioMediaTypeDef;

/** @brief 唤醒原因，供播放任务把它映射到自己的事件槽 FLAG。 */
typedef enum
{
    STORAGE_AUDIO_WAKE_STREAM_RECORD = 0U, /**< 流记录信箱变更。 */
    STORAGE_AUDIO_WAKE_BLOCK_READY         /**< 就绪队列有新块。 */
} Storage_AudioWakeTypeDef;

/** @brief 唤醒播放任务的回调；在 Storage Task 上下文调用，不得阻塞。 */
typedef void (*Storage_AudioWake_Callback_t)(Storage_AudioWakeTypeDef reason);

/* Storage Task 内部。 */
void storage_audio_stream_init(TaskHandle_t storage_task);
void storage_audio_stream_process_request(Storage_AudioMediaTypeDef media);
void storage_audio_stream_read_block(Storage_AudioMediaTypeDef media);
bool storage_audio_stream_has_work(Storage_AudioMediaTypeDef media);
bool storage_audio_stream_is_open(void);
void storage_audio_stream_card_removed(void);

/* 播放任务调用。 */
void storage_audio_stream_set_wake_callback(Storage_AudioWake_Callback_t callback);
bool storage_audio_stream_request(const Storage_AudioRequestTypeDef *request);
bool storage_audio_stream_get_record(Storage_AudioRecordTypeDef *record);
bool storage_audio_stream_return_block(Storage_AudioBlockTypeDef *block);

#endif /* STORAGE_AUDIO_STREAM_H */
