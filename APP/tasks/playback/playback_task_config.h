/**
 * @file playback_task_config.h
 * @brief 播放任务的固定配置：状态机每次唤醒的循环上界、T_data 与 T_out 超时、通知 FLAG、命令 FIFO 深度与唤醒节拍。
 */

#ifndef PLAYBACK_TASK_CONFIG_H
#define PLAYBACK_TASK_CONFIG_H

/* playback_machine.h */
#define PLAYBACK_MACHINE_STALE_BLOCK_LIMIT  32U  /* 每次唤醒归还就绪队列队首旧序号块的上限（B1）；与块池块数 STORAGE_AUDIO_BLOCK_COUNT 同值（不引用以免本头依赖 Storage，由 playback_task.c 的 _Static_assert 核对）。 */
#define PLAYBACK_MACHINE_FEED_BLOCK_LIMIT   32U  /* 一轮交字节最多处理的块数（手中块加队列），与块池块数同值（同上由 _Static_assert 核对）。 */
#define PLAYBACK_MACHINE_FEED_ROUND_LIMIT   16U  /* 每次唤醒内“交字节 → 推进”的最大轮数；输入不够时本次结束去等块（B1）。 */
#define PLAYBACK_MACHINE_LOG_TEXT_SIZE      64U  /* 单条 INFO 日志文本的格式化缓冲区字节数（含结尾 0）；放不下时末字符为 ~ 标记截断。 */

/* playback_machine.c：超时判定（#71 B7、B8），按外壳传入的单调毫秒时刻比较，经过时间达到该值即到期。 */
#define PLAYBACK_MACHINE_DATA_TIMEOUT_MS    2000U  /* T_data：需要数据却没有的最长等待，到期停止 NO_DATA；板级需 ≥ 2 × Storage 单圈最大耗时（#52）。 */
#define PLAYBACK_MACHINE_OUTPUT_TIMEOUT_MS  500U   /* T_out：Platform 运行时没有新的半区释放的最长时间，到期按 AUDIO_ERROR 处理。 */

/* 同步预算 R 是播放模块的配置常量（Service/playback/playback_service_config.h 的 PLAYBACK_SERVICE_SYNC_BUDGET_BYTES），此处不重复。 */

/* playback_task.h */
#define PLAYBACK_NOTIFY_FLAG_COMMAND        (1UL << 0)  /* 事件槽：GUI 向命令 FIFO 入队了命令（K2）。 */
#define PLAYBACK_NOTIFY_FLAG_STREAM_RECORD  (1UL << 1)  /* 事件槽：Storage 覆盖写了流记录信箱（C2）。 */
#define PLAYBACK_NOTIFY_FLAG_BLOCK_READY    (1UL << 2)  /* 事件槽：Storage 把新块放进了就绪队列。 */
#define PLAYBACK_NOTIFY_FLAG_AUDIO          (1UL << 3)  /* 事件槽：Platform 音频释放了半区或报告了故障（ISR 置位）。 */
#define PLAYBACK_NOTIFY_FLAG_EVENT_MASK     (PLAYBACK_NOTIFY_FLAG_COMMAND | \
                                             PLAYBACK_NOTIFY_FLAG_STREAM_RECORD | \
                                             PLAYBACK_NOTIFY_FLAG_BLOCK_READY | \
                                             PLAYBACK_NOTIFY_FLAG_AUDIO)  /* 任务一次收取并清除的事件位；FLAG 只用于唤醒，醒来后以信箱与队列内容为准。 */

/* playback_task.c */
#define PLAYBACK_COMMAND_QUEUE_DEPTH        8U   /* 命令 FIFO 深度（K2 定值）；也是状态机一次唤醒处理命令数的上界（B1）。 */
#define PLAYBACK_TASK_WAKE_PERIOD_MS        100U /* 无事件时的最长等待：让 T_data、T_out 这类只靠时刻到期的判定在没有 FLAG 时也能被评估。 */

#endif /* PLAYBACK_TASK_CONFIG_H */
