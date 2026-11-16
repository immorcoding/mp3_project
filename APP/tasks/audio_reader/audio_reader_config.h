/**
 * @file audio_reader_config.h
 * @brief 音频读取任务的固定配置：音频块与块池、通知标志、水位与让路、SERVICE_BUSY 重试、定位符失效测试钩子。
 *
 * @details
 *          水位、让路、重试间隔都是“初值，由板级验收修正”（#194 第 4 条，R4.1、R4.2、R5.4），改常量即可，不改代码。
 *
 *          定位符失效测试钩子（DIAG-7、#215、R4.4）：默认关闭，代码写成 `if (AUDIO_READER_HOOK_LOCATOR_MISS != 0U)`，
 *          默认构建也编译；发布构建不开。打开方式：configure 时 `cmake --preset firmware-debug -DMP3_HOOK_LOCATOR_MISS=1`，
 *          只允许 Debug。打开后音频读取任务在每次开流和重开流前把曲库读出的文件定位符清零（全零表示“无定位符”），
 *          Filesystem 因此一定判为核对不符，走真实的“退回按路径打开”路径：日志出现 `Locator miss` WARN，
 *          12000 首同文件夹开最后一首要 1.4 至 2.1 s（T_data 5 s 之内）。只影响音频流；前台任务的曲名和窗口行在自己的开文件处也有同一个钩子判断（R5.5）。
 */

#ifndef AUDIO_READER_CONFIG_H
#define AUDIO_READER_CONFIG_H

/* audio_reader_block.h */
#define AUDIO_READER_BLOCK_BYTES  (32U * 1024U)  /* 单块数据容量，单位为字节；512 B 整数倍且不超过 SD bounce buffer（32 KiB）。 */
#define AUDIO_READER_BLOCK_COUNT  32U            /* 块池块数；初值 32 块共 1 MiB，320 kbps 下约 26 s。须不小于 3。 */

#define AUDIO_READER_POOL_SECTION ".storage_audio_pool" /* 块池所在的链接段；链接脚本独立的 SDRAM NOLOAD 段，位于帧缓冲之后；段名不随改名而变（#202 A 节）。 */

/* audio_reader_task.c、audio_reader_stream.c：任务私有通知槽与标志位。槽号只在本任务的通知数组内有效。 */
#define AUDIO_READER_NOTIFY_EVENT_INDEX         0U         /* 唯一的等待点：新请求、块归还与卷事件以 FLAG 共存在这一个槽里。 */
#define AUDIO_READER_NOTIFY_COUNT               1U         /* 本任务占用的通知槽数量。 */
#define AUDIO_READER_NOTIFY_FLAG_VOLUME         (1UL << 0) /* 事件槽：卷事件（卷任务或 Flash 启动任务发布了卷就绪或卷消失；任务入口登记订阅）。 */
#define AUDIO_READER_NOTIFY_FLAG_REQUEST        (1UL << 1) /* 事件槽：播放任务写了请求信箱。 */
#define AUDIO_READER_NOTIFY_FLAG_BLOCK_RETURNED (1UL << 2) /* 事件槽：播放任务归还了音频块。 */
#define AUDIO_READER_NOTIFY_FLAG_EVENT_MASK     (AUDIO_READER_NOTIFY_FLAG_VOLUME | \
                                                 AUDIO_READER_NOTIFY_FLAG_REQUEST | \
                                                 AUDIO_READER_NOTIFY_FLAG_BLOCK_RETURNED) /* 唯一的等待点一次收取并清除的事件位；FLAG 只用于唤醒，醒来后以信箱、队列和卷序号为准。 */

/* audio_reader_pace.c：水位与让路（初值，由板级验收修正）。 */
#define AUDIO_READER_WATERMARK_BLOCKS 4U  /* 安全水位：就绪队列里不到这么多块就连续补块，不让路。按就绪队列算，不含播放任务手里那块；320 kbps 下 4 块约 3.3 s，够撑过最坏的非故障等锁 2.05 s。初值，由板级验收修正。 */
#define AUDIO_READER_YIELD_TICKS      1U  /* 水位以上每读一块让路多少 tick；让的是 SD 锁。让路用唯一的等待点，不用 vTaskDelay，新请求、块归还、卷事件能立刻叫醒。改 0 即“不让路”做对比（R5.4），不算钩子。初值，由板级验收修正。 */

/* audio_reader_stream.c：卷宽限。读块或开流得到 NO_MEDIA、NOT_READY、INVALID_HANDLE 而卷挂着、序号没变时，
 * 可能是卡刚拔、边沿已到而卷任务还没轮到（与读取任务同级，卡检测中断不会让它立刻抢占，闸还没关、序号还没变），
 * 也可能是卡还在、设备出错（没有边沿就没有卷事件）。先在唯一的等待点上等一个有上界的时间看卷事件，
 * 期间来了卷事件按拔卡处理，超时仍没变才报读错误。 */
#define AUDIO_READER_VOLUME_GRACE_MS  100U  /* 宽限上界，单位为毫秒；要盖住消抖 30 ms 加刷新与卸载（卷任务的卸载完成在边沿后 +34 至 35 ms，spike 实测）并留余量。等待期间新请求、块归还、卷事件仍能立刻叫醒。初值，由板级验收修正（R4.5、R4.6）。 */

/* audio_reader_stream.c：SERVICE_BUSY（文件槽用尽）过约 10 ms 重试，不算文件错误（#194 修正的第 13 条）。 */
#define AUDIO_READER_BUSY_RETRY_MS    10U   /* 两次重试之间至少隔多久，单位为毫秒；等的是唯一的等待点，不是 vTaskDelay。 */
#define AUDIO_READER_BUSY_LOG_EVERY   100U  /* 日志节拍：同一次开流或读块遇到 SERVICE_BUSY，第 1 次记一行，之后每满这么多次再记一行（约 1 s 一行），不刷屏（DIAG-3）。 */

/* audio_reader_stream.c：定位符失效测试钩子（说明见文件头）。 */
#ifndef AUDIO_READER_HOOK_LOCATOR_MISS
#define AUDIO_READER_HOOK_LOCATOR_MISS 0U /* 钩子开关，默认关；构建时 -D 或 CMake 变量 MP3_HOOK_LOCATOR_MISS=1 打开，发布构建不开。 */
#endif

#endif /* AUDIO_READER_CONFIG_H */
