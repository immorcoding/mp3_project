/**
  ******************************************************************************
  * @file    playback_machine.h
  * @brief   播放任务的纯 C 状态机：命令、流交接、切歌、暂停、播完，以及坏文件、超时、故障与停止原因（#71 B1–B17、K3–K5）。
  * @note    不依赖 FreeRTOS、HAL 或音频读取任务的头：音频读取任务（请求信箱、流记录、块队列）与播放模块经
  *          Ops 注入，时刻由调用方传入（B9）。任务外壳负责等待通知、取空命令 FIFO、读单调毫秒时刻、
  *          把 Ops 接到 audio_reader_stream_* 与 Service_Playback_*，并保证单线程调用本模块。
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

/** @brief 请求信箱内容（播放任务 → 音频读取任务）；StreamSeq 为 0 表示“无流”。与 AudioReader_RequestTypeDef 一一对应。 */
typedef struct
{
    uint32_t StreamSeq;    /**< 播放任务分配的单调流序号；0 = 无流。 */
    uint32_t Generation;   /**< 曲库代次；0 表示“当前”。 */
    uint16_t Position;     /**< 播放列表位置。 */
    uint32_t AudioOffset;  /**< 相对音频区间起点的字节数；切歌与普通开流为 0，跳转为映射给出的偏移。 */
} Playback_MachineRequestTypeDef;

/** @brief 开启结果。与 AudioReader_OpenResultTypeDef 一一对应。 */
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

/** @brief 终止状态。与 AudioReader_EndTypeDef 一一对应。 */
typedef enum
{
    PLAYBACK_MACHINE_END_NONE = 0U,
    PLAYBACK_MACHINE_END_ENDED,
    PLAYBACK_MACHINE_END_READ_ERROR,
    PLAYBACK_MACHINE_END_CARD_REMOVED
} Playback_MachineEndTypeDef;

/** @brief 流记录信箱内容（音频读取任务 → 播放任务）。与 AudioReader_RecordTypeDef 一一对应。 */
typedef struct
{
    uint32_t StreamSeq;                          /**< 这份记录属于哪个流。 */
    Playback_MachineOpenResultTypeDef OpenResult;/**< 开启结果。 */
    uint32_t Generation;                         /**< OK 时的实际代次。 */
    uint16_t TrackCount;                         /**< OK 时的曲目数。 */
    uint32_t AudioBytes;                         /**< OK 时的音频字节数。 */
    Playback_MachineEndTypeDef End;              /**< 终止状态。 */
} Playback_MachineRecordTypeDef;

/** @brief 就绪队列里一块的视图；Handle 由音频读取任务解释，状态机只原样交还。 */
typedef struct
{
    void *Handle;              /**< 音频读取任务的块句柄。 */
    uint32_t StreamSeq;        /**< 块头：所属流序号。 */
    uint32_t ValidBytes;       /**< 块头：有效字节数。 */
    const uint8_t *Data;       /**< 块数据，仅在归还之前有效。 */
} Playback_MachineBlockTypeDef;

/** @brief 音频流 Ops：请求信箱、流记录信箱、就绪队列；对应 audio_reader_stream_*（外壳适配）。 */
typedef struct
{
    bool (*WriteRequest)(const Playback_MachineRequestTypeDef *request);  /**< 覆盖写请求信箱；不阻塞。 */
    bool (*ReadRecord)(Playback_MachineRecordTypeDef *record);            /**< 窥视流记录信箱，不消费；无记录返回 false。 */
    bool (*PeekReadyBlock)(Playback_MachineBlockTypeDef *block);          /**< 窥视就绪队列队首，不取走。 */
    bool (*TakeReadyBlock)(Playback_MachineBlockTypeDef *block);          /**< 取走就绪队列队首。 */
    void (*ReturnBlock)(const Playback_MachineBlockTypeDef *block);       /**< 放回空闲队列并置“块已归还”。 */
} Playback_MachineStreamOpsTypeDef;

/** @brief 播放模块 Ops：对应 Service_Playback_* 的输入与输出管线操作（外壳直接填函数指针）。 */
typedef struct
{
    Service_StatusTypeDef (*BeginStream)(uint32_t audio_bytes);
    Service_StatusTypeDef (*MapSeek)(uint32_t target_ms, Service_Playback_SeekPointTypeDef *point);
    Service_StatusTypeDef (*ReopenStream)(uint32_t landing_ms);
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
    Playback_MachineStreamOpsTypeDef Stream;                          /**< 音频读取任务。 */
    Playback_MachinePlayerOpsTypeDef Player;                          /**< 播放模块。 */
    void (*PublishSnapshot)(const Playback_SnapshotTypeDef *snapshot);/**< 覆盖写状态信箱；只在快照变化时调用。 */
    void (*LogInfo)(const char *text);                                /**< INFO 日志（可为空）；只在命令与曲目边界调用，不进每帧路径；文本由实现复制。 */
    void (*LogDebug)(const char *text);                               /**< DEBUG 日志（可为空）；只记被静默忽略的命令；文本由实现复制。 */
    uint32_t (*ReadCycles)(void);                                     /**< 周期计数器读数（DWT，可为空；为空时跳转时延记 0）；只用差值，回绕安全。 */
    uint32_t (*CyclesToUs)(uint32_t cycles);                          /**< 把周期数换算成微秒（可为空；为空时跳转时延记 0）。 */
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
    uint32_t ConsecutiveSkips;                 /**< 连跳计数（B5）：输出真正 Start（OutputStarted）、用户换曲命令、进入停止态时清零。 */
    bool PumpNeedInput;                        /**< 本次唤醒最后一次推进回报了“补半区时输入不够”（B7 的断流判据之一）。 */
    Playback_MachineOpenResultTypeDef SeenOpen;/**< 当前流已见过的开启结果，用来发现“收到应答”（B7 重新计时）。 */
    Playback_MachineEndTypeDef SeenEnd;        /**< 当前流已见过的终止状态。 */
    bool DataEvent;                            /**< 本次唤醒收到本流的应答或块：T_data 重新计时。 */
    bool DataWaitActive;                       /**< T_data 正在计时。 */
    uint32_t DataWaitSinceMs;                  /**< T_data 的起算时刻。 */
    bool OutputRunning;                        /**< Platform 输出正在运行（T_out 只在此时计时，B8）。 */
    uint32_t OutputSinceMs;                    /**< T_out 的起算时刻：Start 返回或最近一次半区释放。 */
    bool SeekActive;                           /**< 一次已执行的跳转尚未记 INFO（等输出重新起播或预填完成）；切歌、停止或新跳转使其作废。 */
    bool SeekAwaitReopen;                      /**< 跳转的新流尚未开启 OK 重开：此前不接受任何 DecodedMs 回报（旧流的位置会把落点顶回去）。 */
    Service_Playback_SeekPointTypeDef SeekPoint;/**< 本次跳转的映射结果（偏移、落点、方式、是否钳位）。 */
    uint32_t SeekTargetMs;                     /**< 本次跳转的目标毫秒。 */
    uint32_t SeekStartCycles;                  /**< 处理 SEEK 命令时的周期计数器读数，供 lat。 */
    bool SwitchActive;                         /**< 一次换曲（用户的 NEXT、PREV、SELECT，或自动前进）已执行且输出尚未重新起播（DIAG-6）；停止使其作废，更新的用户换曲覆盖它。 */
    bool SwitchAuto;                           /**< 等待起播的换曲是自动前进（播完前进、跳过坏文件续上），Switch 行带 auto 后缀；用户换曲为假。 */
    uint32_t SwitchStartCycles;               /**< 处理该换曲命令时的周期计数器读数，供 Switch 的 lat。 */
    uint32_t UnderrunTotal;                    /**< 开机以来播放模块回报的欠载次数累计（Platform 的计数每次 Start 清零，这里累加）。 */
    bool SeekHoldEnd;                         /**< 当前流由跳转重开且尚未起播过：暂停中回报流尾放完（含钳位后零帧）时不前进，等恢复。 */
} Playback_MachineTypeDef;

uint32_t playback_machine_underrun_total(const Playback_MachineTypeDef *machine);

void playback_machine_init(Playback_MachineTypeDef *machine, const Playback_MachineOpsTypeDef *ops);
void playback_machine_step(Playback_MachineTypeDef *machine,
                           const Playback_CommandTypeDef *commands,
                           uint32_t command_count,
                           uint32_t now_ms);

#endif /* PLAYBACK_MACHINE_H */
