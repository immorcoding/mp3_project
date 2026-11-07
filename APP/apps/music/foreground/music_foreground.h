/**
 * @file music_foreground.h
 * @brief 音乐前台流程：前台任务轮流调用的单步函数，读曲名和 Queue 窗口各一行标签，经 `ui_cell` 发布界面数据。
 *
 * @details
 *          GUI 写最新需求（曲名一格、窗口一格，新的覆盖旧的），前台任务读文件、发布结果；GUI 不等文件，也不查询卷状态
 *          （#193、#197、#200、STOR-6、STOR-15、STOR-16）。
 *
 *          **一步**（`music_foreground_step()`）：先看曲名需求，有就处理它（命中行缓存不读文件，否则读一首的标签，最多一次文件调用）；
 *          没有文件调用时，再最多读窗口的一行（命中行缓存的行不读文件，可以一次拼进好几行）；最后重新看一次需求。检查点在两行
 *          之间，一行之内不中断。一步最多一次文件调用加约 1 ms CPU，不碰 Flash 卷（ARC-26）。返回值告诉前台任务多久后再叫它。
 *
 *          **窗口**：一窗凑齐才发布（GUI 只见整窗，STOR-16）；曲库代次变了或 SD 不在时发布代次 0 的空窗；新需求覆盖旧需求
 *          （起点变了就从头重拼，已读的行靠行缓存不重读）；曲库发布后（`catalog.c` 叫醒前台任务一次）和卷就绪后重新处理最新需求，
 *          因为 GUI 不会重写没变的需求。**曲名**：代次 0 表示“当前”；结果带回所答的需求序号；曲库不可用时发代次 0 的结果，
 *          曲库发布后再答一次。
 *
 *          **行缓存**：键是（代次，位置），只缓存结果稳定的行；打开失败、读取错误、`SERVICE_BUSY` 和卷不可用都不缓存。
 *          `SERVICE_BUSY` 一律过约 10 ms 重试，不设次数上限，不算文件错误（ARC-27）。**卷不可用**（拔卡的正常竞态）也不是文件错误：
 *          窗口行读到它时不填行、不发布，已发布的窗口保持不变，这一窗挂起等卷事件（卷消失由代次 0 的空窗收尾）或新需求。
 *          缓冲与状态都在 `.apps_sdram`，不占前台任务的栈与 DTCM（ARC-28）。
 *
 *          主机测试是单线程的（`Tests/music_foreground`）：证明分支走对了，不证明真实并发。
 */

#ifndef MUSIC_FOREGROUND_H
#define MUSIC_FOREGROUND_H

#include <stdint.h>

/* Boot 任务调用（注册表的 boot_init 经 `music_boot_init()`）：清零行缓存键表与流程状态。 */
void music_foreground_boot_init(void);

/* 前台任务调用（注册表的 foreground_step）：做一小步，返回多少毫秒后再叫它：0 马上，FOREGROUND_WAIT_FOREVER 等通知。 */
uint32_t music_foreground_step(void);

#endif /* MUSIC_FOREGROUND_H */
