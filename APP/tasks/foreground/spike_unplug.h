/**
  ******************************************************************************
  * @file    spike_unplug.h
  * @brief   拔卡时序时间线（spike #191）：Storage Task 在各阶段记毫秒，前台任务读出并打印。
  *
  * @details
  *          所有时刻都相对检测中断那一刻的 tick（ISR 里只存 tick，不记日志、不格式化）。阶段依次是：
  *          消抖结束、音频流关闭、曲库作废、进入卸载、卸载返回。前台任务那次读取返回的状态、时刻和
  *          进行中读取持锁多久由前台任务自己记入。只覆盖拔卡；插卡事件会取消时间线。
  *          时间线单次使用：打印后重新待命。ISR 与两个任务之间不加锁，只传 32 位值，spike 可接受。
  ******************************************************************************
  */

#ifndef SPIKE_UNPLUG_H
#define SPIKE_UNPLUG_H

#include <stdbool.h>
#include <stdint.h>

/** @brief 拔卡路径上被记录时刻的阶段。 */
typedef enum
{
    SPIKE_UNPLUG_STAGE_DEBOUNCE_END = 0U, /**< 消抖结束，Storage 开始处理这次插拔事件。 */
    SPIKE_UNPLUG_STAGE_STREAM_CLOSED,     /**< 音频流已关闭。 */
    SPIKE_UNPLUG_STAGE_CATALOG_INVALID,   /**< 曲库已作废。 */
    SPIKE_UNPLUG_STAGE_UNMOUNT_ENTER,     /**< 进入卸载（卸载要先拿 SD 卷锁，可能在等在途读取）。 */
    SPIKE_UNPLUG_STAGE_UNMOUNT_DONE,      /**< 卸载返回。 */
    SPIKE_UNPLUG_STAGE_COUNT              /**< 阶段个数，不是真实阶段。 */
} SpikeUnplugStageTypeDef;

/**
 * @brief 拔出收尾是怎么被触发的（板测发现，#191）。
 * @note  传输在途时拔卡，设备层把状态直接写成 NOT_PRESENT，之后 `Platform_SD_Process` 比较前后状态相同，
 *        不给 REMOVED 事件（被吞）。所以拔出判断另加“卡不在且卷还挂着”这一条，两条路径要分开计数。
 */
typedef enum
{
    SPIKE_UNPLUG_VIA_STATE = 0U,   /**< `Platform_SD_Process` 给出 REMOVED（状态变化）。 */
    SPIKE_UNPLUG_VIA_PRESENCE,     /**< 事件被吞：卡检测电平显示不在，且卷还挂着。 */
    SPIKE_UNPLUG_VIA_INSERT        /**< 只收到 INSERTED 而卷还挂着（换卡，或拔出被吞且在消抖内插回）。 */
} SpikeUnplugViaTypeDef;

/** @brief 一次完整的拔卡时间线，所有时刻为相对检测中断的毫秒。 */
typedef struct
{
    SpikeUnplugViaTypeDef Via;                  /**< 拔出收尾由哪条判断触发。 */
    uint32_t StageMs[SPIKE_UNPLUG_STAGE_COUNT]; /**< 各阶段时刻；未到达的阶段为 UINT32_MAX。 */
    uint32_t UnmountStatus;                     /**< 卸载返回的 Service 状态值。 */
    bool ForegroundRead;                        /**< 前台任务是否记到了拔卡后第一次失败的读取。 */
    uint32_t ForegroundStatus;                  /**< 该次读取返回的 Service 状态值。 */
    uint32_t ForegroundMs;                      /**< 该次读取返回的时刻。 */
    uint32_t ForegroundHoldUs;                  /**< 该次读取持锁多久，微秒。 */
} SpikeUnplugReportTypeDef;

void spike_unplug_note_detect_isr(void);
void spike_unplug_set_via(SpikeUnplugViaTypeDef via);
void spike_unplug_note(SpikeUnplugStageTypeDef stage);
void spike_unplug_note_unmount_done(uint32_t status);
void spike_unplug_cancel(void);
void spike_unplug_note_foreground_read(uint32_t status, uint32_t hold_us);
bool spike_unplug_take_report(SpikeUnplugReportTypeDef *report);

#endif /* SPIKE_UNPLUG_H */
