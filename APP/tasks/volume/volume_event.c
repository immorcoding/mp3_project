/**
 ******************************************************************************
 * @file    volume_event.c
 * @brief   卷事件实现：每卷一个状态字，先更新再叫醒订阅者（见 volume_event.h）。
 ******************************************************************************
 */

#include "APP/tasks/volume/volume_event.h"
#include "APP/tasks/volume/volume_task_config.h"

/** @brief 状态字的最低位是挂载位，其余 31 位是卷序号。 */
#define VOLUME_EVENT_MOUNTED_BIT 1U
#define VOLUME_EVENT_SEQUENCE_SHIFT 1U
#define VOLUME_EVENT_SEQUENCE_MASK 0x7FFFFFFFU

/** @brief 一个订阅者：被叫醒时对这个任务的这个通知槽置这些位。 */
typedef struct
{
    TaskHandle_t Task;
    UBaseType_t NotifyIndex;
    uint32_t Flag;
} Volume_SubscriberTypeDef;

/** @brief 每卷一个状态字；每个字只有一个写者（SD：卷任务，Flash：Flash 启动任务），读写都不拿锁。 */
static volatile uint32_t volume_event_words[VOLUME_ID_COUNT];

/** @brief 边沿时刻（诊断）：发布者记下的“这次对账的第一个边沿”，以及随上一次发布抄出去的那一份。只有发布者写，读不拿锁。 */
static volatile uint32_t volume_event_pending_edge[VOLUME_ID_COUNT];
static volatile bool volume_event_pending_edge_valid[VOLUME_ID_COUNT];
static volatile uint32_t volume_event_published_edge_tick[VOLUME_ID_COUNT];
static volatile bool volume_event_published_edge_valid[VOLUME_ID_COUNT];

/** @brief 订阅表。登记在临界区里先填表项再加计数，发布者读到计数时表项一定已写完；登记只增不减。 */
static Volume_SubscriberTypeDef volume_event_subscribers[VOLUME_SUBSCRIBER_MAX];
static volatile uint32_t volume_event_subscriber_count;

/**
 * @brief  读一个卷的状态。
 * @param[in] volume 卷。
 * @return 是否挂载与卷序号；卷号越界返回未挂载、序号 0。
 * @note   任何任务可调用，读一个 32 位字，不拿锁。
 */
Volume_StatusTypeDef volume_status(Volume_IdTypeDef volume)
{
    Volume_StatusTypeDef status = {false, 0U};
    uint32_t word;

    if ((unsigned)volume >= (unsigned)VOLUME_ID_COUNT)
    {
        return status;
    }

    word = volume_event_words[volume];
    status.Mounted = (word & VOLUME_EVENT_MOUNTED_BIT) != 0U;
    status.Sequence = word >> VOLUME_EVENT_SEQUENCE_SHIFT;
    return status;
}

/**
 * @brief  登记一个订阅者：之后每个卷事件都会对 task 的 notify_index 槽置 flag 位。
 * @param[in] task 订阅者任务句柄，不为空；通常是调用者自己（xTaskGetCurrentTaskHandle()）。
 * @param[in] notify_index 通知槽。
 * @param[in] flag 要置的标志位，不为 0。
 * @return 登记成功（含完全相同的登记已存在）为真；任务为空、标志为 0 或超过 `VOLUME_SUBSCRIBER_MAX` 为假。
 * @note   在自己任务入口调用，**登记之后先用 `volume_status()` 比对一次序号**：登记之前发布的事件不会补叫醒，
 *         但状态字已经是最新的，所以晚登记不会漏。两个卷的事件都会叫醒同一个订阅者，订阅者自己看关心哪个卷。
 */
bool volume_subscribe(TaskHandle_t task, UBaseType_t notify_index, uint32_t flag)
{
    bool accepted = false;
    uint32_t count;

    if ((task == NULL) || (flag == 0U))
    {
        return false;
    }

    taskENTER_CRITICAL();
    count = volume_event_subscriber_count;
    for (uint32_t index = 0U; index < count; index++)
    {
        if ((volume_event_subscribers[index].Task == task) &&
            (volume_event_subscribers[index].NotifyIndex == notify_index) &&
            (volume_event_subscribers[index].Flag == flag))
        {
            accepted = true;
            break;
        }
    }
    if (!accepted && (count < VOLUME_SUBSCRIBER_MAX))
    {
        volume_event_subscribers[count].Task = task;
        volume_event_subscribers[count].NotifyIndex = notify_index;
        volume_event_subscribers[count].Flag = flag;
        volume_event_subscriber_count = count + 1U; /* 表项写完才加计数。 */
        accepted = true;
    }
    taskEXIT_CRITICAL();

    return accepted;
}

/**
 * @brief  记下一次对账的第一个卡检测边沿的 tick（诊断，随之后的卷事件发布）。
 * @param[in] volume 卷；越界什么都不做。
 * @param[in] tick 边沿时刻（tick）。
 * @note   只由该卷的发布者调用（SD：卷任务），在这次对账的第一个边沿；之后每个事件都带着它，直到下一次对账记新的。
 */
void volume_event_note_edge(Volume_IdTypeDef volume, uint32_t tick)
{
    if ((unsigned)volume >= (unsigned)VOLUME_ID_COUNT)
    {
        return;
    }

    volume_event_pending_edge[volume] = tick;
    volume_event_pending_edge_valid[volume] = true;
}

/**
 * @brief  读最近一次发布所属的边沿 tick。
 * @param[in] volume 卷。
 * @param[out] tick 接收边沿 tick；返回假时不改。
 * @return 读到为真。没有边沿（开机挂载、从未记过）、读取期间又发布了新事件、卷号越界或 tick 为空为假。
 * @note   任何任务可调用，不拿锁：先读状态字，再读边沿，再读一次状态字，两次状态字不同说明读的时候发布了新事件，放弃。
 */
bool volume_event_published_edge(Volume_IdTypeDef volume, uint32_t *tick)
{
    uint32_t before;
    uint32_t value;
    bool valid;

    if (((unsigned)volume >= (unsigned)VOLUME_ID_COUNT) || (tick == NULL))
    {
        return false;
    }

    before = volume_event_words[volume];
    value = volume_event_published_edge_tick[volume];
    valid = volume_event_published_edge_valid[volume];
    if (!valid || (volume_event_words[volume] != before))
    {
        return false;
    }

    *tick = value;
    return true;
}

/**
 * @brief  逐个叫醒订阅者：对其登记的槽置标志位，不阻塞、不等回话。
 */
static void volume_event_wake_subscribers(void)
{
    const uint32_t count = volume_event_subscriber_count;

    for (uint32_t index = 0U; index < count; index++)
    {
        (void)xTaskNotifyIndexed(volume_event_subscribers[index].Task,
                                 volume_event_subscribers[index].NotifyIndex,
                                 volume_event_subscribers[index].Flag,
                                 eSetBits);
    }
}

/**
 * @brief  把一个卷的状态字更新为“序号加 1，挂载位为 mounted”，然后叫醒订阅者。
 * @param[in] volume 卷，须已通过范围检查。
 * @param[in] mounted 新的挂载位。
 * @return 新的卷序号。
 * @note   先写状态字再叫醒（volatile 写在通知之前完成；通知本身进临界区，起到屏障作用）。
 */
static uint32_t volume_event_publish(Volume_IdTypeDef volume, bool mounted)
{
    const uint32_t sequence = ((volume_event_words[volume] >> VOLUME_EVENT_SEQUENCE_SHIFT) + 1U) &
                              VOLUME_EVENT_SEQUENCE_MASK;

    /* 先抄边沿再写状态字：叫醒之后订阅者读到的边沿一定属于这次事件。 */
    volume_event_published_edge_tick[volume] = volume_event_pending_edge[volume];
    volume_event_published_edge_valid[volume] = volume_event_pending_edge_valid[volume];
    volume_event_words[volume] = (sequence << VOLUME_EVENT_SEQUENCE_SHIFT) |
                                 (mounted ? VOLUME_EVENT_MOUNTED_BIT : 0U);
    volume_event_wake_subscribers();
    return sequence;
}

/**
 * @brief  发布“卷就绪”：该卷序号加 1、挂载位置 1，然后叫醒订阅者。
 * @param[in] volume 卷；该卷的发布者唯一（SD：卷任务，Flash：Flash 启动任务）。
 * @return 新的卷序号；卷号越界返回 0 且什么都不做。
 */
uint32_t volume_publish_ready(Volume_IdTypeDef volume)
{
    return ((unsigned)volume < (unsigned)VOLUME_ID_COUNT) ? volume_event_publish(volume, true) : 0U;
}

/**
 * @brief  发布“卷消失”：该卷序号加 1、挂载位清 0，然后叫醒订阅者。
 * @param[in] volume 卷；该卷的发布者唯一。
 * @return 新的卷序号；卷号越界返回 0 且什么都不做。
 * @note   调用者必须已经完成卸载（该卷全部 Token 已失效、槽已释放）：卷任务不等订阅者，
 *         订阅者晚点收尾也碰不到旧卡，靠的就是这个顺序。
 */
uint32_t volume_publish_gone(Volume_IdTypeDef volume)
{
    return ((unsigned)volume < (unsigned)VOLUME_ID_COUNT) ? volume_event_publish(volume, false) : 0U;
}
