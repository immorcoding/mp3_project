/**
 ******************************************************************************
 * @file    background_flash_pressure.h
 * @brief   Flash 压力测试钩子：后台任务里注册的一个作业（DIAG-7、#213；清单 1 C2/C3、R6.2 至 R6.4）。
 *
 * @details
 *          默认关闭（`BACKGROUND_HOOK_FLASH_PRESSURE`），代码写成 `if (开关)`，默认构建也编译；发布构建不开。
 *          打开后，Flash 卷就绪时开始在 Flash 卷根目录循环：新建 `FLASHP.TMP`、32 KiB 一块写到 4 MiB、
 *          每写一块给 FTL 一次回收机会、写满关闭并删除。每步最多一次文件调用，返回多久后再叫它。
 *          日志与调参见 `APP/tasks/background/README.md` 与 `background_task_config.h`。
 ******************************************************************************
 */

#ifndef BACKGROUND_FLASH_PRESSURE_H
#define BACKGROUND_FLASH_PRESSURE_H

#include <stdint.h>

/* 作业单步：返回多少毫秒后再调；钩子关闭时恒为 BACKGROUND_WAIT_FOREVER 且什么都不做。 */
uint32_t background_flash_pressure_step(void);

/* 后台任务入口调用一次：钩子打开时投一行 WARN `HOOK: flash_pressure ON …`，关闭时什么都不投。 */
void background_flash_pressure_announce(void);

#endif /* BACKGROUND_FLASH_PRESSURE_H */
