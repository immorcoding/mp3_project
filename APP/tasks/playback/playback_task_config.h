/**
 * @file playback_task_config.h
 * @brief 播放任务的固定上界：每次唤醒内状态机的循环次数。
 */

#ifndef PLAYBACK_TASK_CONFIG_H
#define PLAYBACK_TASK_CONFIG_H

/* playback_machine.h */
#define PLAYBACK_MACHINE_STALE_BLOCK_LIMIT  32U  /* 每次唤醒归还就绪队列队首旧序号块的上限（B1）；与块池块数 STORAGE_AUDIO_BLOCK_COUNT 同值，不引用以免本头依赖 Storage。 */
#define PLAYBACK_MACHINE_FEED_BLOCK_LIMIT   32U  /* 一轮交字节最多处理的块数（手中块加队列），与块池块数同值。 */
#define PLAYBACK_MACHINE_FEED_ROUND_LIMIT   16U  /* 每次唤醒内“交字节 → 推进”的最大轮数；输入不够时本次结束去等块（B1）。 */

#endif /* PLAYBACK_TASK_CONFIG_H */
