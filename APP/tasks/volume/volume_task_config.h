/**
  ******************************************************************************
  * @file    volume_task_config.h
  * @brief   卷任务的私有参数：通知槽与标志位、消抖静默时间、订阅者上限、假抖动测试钩子。
  *
  * @details
  *          假抖动测试钩子（DIAG-7、#211 R3.4）：默认关闭，代码写成 `if (VOLUME_HOOK_FAKE_BOUNCE != 0U)`，默认构建也编译；
  *          发布构建不开。打开方式：configure 时 `cmake --preset firmware-debug -DMP3_HOOK_FAKE_BOUNCE=1`，只允许 Debug。
  *          打开后卷任务每隔 VOLUME_HOOK_FAKE_BOUNCE_PERIOD_MS 自己调用一次边沿入口，就像卡检测脚抖了一下；
  *          卡其实没动，对账应该走“只开闸”这条路径（日志 `path=gate_only`），播放的流和 Queue 窗口都不该受影响。
  ******************************************************************************
  */

#ifndef VOLUME_TASK_CONFIG_H
#define VOLUME_TASK_CONFIG_H

/* volume_task.h、volume_sd.c */
#define VOLUME_NOTIFY_FLAG_SD_DETECT     (1UL << 0)  /* 卷任务通知槽：卡检测边沿（ISR 里置位，只叫醒，不带别的信息）。 */
#define VOLUME_NOTIFY_FLAG_EVENT_MASK    (VOLUME_NOTIFY_FLAG_SD_DETECT)  /* 卷任务一次收取并清除的事件位。 */

/* volume_sd.c */
#define VOLUME_SD_DEBOUNCE_MS            30U   /* SD 卡检测输入在最后一个边沿之后的最短静默时间，单位为毫秒；新边沿重新计时。 */

/* volume_event.c */
#define VOLUME_SUBSCRIBER_MAX            4U    /* 卷事件订阅者上限；最终形态是音频读取、前台、后台三个，留一个余量。超出时登记失败。 */

/* volume_flash.c */
#define VOLUME_FLASH_AUTO_FORMAT         1U    /* MountFlash 得到无文件系统时是否 FormatAndMountFlash（仅 Flash 启动任务的启动路径使用）。 */

/* volume_sd.c（假抖动测试钩子，说明见文件头） */
#ifndef VOLUME_HOOK_FAKE_BOUNCE
#define VOLUME_HOOK_FAKE_BOUNCE          0U    /* 钩子开关，默认关；构建时 -D 或 CMake 变量 MP3_HOOK_FAKE_BOUNCE=1 打开，发布构建不开。 */
#endif
#define VOLUME_HOOK_FAKE_BOUNCE_PERIOD_MS 5000U  /* 钩子打开时，两次人造边沿之间的间隔，单位为毫秒。 */

#endif /* VOLUME_TASK_CONFIG_H */
