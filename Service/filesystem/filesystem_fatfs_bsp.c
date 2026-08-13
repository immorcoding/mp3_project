/**
  ******************************************************************************
  * @file    filesystem_fatfs_bsp.c
  * @brief   实现 CubeMX FatFs BSP_SD Override Seam 的 Adapter。
  *
  * @details
  *          CubeMX 在 FATFS/Target 中生成 BSP_SD_* 弱默认实现。本 Adapter 提供
  *          强定义，但不让生成的 DiskIO 源码包含 Filesystem Service。所有异步执行
  *          细节均隐藏在 filesystem_sd_transfer.c 内。
  ******************************************************************************
  */

#include "FATFS/Target/bsp_driver_sd.h"
#include "Service/filesystem/filesystem_config.h"

#include <string.h>

#include "Platform/sd/platform_sd.h"
#include "Service/filesystem/filesystem_sd_transfer.h"

/**
  * @brief  根据 Platform SD 状态实现 FatFs BSP 初始化钩子。
  * @retval MSD_OK 卡已就绪，可进行逻辑块访问。
  * @retval MSD_ERROR_SD_NOT_PRESENT 当前未检测到卡。
  * @retval MSD_ERROR 卡存在但尚未就绪。
  */
uint8_t BSP_SD_Init(void)
{
    if (Platform_SD_GetState() == PLATFORM_SD_STATE_READY)
    {
        return MSD_OK;
    }

    return Platform_SD_IsPresent() ? MSD_ERROR : MSD_ERROR_SD_NOT_PRESENT;
}

/**
  * @brief  保持 Cube BSP 中断配置契约。
  * @retval MSD_OK 卡检测与 SDMMC 回调已由 Platform SD 持有。
  */
uint8_t BSP_SD_ITConfig(void)
{
    return MSD_OK;
}

/**
  * @brief  提供由 SDMMC DMA 支持的同步 FatFs 块读取。
  * @retval MSD_OK 所有请求块均已到达 p_data。
  * @retval MSD_ERROR 请求未能以同步方式完成。
  */
uint8_t BSP_SD_ReadBlocks(uint32_t *p_data,
                          uint32_t read_address,
                          uint32_t block_count,
                          uint32_t timeout_ms)
{
    return filesystem_sd_transfer_read_blocks((uint8_t *)p_data,
                                              read_address,
                                              block_count,
                                              timeout_ms)
        ? MSD_OK
        : MSD_ERROR;
}

/**
  * @brief  提供由 SDMMC DMA 支持的同步 FatFs 块写入。
  * @retval MSD_OK 所有请求块均已提交到卡。
  * @retval MSD_ERROR 请求未能以同步方式完成。
  */
uint8_t BSP_SD_WriteBlocks(uint32_t *p_data,
                           uint32_t write_address,
                           uint32_t block_count,
                           uint32_t timeout_ms)
{
    return filesystem_sd_transfer_write_blocks((const uint8_t *)p_data,
                                               write_address,
                                               block_count,
                                               timeout_ms)
        ? MSD_OK
        : MSD_ERROR;
}

/**
  * @brief  保持带 DMA 名称的 Cube 读钩子对 FatFs 调用者仍为同步。
  * @retval MSD_OK 所有请求块均已到达 p_data。
  * @retval MSD_ERROR 请求未能以同步方式完成。
  */
uint8_t BSP_SD_ReadBlocks_DMA(uint32_t *p_data,
                              uint32_t read_address,
                              uint32_t block_count)
{
    return BSP_SD_ReadBlocks(p_data,
                             read_address,
                             block_count,
                             FILESYSTEM_FATFS_BSP_DMA_TIMEOUT_MS);
}

/**
  * @brief  保持带 DMA 名称的 Cube 写钩子对 FatFs 调用者仍为同步。
  * @retval MSD_OK 所有请求块均已提交到卡。
  * @retval MSD_ERROR 请求未能以同步方式完成。
  */
uint8_t BSP_SD_WriteBlocks_DMA(uint32_t *p_data,
                               uint32_t write_address,
                               uint32_t block_count)
{
    return BSP_SD_WriteBlocks(p_data,
                              write_address,
                              block_count,
                              FILESYSTEM_FATFS_BSP_DMA_TIMEOUT_MS);
}

/**
  * @brief  报告当前 Platform SD Interface 尚未提供擦除操作。
  * @retval MSD_ERROR 在 Platform SD 提供擦除能力前，此操作有意不支持。
  */
uint8_t BSP_SD_Erase(uint32_t start_address, uint32_t end_address)
{
    (void)start_address;
    (void)end_address;
    return MSD_ERROR;
}

/**
  * @brief  将 Platform SD 生命周期状态转换为 Cube BSP 状态。
  * @retval SD_TRANSFER_OK 卡可接受下一次传输。
  * @retval SD_TRANSFER_BUSY 卡缺失、忙或已失败。
  */
uint8_t BSP_SD_GetCardState(void)
{
    return (Platform_SD_GetState() == PLATFORM_SD_STATE_READY)
        ? SD_TRANSFER_OK
        : SD_TRANSFER_BUSY;
}

/**
  * @brief  将 Platform SD 介质快照复制到 Cube BSP 卡信息类型。
  * @param  card_info Cube FatFs DiskIO 契约要求的接收对象。
  */
void BSP_SD_GetCardInfo(BSP_SD_CardInfo *card_info)
{
    Platform_SD_InfoTypeDef info;

    if (card_info == NULL)
    {
        return;
    }

    (void)memset(card_info, 0, sizeof(*card_info));
    if (Platform_SD_GetInfo(&info) != PLATFORM_OK)
    {
        return;
    }

    card_info->CardType = info.CardType;
    card_info->CardVersion = info.CardVersion;
    card_info->BlockNbr = info.BlockCount;
    card_info->BlockSize = info.BlockSize;
    card_info->LogBlockNbr = info.BlockCount;
    card_info->LogBlockSize = info.BlockSize;
}

/**
  * @brief  通过 Platform SD 实现 Cube 卡检测查询。
  * @retval SD_PRESENT 卡检测 GPIO 采样表明已经插卡。
  * @retval SD_NOT_PRESENT 当前未检测到卡。
  */
uint8_t BSP_SD_IsDetected(void)
{
    return Platform_SD_IsPresent() ? SD_PRESENT : SD_NOT_PRESENT;
}
