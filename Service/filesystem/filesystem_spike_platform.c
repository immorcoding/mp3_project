/**
 * @file filesystem_spike_platform.c
 * @brief 需要文件锁的 Platform 调用（spike #191，仅固件）：热插拔刷新、Flash 块计数、带前后计数的回收。
 *
 * @details
 *          这三个入口都直接进 Platform，但和文件接口共享同一个设备（SD 卡或 NOR），所以也走卷锁：
 *          SD 的 `Platform_SD_Process`/`Refresh` 在无卡时 DeInit 端口、插卡时 Init，不能和别的任务的
 *          DMA 传输并发；FTL 的块计数和回收推进与读写共用同一组 RAM 表。主机测试不编译本文件。
 *          每个入口拿一次卷锁、返回前放，不和其他公开函数互相调用，所以不会重复拿锁。
 */

#include "Service/filesystem/filesystem_spike.h"

#include <stddef.h>

#include "Service/filesystem/filesystem_lock.h"
#include "Service/filesystem/flash/filesystem_flash_transfer.h"

/** @brief FTL 一个数据块的字节数，与 FTL 的 4096 B 擦除块一致。 */
#define FILESYSTEM_SPIKE_FTL_BLOCK_BYTES 4096U
/** @brief FTL 一个逻辑组包含的逻辑扇区数。 */
#define FILESYSTEM_SPIKE_FTL_GROUP_SECTORS 7U
/** @brief FTL 分区开头和结尾各占一个卷头块。 */
#define FILESYSTEM_SPIKE_FTL_HEADER_BLOCKS 2U

/**
 * @brief 在已持有 Flash 卷锁时读出块计数与几何。
 * @param[out] blocks 接收快照。
 * @return true 读取成功；false 卷未绑定。
 * @note  数据块数由 FTL 分区字节数推出，预留块数 = 数据块数 − 逻辑组数（逻辑扇区数除以 7）。
 */
static bool filesystem_spike_read_blocks_locked(Service_Filesystem_SpikeFlashBlocksTypeDef *blocks)
{
    Platform_Flash_VolumeDiagnosticsTypeDef diagnostics;
    Platform_Flash_VolumeInfoTypeDef info;
    uint32_t groups;

    if ((Platform_Flash_GetVolumeDiagnostics(&diagnostics) != PLATFORM_OK) ||
        (Platform_Flash_GetVolumeInfo(&info) != PLATFORM_OK))
    {
        return false;
    }

    groups = info.SectorCount / FILESYSTEM_SPIKE_FTL_GROUP_SECTORS;
    blocks->FreeBlocks = diagnostics.FreeBlocks;
    blocks->StaleBlocks = diagnostics.StaleBlocks;
    blocks->ValidGroups = diagnostics.ValidGroups;
    blocks->SessionErases = diagnostics.SessionErases;
    blocks->DataBlocks = (info.PhysicalBytes / FILESYSTEM_SPIKE_FTL_BLOCK_BYTES) - FILESYSTEM_SPIKE_FTL_HEADER_BLOCKS;
    blocks->ReserveBlocks = (blocks->DataBlocks > groups) ? (blocks->DataBlocks - groups) : 0U;
    return true;
}

/**
 * @brief 在 SD 卷锁内做一次已消抖的热插拔刷新，等价于 `Platform_SD_Process()`。
 * @param[out] event 接收本次状态变化事件。
 * @return Platform 状态，同 `Platform_SD_Process()`。
 * @note  入口拿 SD 卷锁、操作名 `HotplugRefresh` 进锁统计，所以能看到插卡初始化持锁多久；
 *        刷新会等在途的 SD 传输先返回。调用者不得已持有任何文件锁。
 */
Platform_StatusTypeDef Service_Filesystem_SpikeHotplugProcess(Platform_SD_EventTypeDef *event)
{
    Platform_StatusTypeDef status;

    filesystem_lock_acquire(SERVICE_FILESYSTEM_VOLUME_SD, "HotplugRefresh");
    status = Platform_SD_Process(event);
    filesystem_lock_release(SERVICE_FILESYSTEM_VOLUME_SD);
    return status;
}

/**
 * @brief 读 Flash 卷的块计数与几何。
 * @param[out] blocks 接收快照。
 * @return true 读取成功；false 卷未绑定或参数为空。
 * @note  入口拿 Flash 卷锁，操作名 `FlashBlocks`。
 */
bool Service_Filesystem_SpikeFlashBlocks(Service_Filesystem_SpikeFlashBlocksTypeDef *blocks)
{
    bool ok = false;

    if (blocks != NULL)
    {
        filesystem_lock_acquire(SERVICE_FILESYSTEM_VOLUME_FLASH, "FlashBlocks");
        ok = filesystem_spike_read_blocks_locked(blocks);
        filesystem_lock_release(SERVICE_FILESYSTEM_VOLUME_FLASH);
    }
    return ok;
}

/**
 * @brief 在同一次 Flash 卷锁内读回收前后的块计数并执行一次回收。
 * @param[out] before 回收前的快照，可为空。
 * @param[out] after 回收后的快照，可为空。
 * @return 回收的 Service 状态，同 `Service_Filesystem_ReclaimFlash()`。
 * @note  入口拿 Flash 卷锁，操作名 `ReclaimFlash`，所以持锁时长包含回收本身加两次计数读取（微秒级）。
 *        回收是否真擦除看两个快照 `SessionErases` 的差值：FTL 在擦除并校验完一块后才加一。
 */
Service_StatusTypeDef Service_Filesystem_SpikeReclaimFlashMeasured(
    Service_Filesystem_SpikeFlashBlocksTypeDef *before,
    Service_Filesystem_SpikeFlashBlocksTypeDef *after)
{
    Service_Filesystem_SpikeFlashBlocksTypeDef local_before = {0};
    Service_Filesystem_SpikeFlashBlocksTypeDef local_after = {0};
    Service_StatusTypeDef status;

    filesystem_lock_acquire(SERVICE_FILESYSTEM_VOLUME_FLASH, "ReclaimFlash");
    (void)filesystem_spike_read_blocks_locked(&local_before);
    status = filesystem_flash_transfer_reclaim();
    (void)filesystem_spike_read_blocks_locked(&local_after);
    filesystem_lock_release(SERVICE_FILESYSTEM_VOLUME_FLASH);

    if (before != NULL)
    {
        *before = local_before;
    }
    if (after != NULL)
    {
        *after = local_after;
    }
    return status;
}
