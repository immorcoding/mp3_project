/**
 ******************************************************************************
 * @file    playback_task_test_support.h
 * @brief   playback_machine 主机测试的替身与场景助手：调用日志、假存储窗口、假播放模块、快照记录。
 * @note    替身只实现 playback_machine.h 的 Ops，不向生产头增加测试专用入口。
 ******************************************************************************
 */

#ifndef PLAYBACK_TASK_TEST_SUPPORT_H
#define PLAYBACK_TASK_TEST_SUPPORT_H

#include <stdbool.h>
#include <stdint.h>

#include "APP/tasks/playback/playback_machine.h"

/** @brief 替身记录的调用种类，用于断言调用顺序（B11）。 */
typedef enum
{
    CALL_WRITE_REQUEST = 0U,
    CALL_TAKE_BLOCK,
    CALL_RETURN_BLOCK,
    CALL_BEGIN_STREAM,
    CALL_FEED,
    CALL_FINISH_INPUT,
    CALL_PUMP,
    CALL_PAUSE,
    CALL_RESUME,
    CALL_ABANDON,
    CALL_PUBLISH
} CallKindTypeDef;

/* 调用日志。 */
void call_log_reset(void);
uint32_t call_log_count(void);
CallKindTypeDef call_log_at(uint32_t index);
uint32_t call_log_count_of(CallKindTypeDef kind);
/** @brief 第 nth（从 0 起）次 kind 在日志里的位置；不存在返回 UINT32_MAX。 */
uint32_t call_log_index_of(CallKindTypeDef kind, uint32_t nth);

/* 快照记录（PublishSnapshot 的接收端）。 */
uint32_t published_count(void);
Playback_SnapshotTypeDef published_last(void);

/* 假存储窗口：脚本化 Storage 一侧。 */
#define FAKE_BLOCK_CAPACITY 64U
#define FAKE_BLOCK_POOL     8U
uint32_t fake_storage_request_count(void);
Playback_MachineRequestTypeDef fake_storage_last_request(void);
/** @brief Storage 侧整份覆盖写流记录信箱（开启 OK）。 */
void fake_storage_open_ok(uint32_t stream_seq, uint32_t generation, uint16_t track_count, uint32_t audio_bytes);
/** @brief Storage 侧整份覆盖写流记录信箱（任意内容）。 */
void fake_storage_write_record(const Playback_MachineRecordTypeDef *record);
/** @brief 保留开启参数，只改终止状态（ENDED 在最后一块入队之后写）。 */
void fake_storage_set_end(uint32_t stream_seq, Playback_MachineEndTypeDef end);
/** @brief 把 length 字节（值为 fill_value 起递增）作为带 stream_seq 戳的块放入就绪队列。 */
void fake_storage_enqueue_block(uint32_t stream_seq, uint32_t length, uint8_t fill_value);
uint32_t fake_storage_ready_count(void);
/** @brief 已取走但尚未归还的块数。 */
uint32_t fake_storage_outstanding(void);
/** @brief 曾同时持有的最大块数（B15：最多一块）。 */
uint32_t fake_storage_max_outstanding(void);
/** @brief 已归还的块数。 */
uint32_t fake_storage_returned_count(void);
/** @brief 第 n 个被归还的块的流序号。 */
uint32_t fake_storage_returned_seq(uint32_t index);

/* 假播放模块：按窗口容量与每帧字节数建模解码。 */
typedef struct
{
    uint32_t WindowCapacity;   /**< 线性窗口容量，默认 40 B。 */
    uint32_t BytesPerFrame;    /**< 每帧字节数，同时是 W_min，默认 10 B。 */
    uint32_t FrameMs;          /**< 每帧时长，默认 26 ms。 */
    uint32_t DurationMs;       /**< 首帧给出的总时长，默认 180000 ms。 */
    uint32_t FramesPerPump;    /**< 每次 Pump 最多解几帧，默认 2。 */
} FakePlayerConfigTypeDef;
void fake_player_configure(const FakePlayerConfigTypeDef *config);
uint32_t fake_player_begin_count(void);
uint32_t fake_player_last_audio_bytes(void);
uint32_t fake_player_fed_total(void);
/** @brief 已喂入的字节（按顺序，至多 FAKE_FED_CAPACITY）。 */
#define FAKE_FED_CAPACITY 1024U
const uint8_t *fake_player_fed_bytes(void);
bool fake_player_is_paused(void);
bool fake_player_stream_active(void);
bool fake_player_input_finished(void);
/** @brief 模拟“最后一个含真实数据的半区已释放”：之后输入读尽且窗口空时 Pump 回报 StreamEnded。 */
void fake_player_release_tail(void);

/* 场景助手：状态机实例与步进。 */
void world_reset(void);
Playback_MachineTypeDef *world_machine(void);
/** @brief 单条命令加一次唤醒，时刻按调用递增 10 ms。 */
void world_send(Playback_CommandKindTypeDef kind);
void world_send_select(uint32_t generation, uint16_t index);
/** @brief 多条命令放进同一次唤醒。 */
void world_step_commands(const Playback_CommandTypeDef *commands, uint32_t count);
/** @brief 没有命令的一次唤醒。 */
void world_wake(void);
/** @brief 最近分配的流序号（= 最近一次请求信箱写入的序号）。 */
uint32_t world_current_seq(void);
Playback_SnapshotTypeDef world_snapshot(void);
/** @brief 已发出命令的最后序号（world_send* 自增）。 */
uint32_t world_last_command_seq(void);
/** @brief 开启参数里的音频字节数（场景助手统一使用）。 */
#define WORLD_AUDIO_BYTES 4096U
/** @brief Storage 对当前流应答开启 OK，并唤醒一次。 */
void world_open_ok(uint32_t generation, uint16_t track_count);
/** @brief 给当前流放入一块并唤醒一次。 */
void world_feed_block(uint32_t length);
/** @brief 从停止态 TOGGLE 开播，应答开启 OK，放入首块并唤醒，直到首帧解出。 */
void world_start_playing(uint32_t generation, uint16_t track_count);

#endif /* PLAYBACK_TASK_TEST_SUPPORT_H */
