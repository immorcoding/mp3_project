/**
  ******************************************************************************
  * @file    playback_contract.h
  * @brief   播放任务公开合同：GUI 音乐分区与模拟器脚本化后端共用的命令与快照 Interface。
  * @note    来源：#71 K1–K5、#53。只含类型与两个函数声明，不暴露 FreeRTOS 句柄。
  *          GUI 侧只有音乐应用的 GUI 分区（APP/apps/music/gui）包含本头，Service/gui 不得包含。
  *          任务入口与通知槽在 playback_task.h，不在本头里。
  ******************************************************************************
  */

#ifndef PLAYBACK_CONTRACT_H
#define PLAYBACK_CONTRACT_H

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    PLAYBACK_OK = 0U,         /* 命令已入队。 */
    PLAYBACK_QUEUE_FULL       /* 命令 FIFO 已满，本条被丢弃；调用方记日志，不阻塞。 */
} Playback_StatusTypeDef;

typedef enum
{
    PLAYBACK_COMMAND_TOGGLE = 0U,
    PLAYBACK_COMMAND_NEXT,
    PLAYBACK_COMMAND_PREV,
    PLAYBACK_COMMAND_SELECT,
    PLAYBACK_COMMAND_SEEK    /* 跳转：Generation/Index 为 GUI 按下时显示的曲目，TargetMs 为目标毫秒。 */
} Playback_CommandKindTypeDef;

typedef struct
{
    Playback_CommandKindTypeDef Kind;
    uint32_t Seq;          /* GUI 分配的递增命令序号，只给发送成功的命令编号。 */
    uint32_t Generation;   /* SELECT 与 SEEK：曲目身份的曲库代次（SELECT 为 GUI 所见列表窗口，SEEK 为 GUI 按下时显示的曲目）。 */
    uint16_t Index;        /* SELECT 与 SEEK：曲目身份的播放列表位置。 */
    uint32_t TargetMs;     /* 仅 SEEK：目标毫秒（松手比例 × 按下时显示的总时长）。 */
} Playback_CommandTypeDef;

typedef enum
{
    PLAYBACK_STATE_STOPPED = 0U,
    PLAYBACK_STATE_PLAYING,
    PLAYBACK_STATE_PAUSED
} Playback_StateTypeDef;

typedef enum
{
    PLAYBACK_STOP_REASON_NONE = 0U,        /* 开机后未播过。 */
    PLAYBACK_STOP_REASON_CARD_REMOVED,
    PLAYBACK_STOP_REASON_NOT_READY,        /* 开启应答 NOT_READY：SD 未就绪或曲库没好。插卡或开机后曲库扫描期间音频读取任务立刻应答 NOT_READY（#215，原来请求排队，T_data 内扫完才出声），要等扫完再点一次播放。 */
    PLAYBACK_STOP_REASON_NO_TRACK,         /* 空库或下标越界。 */
    PLAYBACK_STOP_REASON_CATALOG_CHANGED,  /* 开启应答 STALE。 */
    PLAYBACK_STOP_REASON_READ_ERROR,       /* 开启 IO_ERROR 或流中 READ_ERROR。 */
    PLAYBACK_STOP_REASON_NO_DATA,          /* T_data 到期。 */
    PLAYBACK_STOP_REASON_AUDIO_ERROR,      /* TRANSFER_ERROR 或 T_out 到期，可重试。 */
    PLAYBACK_STOP_REASON_AUDIO_FAULT,      /* STOP_FAILED 锁存，重启前不能出声。 */
    PLAYBACK_STOP_REASON_ALL_BAD,          /* 整轮无可播。 */
    PLAYBACK_STOP_REASON_COUNT             /* 哨兵：原因个数，不是真实原因；新增原因须放在它之前。 */
} Playback_StopReasonTypeDef;

typedef struct
{
    Playback_StateTypeDef State;
    Playback_StopReasonTypeDef StopReason;  /* 仅 STOPPED 有意义；其他状态为 NONE。 */
    bool HasTrack;                          /* false = 无当前曲；此时 Generation、Index 为 0 且无意义。 */
    uint32_t Generation;                    /* 当前曲的曲库代次；0 = 开启 OK 前尚未确认。 */
    uint16_t Index;                         /* 当前曲的播放列表位置。 */
    uint32_t PositionMs;                    /* 已解码时间。 */
    uint32_t DurationMs;                    /* 0 = 未知。 */
    uint32_t SkippedBadCount;               /* 开机以来已跳过坏文件数，单调。 */
    uint32_t StopCount;                     /* 开机以来进入停止的次数，单调。 */
    uint32_t HandledCommandSeq;             /* 已处理（含忽略、合并）的最后一条命令序号。 */
    uint32_t TrackStartCount;               /* 开机以来从曲首开始播放一首曲目的次数，单调；SELECT（含同曲重选）、NEXT/PREV、停止态开播、播完前进、跳过坏文件后开下一首各加 1，跳转（含跳到 0）不加。 */
} Playback_SnapshotTypeDef;

Playback_StatusTypeDef playback_command_send(const Playback_CommandTypeDef *command);
void playback_snapshot_read(Playback_SnapshotTypeDef *snapshot);

#endif /* PLAYBACK_CONTRACT_H */
