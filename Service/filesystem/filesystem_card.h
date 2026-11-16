/**
 ******************************************************************************
 * @file    filesystem_card.h
 * @brief   SD 卡槽的卷生命周期入口：启动、刷新、暂停和恢复（闸）；调用者是卷任务（ADR-0024、#195）。
 *
 * @details
 *          卡检测、消抖和对账由 APP 的卷任务决定时机；本头只提供“在 SD 卷锁里读写设备状态”和“挡住
 *          文件与目录入口”的机制，APP 因此不再包含 `platform_sd.h`。
 *
 *          - 启动和刷新会改 Device 状态，所以都在 SD 卷锁内执行。这不是风格问题：CubeMX 生成的
 *            `sd_diskio.c` 在读写成功之后有一个没有超时的 `while (BSP_SD_GetCardState() != MSD_OK) {}`，
 *            Platform 状态不是 READY 时 `BSP_SD_GetCardState` 返回 BUSY；设备状态若在别的任务持锁读写的中途
 *            变成非 READY，那个循环会持着 SD 卷锁空转。
 *          - 暂停和恢复是闸：暂停之后，文件和目录入口在拿锁之前等闸打开；闸只用于 SD，不挡 Flash，不挡本头
 *            的启动和刷新，也不挡 `InitSD/MountSD/UnmountSD`；已经持锁的调用不被打断。
 *          - 暂停和恢复不拿卷锁、不阻塞，可以在任何任务里调用；它们不检查调用者拒绝名单。启动和刷新要等 SD 卷锁，
 *            所以和其他会等锁的入口一样，GUI 与播放任务调用会断言停机。
 ******************************************************************************
 */

#ifndef FILESYSTEM_CARD_H
#define FILESYSTEM_CARD_H

#include <stdbool.h>
#include <stdint.h>

#include "Service/service.h"

/**
 * @brief 卡检测边沿的 ISR 回调；与 `Platform_SD_DetectCallback_t` 同形，但 APP 不必认识 Platform。
 * @param context 启动时给的上下文。
 * @note  回调在 ISR 上下文执行：只能置位、调用 xxxFromISR() 或做常数时间的事，不得记日志、访问 SDMMC 或文件系统。
 */
typedef void (*Service_Filesystem_CardDetectCallback_t)(void *context);

/**
 * @brief 一次启动或刷新之后卡的现状。失败时也尽力填写，卷任务据此对账。
 */
typedef struct
{
    bool Present;            /**< 卡检测脚现在表示槽里有卡。 */
    bool Ready;              /**< 设备 READY，可以访问逻辑块。 */
    bool Reinitialized;      /**< 本次调用把卡（重新）初始化过：换了卡，或读写失败时先把设备写成无卡、卡又回来。 */
    uint64_t CapacityBytes;  /**< 逻辑容量；`Ready` 为假时为 0。 */
    uint32_t BlockSize;      /**< 逻辑块字节数；`Ready` 为假时为 0。 */
    uint32_t DeviceError;    /**< 最近一次设备错误的阶段（诊断）；没有错误为 0。 */
    uint32_t PortStatus;     /**< 最近一次归一化的底层结果（诊断）；没有错误为 0。 */
} Service_Filesystem_CardStatusTypeDef;

Service_StatusTypeDef Service_Filesystem_StartSD(Service_Filesystem_CardDetectCallback_t detect_callback,
                                                 void *detect_context,
                                                 Service_Filesystem_CardStatusTypeDef *card);
Service_StatusTypeDef Service_Filesystem_RefreshSD(Service_Filesystem_CardStatusTypeDef *card);
void Service_Filesystem_PauseSD(void);
void Service_Filesystem_ResumeSD(void);

#endif /* FILESYSTEM_CARD_H */
