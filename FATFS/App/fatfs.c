/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file   fatfs.c
  * @brief  Code for fatfs applications
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
#include <string.h>

#include "FATFS/Target/bsp_driver_sd.h"
#include "Platform/sd/platform_sd.h"
/* USER CODE END Header */
#include "fatfs.h"

uint8_t retSD;    /* Return value for SD */
char SDPath[4];   /* SD logical drive path */
FATFS SDFatFS;    /* File system object for SD logical drive */
FIL SDFile;       /* File object for SD */
uint8_t retUSER;    /* Return value for USER */
char USERPath[4];   /* USER logical drive path */
FATFS USERFatFS;    /* File system object for USER logical drive */
FIL USERFile;       /* File object for USER */

/* USER CODE BEGIN Variables */

/* USER CODE END Variables */

void MX_FATFS_Init(void)
{
  /*## FatFS: Link the SD driver ###########################*/
  retSD = FATFS_LinkDriver(&SD_Driver, SDPath);
  /*## FatFS: Link the USER driver ###########################*/
  retUSER = FATFS_LinkDriver(&USER_Driver, USERPath);

  /* USER CODE BEGIN Init */
  /* additional user code for init */
  /* USER CODE END Init */
}

/* USER CODE BEGIN Application */
/**
  * @brief  将 Platform SD 调用结果转换为 Cube FatFs BSP 状态。
  * @param  status Platform SD 的调用返回值。
  * @retval MSD_OK Platform 操作成功。
  * @retval MSD_ERROR_SD_NOT_PRESENT 当前没有检测到 SD 卡。
  * @retval MSD_ERROR Platform 操作失败但介质仍被检测到。
  */
static uint8_t fatfs_sd_platform_status(Platform_StatusTypeDef status)
{
    if (status == PLATFORM_OK)
    {
        return MSD_OK;
    }

    return Platform_SD_IsPresent() ? MSD_ERROR : MSD_ERROR_SD_NOT_PRESENT;
}

/**
  * @brief  让 FatFs 检查已经由 Storage Task 初始化的 Platform SD 状态。
  * @retval MSD_OK 当前 Platform SD 已就绪。
  * @retval MSD_ERROR_SD_NOT_PRESENT 当前没有插卡。
  * @retval MSD_ERROR SD 尚未初始化或处于错误状态。
  * @note   不调用 Platform_SD_Init()，因为卡检测、消抖和生命周期只能由
  *         Storage Task 管理。这样 disk_initialize() 不会绕过 Platform。
  */
uint8_t BSP_SD_Init(void)
{
    Platform_SD_StateTypeDef state = Platform_SD_GetState();

    if (state == PLATFORM_SD_STATE_READY)
    {
        return MSD_OK;
    }

    return Platform_SD_IsPresent() ? MSD_ERROR : MSD_ERROR_SD_NOT_PRESENT;
}

/**
  * @brief  保持 Cube FatFs BSP 接口兼容。
  * @retval MSD_OK 卡检测 EXTI 已由 Platform SD 在 Storage Task 初始化时注册。
  */
uint8_t BSP_SD_ITConfig(void)
{
    return MSD_OK;
}

/**
  * @brief  通过 Platform SD 同步读取连续逻辑块。
  * @param  pData 接收数据的扇区缓冲区。
  * @param  ReadAddr 起始逻辑块地址。
  * @param  NumOfBlocks 连续逻辑块数量。
  * @param  Timeout Cube BSP 超时参数；当前 Platform 使用其固定同步超时策略。
  * @retval Cube FatFs BSP 状态。
  */
uint8_t BSP_SD_ReadBlocks(uint32_t *pData,
                          uint32_t ReadAddr,
                          uint32_t NumOfBlocks,
                          uint32_t Timeout)
{
    (void)Timeout;

    if ((pData == NULL) || (NumOfBlocks == 0U))
    {
        return MSD_ERROR;
    }

    return fatfs_sd_platform_status(Platform_SD_ReadBlocks((uint8_t *)pData,
                                                            ReadAddr,
                                                            NumOfBlocks));
}

/**
  * @brief  通过 Platform SD 同步写入连续逻辑块。
  * @param  pData 提供数据的扇区缓冲区。
  * @param  WriteAddr 起始逻辑块地址。
  * @param  NumOfBlocks 连续逻辑块数量。
  * @param  Timeout Cube BSP 超时参数；当前 Platform 使用其固定同步超时策略。
  * @retval Cube FatFs BSP 状态。
  */
uint8_t BSP_SD_WriteBlocks(uint32_t *pData,
                           uint32_t WriteAddr,
                           uint32_t NumOfBlocks,
                           uint32_t Timeout)
{
    (void)Timeout;

    if ((pData == NULL) || (NumOfBlocks == 0U))
    {
        return MSD_ERROR;
    }

    return fatfs_sd_platform_status(Platform_SD_WriteBlocks((const uint8_t *)pData,
                                                             WriteAddr,
                                                             NumOfBlocks));
}

/**
  * @brief  阻止 Cube 默认 BSP 直接绕过 Platform 发起 DMA 读取。
  * @retval MSD_ERROR 当前同步 FatFs Bridge 不支持 DMA。
  * @note   DMA 将在 SDMMC IRQ、缓存和 Storage Task 通知链路完整后单独接入。
  */
uint8_t BSP_SD_ReadBlocks_DMA(uint32_t *pData,
                              uint32_t ReadAddr,
                              uint32_t NumOfBlocks)
{
    (void)pData;
    (void)ReadAddr;
    (void)NumOfBlocks;
    return MSD_ERROR;
}

/**
  * @brief  阻止 Cube 默认 BSP 直接绕过 Platform 发起 DMA 写入。
  * @retval MSD_ERROR 当前同步 FatFs Bridge 不支持 DMA。
  */
uint8_t BSP_SD_WriteBlocks_DMA(uint32_t *pData,
                               uint32_t WriteAddr,
                               uint32_t NumOfBlocks)
{
    (void)pData;
    (void)WriteAddr;
    (void)NumOfBlocks;
    return MSD_ERROR;
}

/**
  * @brief  拒绝尚未由 Platform SD 提供的块擦除接口。
  * @retval MSD_ERROR 当前 Platform SD 不公开擦除操作。
  */
uint8_t BSP_SD_Erase(uint32_t StartAddr, uint32_t EndAddr)
{
    (void)StartAddr;
    (void)EndAddr;
    return MSD_ERROR;
}

/**
  * @brief  查询 Platform SD 是否已经完成同步块操作并回到 READY。
  * @retval SD_TRANSFER_OK 当前可以访问逻辑块。
  * @retval SD_TRANSFER_BUSY 介质未就绪、正忙或发生错误。
  */
uint8_t BSP_SD_GetCardState(void)
{
    return (Platform_SD_GetState() == PLATFORM_SD_STATE_READY)
        ? SD_TRANSFER_OK
        : SD_TRANSFER_BUSY;
}

/**
  * @brief  将 Platform 缓存的卡信息复制为 Cube FatFs BSP 信息结构。
  * @param  CardInfo 接收卡信息的对象；为空时直接返回。
  * @note   Platform 不保存 HAL 特有的 Class、RCA 和速度字段，因此这些字段清零。
  */
void BSP_SD_GetCardInfo(BSP_SD_CardInfo *CardInfo)
{
    Platform_SD_InfoTypeDef info;

    if (CardInfo == NULL)
    {
        return;
    }

    (void)memset(CardInfo, 0, sizeof(*CardInfo));

    if (Platform_SD_GetInfo(&info) != PLATFORM_OK)
    {
        return;
    }

    CardInfo->CardType = info.CardType;
    CardInfo->CardVersion = info.CardVersion;
    CardInfo->BlockNbr = info.BlockCount;
    CardInfo->BlockSize = info.BlockSize;
    CardInfo->LogBlockNbr = info.BlockCount;
    CardInfo->LogBlockSize = info.BlockSize;
}

/**
  * @brief  读取 Platform SD 当前的物理卡检测状态。
  * @retval SD_PRESENT 当前检测到 SD 卡。
  * @retval SD_NOT_PRESENT 当前未检测到 SD 卡，或 Platform 尚未完成绑定。
  */
uint8_t BSP_SD_IsDetected(void)
{
    return Platform_SD_IsPresent() ? SD_PRESENT : SD_NOT_PRESENT;
}

/* USER CODE END Application */
