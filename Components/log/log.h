/**
  ******************************************************************************
  * @file    log.h
  * @brief   系统日志模块公开接口。
  *
  * @details
  *          本文件仅公开默认日志实例的等级控制、消息生产接口和消费处理接口。
  *          初始化和输出装配接口位于 log_adapter.h，普通应用代码不能直接改写
  *          绑定关系。
  *
  *          LOG_Write()/LOG_Printf() 是“生产者”：只把完整消息复制进固定
  *          深度 RAM 队列。LOG_Process() 是“消费者”：每次最多向 Adapter 提交
  *          队首一条消息。因此 LOG_OK 通常表示消息已入队或被过滤，不表示
  *          PC 终端已经显示该消息。
  *
  *          重复初始化会清空队列和统计值；队列满时新消息会替换最旧消息。
  *          LOG_Printf() 对超长消息返回错误而不发送截断文本。
  *
  * @warning 当前实现面向单线程主循环，不包含锁和临界区。不要同时从中断、
  *          RTOS 多任务和主循环访问日志 API。
  ******************************************************************************
  */

#ifndef LOG_H
#define LOG_H

/* Includes ------------------------------------------------------------------*/
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Exported types ------------------------------------------------------------*/
/**
  * @brief 日志等级。
  * @note  数值越大，允许输出的信息越详细。
  */
typedef enum
{
    LOG_LEVEL_NONE = 0U, /*!< 关闭全部日志输出。 */
    LOG_LEVEL_ERROR,     /*!< 严重错误信息。 */
    LOG_LEVEL_WARN,      /*!< 警告信息。 */
    LOG_LEVEL_INFO,      /*!< 正常运行信息。 */
    LOG_LEVEL_DEBUG      /*!< 调试详细信息。 */
} LOG_LevelTypeDef;

/** @brief 日志接口返回状态。 */
typedef enum
{
    LOG_OK = 0U, /*!< 操作成功，或日志被等级策略正常过滤。 */
    LOG_ERROR    /*!< 参数、状态、格式化或底层输出发生错误。 */
} LOG_StatusTypeDef;

/** @brief 日志模块运行状态。 */
typedef enum
{
    LOG_STATE_RESET = 0U, /*!< 尚未初始化。 */
    LOG_STATE_READY,      /*!< 已初始化，可以输出日志。 */
    LOG_STATE_ERROR       /*!< 初始化或内部接口发生错误。 */
} LOG_StateTypeDef;

/**
  * @brief 日志队列及输出 Adapter 的运行统计快照。
  * @note  各字段为自 LOG_Init() 起的累计值；再次初始化会全部清零。
  */
typedef struct
{
    uint32_t PendingCount;     /*!< 当前等待发送的日志数。 */
    uint32_t EnqueuedCount;    /*!< 累计进入 RAM 队列的日志数。 */
    uint32_t SentCount;        /*!< 累计被 Adapter 接受的日志数，不等同于主机已显示。 */
    uint32_t DroppedCount;     /*!< 队列溢出或发送错误造成的丢弃数。 */
    uint32_t OutputErrorCount; /*!< 输出 Adapter 提交错误累计数。 */
} LOG_StatsTypeDef;

/* Exported functions --------------------------------------------------------*/
LOG_StatusTypeDef LOG_SetLevel(LOG_LevelTypeDef Level);

LOG_LevelTypeDef LOG_GetLevel(void);

LOG_StateTypeDef LOG_GetState(void);

LOG_StatusTypeDef LOG_Process(void);

LOG_StatusTypeDef LOG_GetStats(LOG_StatsTypeDef *Stats);

LOG_StatusTypeDef LOG_Write(LOG_LevelTypeDef MessageLevel,
                            const char *Data,
                            uint32_t Length);

LOG_StatusTypeDef LOG_Printf(LOG_LevelTypeDef MessageLevel,
                             const char *Tag,
                             const char *Format,
                             ...);

#ifdef __cplusplus
}
#endif

#endif /* LOG_H */
