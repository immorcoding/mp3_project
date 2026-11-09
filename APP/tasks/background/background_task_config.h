/**
  ******************************************************************************
  * @file    background_task_config.h
  * @brief   后台任务的私有参数：通知槽与标志位、"等通知"的哨兵值、Flash 压力测试钩子的开关与调参。
  *
  * @details
  *          Flash 压力钩子（DIAG-7、#213、清单 1 C2/C3、R6.2 至 R6.4）：默认关闭，代码写成
  *          `if (BACKGROUND_HOOK_FLASH_PRESSURE != 0U)`，默认构建也编译；发布构建不开。打开方式：
  *          configure 时 `cmake --preset firmware-debug -DMP3_HOOK_FLASH_PRESSURE=1`，只允许 Debug。
  *          打开后后台任务里多一个作业：Flash 卷就绪之后在 Flash 卷根目录循环"新建文件、分块写满、关闭、删除"，
  *          把空闲块压到回收水位以下；真擦除由后台任务的回收作业（`background_reclaim.c`，100 ms 周期、一次一块）给的机会触发（#220 起钩子默认不另调回收）。
  ******************************************************************************
  */

#ifndef BACKGROUND_TASK_CONFIG_H
#define BACKGROUND_TASK_CONFIG_H

#include <stdint.h>

/* background_loop.c、background_task.c */
#define BACKGROUND_NOTIFY_FLAG_WAKE      (1UL << 0)  /* 后台任务通知槽：有人调了 `background_task_wake()`（应用改了自己的状态，请再调一遍作业）。 */
#define BACKGROUND_NOTIFY_FLAG_VOLUME    (1UL << 1)  /* 后台任务通知槽：卷事件（卷任务或 Flash 启动任务发布了卷就绪或卷消失；任务入口登记订阅）。FLAG 只用于唤醒，醒来后各作业自己比对卷序号。 */
#define BACKGROUND_NOTIFY_FLAG_EVENT_MASK (BACKGROUND_NOTIFY_FLAG_WAKE | BACKGROUND_NOTIFY_FLAG_VOLUME) /* 一次收取并清除的事件位。 */
#define BACKGROUND_JOB_MAX               8U          /* 作业表最多项数：内置作业（回收、压力钩子）加注册表各应用的 `background_step`；`background_jobs_build()` 放不下时断言停机。 */
#define BACKGROUND_WAIT_FOREVER          UINT32_MAX  /* 作业返回它表示"我不需要定时再叫，等通知"；整圈都是它时任务一直阻塞。 */

/* background_reclaim.c：Flash 回收作业（STOR-12、#220）。 */
#define BACKGROUND_RECLAIM_PERIOD_MS     100U        /* 两次回收机会之间的最短间隔（从上一次调用开始算），单位为毫秒；一次最多一块，不代表每次都会擦除。 */

/* background_flash_pressure.c（Flash 压力钩子，说明见文件头） */
#ifndef BACKGROUND_HOOK_FLASH_PRESSURE
#define BACKGROUND_HOOK_FLASH_PRESSURE   0U    /* 钩子开关，默认关；构建时 -D 或 CMake 变量 MP3_HOOK_FLASH_PRESSURE=1 打开，发布构建不开。 */
#endif
#ifndef BACKGROUND_HOOK_FLASH_PRESSURE_CHUNK_BYTES
#define BACKGROUND_HOOK_FLASH_PRESSURE_CHUNK_BYTES 32768U  /* 每步写的字节数，等于单次写上限 32 KiB（`SERVICE_FILESYSTEM_IO_MAX_BYTES`）；调小只会让每步更短。 */
#endif
#ifndef BACKGROUND_HOOK_FLASH_PRESSURE_FILE_BYTES
#define BACKGROUND_HOOK_FLASH_PRESSURE_FILE_BYTES (4U * 1024U * 1024U) /* 一个压力文件写满多少字节就关闭并删除；4 MiB 的删除约 0.55 s；CMake 缓存变量 MP3_HOOK_FLASH_PRESSURE_FILE_MIB（1 至 16，默认 4）把它传进来，16 MiB 满额删除不用改源码。 */
#endif
#ifndef BACKGROUND_HOOK_FLASH_PRESSURE_PACE_MS
#define BACKGROUND_HOOK_FLASH_PRESSURE_PACE_MS 100U  /* 每写一块、回收一次之后让出的时间，单位为毫秒；也是回收的节拍（与产品回收 100 ms 一次一致）。 */
#endif
#ifndef BACKGROUND_HOOK_FLASH_PRESSURE_FREE_FLOOR
#define BACKGROUND_HOOK_FLASH_PRESSURE_FREE_FLOOR 0U /* 空闲块低于它就暂停写入、只等回收；0 表示不设下限，一路压到 FTL 自己回收才能继续写（R6.4 要的最坏情形）。想只压到水位以下而不耗尽，设成比水位（本板 462）小的数，例如 300；CMake 缓存变量 MP3_HOOK_FLASH_PRESSURE_FREE_FLOOR 把它传进来。 */
#endif
#ifndef BACKGROUND_HOOK_FLASH_PRESSURE_RECLAIM
#define BACKGROUND_HOOK_FLASH_PRESSURE_RECLAIM 0U /* 写一块之后是否由本作业另调一次回收；默认 0：产品的回收由后台任务自己的回收作业（`background_reclaim.c`，100 ms 周期、一次一块）负责，钩子再调会让回收被叫两次、擦除节拍比产品高一倍；主机测试用 -D 打开它来覆盖钩子自己的回收分支。 */
#endif
#ifndef BACKGROUND_HOOK_FLASH_PRESSURE_LOG_PERIOD_MS
#define BACKGROUND_HOOK_FLASH_PRESSURE_LOG_PERIOD_MS 10000U /* 统计行的周期，单位为毫秒；Monitor 5 s 一批，这行与它同时到时最坏占 1 块（DIAG-3）。 */
#endif
#ifndef BACKGROUND_HOOK_FLASH_PRESSURE_WAIT_VOLUME_MS
#define BACKGROUND_HOOK_FLASH_PRESSURE_WAIT_VOLUME_MS 500U /* Flash 卷没挂载（含 Flash 启动任务的 MountFlash 期间）或本作业已停下时，多久回头看一次卷序号。 */
#endif
#ifndef BACKGROUND_HOOK_FLASH_PRESSURE_SOURCE_ADDRESS
#define BACKGROUND_HOOK_FLASH_PRESSURE_SOURCE_ADDRESS 0x08000000UL /* 写入的内容取自这里：片内 Flash 的固件映像开头（映像远大于 32 KiB，已编程，可读）。内容无所谓，FTL 不去重；这样不占 32 KiB 的 RAM。 */
#endif
#define BACKGROUND_HOOK_FLASH_PRESSURE_BUSY_RETRY_MAX 3U   /* 新建文件遇到 SERVICE_BUSY（文件已存在或槽用尽）最多重试几次，超过就停。 */
#define BACKGROUND_HOOK_FLASH_PRESSURE_BUSY_RETRY_MS  10U  /* 重试间隔（ARC-27：SERVICE_BUSY 过约 10 ms 重试，不算文件错误）。 */

#endif /* BACKGROUND_TASK_CONFIG_H */
