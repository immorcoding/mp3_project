/**
  ******************************************************************************
  * @file    spike_unplug.c
  * @brief   拔卡时序时间线的状态与读写（spike #191）。
  ******************************************************************************
  */

#include "APP/tasks/foreground/spike_unplug.h"

#include "Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/task.h"

/** @brief 前台任务那次读取迟迟没有失败时，最多等多久就把时间线打出来（毫秒）。 */
#define SPIKE_UNPLUG_FOREGROUND_WAIT_MS 3000U

/** @brief 阶段未到达的标记。 */
#define SPIKE_UNPLUG_NOT_REACHED UINT32_MAX

static volatile bool spike_unplug_armed;
static volatile TickType_t spike_unplug_t0;
static volatile uint32_t spike_unplug_stage_ms[SPIKE_UNPLUG_STAGE_COUNT];
static volatile uint32_t spike_unplug_unmount_status;
static volatile SpikeUnplugViaTypeDef spike_unplug_via;
static volatile bool spike_unplug_foreground_set;
static volatile uint32_t spike_unplug_foreground_status;
static volatile uint32_t spike_unplug_foreground_ms;
static volatile uint32_t spike_unplug_foreground_hold_us;

/**
 * @brief 计算相对检测中断经过的毫秒数。
 * @return 毫秒；调用者已确认时间线待命。
 */
static uint32_t spike_unplug_elapsed_ms(void)
{
    return (uint32_t)(xTaskGetTickCount() - spike_unplug_t0) * (uint32_t)portTICK_PERIOD_MS;
}

/**
 * @brief 检测中断里记下第一个边沿的 tick，并让时间线待命（ISR 调用）。
 * @note  只存 tick，不记日志；时间线已待命时忽略后续抖动边沿，所以起点是第一个边沿。
 */
void spike_unplug_note_detect_isr(void)
{
    uint32_t index;

    if (spike_unplug_armed)
    {
        return;
    }

    spike_unplug_t0 = xTaskGetTickCountFromISR();
    for (index = 0U; index < (uint32_t)SPIKE_UNPLUG_STAGE_COUNT; index++)
    {
        spike_unplug_stage_ms[index] = SPIKE_UNPLUG_NOT_REACHED;
    }
    spike_unplug_foreground_set = false;
    spike_unplug_armed = true;
}

/**
 * @brief 记下这次拔出收尾是由哪条判断触发的。
 * @param[in] via 触发来源。
 * @note  时间线未待命时忽略；Storage Task 在开始收尾之前调用。
 */
void spike_unplug_set_via(SpikeUnplugViaTypeDef via)
{
    if (spike_unplug_armed)
    {
        spike_unplug_via = via;
    }
}

/**
 * @brief 记下某个阶段相对检测中断的毫秒。
 * @param[in] stage 到达的阶段。
 * @note  时间线未待命（例如启动时的初始挂载）时忽略。
 */
void spike_unplug_note(SpikeUnplugStageTypeDef stage)
{
    if (!spike_unplug_armed || ((uint32_t)stage >= (uint32_t)SPIKE_UNPLUG_STAGE_COUNT))
    {
        return;
    }

    spike_unplug_stage_ms[stage] = spike_unplug_elapsed_ms();
}

/**
 * @brief 记下卸载返回的时刻和状态。
 * @param[in] status 卸载返回的 Service 状态值。
 */
void spike_unplug_note_unmount_done(uint32_t status)
{
    spike_unplug_unmount_status = status;
    spike_unplug_note(SPIKE_UNPLUG_STAGE_UNMOUNT_DONE);
}

/**
 * @brief 取消时间线：这次边沿不是拔卡（插卡，或消抖后没有变化）。
 */
void spike_unplug_cancel(void)
{
    spike_unplug_armed = false;
}

/**
 * @brief 前台任务记下拔卡后第一次失败的读取。
 * @param[in] status 该次读取返回的 Service 状态值（非 OK）。
 * @param[in] hold_us 该次读取持锁多久，微秒；在途读取等 DMA 超时时可达数百毫秒。
 * @note  只记第一次；时间线未待命时忽略。
 */
void spike_unplug_note_foreground_read(uint32_t status, uint32_t hold_us)
{
    if (!spike_unplug_armed || spike_unplug_foreground_set)
    {
        return;
    }

    spike_unplug_foreground_ms = spike_unplug_elapsed_ms();
    spike_unplug_foreground_status = status;
    spike_unplug_foreground_hold_us = hold_us;
    spike_unplug_foreground_set = true;
}

/**
 * @brief 前台任务取走一次完整的时间线。
 * @param[out] report 接收时间线。
 * @return true 卸载已返回，且前台读取已记下或等够了 3 s；取走后重新待命。false 尚未完整。
 */
bool spike_unplug_take_report(SpikeUnplugReportTypeDef *report)
{
    uint32_t index;

    if (!spike_unplug_armed ||
        (spike_unplug_stage_ms[SPIKE_UNPLUG_STAGE_UNMOUNT_DONE] == SPIKE_UNPLUG_NOT_REACHED))
    {
        return false;
    }
    if (!spike_unplug_foreground_set &&
        (spike_unplug_elapsed_ms() < SPIKE_UNPLUG_FOREGROUND_WAIT_MS))
    {
        return false;
    }

    for (index = 0U; index < (uint32_t)SPIKE_UNPLUG_STAGE_COUNT; index++)
    {
        report->StageMs[index] = spike_unplug_stage_ms[index];
    }
    report->Via = spike_unplug_via;
    report->UnmountStatus = spike_unplug_unmount_status;
    report->ForegroundRead = spike_unplug_foreground_set;
    report->ForegroundStatus = spike_unplug_foreground_status;
    report->ForegroundMs = spike_unplug_foreground_ms;
    report->ForegroundHoldUs = spike_unplug_foreground_hold_us;
    spike_unplug_armed = false;
    return true;
}
