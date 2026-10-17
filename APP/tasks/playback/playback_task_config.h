/**
 * @file playback_task_config.h
 * @brief 播放任务的固定上界与超时：每次唤醒内状态机的循环次数，T_data、T_out。
 */

#ifndef PLAYBACK_TASK_CONFIG_H
#define PLAYBACK_TASK_CONFIG_H

/* playback_machine.h */
#define PLAYBACK_MACHINE_STALE_BLOCK_LIMIT  32U  /* 每次唤醒归还就绪队列队首旧序号块的上限（B1）；与块池块数 STORAGE_AUDIO_BLOCK_COUNT 同值，不引用以免本头依赖 Storage。 */
#define PLAYBACK_MACHINE_FEED_BLOCK_LIMIT   32U  /* 一轮交字节最多处理的块数（手中块加队列），与块池块数同值。 */
#define PLAYBACK_MACHINE_FEED_ROUND_LIMIT   16U  /* 每次唤醒内“交字节 → 推进”的最大轮数；输入不够时本次结束去等块（B1）。 */

/* playback_machine.c：超时判定（#71 B7、B8），按外壳传入的单调毫秒时刻比较，经过时间达到该值即到期。 */
#define PLAYBACK_MACHINE_DATA_TIMEOUT_MS    2000U  /* T_data：需要数据却没有的最长等待，到期停止 NO_DATA；板级需 ≥ 2 × Storage 单圈最大耗时（#52）。 */
#define PLAYBACK_MACHINE_OUTPUT_TIMEOUT_MS  500U   /* T_out：Platform 运行时没有新的半区释放的最长时间，到期按 AUDIO_ERROR 处理。 */

/* 同步预算 R 是播放模块的配置常量（Service/playback/playback_service_config.h 的 PLAYBACK_SERVICE_SYNC_BUDGET_BYTES），此处不重复。 */

#endif /* PLAYBACK_TASK_CONFIG_H */
