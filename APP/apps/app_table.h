/**
  ******************************************************************************
  * @file    app_table.h
  * @brief   应用注册表：一个应用一行，共用层只经本表接入应用。
  *
  * @details
  *          字段是 `boot_init`（Boot 任务在创建任务前逐行调用）、`gui_init`、`gui_step`、`foreground_step`；
  *          后台作业的 `background_step` 随后台任务搬扫描和回收的票再加（现在的作业表在 `APP/tasks/background/`）。
  *          空字段（NULL）由调用方跳过。GUI 任务、前台任务与 GUI 模拟器遍历同一张表。
  ******************************************************************************
  */

#ifndef APP_TABLE_H
#define APP_TABLE_H

#include <stddef.h>
#include <stdint.h>

#include "Service/gui/gui_service.h"

/** @brief 注册表的一行：一个应用接入共用层的全部入口。 */
typedef struct
{
    /** 应用名，只用于日志（前台 step 耗时行）；静态存储期字符串。 */
    const char *name;
    /** Boot 任务在创建任何用到它的任务之前调用一次（格子、缓存键表等内存准备）；NULL 跳过。单线程环境，不得阻塞、不得碰文件接口。 */
    void (*boot_init)(void);
    /** GUI 任务在 Service_GUI_Init() 成功之后调用一次；NULL 跳过。 */
    void (*gui_init)(void);
    /** GUI 任务每圈在 Service_GUI_ConsumeInput() 之后、Service_GUI_Process() 之前调用；NULL 跳过。 */
    void (*gui_step)(const Service_GUI_InputTypeDef *input);
    /**
     * 前台任务每圈调用一次；NULL 跳过。一步最多一次文件调用加约 1 ms CPU，不碰 Flash 卷（ARC-26）。
     * 返回多少毫秒后再叫它：0 马上，`FOREGROUND_WAIT_FOREVER` 等通知。
     */
    uint32_t (*foreground_step)(void);
} AppTable_EntryTypeDef;

/** @brief 注册表行数。 */
extern const size_t app_table_count;

/** @brief 注册表；顺序即调用顺序。 */
extern const AppTable_EntryTypeDef app_table[];

/* Boot 任务调用：按表的顺序逐行调用 boot_init，空字段跳过；必须在创建用到这些内存的任务之前调完。 */
void app_table_boot_init(void);

/* 同上，但遍历调用者给的表；`app_table_boot_init()` 的实现，也是主机测试的接缝。 */
void app_table_boot_init_rows(const AppTable_EntryTypeDef *rows, size_t count);

#endif /* APP_TABLE_H */
