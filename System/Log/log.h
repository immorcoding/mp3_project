/**
  ******************************************************************************
  * @file    log.h
  * @brief   系统日志模块公开接口。
  *
  * @details
  *          本文件仅公开默认日志实例的初始化、等级控制、消息生产接口和
  *          主循环处理接口。日志 Handle、输出操作表及端口上下文均为模块
  *          内部实现，应用层不能直接改写绑定关系。
  *
  *          LOG_Write()/LOG_Printf() 是“生产者”：只把完整消息复制进固定
  *          深度 RAM 队列。LOG_Process() 是“消费者”：每次最多向端口提交
  *          队首一条消息。因此 LOG_OK 通常表示消息已入队或被过滤，不表示
  *          PC 终端已经显示该消息。
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
  * @brief 日志队列及输出端口的运行统计快照。
  * @note  各字段为自 LOG_Init() 起的累计值；再次初始化会全部清零。
  */
typedef struct
{
    uint32_t PendingCount;     /*!< 当前等待发送的日志数。 */
    uint32_t EnqueuedCount;    /*!< 累计进入 RAM 队列的日志数。 */
    uint32_t SentCount;        /*!< 累计被端口接受的日志数，不等同于主机已显示。 */
    uint32_t DroppedCount;     /*!< 队列溢出或发送错误造成的丢弃数。 */
    uint32_t OutputErrorCount; /*!< 输出 Adapter 提交错误累计数。 */
} LOG_StatsTypeDef;

/* Exported functions --------------------------------------------------------*/
/**
  * @brief  初始化默认日志实例并绑定内部输出端口与时间源。
  * @note   重复调用会清空待发送队列、统计值和端口调试快照，尚未发送的
  *         日志会丢失。
  * @retval LOG_OK    初始化成功。
  * @retval LOG_ERROR 默认配置、端口绑定或接口校验失败。
  */
LOG_StatusTypeDef LOG_Init(void);

/**
  * @brief  修改默认日志过滤等级。
  * @param  Level 新的过滤等级，LOG_LEVEL_NONE 表示过滤全部新消息。
  * @note   只影响调用后的新消息；已经在 RAM 队列中的消息不会被删除。
  * @retval LOG_OK    设置成功。
  * @retval LOG_ERROR 日志尚未初始化或等级非法。
  */
LOG_StatusTypeDef LOG_SetLevel(LOG_LevelTypeDef Level);

/**
  * @brief  获取当前日志过滤等级。
  * @retval LOG_LevelTypeDef 当前过滤等级；初始化前默认为 LOG_LEVEL_NONE。
  */
LOG_LevelTypeDef LOG_GetLevel(void);

/**
  * @brief  获取日志模块当前运行状态。
  * @retval LOG_StateTypeDef 当前运行状态。
  */
LOG_StateTypeDef LOG_GetState(void);

/**
  * @brief  非阻塞处理一条待发送日志。
  * @note   输出未就绪或正忙时保留队首日志并返回 LOG_OK。
  *         应在应用主循环中高频、重复调用。本函数一次最多提交一条消息，
  *         不会用循环等待 USB 完成。
  * @retval LOG_OK    队列为空、消息已提交或输出暂时不可用。
  * @retval LOG_ERROR 日志未初始化或输出 Adapter 返回不可恢复错误。
  */
LOG_StatusTypeDef LOG_Process(void);

/**
  * @brief  获取日志队列及输出统计快照。
  * @param  Stats 接收统计数据的对象。
  * @note   该快照用于调试和运行监控，不会清零内部统计计数器。
  * @retval LOG_OK    获取成功。
  * @retval LOG_ERROR 参数为空或日志尚未初始化。
  */
LOG_StatusTypeDef LOG_GetStats(LOG_StatsTypeDef *Stats);

/**
  * @brief  将一段已经装配完成的日志文本复制到 RAM 队列。
  * @param  MessageLevel 当前消息等级。
  * @param  Data         待复制文本；函数返回后调用者可立即复用原缓冲区。
  * @param  Length       文本长度，不包含字符串结尾的 '\0'。
  * @note   队列已满时自动丢弃最旧消息，再保存本次新消息；可通过
  *         LOG_GetStats() 的 DroppedCount 发现溢出。
  * @retval LOG_OK    消息已入队，或被当前等级策略正常过滤。
  * @retval LOG_ERROR 参数、状态或文本长度无效。
  */
LOG_StatusTypeDef LOG_Write(LOG_LevelTypeDef MessageLevel,
                            const char *Data,
                            uint32_t Length);

/**
  * @brief  装配一条格式化日志并复制到 RAM 队列。
  *
  * @details 最终格式为："<Level> (<TimestampMs>) <Tag>: <Message>\r\n"。
  *          例如："I (1234) PMIC: init success\r\n"。这里的时间戳来自
  *          格式化/入队时刻，而不是 USB 实际发送时刻。
  *
  * @param  MessageLevel 当前消息等级。
  * @param  Tag          模块标签，例如 "PMIC"；不可为 NULL。
  * @param  Format       printf 风格的正文格式字符串；不可为 NULL。
  * @param  ...          Format 对应的可变参数。
  * @note   完整消息（含前缀、CRLF 和字符串终止符）必须放入固定大小缓冲区；
  *         超长消息返回 LOG_ERROR，不会输出截断文本。
  * @retval LOG_OK    消息已入队，或被当前等级策略正常过滤。
  * @retval LOG_ERROR 参数、状态或格式化长度无效。
  */
LOG_StatusTypeDef LOG_Printf(LOG_LevelTypeDef MessageLevel,
                             const char *Tag,
                             const char *Format,
                             ...);

#ifdef __cplusplus
}
#endif

#endif /* LOG_H */
