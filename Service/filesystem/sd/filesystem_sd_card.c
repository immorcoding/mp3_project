/**
 ******************************************************************************
 * @file    filesystem_sd_card.c
 * @brief   SD 卡槽的启动与刷新入口：在 SD 卷锁内读写 Platform SD 的设备状态。
 *
 * @details
 *          为什么必须持 SD 卷锁：CubeMX 生成的 `FATFS/Target/sd_diskio.c` 在 `SD_read`/`SD_write` 成功之后有一个
 *          没有超时的 `while (BSP_SD_GetCardState() != MSD_OK) {}`，而 `BSP_SD_GetCardState` 在 Platform 状态
 *          不是 READY 时返回 BUSY。设备状态若在别的任务持锁读写的中途变成非 READY（刷新发现拔卡、中止停不住进
 *          ERROR），那个循环会持着 SD 卷锁空转。卷锁保证改设备状态的入口（本文件）和用设备的入口（读写）互斥，
 *          状态因此不会在持锁的读写中途被改。生成文件不能改，也不要靠改它解决。
 *
 *          本文件只依赖 Platform SD，不依赖 FreeRTOS 以外的 APP 类型；暂停和恢复（闸）在 filesystem_entry.c。
 ******************************************************************************
 */

#include "Service/filesystem/filesystem_card.h"

#include <string.h>

#include "Platform/sd/platform_sd.h"
#include "Service/filesystem/filesystem_lock.h"

/**
 * @brief 把 Platform 当前的卡状态写进调用者的结果；调用者已持 SD 卷锁。
 * @param[out] card 结果，非空。
 * @note  失败时也尽力填写：卷任务在刷新失败而卡不在时同样要卸载，需要知道卡不在。
 */
static void filesystem_sd_card_fill(Service_Filesystem_CardStatusTypeDef *card)
{
    Platform_SD_InfoTypeDef info;
    Platform_SD_DiagnosticsTypeDef diagnostics;

    card->Present = Platform_SD_IsPresent();
    card->Ready = (Platform_SD_GetState() == PLATFORM_SD_STATE_READY);
    card->CapacityBytes = 0U;
    card->BlockSize = 0U;
    card->DeviceError = 0U;
    card->PortStatus = 0U;

    if (card->Ready && (Platform_SD_GetInfo(&info) == PLATFORM_OK))
    {
        card->CapacityBytes = info.CapacityBytes;
        card->BlockSize = info.BlockSize;
    }
    if (Platform_SD_GetDiagnostics(&diagnostics) == PLATFORM_OK)
    {
        card->DeviceError = diagnostics.DeviceError;
        card->PortStatus = diagnostics.PortStatus;
    }
}

/**
 * @brief 注册卡检测回调并初始化当前介质（启动入口，只调用一次）。
 * @param[in] detect_callback 卡检测边沿到达时在 ISR 里调用的轻量通知，非空。
 * @param[in] detect_context 原样传给回调的上下文，可为 NULL。
 * @param[out] card 返回卡的现状，非空；`Reinitialized` 在卡就绪时为真（本次调用把它初始化了）。
 * @retval SERVICE_OK Platform 初始化成功；无卡同样成功，此时 `Ready` 为假。
 * @retval SERVICE_INVALID_PARAM 回调或结果指针为空。
 * @retval SERVICE_ERROR Platform 初始化失败（重复调用也是这个结果）；`card` 仍按当前状态填写，含诊断。
 * @note  拿 SD 卷锁一次（调用者不得在拒绝名单里，入口断言）。插卡初始化持锁约 186 ms，此时没有在读的音频。
 *        本函数只做 Platform 一侧：卡就绪后卷任务还要调 `InitSD()`（建立完成信号量并订阅传输事件）再 `MountSD()`，
 *        与原先 Storage Task 的次序一致。不开闸也不关闸。
 */
Service_StatusTypeDef Service_Filesystem_StartSD(Service_Filesystem_CardDetectCallback_t detect_callback,
                                                 void *detect_context,
                                                 Service_Filesystem_CardStatusTypeDef *card)
{
    Service_StatusTypeDef status;

    filesystem_lock_check_caller();
    if ((detect_callback == NULL) || (card == NULL))
    {
        return SERVICE_INVALID_PARAM;
    }

    filesystem_lock_take(SERVICE_FILESYSTEM_VOLUME_SD, FILESYSTEM_OP_START);
    status = (Platform_SD_Init(detect_callback, detect_context) == PLATFORM_OK) ? SERVICE_OK : SERVICE_ERROR;
    filesystem_sd_card_fill(card);
    card->Reinitialized = (status == SERVICE_OK) && card->Ready;
    filesystem_lock_give(SERVICE_FILESYSTEM_VOLUME_SD);
    return status;
}

/**
 * @brief 在 SD 卷锁内刷新卡的现状，给出对账需要的全部事实（刷新入口）。
 * @param[out] card 返回卡的现状，非空。
 * @retval SERVICE_OK 刷新成功。
 * @retval SERVICE_INVALID_PARAM 结果指针为空。
 * @retval SERVICE_ERROR 刷新失败（Platform 未启动，或设备刷新失败）；`card` 仍按当前状态填写，卷任务在“刷新失败
 *         但卡不在”时同样卸载。
 * @note  拿 SD 卷锁一次，所以不会和别的任务的读写并发（见文件头）；等锁的最长时间是在途调用的持锁时间，
 *        真拔卡时它很快失败返回（2.0 至 4.2 ms），故障路径约 0.6 s。卡在而设备没就绪、又没有边沿时不自动恢复。
 *        `Reinitialized` 为真表示这次刷新把卡（重新）初始化过：换了卡，或读写失败时设备被先写成无卡、卡又回来
 *        （被吞的拔出）。它由 Platform 的 INSERTED 事件得出，只按进入时的设备状态判断，所以不依赖有没有人看到过拔出。
 *        不受闸挡，所以卷任务可以在关闸期间调用。
 */
Service_StatusTypeDef Service_Filesystem_RefreshSD(Service_Filesystem_CardStatusTypeDef *card)
{
    Platform_SD_EventTypeDef event = PLATFORM_SD_EVENT_NONE;
    Service_StatusTypeDef status;

    filesystem_lock_check_caller();
    if (card == NULL)
    {
        return SERVICE_INVALID_PARAM;
    }

    filesystem_lock_take(SERVICE_FILESYSTEM_VOLUME_SD, FILESYSTEM_OP_REFRESH);
    status = (Platform_SD_Process(&event) == PLATFORM_OK) ? SERVICE_OK : SERVICE_ERROR;
    filesystem_sd_card_fill(card);
    card->Reinitialized = (status == SERVICE_OK) && (event == PLATFORM_SD_EVENT_INSERTED);
    filesystem_lock_give(SERVICE_FILESYSTEM_VOLUME_SD);
    return status;
}
