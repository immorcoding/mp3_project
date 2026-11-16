/**
  ******************************************************************************
  * @file    playback_sim.c
  * @brief   模拟器假播放后端：按 #71 公开合同实现命令 FIFO 与播放快照。
  *
  * @details
  *          替换 APP/tasks/playback，只实现 playback_contract.h 的两个函数，外加
  *          playback_sim.h 给 sim_main.c 的控制入口。按命令响应的简化状态机，遵守
  *          K3 命令表、K4 字段语义与 K5 转换点：
  *          - 命令 FIFO 深度同产品（PLAYBACK_COMMAND_QUEUE_DEPTH，K2）；满时返回
  *            PLAYBACK_QUEUE_FULL。每圈 playback_sim_step() 把 FIFO 取空，逐条处理并
  *            回显序号（含被忽略的命令）；挂起时命令只入队（S 键）。
  *          - 无卡（假音乐前台流程未挂载）时开启当圈按 NOT_READY 停止，GUI 看不到开启中的播放态
  *            （#219）：产品里卡没了曲库代次是 0，音频读取任务不读文件、立刻应答 NOT_READY
  *            （#194 第 15 条），它与播放任务的优先级都高于 GUI。不模拟“扫描中”（代次 0 而卡在）、
  *            开流宽限与读块被拔卡打断（#200 第 14、15 条）。
  *          - 有卡时“开启”一首后约 PLAYBACK_SIM_OPEN_MS 才有应答：期间三态照报（播放/暂停）、
  *            位置 0、总时长未知、代次可能为 0（未确认）；应答时按假音乐前台流程的曲库判定
  *            STALE（代次不符 -> CATALOG_CHANGED）、NO_TRACK（越界），否则确认代次、取得曲目数
  *            并给出总时长。产品里 STALE、NO_TRACK 也在读文件之前应答，假后端仍等这段时长。
  *          - 位置只在开启 OK 后的播放态按虚拟时钟推进；到总时长自动前进（末曲回首曲）。
  *          - 拔卡（C 键）：有流（播放或暂停）时停止 CARD_REMOVED、无当前曲。
  *          - 坏文件（B 键）：每按一次，让之后一次开启应答为坏文件。坏文件按 B5 跳过：
  *            SkippedBadCount +1，沿最近一次移动的方向开启下一首、三态不变；连跳数达到
  *            曲目数 N 即停止 ALL_BAD（游标保留），N 未知时停止 NO_TRACK。连跳数在开启 OK、
  *            用户换曲命令与进入停止时清零（假后端没有输出，以开启 OK 代替 OutputStarted）。
  *          - 指定停止（R 键）：按 K1 枚举顺序取下一个非 NONE 原因立即进入停止（停止态
  *            也再进入一次，停止次数 +1），副作用同 K4；AUDIO_FAULT 锁存后停止态的
  *            TOGGLE、SELECT 只让停止次数 +1（K3、B10），重启模拟器才解除。
  *          - 跳转（SEEK，#172 K）：先核对曲目身份（停止态、无当前曲、身份不符或总时长未知
  *            一律忽略），然后 PositionMs = min(TargetMs, DurationMs)，TrackStartCount 不变；
  *            播放中落在末尾则下一圈按播完前进，暂停中停在末尾。FIFO 里连续的 SEEK 只执行
  *            最后一条（最新者胜），被跳过的同样回显。`--seek-echo-delay` 让队首的 SEEK
  *            多等若干毫秒再处理（期间后面的命令也等着），用于拍“松手后停在目标”。
  *          - TrackStartCount：每次从曲首开启一首（SELECT、播放/暂停中 NEXT/PREV、停止态开播、
  *            播完前进、跳过坏文件后开下一首）加 1。
  *          不解码、不读块，没有 T_data/T_out 计时，其余停止原因只能用 R 键注入。
  ******************************************************************************
  */

#include "playback_sim.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "APP/tasks/playback/playback_contract.h"
#include "APP/tasks/playback/playback_task_config.h"
#include "fakes/music_sim.h"

#define PLAYBACK_SIM_OPEN_MS               100U     /* 开启请求到应答（首帧给出总时长）的虚拟时长。 */
#define PLAYBACK_SIM_DURATION_BASE_MS      150000U  /* 第 0 首的总时长（2:30）。 */
#define PLAYBACK_SIM_DURATION_STEP_MS      10000U   /* 每往后一首总时长增加的量，使各曲总时长可区分。 */

/** @brief 假后端的全部状态；只在 GUI 同一线程里访问。 */
typedef struct
{
    Playback_SnapshotTypeDef snapshot;                               /**< 对外发布的快照。 */
    Playback_CommandTypeDef fifo[PLAYBACK_COMMAND_QUEUE_DEPTH];      /**< 命令 FIFO。 */
    uint16_t fifo_head;                                              /**< 队首下标。 */
    uint16_t fifo_count;                                             /**< 队内命令数。 */
    bool suspended;                                                  /**< 命令处理已挂起（S 键）。 */
    bool opening;                                                    /**< 正在等开启应答。 */
    uint32_t open_elapsed_ms;                                        /**< 本次开启已等待的虚拟时长。 */
    uint16_t track_count;                                            /**< 最近一次开启 OK 带回的曲目数；0 = 未知。 */
    bool has_clock;                                                  /**< 已记下上次推进的时刻。 */
    uint32_t last_ms;                                                /**< 上次推进的时刻。 */
    uint16_t bad_pending;                                            /**< 还要按坏文件应答的开启次数（B 键累加）。 */
    uint16_t consecutive_skips;                                      /**< 本轮连续跳过的坏文件数（B5 连跳计数）。 */
    bool skip_forward;                                               /**< 跳过坏文件的方向：最近一次移动是否向后。 */
    bool audio_fault_latched;                                        /**< 已因 AUDIO_FAULT 锁存，重启才解除。 */
    Playback_StopReasonTypeDef last_forced_reason;                   /**< R 键上一次注入的停止原因；NONE = 尚未注入。 */
    uint32_t seek_echo_delay_ms;                                     /**< 队首 SEEK 处理前额外等待的虚拟时长（--seek-echo-delay）。 */
    uint32_t seek_waited_ms;                                         /**< 队首 SEEK 已等待的虚拟时长。 */
} PlaybackSim_StateTypeDef;

static PlaybackSim_StateTypeDef playback_sim;

/**
 * @brief 进入停止：位置与总时长清零、停止次数 +1、连跳计数清零；按 K4 决定游标保留
 *        还是重置，AUDIO_FAULT 另外锁存。
 * @param[in] reason 停止原因，非 NONE。
 */
static void playback_sim_enter_stopped(Playback_StopReasonTypeDef reason)
{
    Playback_SnapshotTypeDef *snapshot = &playback_sim.snapshot;

    snapshot->State = PLAYBACK_STATE_STOPPED;
    snapshot->StopReason = reason;
    snapshot->PositionMs = 0U;
    snapshot->DurationMs = 0U;
    snapshot->StopCount++;
    playback_sim.opening = false;
    playback_sim.consecutive_skips = 0U;

    if (reason == PLAYBACK_STOP_REASON_AUDIO_FAULT)
    {
        playback_sim.audio_fault_latched = true;
    }

    if ((reason == PLAYBACK_STOP_REASON_CARD_REMOVED) ||
        (reason == PLAYBACK_STOP_REASON_NOT_READY) ||
        (reason == PLAYBACK_STOP_REASON_NO_TRACK) ||
        (reason == PLAYBACK_STOP_REASON_CATALOG_CHANGED))
    {
        snapshot->HasTrack = false;
        snapshot->Generation = 0U;
        snapshot->Index = 0U;
        playback_sim.track_count = 0U;
    }

    printf("[playback] stopped (reason %d, stops %u)\n",
           (int)reason, (unsigned)snapshot->StopCount);
}

/**
 * @brief 开启一首：曲目身份换成新游标，位置 0、总时长未知，等应答（K5 切歌）。
 * @param[in] generation 游标代次；0 表示尚未确认。
 * @param[in] index 播放列表位置。
 * @note  三态由调用方设置；开启期间播放态仍报播放（K4）。
 */
static void playback_sim_open(uint32_t generation, uint16_t index)
{
    Playback_SnapshotTypeDef *snapshot = &playback_sim.snapshot;

    snapshot->HasTrack = true;
    snapshot->Generation = generation;
    snapshot->Index = index;
    snapshot->PositionMs = 0U;
    snapshot->DurationMs = 0U;
    snapshot->StopReason = PLAYBACK_STOP_REASON_NONE;
    snapshot->TrackStartCount++;
    playback_sim.opening = true;
    playback_sim.open_elapsed_ms = 0U;
}

/**
 * @brief 开启应答为坏文件时跳过（B5）：跳过数 +1，沿最近一次移动的方向开启下一首，
 *        三态不变；连跳数达到曲目数即停止 ALL_BAD（游标保留）。
 * @note  曲目数未知（开机第一首就是坏文件）时无法计数，按 NO_TRACK 停止，不计跳过。
 */
static void playback_sim_skip_bad(void)
{
    Playback_SnapshotTypeDef *snapshot = &playback_sim.snapshot;
    const uint16_t count = playback_sim.track_count;
    uint16_t index;

    if (count == 0U)
    {
        playback_sim_enter_stopped(PLAYBACK_STOP_REASON_NO_TRACK);
        return;
    }

    snapshot->SkippedBadCount++;
    playback_sim.consecutive_skips++;
    printf("[playback] bad file %u skipped (skipped %u)\n",
           (unsigned)snapshot->Index, (unsigned)snapshot->SkippedBadCount);

    if (playback_sim.consecutive_skips >= count)
    {
        playback_sim_enter_stopped(PLAYBACK_STOP_REASON_ALL_BAD);
        return;
    }

    index = playback_sim.skip_forward ?
            (uint16_t)((snapshot->Index + 1U) % count) :
            (uint16_t)((snapshot->Index + count - 1U) % count);
    playback_sim_open(snapshot->Generation, index);
}

/**
 * @brief 有卡时的开启应答：按假音乐前台流程的曲库判定 STALE、NO_TRACK；B 键排队的坏文件按 B5 跳过；
 *        OK 时确认代次、曲目数与总时长，并清零连跳计数。
 * @note  无卡的 NOT_READY 不经过这里，由 playback_sim_advance() 当圈判定（#219）。
 */
static void playback_sim_open_reply(void)
{
    Playback_SnapshotTypeDef *snapshot = &playback_sim.snapshot;
    const uint32_t catalog_generation = music_sim_generation_get();
    const uint16_t count = music_sim_track_count();

    playback_sim.opening = false;

    if ((snapshot->Generation != 0U) && (snapshot->Generation != catalog_generation))
    {
        playback_sim_enter_stopped(PLAYBACK_STOP_REASON_CATALOG_CHANGED);
        return;
    }

    if (snapshot->Index >= count)
    {
        playback_sim_enter_stopped(PLAYBACK_STOP_REASON_NO_TRACK);
        return;
    }

    if (playback_sim.bad_pending > 0U)
    {
        playback_sim.bad_pending--;
        playback_sim_skip_bad();
        return;
    }

    playback_sim.consecutive_skips = 0U;
    snapshot->Generation = catalog_generation;
    snapshot->DurationMs = PLAYBACK_SIM_DURATION_BASE_MS +
                           ((uint32_t)snapshot->Index * PLAYBACK_SIM_DURATION_STEP_MS);
    playback_sim.track_count = count;
}

/**
 * @brief NEXT / PREV（K3）：环形步进游标；播放与暂停中切到新曲开头并保持三态，
 *        停止态只移动游标；无游标或曲目数未知时忽略。
 * @param[in] forward 为真向后（NEXT），为假向前（PREV）。
 */
static void playback_sim_step_track(bool forward)
{
    Playback_SnapshotTypeDef *snapshot = &playback_sim.snapshot;
    const uint16_t count = playback_sim.track_count;
    uint16_t index;

    if (!snapshot->HasTrack || (count == 0U))
    {
        return;
    }

    index = forward ?
            (uint16_t)((snapshot->Index + 1U) % count) :
            (uint16_t)((snapshot->Index + count - 1U) % count);

    if (snapshot->State == PLAYBACK_STATE_STOPPED)
    {
        snapshot->Index = index;
        return;
    }

    playback_sim_open(snapshot->Generation, index);
}

/**
 * @brief 处理一条命令（K3）并回显其序号。
 * @param[in] command 已出队的命令。
 */
static void playback_sim_handle(const Playback_CommandTypeDef *command)
{
    Playback_SnapshotTypeDef *snapshot = &playback_sim.snapshot;

    /* AUDIO_FAULT 锁存（K3、B10）：停止态的 TOGGLE、SELECT 不开流、不动游标，只让停止次数 +1。 */
    if (playback_sim.audio_fault_latched &&
        (snapshot->State == PLAYBACK_STATE_STOPPED) &&
        ((command->Kind == PLAYBACK_COMMAND_TOGGLE) || (command->Kind == PLAYBACK_COMMAND_SELECT)))
    {
        snapshot->StopCount++;
        printf("[playback] audio fault latched, stops %u\n", (unsigned)snapshot->StopCount);
        snapshot->HandledCommandSeq = command->Seq;
        return;
    }

    /* 换曲命令清零连跳计数并决定之后跳过坏文件的方向（B5）。 */
    if (command->Kind != PLAYBACK_COMMAND_TOGGLE)
    {
        playback_sim.consecutive_skips = 0U;
        playback_sim.skip_forward = (command->Kind != PLAYBACK_COMMAND_PREV);
    }

    switch (command->Kind)
    {
        case PLAYBACK_COMMAND_TOGGLE:
            if (snapshot->State == PLAYBACK_STATE_PLAYING)
            {
                snapshot->State = PLAYBACK_STATE_PAUSED;
            }
            else if (snapshot->State == PLAYBACK_STATE_PAUSED)
            {
                snapshot->State = PLAYBACK_STATE_PLAYING;
            }
            else
            {
                /* 停止态开播：打开游标所指曲目；无游标时打开 {代次 0, 位置 0}。之后坏文件向后跳。 */
                playback_sim.skip_forward = true;
                if (snapshot->HasTrack)
                {
                    playback_sim_open(snapshot->Generation, snapshot->Index);
                }
                else
                {
                    playback_sim_open(0U, 0U);
                }
                snapshot->State = PLAYBACK_STATE_PLAYING;
            }
            break;

        case PLAYBACK_COMMAND_NEXT:
            playback_sim_step_track(true);
            break;

        case PLAYBACK_COMMAND_PREV:
            playback_sim_step_track(false);
            break;

        case PLAYBACK_COMMAND_SELECT:
            /* 三态下都切到该曲播放；代次不符在应答时按 STALE 停止。 */
            if (command->Generation != snapshot->Generation)
            {
                playback_sim.track_count = 0U;
            }
            playback_sim_open(command->Generation, command->Index);
            snapshot->State = PLAYBACK_STATE_PLAYING;
            break;

        case PLAYBACK_COMMAND_SEEK:
            /* 忽略：停止态、无当前曲、曲目身份不符、开启中或总时长未知（#172 C）。越尾不算忽略。 */
            if ((snapshot->State == PLAYBACK_STATE_STOPPED) || !snapshot->HasTrack ||
                (command->Generation != snapshot->Generation) || (command->Index != snapshot->Index) ||
                playback_sim.opening || (snapshot->DurationMs == 0U))
            {
                printf("[playback] seek seq %u ignored\n", (unsigned)command->Seq);
                break;
            }
            snapshot->PositionMs = (command->TargetMs < snapshot->DurationMs) ?
                                   command->TargetMs : snapshot->DurationMs;
            printf("[playback] seek idx=%u land=%u\n",
                   (unsigned)snapshot->Index, (unsigned)snapshot->PositionMs);
            break;

        default:
            break;
    }

    snapshot->HandledCommandSeq = command->Seq;
}

/**
 * @brief 按虚拟时钟推进开启应答与播放位置；到总时长自动前进。
 * @param[in] elapsed_ms 距上次推进的虚拟时长。
 */
static void playback_sim_advance(uint32_t elapsed_ms)
{
    Playback_SnapshotTypeDef *snapshot = &playback_sim.snapshot;

    if (playback_sim.opening)
    {
        /* 无卡当圈应答 NOT_READY（#219）：handle 与本函数在同一圈，GUI step 之前就已停止。 */
        if (!music_sim_mounted())
        {
            playback_sim_enter_stopped(PLAYBACK_STOP_REASON_NOT_READY);
            return;
        }

        playback_sim.open_elapsed_ms += elapsed_ms;
        if (playback_sim.open_elapsed_ms >= PLAYBACK_SIM_OPEN_MS)
        {
            playback_sim_open_reply();
        }
        return;
    }

    if (snapshot->State != PLAYBACK_STATE_PLAYING)
    {
        return;
    }

    snapshot->PositionMs += elapsed_ms;
    if ((snapshot->DurationMs != 0U) && (snapshot->PositionMs >= snapshot->DurationMs))
    {
        /* 播完自动前进：末曲回首曲，不出现停止态（K5）；之后坏文件向后跳。 */
        snapshot->PositionMs = snapshot->DurationMs;
        playback_sim.skip_forward = true;
        playback_sim_step_track(true);
    }
}

/**
 * @brief 复位为开机状态：全零快照（停止、NONE、无当前曲），FIFO 空，未挂起。
 * @note  须在 gui_music_init() 之前调用。
 */
void playback_sim_init(void)
{
    (void)memset(&playback_sim, 0, sizeof(playback_sim));
}

/**
 * @brief 代替播放任务走一圈：先把 FIFO 取空逐条处理（挂起时跳过），再推进虚拟时钟。
 * @param[in] now_ms 模拟器时钟的当前时刻（单调，毫秒）。
 * @note  每圈在 gui_music_step() 之前调用，因此 GUI 本圈发出的命令在下一圈回显。
 */
void playback_sim_step(uint32_t now_ms)
{
    Playback_CommandTypeDef command;

    const uint32_t elapsed_ms = playback_sim.has_clock ? (now_ms - playback_sim.last_ms) : 0U;

    while (!playback_sim.suspended && (playback_sim.fifo_count > 0U))
    {
        command = playback_sim.fifo[playback_sim.fifo_head];
        if ((command.Kind == PLAYBACK_COMMAND_SEEK) && (playback_sim.seek_echo_delay_ms > 0U))
        {
            /* 回显延迟：队首 SEEK 等够才处理，后面的命令随之等待。 */
            playback_sim.seek_waited_ms += elapsed_ms;
            if (playback_sim.seek_waited_ms < playback_sim.seek_echo_delay_ms)
            {
                break;
            }
        }
        playback_sim.seek_waited_ms = 0U;
        playback_sim.fifo_head = (uint16_t)((playback_sim.fifo_head + 1U) % PLAYBACK_COMMAND_QUEUE_DEPTH);
        playback_sim.fifo_count--;

        /* 最新者胜：后面紧跟的仍是 SEEK 时只回显本条，不执行。 */
        if ((command.Kind == PLAYBACK_COMMAND_SEEK) && (playback_sim.fifo_count > 0U) &&
            (playback_sim.fifo[playback_sim.fifo_head].Kind == PLAYBACK_COMMAND_SEEK))
        {
            playback_sim.snapshot.HandledCommandSeq = command.Seq;
            continue;
        }
        playback_sim_handle(&command);
    }

    if (playback_sim.has_clock)
    {
        playback_sim_advance(elapsed_ms);
    }
    playback_sim.last_ms = now_ms;
    playback_sim.has_clock = true;
}

/**
 * @brief S 键：挂起或恢复命令处理；挂起时命令只入队，位置照走。
 */
void playback_sim_toggle_suspend(void)
{
    playback_sim.suspended = !playback_sim.suspended;
    printf("[playback] %s (queued %u)\n",
           playback_sim.suspended ? "suspended" : "resumed",
           (unsigned)playback_sim.fifo_count);
}

/**
 * @brief C 键拔卡时由 sim_main.c 调用：有流（播放或暂停）时停止 CARD_REMOVED（K5）。
 * @note  停止态没有流，没有终止事实可处理，游标保留；之后开播同一圈得到 NOT_READY（#219）。
 */
void playback_sim_card_removed(void)
{
    if (playback_sim.snapshot.State != PLAYBACK_STATE_STOPPED)
    {
        playback_sim_enter_stopped(PLAYBACK_STOP_REASON_CARD_REMOVED);
    }
}

/**
 * @brief B 键：让之后一次开启应答为坏文件；连按 k 次则之后 k 次开启都是坏文件。
 * @note  曲目数为 10 时连按 10 次再换曲，整轮都坏，按 ALL_BAD 停止。
 */
void playback_sim_mark_bad(void)
{
    if (playback_sim.bad_pending < UINT16_MAX)
    {
        playback_sim.bad_pending++;
    }
    printf("[playback] bad pending %u\n", (unsigned)playback_sim.bad_pending);
}

/**
 * @brief R 键：按 K1 枚举顺序取下一个非 NONE 停止原因，立即进入停止（K4 副作用同真实后端）。
 * @note  ALL_BAD 之后回到 CARD_REMOVED。停止态也再进入一次（停止次数 +1），便于逐个拍短语帧；
 *        注入 AUDIO_FAULT 会锁存，之后停止态的 TOGGLE、SELECT 只让停止次数 +1。
 */
void playback_sim_stop_next_reason(void)
{
    Playback_StopReasonTypeDef reason = playback_sim.last_forced_reason;

    reason = (reason >= PLAYBACK_STOP_REASON_ALL_BAD) ?
             PLAYBACK_STOP_REASON_CARD_REMOVED :
             (Playback_StopReasonTypeDef)(reason + 1);
    playback_sim.last_forced_reason = reason;
    playback_sim_enter_stopped(reason);
}

/**
 * @brief --seek-echo-delay：之后队首的 SEEK 先等 delay_ms 虚拟时长再处理；0 恢复为下一圈处理。
 * @param[in] delay_ms 额外等待的毫秒数。
 */
void playback_sim_set_seek_echo_delay(uint32_t delay_ms)
{
    playback_sim.seek_echo_delay_ms = delay_ms;
    playback_sim.seek_waited_ms = 0U;
    printf("[playback] seek echo delay %u ms\n", (unsigned)delay_ms);
}

/**
 * @brief 同合同：命令入 FIFO，不阻塞；满或参数为空时丢弃并返回 PLAYBACK_QUEUE_FULL。
 * @param[in] command 待发送的命令，按值拷入 FIFO。
 * @return PLAYBACK_OK 已入队；PLAYBACK_QUEUE_FULL 已丢弃。
 */
Playback_StatusTypeDef playback_command_send(const Playback_CommandTypeDef *command)
{
    uint16_t tail;

    if ((command == NULL) || (playback_sim.fifo_count >= PLAYBACK_COMMAND_QUEUE_DEPTH))
    {
        printf("[playback] queue full, seq %u dropped\n",
               (command == NULL) ? 0U : (unsigned)command->Seq);
        return PLAYBACK_QUEUE_FULL;
    }

    tail = (uint16_t)((playback_sim.fifo_head + playback_sim.fifo_count) % PLAYBACK_COMMAND_QUEUE_DEPTH);
    playback_sim.fifo[tail] = *command;
    playback_sim.fifo_count++;
    return PLAYBACK_OK;
}

/**
 * @brief 同合同：拷出最新一份完整快照，不取走。
 * @param[out] snapshot 快照；NULL 忽略。
 */
void playback_snapshot_read(Playback_SnapshotTypeDef *snapshot)
{
    if (snapshot != NULL)
    {
        *snapshot = playback_sim.snapshot;
    }
}
