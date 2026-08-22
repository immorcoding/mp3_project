/**
  ******************************************************************************
  * @file    led.h
  * @brief   可复用 LED Device 的公共类型和 Interface。
  *
  * @details
  *          LED Device 只表达逻辑上的 ON/OFF，不表达 GPIO 高低电平、I2C 地址或
  *          LED 控制器寄存器。PortOps 是具体驱动后端的可替换 Seam；Platform
  *          持有 Device Handle 与 Adapter Context，并为上层分配板级 LED 编号。
  ******************************************************************************
  */

#ifndef LED_H
#define LED_H

#include <stdbool.h>

/** @brief LED Device 函数的立即返回状态。 */
typedef enum
{
    LED_OK = 0, /**< 本次请求已成功完成。 */
    LED_ERROR   /**< 本次请求失败，具体原因保存在 Handle 中。 */
} LED_StatusTypeDef;

/** @brief LED 对上层暴露的逻辑亮灭状态。 */
typedef enum
{
    LED_OFF = 0, /**< 请求熄灭 LED。 */
    LED_ON        /**< 请求点亮 LED。 */
} LED_OnOffTypeDef;

/** @brief LED Device 的持续生命周期状态。 */
typedef enum
{
    LED_LIFECYCLE_RESET = 0, /**< 尚未完成初始化。 */
    LED_LIFECYCLE_READY,     /**< 可以接受亮灭控制。 */
    LED_LIFECYCLE_BUSY,      /**< 正在调用同步 Port 操作。 */
    LED_LIFECYCLE_ERROR      /**< 初始化阶段发生不可恢复失败。 */
} LED_LifecycleStateTypeDef;

/** @brief LED Device 可理解的归一化 Port 状态。 */
typedef enum
{
    LED_PORT_OK = 0, /**< Port 操作成功。 */
    LED_PORT_ERROR,  /**< Port 发生未进一步分类的错误。 */
    LED_PORT_BUSY,   /**< Port 暂时忙，可由调用者稍后重试。 */
    LED_PORT_TIMEOUT /**< Port 操作超时。 */
} LED_PortStatusTypeDef;

/** @brief LED Device 记录的最近一次失败阶段。 */
typedef enum
{
    LED_ERROR_NONE = 0,        /**< 没有错误。 */
    LED_ERROR_INVALID_PARAM,   /**< Handle 或逻辑状态参数无效。 */
    LED_ERROR_PORT_NOT_BOUND,  /**< PortOps、Context 或必需操作未绑定。 */
    LED_ERROR_NOT_READY,       /**< 生命周期尚未就绪。 */
    LED_ERROR_PORT_INITIALIZE, /**< Port 初始化失败。 */
    LED_ERROR_PORT_SET          /**< Port 设置逻辑亮灭状态失败。 */
} LED_ErrorTypeDef;

typedef LED_PortStatusTypeDef (*LED_PortInitializeFunc)(void *context);
typedef LED_PortStatusTypeDef (*LED_PortSetStateFunc)(void *context,
                                                       LED_OnOffTypeDef state);

/** @brief LED Device 使用的底层 Port 操作表。 */
typedef struct
{
    LED_PortInitializeFunc Initialize;
    LED_PortSetStateFunc SetState;
} LED_PortOpsTypeDef;

/** @brief LED Device Handle。 */
typedef struct
{
    const LED_PortOpsTypeDef *PortOps;
    void *PortContext;
    volatile LED_LifecycleStateTypeDef LifecycleState;
    volatile LED_ErrorTypeDef ErrorCode;
    volatile LED_PortStatusTypeDef LastPortStatus;
    LED_OnOffTypeDef OnOffState;
} LED_HandleTypeDef;

LED_StatusTypeDef LED_Init(LED_HandleTypeDef *hled);
LED_StatusTypeDef LED_Set(LED_HandleTypeDef *hled, LED_OnOffTypeDef state);
LED_StatusTypeDef LED_Toggle(LED_HandleTypeDef *hled);

#endif /* LED_H */
