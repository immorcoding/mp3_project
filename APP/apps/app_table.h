/**
  ******************************************************************************
  * @file    app_table.h
  * @brief   应用注册表：一个应用一行，共用层只经本表接入应用。
  *
  * @details
  *          当前只有 GUI 两列（gui_init、gui_step）；其他列随用到它的票再加。
  *          空字段（NULL）由调用方跳过。GUI 任务与 GUI 模拟器遍历同一张表。
  ******************************************************************************
  */

#ifndef APP_TABLE_H
#define APP_TABLE_H

#include <stddef.h>

#include "Service/gui/gui_service.h"

/** @brief 注册表的一行：一个应用接入共用层的全部入口。 */
typedef struct
{
    /** GUI 任务在 Service_GUI_Init() 成功之后调用一次；NULL 跳过。 */
    void (*gui_init)(void);
    /** GUI 任务每圈在 Service_GUI_ConsumeInput() 之后、Service_GUI_Process() 之前调用；NULL 跳过。 */
    void (*gui_step)(const Service_GUI_InputTypeDef *input);
} AppTable_EntryTypeDef;

/** @brief 注册表行数。 */
extern const size_t app_table_count;

/** @brief 注册表；顺序即调用顺序。 */
extern const AppTable_EntryTypeDef app_table[];

#endif /* APP_TABLE_H */
