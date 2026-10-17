/**
  ******************************************************************************
  * @file    playback_machine.h
  * @brief   播放任务的纯 C 状态机：命令、流交接、切歌、暂停、播完，以及坏文件、超时、故障与停止原因（#71 B1–B17、K3–K5）。
  * @note    不依赖 FreeRTOS、HAL 或 Storage 的头：存储窗口（请求信箱、流记录、块队列）与播放模块经
  *          Ops 注入，时刻由调用方传入（B9）。任务外壳负责等待通知、取空命令 FIFO、读单调毫秒时刻、
  *          把 Ops 接到 storage_audio_stream_* 与 Service_Playback_*，并保证单线程调用本模块。
  *          游标即快照中的 {HasTrack, Generation, Index}，只由本状态机写（ADR-0020 第 4 条）。
  ******************************************************************************
  */

#ifndef PLAYBACK_MACHINE_H
#define PLAYBACK_MACHINE_H

#include <stdbool.h>
#include <stdint.h>

#include "APP/tasks/playback/playback_contract.h"
#include "APP/tasks/playback/playback_task_config.h"
#include "Service/playback/playback_service.h"

/** @brief 请求信箱内容（播放任务 → Storage）；StreamSeq 为 0 表示“无流”。与 Storage_AudioRequestTypeDef 一一对应。 */
typedef struct
{
    uint32_t StreamSeq;    /**< 播放任务分配的单调流序号；0 = 无流。 */
    uint32_t Generation;   /**< 曲库代次；0 表示“当前”。 */
    uint16_t Position;     /**< 播放列表位置。 */
} Playback_MachineRequestTypeDef;

/** @brief 开启结果。与 Storage_AudioOpenResultTypeDef 一一对应。 */
typedef enum
{
    PLAYBACK_MACHINE_OPEN_PENDING = 0U,
    PLAYBACK_MACHINE_OPEN_OK,
    PLAYBACK_MACHINE_OPEN_NOT_READY,
    PLAYBACK_MACHINE_OPEN_STALE,
    PLAYBACK_MACHINE_OPEN_NO_TRACK,
    PLAYBACK_MACHINE_OPEN_BAD_FILE,
    PLAYBACK_MACHINE_OPEN_IO_ERROR
} Playback_MachineOpenResultTypeDef;

/** @brief 终止状态。与 Storage_AudioEndTypeDef 一一对应。 */
typedef enum
{
    PLAYBACK_MACHINE_END_NONE = 0U,
    PLAYBACK_MACHINE_END_ENDED,
    PLAYBACK_MACHINE_END_READ_ERROR,
    PLAYBACK_MACHINE_END_CARD_REMOVED
} Playback_MachineEndTypeDef;

/** @brief 流记录信箱内容（Storage → 播放任务）。与 Storage_AudioRecordTypeDef 一一对应。 */
typedef struct
{
    uint32_t StreamSeq;                          /**< 这份记录属于哪个流。 */
    Playback_MachineOpenResultTypeDef OpenResult;/**< 开启结果。 */
    uint32_t Generation;                         /**< OK 时的实际代次。 */
    uint16_t TrackCount;                         /**< OK 时的曲目数。 */
    uint32_t AudioBytes;                         /**< OK 时的音频字节数。 */
    Playback_MachineEndTypeDef End;              /**< 终止状态。 */
} Playback_MachineRecordTypeDef;

/** @brief 就绪队列里一块的视图；Handle 由存储窗口解释，状态机只原样交还。 */
typedef struct
{
    void *Handle;              /**< 存储窗口的块句柄。 */
    uint32_t StreamSeq;        /**< 块头：所属流序号。 */
    uint32_t ValidBytes;       /**< 块头：有效字节数。 */
    const uint8_t *Data;       /**< 块数据，仅在归还之前有效。 */
} Playback_MachineBlockTypeDef;

/** @brief 存储窗口 Ops：请求信箱、流记录信箱、就绪队列；对应 storage_audio_stream_*（外壳适配）。 */
typedef struct
{
    bool (*WriteRequest)(const Playback_MachineRequestTypeDef *request);  /**< 覆盖写请求信箱；不阻塞。 */
    bool (*ReadRecord)(Playback_MachineRecordTypeDef *record);            /**< 窥视流记录信箱，不消费；无记录返回 false。 */
    bool (*PeekReadyBlock)(Playback_MachineBlockTypeDef *block);          /**< 窥视就绪队列队首，不取走。 */
    bool (*TakeReadyBlock)(Playback_MachineBlockTypeDef *block);          /**< 取走就绪队列队首。 */
    void (*ReturnBlock)(const Playback_MachineBlockTypeDef *block);       /**< 放回空闲队列并置“块已归还”。 */
} Playback_MachineStorageOpsTypeDef;

/** @brief 播放模块 Ops：对应 Service_Playback_* 的输入与输出管线操作（外壳直接填函数指针）。 */
typedef struct
{
    Service_StatusTypeDef (*BeginStream)(uint32_t audio_bytes);
    Service_StatusTypeDef (*Feed)(const uint8_t *data, uint32_t length, uint32_t *accepted);
    Service_StatusTypeDef (*FinishInput)(void);
    Service_StatusTypeDef (*Pump)(Service_Playback_PumpReportTypeDef *report);
    Service_StatusTypeDef (*PauseOutput)(void);
    Service_StatusTypeDef (*ResumeOutput)(void);
    Service_StatusTypeDef (*AbandonStream)(void);
} Playback_MachinePlayerOpsTypeDef;

/** @brief 状态机的全部外部依赖。 */
typedef struct
{
    Playback_MachineStorageOpsTypeDef Storage;                        /**< 存储窗口。 */
    Playback_MachinePlayerOpsTypeDef Player;                          /**< 播放模块。 */
    void (*PublishSnapshot)(const Playback_SnapshotTypeDef *snapshot);/**< 覆盖写状态信箱；只在快照变化时调用。 */
} Playback_MachineOpsTypeDef;

/**
 * @brief 状态机实例；字段仅供 playback_machine.c 使用，外壳静态持有即可。
 */
typedef struct
{
    const Playback_MachineOpsTypeDef *Ops;     /**< 外部依赖。 */
    Playback_SnapshotTypeDef Snapshot;         /**< 工作快照；游标即其中的曲目身份。 */
    Playback_SnapshotTypeDef Published;        /**< 最近一次发布的快照，用于判断“变化”。 */
    uint32_t NowMs;                            /**< 最近一次调用传入的单调毫秒时刻（B9）。 */
    uint32_t LastStreamSeq;                    /**< 已分配过的最大流序号。 */
    uint32_t StreamSeq;                        /**< 当前流序号；0 = 无流。 */
    uint16_t TrackCount;                       /**< 最近一次开启 OK 带回的曲目数 N；0 = 未知。 */
    bool StreamBegun;                          /**< 开启 OK 已处理，播放模块已 BeginStream。 */
    bool InputFinished;                        /**< 已向播放模块 FinishInput。 */
    bool RecordValid;                          /**< Record 是当前流的记录。 */
    Playback_MachineRecordTypeDef Record;      /**< 本次唤醒读到的当前流记录。 */
    bool Holding;                              /**< 手中持有一块。 */
    Playback_MachineBlockTypeDef Held;         /**< 手中的块。 */
    uint32_t HeldOffset;                       /**< 手中块已拷进播放模块的字节数。 */
    bool AudioFaultLatched;                    /**< STOP_FAILED 已锁存（B10）：重启前 TOGGLE/SELECT 只让停止次数加一。 */
    bool SkipForward;                          /**< 自动跳过坏文件的方向：true 向后（索引增大），false 向前（PREV 之后，B5）。 */
    uint32_t ConsecutiveSkips;                 /**< 连跳计数（B5）：首帧解出、用户换曲命令、进入停止态时清零。 */
    bool PumpNeedInput;                        /**< 本次唤醒最后一次推进回报了“补半区时输入不够”（B7 的断流判据之一）。 */
    Playback_MachineOpenResultTypeDef SeenOpen;/**< 当前流已见过的开启结果，用来发现“收到应答”（B7 重新计时）。 */
    Playback_MachineEndTypeDef SeenEnd;        /**< 当前流已见过的终止状态。 */
    bool DataEvent;                            /**< 本次唤醒收到本流的应答或块：T_data 重新计时。 */
    bool DataWaitActive;                       /**< T_data 正在计时。 */
    uint32_t DataWaitSinceMs;                  /**< T_data 的起算时刻。 */
    bool OutputRunning;                        /**< Platform 输出正在运行（T_out 只在此时计时，B8）。 */
    uint32_t OutputSinceMs;                    /**< T_out 的起算时刻：Start 返回或最近一次半区释放。 */
} Playback_MachineTypeDef;

void playback_machine_init(Playback_MachineTypeDef *machine, const Playback_MachineOpsTypeDef *ops);
void playback_machine_step(Playback_MachineTypeDef *machine,
                           const Playback_CommandTypeDef *commands,
                           uint32_t command_count,
                           uint32_t now_ms);

#endif /* PLAYBACK_MACHINE_H */
