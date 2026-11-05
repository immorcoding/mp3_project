/**
 ******************************************************************************
 * @file    volume_event.h
 * @brief   卷事件：每卷一个状态字（是否挂载加递增的卷序号），变化时先更新再叫醒订阅者。
 *
 * @details
 *          卷事件只有“卷就绪”和“卷消失”两种，形式是叫醒加卷序号（#195 第 3、4 条）。每个事件让该卷的序号加 1，
 *          所以快速拔插（卸载再挂载）序号跳 2。订阅者被叫醒后比对自己上次见到的序号：不同就是“变过了”，
 *          处理方式是“旧的全部作废，再看现状”，不需要知道中间发生了几次。
 *
 *          - 状态字每卷一个，读写都不拿锁：一个 32 位字，Cortex-M7 上读写原子。**每个字只有一个写者**
 *            （SD 的字只有卷任务写，Flash 的字只有 Flash 启动任务写），所以发布不需要锁，也没有读-改-写的竞争。
 *          - 先更新再叫醒：状态字写完之后才逐个通知订阅者，被叫醒的任务读到的一定是新状态。
 *          - 订阅者在自己任务入口用一行登记（任务句柄、通知槽、标志位），登记后先比对一次序号：
 *            登记之前发布的事件不会补叫醒，但状态字已经是最新的，所以晚登记不会漏。
 *          - 卷任务不等订阅者：叫醒是 `xTaskNotifyIndexed(eSetBits)`，不阻塞、不等回话。
 *          - 订阅者上限是配置常量 `VOLUME_SUBSCRIBER_MAX`。
 *
 *          卷序号只回答“卷变过没有”，不用来判断现在能不能调文件接口（调用者看文件接口的返回值，
 *          `NO_MEDIA`、`NOT_READY`、`INVALID_HANDLE` 不是文件错误，先看卷序号）。
 ******************************************************************************
 */

#ifndef VOLUME_EVENT_H
#define VOLUME_EVENT_H

#include <stdbool.h>
#include <stdint.h>

#include "Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/task.h"

/** @brief 有卷事件的卷。 */
typedef enum
{
    VOLUME_ID_SD = 0U,
    VOLUME_ID_FLASH,
    VOLUME_ID_COUNT
} Volume_IdTypeDef;

/** @brief 一个卷在某一刻的状态：是否挂载加卷序号（开机为未挂载、序号 0）。 */
typedef struct
{
    bool Mounted;       /**< 卷现在是否挂载。 */
    uint32_t Sequence;  /**< 卷序号：每发布一次卷就绪或卷消失加 1（31 位，回绕后继续）。 */
} Volume_StatusTypeDef;

/* 任何任务调用，读一个字，不拿锁。 */
Volume_StatusTypeDef volume_status(Volume_IdTypeDef volume);

/* 订阅者在自己任务入口调用一次。 */
bool volume_subscribe(TaskHandle_t task, UBaseType_t notify_index, uint32_t flag);

/* 发布者调用：SD 只有卷任务，Flash 只有 Flash 启动任务。不得从 ISR 调用。 */
uint32_t volume_publish_ready(Volume_IdTypeDef volume);
uint32_t volume_publish_gone(Volume_IdTypeDef volume);

#endif /* VOLUME_EVENT_H */
