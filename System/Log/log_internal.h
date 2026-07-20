/**
  ******************************************************************************
  * @file    log_internal.h
  * @brief   系统日志模块内部类型定义。
  *
  * @details
  *          本文件只供 log.c 与日志 Port 使用。应用层和普通功能模块应仅
  *          包含 log.h，不应依赖本文件中的 Handle、Ops 或 Context 类型。
  ******************************************************************************
  */

#ifndef LOG_INTERNAL_H
#define LOG_INTERNAL_H

/* Includes ------------------------------------------------------------------*/
#include "Log/log.h"

/* Internal types ------------------------------------------------------------*/
/**
  * @brief 底层日志输出函数类型。
  * @param Context 输出后端对象上下文。
  * @param Data    待输出文本。
  * @param Length  文本长度，不包含结尾的 '\0'。
  * @retval LOG_OK    输出成功。
  * @retval LOG_ERROR 输出后端参数无效或写入失败。
  */
typedef LOG_StatusTypeDef (*LOG_OutputFuncTypeDef)(void *Context,
                                                   const char *Data,
                                                   uint32_t Length);

/** @brief 输出后端操作表。 */
typedef struct
{
    LOG_OutputFuncTypeDef Write; /*!< 文本写入函数。 */
} LOG_OutputOpsTypeDef;

/** @brief 已绑定的输出后端及其对象上下文。 */
typedef struct
{
    const LOG_OutputOpsTypeDef *Ops; /*!< 输出后端操作表。 */
    void *Context;                   /*!< 传给输出函数的对象上下文。 */
} LOG_OutputTypeDef;

/**
  * @brief 毫秒时间源函数类型。
  * @param Context 时间源对象上下文。
  * @retval uint32_t 当前毫秒时间戳，允许自然溢出。
  */
typedef uint32_t (*LOG_GetTimeMsFuncTypeDef)(void *Context);

/** @brief 已绑定的时间源及其对象上下文。 */
typedef struct
{
    LOG_GetTimeMsFuncTypeDef GetTimeMs; /*!< 获取毫秒时间戳的函数。 */
    void *Context;                      /*!< 传给时间函数的对象上下文。 */
} LOG_TimeSourceTypeDef;

/** @brief 默认日志对象的内部 Handle。 */
typedef struct
{
    LOG_OutputTypeDef Output;         /*!< 当前输出后端绑定。 */
    LOG_TimeSourceTypeDef TimeSource; /*!< 当前时间源绑定。 */
    LOG_LevelTypeDef Level;           /*!< 当前过滤等级。 */
    LOG_StateTypeDef State;           /*!< 当前运行状态。 */
} LOG_HandleTypeDef;

#endif /* LOG_INTERNAL_H */
