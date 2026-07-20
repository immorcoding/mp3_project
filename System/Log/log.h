/**
  ******************************************************************************
  * @file    log.h
  * @brief   系统日志模块公开接口。
  *
  * @details
  *          本文件仅公开默认日志实例的初始化、等级控制和日志输出接口。
  *          日志 Handle、输出操作表及端口上下文均为模块内部实现，应用层
  *          无法直接装配或修改。
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

/* Exported functions --------------------------------------------------------*/
/**
  * @brief  初始化默认日志实例并绑定内部输出端口与时间源。
  * @note   重复调用会重新绑定端口，并清空 RAM 日志快照。
  * @retval LOG_OK    初始化成功。
  * @retval LOG_ERROR 默认配置、端口绑定或接口校验失败。
  */
LOG_StatusTypeDef LOG_Init(void);

/**
 * @brief 修改默认日志过滤等级。
 * @param Level 新的过滤等级，LOG_LEVEL_NONE 表示关闭日志输出。
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
 * @brief 输出一段已经装配完成的日志文本。
 * @param MessageLevel 当前消息等级。
 * @param Data         待输出文本。
 * @param Length       文本长度，不包含字符串结尾的 '\0'。
 * @retval LOG_OK    输出成功，或消息被等级正常过滤。
 * @retval LOG_ERROR 参数、状态或底层输出接口无效。
 */
LOG_StatusTypeDef LOG_Write(LOG_LevelTypeDef MessageLevel,
                            const char *Data,
                            uint32_t Length);

/**
 * @brief 输出一条格式化日志。
 *
 * @details 最终格式为："<Level> (<TimestampMs>) <Tag>: <Message>\r\n"。
 *          例如："I (1234) PMIC: init success\r\n"。
 *
 * @param MessageLevel 当前消息等级。
 * @param Tag          模块标签，例如 "PMIC"。
 * @param Format       printf 风格的正文格式字符串。
 * @param ...          Format 对应的可变参数。
 * @retval LOG_OK    输出成功，或消息被等级正常过滤。
 * @retval LOG_ERROR 参数、格式化或底层输出失败。
 */
LOG_StatusTypeDef LOG_Printf(LOG_LevelTypeDef MessageLevel,
                             const char *Tag,
                             const char *Format,
                             ...);

#ifdef __cplusplus
}
#endif

#endif /* LOG_H */
