/**
  ******************************************************************************
  * @file    sd.c
  * @brief   SD 卡块设备状态机、范围校验和错误处理实现。
  *
  * @details
  *          本文件不包含 STM32 HAL 类型。所有硬件访问均通过句柄中的
  *          SDCard_PortOpsTypeDef 完成，使相同 Device Implementation 可以
  *          绑定 SDMMC、SPI SD 或测试 Adapter。
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "Components/sd/sd.h"
#include "Components/sd/sd_config.h"

#include <stddef.h>
#include <string.h>

/* Private functions ---------------------------------------------------------*/
/**
  * @brief  判断句柄是否已经安装完整且成对的 Port 依赖。
  * @param  hsdcard SD 卡设备句柄。
  * @retval true  Port Interface 完整。
  * @retval false 至少一个必需函数或 Context 缺失。
  */
static bool sdcard_is_port_bound(const SDCard_HandleTypeDef *hsdcard)
{
    /* StartReadBlocks 与 StartWriteBlocks 是 DMA 可选能力，不属于基础绑定条件。 */
    return (hsdcard != NULL) &&
           (hsdcard->PortOps != NULL) &&
           (hsdcard->PortContext != NULL) &&
           (hsdcard->PortOps->IsPresent != NULL) &&
           (hsdcard->PortOps->Init != NULL) &&
           (hsdcard->PortOps->DeInit != NULL) &&
           (hsdcard->PortOps->GetInfo != NULL) &&
           (hsdcard->PortOps->ReadBlocks != NULL) &&
           (hsdcard->PortOps->WriteBlocks != NULL) &&
           (hsdcard->PortOps->Sync != NULL);
}

/**
  * @brief  使缓存的介质信息失效并清零。
  * @param  hsdcard SD 卡设备句柄。
  * @retval None
  */
static void sdcard_invalidate_info(SDCard_HandleTypeDef *hsdcard)
{
    (void)memset(&hsdcard->Info, 0, sizeof(hsdcard->Info));
    hsdcard->IsInfoValid = false;
}

/**
  * @brief  清除上一次 Device 和 Port 错误快照。
  * @param  hsdcard SD 卡设备句柄。
  * @retval None
  */
static void sdcard_clear_error(SDCard_HandleTypeDef *hsdcard)
{
    hsdcard->ErrorCode = SDCARD_ERROR_NONE;
    hsdcard->LastPortStatus = SDCARD_PORT_OK;
}

/**
  * @brief  集中保存一次失败的稳定语义诊断。
  * @param  hsdcard SD 卡设备句柄。
  * @param  error Device 层错误阶段。
  * @param  port_status Port 返回的归一化状态。
  * @param  next_state 失败后应进入的持续状态。
  * @retval SDCARD_ERROR
  */
static SDCard_StatusTypeDef sdcard_fail(SDCard_HandleTypeDef *hsdcard,
    SDCard_ErrorTypeDef error,
    SDCard_PortStatusTypeDef port_status,
    SDCard_StateTypeDef next_state)
{
    hsdcard->ErrorCode = error;
    hsdcard->LastPortStatus = port_status;
    hsdcard->State = next_state;

    if (next_state == SDCARD_STATE_NOT_PRESENT)
    {
        sdcard_invalidate_info(hsdcard);
    }

    return SDCARD_ERROR;
}

/**
  * @brief  根据 Port 失败类型选择合适的持续状态。
  * @param  port_status Port 返回的统一状态。
  * @retval SDCard_StateTypeDef
  */
static SDCard_StateTypeDef sdcard_state_after_port_failure(SDCard_PortStatusTypeDef port_status)
{
    if (port_status == SDCARD_PORT_NOT_PRESENT)
    {
        return SDCARD_STATE_NOT_PRESENT;
    }

    /* BUSY 可能只是一次调用冲突，不应把原本可用的介质永久置为 ERROR。 */
    if (port_status == SDCARD_PORT_BUSY)
    {
        return SDCARD_STATE_READY;
    }

    return SDCARD_STATE_ERROR;
}

/**
  * @brief  校验连续块请求不会越过介质末尾。
  * @note   使用减法形式避免 start_block + block_count 发生 32 位溢出。
  */
static bool sdcard_is_range_valid(const SDCard_HandleTypeDef *hsdcard,
                                  uint32_t start_block,
                                  uint32_t block_count)
{
    if ((!hsdcard->IsInfoValid) ||
        (block_count == 0u) ||
        (start_block >= hsdcard->Info.BlockCount))
    {
        return false;
    }

    return block_count <= (hsdcard->Info.BlockCount - start_block);
}

/**
  * @brief  等待 Port 报告介质已经完成内部操作。
  * @param  hsdcard SD 卡设备句柄。
  * @param  error 同步失败时记录的 Device 错误阶段。
  * @retval SDCARD_OK    介质已经回到可传输状态。
  * @retval SDCARD_ERROR 等待失败，诊断信息已写入句柄。
  */
static SDCard_StatusTypeDef sdcard_wait_ready(SDCard_HandleTypeDef *hsdcard,
    SDCard_ErrorTypeDef error)
{
    SDCard_PortStatusTypeDef port_status = hsdcard->PortOps->Sync(hsdcard->PortContext,
                                                                  SDCARD_SYNC_TIMEOUT_MS);

    if (port_status != SDCARD_PORT_OK)
    {
        return sdcard_fail(hsdcard,
                           error,
                           port_status,
                           sdcard_state_after_port_failure(port_status));
    }

    return SDCARD_OK;
}

/* Exported functions --------------------------------------------------------*/
/**
  * @brief  初始化当前介质并缓存逻辑块信息。
  * @param  hsdcard 已由 SD Card Port 绑定 PortOps/PortContext 的句柄。
  * @retval SDCARD_OK
  *         初始化成功；没有插卡同样属于成功识别状态，此时 State 为
  *         SDCARD_STATE_NOT_PRESENT。
  * @retval SDCARD_ERROR
  *         Port 未绑定、底层初始化失败或介质信息非法。
  */
SDCard_StatusTypeDef SDCard_Init(SDCard_HandleTypeDef *hsdcard)
{
    SDCard_PortStatusTypeDef port_status;

    if (hsdcard == NULL)
    {
        return SDCARD_ERROR;
    }

    if (!sdcard_is_port_bound(hsdcard))
    {
        hsdcard->State = SDCARD_STATE_ERROR;
        hsdcard->ErrorCode = SDCARD_ERROR_PORT_NOT_BOUND;
        hsdcard->LastPortStatus = SDCARD_PORT_ERROR;
        return SDCARD_ERROR;
    }

    if (hsdcard->State == SDCARD_STATE_BUSY)
    {
        return sdcard_fail(hsdcard,
                           SDCARD_ERROR_NOT_READY,
                           SDCARD_PORT_OK,
                           SDCARD_STATE_BUSY);
    }

    sdcard_clear_error(hsdcard);

    if (!SDCard_IsPresent(hsdcard))
    {
        /*
         * 无卡是可移除介质的正常状态。若此前 Port 已经初始化，先释放
         * SDMMC/GPIO 资源；随后用 NOT_PRESENT 表达当前物理事实。
         */
        if (hsdcard->IsPortInitialized)
        {
            port_status = hsdcard->PortOps->DeInit(hsdcard->PortContext);
            hsdcard->IsPortInitialized = false;

            if (port_status != SDCARD_PORT_OK)
            {
                return sdcard_fail(hsdcard,
                                   SDCARD_ERROR_PORT_DEINIT,
                                   port_status,
                                   SDCARD_STATE_NOT_PRESENT);
            }
        }

        sdcard_invalidate_info(hsdcard);
        hsdcard->State = SDCARD_STATE_NOT_PRESENT;
        return SDCARD_OK;
    }

    if ((hsdcard->State == SDCARD_STATE_READY) &&
        hsdcard->IsPortInitialized &&
        hsdcard->IsInfoValid)
    {
        /* 已经就绪的重复 Init 是幂等操作，不重新产生 SDMMC 波形。 */
        return SDCARD_OK;
    }

    /* 从 ERROR 等状态重试时，先清理可能残留的底层资源。 */
    if (hsdcard->IsPortInitialized)
    {
        port_status = hsdcard->PortOps->DeInit(hsdcard->PortContext);
        hsdcard->IsPortInitialized = false;

        if (port_status != SDCARD_PORT_OK)
        {
            return sdcard_fail(hsdcard,
                               SDCARD_ERROR_PORT_DEINIT,
                               port_status,
                               SDCARD_STATE_ERROR);
        }
    }

    sdcard_invalidate_info(hsdcard);
    hsdcard->State = SDCARD_STATE_BUSY;

    port_status = hsdcard->PortOps->Init(hsdcard->PortContext);
    if (port_status != SDCARD_PORT_OK)
    {
        return sdcard_fail(hsdcard,
                           SDCARD_ERROR_PORT_INIT,
                           port_status,
                           (port_status == SDCARD_PORT_NOT_PRESENT)
                               ? SDCARD_STATE_NOT_PRESENT
                               : SDCARD_STATE_ERROR);
    }
    hsdcard->IsPortInitialized = true;

    port_status = hsdcard->PortOps->GetInfo(hsdcard->PortContext,
                                            &hsdcard->Info);
    if (port_status != SDCARD_PORT_OK)
    {
        /* GetInfo 失败后主动释放已初始化的 Port，避免留下半初始化状态。 */
        (void)hsdcard->PortOps->DeInit(hsdcard->PortContext);
        hsdcard->IsPortInitialized = false;
        sdcard_invalidate_info(hsdcard);

        return sdcard_fail(hsdcard,
                           SDCARD_ERROR_PORT_GET_INFO,
                           port_status,
                           (port_status == SDCARD_PORT_NOT_PRESENT)
                               ? SDCARD_STATE_NOT_PRESENT
                               : SDCARD_STATE_ERROR);
    }

    if ((hsdcard->Info.BlockCount == 0u) ||
        (hsdcard->Info.BlockSize == 0u))
    {
        (void)hsdcard->PortOps->DeInit(hsdcard->PortContext);
        hsdcard->IsPortInitialized = false;
        sdcard_invalidate_info(hsdcard);

        return sdcard_fail(hsdcard,
                           SDCARD_ERROR_INVALID_INFO,
                           SDCARD_PORT_OK,
                           SDCARD_STATE_ERROR);
    }

    hsdcard->IsInfoValid = true;
    hsdcard->State = SDCARD_STATE_READY;
    return SDCARD_OK;
}

/**
  * @brief  反初始化 Port 并清除缓存的介质信息。
  * @note   本函数是幂等的；RESET 状态下重复调用直接返回成功。
  */
SDCard_StatusTypeDef SDCard_DeInit(SDCard_HandleTypeDef *hsdcard)
{
    SDCard_PortStatusTypeDef port_status;

    if (hsdcard == NULL)
    {
        return SDCARD_ERROR;
    }

    if (!sdcard_is_port_bound(hsdcard))
    {
        hsdcard->State = SDCARD_STATE_ERROR;
        hsdcard->ErrorCode = SDCARD_ERROR_PORT_NOT_BOUND;
        hsdcard->LastPortStatus = SDCARD_PORT_ERROR;
        return SDCARD_ERROR;
    }

    if (hsdcard->State == SDCARD_STATE_BUSY)
    {
        return sdcard_fail(hsdcard,
                           SDCARD_ERROR_NOT_READY,
                           SDCARD_PORT_OK,
                           SDCARD_STATE_BUSY);
    }

    sdcard_clear_error(hsdcard);

    if (hsdcard->IsPortInitialized)
    {
        port_status = hsdcard->PortOps->DeInit(hsdcard->PortContext);
        if (port_status != SDCARD_PORT_OK)
        {
            return sdcard_fail(hsdcard,
                               SDCARD_ERROR_PORT_DEINIT,
                               port_status,
                               SDCARD_STATE_ERROR);
        }
    }

    hsdcard->IsPortInitialized = false;
    sdcard_invalidate_info(hsdcard);
    hsdcard->State = SDCARD_STATE_RESET;
    return SDCARD_OK;
}

/**
  * @brief  根据当前卡检测电平刷新介质生命周期。
  * @details
  *         检测到拔卡时释放 Port 并进入 NOT_PRESENT；从 RESET 或
  *         NOT_PRESENT 检测到插卡时执行初始化。ERROR 状态不会自动反复
  *         重试，必须先拔卡或由调用者显式再次调用 SDCard_Init()。
  * @note   本函数不做机械触点去抖，调用者应在 APP 或未来 Storage task 中
  *         先确认卡检测电平稳定，且不得在 EXTI ISR 中调用。
  */
SDCard_StatusTypeDef SDCard_Refresh(SDCard_HandleTypeDef *hsdcard)
{
    SDCard_PortStatusTypeDef port_status;

    if (hsdcard == NULL)
    {
        return SDCARD_ERROR;
    }

    if (!sdcard_is_port_bound(hsdcard))
    {
        return sdcard_fail(hsdcard,
                           SDCARD_ERROR_PORT_NOT_BOUND,
                           SDCARD_PORT_ERROR,
                           SDCARD_STATE_ERROR);
    }

    if (!SDCard_IsPresent(hsdcard))
    {
        if (hsdcard->IsPortInitialized)
        {
            port_status = hsdcard->PortOps->DeInit(hsdcard->PortContext);
            hsdcard->IsPortInitialized = false;

            if (port_status != SDCARD_PORT_OK)
            {
                return sdcard_fail(hsdcard,
                                   SDCARD_ERROR_PORT_DEINIT,
                                   port_status,
                                   SDCARD_STATE_NOT_PRESENT);
            }
        }

        sdcard_clear_error(hsdcard);
        sdcard_invalidate_info(hsdcard);
        hsdcard->State = SDCARD_STATE_NOT_PRESENT;
        return SDCARD_OK;
    }

    if ((hsdcard->State == SDCARD_STATE_RESET) ||
        (hsdcard->State == SDCARD_STATE_NOT_PRESENT))
    {
        return SDCard_Init(hsdcard);
    }

    if (hsdcard->State == SDCARD_STATE_ERROR)
    {
        return SDCARD_ERROR;
    }

    if (hsdcard->State == SDCARD_STATE_BUSY)
    {
        return sdcard_fail(hsdcard,
                           SDCARD_ERROR_NOT_READY,
                           SDCARD_PORT_OK,
                           SDCARD_STATE_BUSY);
    }

    return SDCARD_OK;
}

/**
  * @brief  从 SD 卡读取连续逻辑块。
  * @details Device 先校验介质状态和请求范围，再调用 Port，最后等待卡回到
  *          可传输状态。这样未来 FatFs Adapter 无需重复这些规则。
  */
SDCard_StatusTypeDef SDCard_ReadBlocks(SDCard_HandleTypeDef *hsdcard,
                                       uint8_t *data,
                                       uint32_t start_block,
                                       uint32_t block_count)
{
    SDCard_PortStatusTypeDef port_status;

    if (hsdcard == NULL)
    {
        return SDCARD_ERROR;
    }

    if ((data == NULL) || (block_count == 0u))
    {
        return sdcard_fail(hsdcard,
                           SDCARD_ERROR_INVALID_PARAM,
                           SDCARD_PORT_OK,
                           hsdcard->State);
    }

    if (!sdcard_is_port_bound(hsdcard))
    {
        return sdcard_fail(hsdcard,
                           SDCARD_ERROR_PORT_NOT_BOUND,
                           SDCARD_PORT_ERROR,
                           SDCARD_STATE_ERROR);
    }

    if (!SDCard_IsPresent(hsdcard))
    {
        return sdcard_fail(hsdcard,
                           SDCARD_ERROR_NOT_PRESENT,
                           SDCARD_PORT_NOT_PRESENT,
                           SDCARD_STATE_NOT_PRESENT);
    }

    if (hsdcard->State != SDCARD_STATE_READY)
    {
        return sdcard_fail(hsdcard,
                           SDCARD_ERROR_NOT_READY,
                           SDCARD_PORT_OK,
                           hsdcard->State);
    }

    if (!sdcard_is_range_valid(hsdcard, start_block, block_count))
    {
        return sdcard_fail(hsdcard,
                           SDCARD_ERROR_OUT_OF_RANGE,
                           SDCARD_PORT_OK,
                           SDCARD_STATE_READY);
    }

    sdcard_clear_error(hsdcard);
    hsdcard->State = SDCARD_STATE_BUSY;

    port_status = hsdcard->PortOps->ReadBlocks(hsdcard->PortContext,
                                               data,
                                               start_block,
                                               block_count,
                                               SDCARD_TRANSFER_TIMEOUT_MS);
    if (port_status != SDCARD_PORT_OK)
    {
        return sdcard_fail(hsdcard,
                           SDCARD_ERROR_PORT_READ,
                           port_status,
                           sdcard_state_after_port_failure(port_status));
    }

    if (sdcard_wait_ready(hsdcard, SDCARD_ERROR_PORT_SYNC) != SDCARD_OK)
    {
        return SDCARD_ERROR;
    }

    hsdcard->State = SDCARD_STATE_READY;
    return SDCARD_OK;
}

/**
  * @brief  向 SD 卡写入连续逻辑块。
  * @details 范围校验使用初始化时缓存的逻辑块数量；写入返回后继续等待卡
  *          完成内部编程，确保上层收到成功时数据阶段已经结束。
  */
SDCard_StatusTypeDef SDCard_WriteBlocks(SDCard_HandleTypeDef *hsdcard,
                                        const uint8_t *data,
                                        uint32_t start_block,
                                        uint32_t block_count)
{
    SDCard_PortStatusTypeDef port_status;

    if (hsdcard == NULL)
    {
        return SDCARD_ERROR;
    }

    if ((data == NULL) || (block_count == 0u))
    {
        return sdcard_fail(hsdcard,
                           SDCARD_ERROR_INVALID_PARAM,
                           SDCARD_PORT_OK,
                           hsdcard->State);
    }

    if (!sdcard_is_port_bound(hsdcard))
    {
        return sdcard_fail(hsdcard,
                           SDCARD_ERROR_PORT_NOT_BOUND,
                           SDCARD_PORT_ERROR,
                           SDCARD_STATE_ERROR);
    }

    if (!SDCard_IsPresent(hsdcard))
    {
        return sdcard_fail(hsdcard,
                           SDCARD_ERROR_NOT_PRESENT,
                           SDCARD_PORT_NOT_PRESENT,
                           SDCARD_STATE_NOT_PRESENT);
    }

    if (hsdcard->State != SDCARD_STATE_READY)
    {
        return sdcard_fail(hsdcard,
                           SDCARD_ERROR_NOT_READY,
                           SDCARD_PORT_OK,
                           hsdcard->State);
    }

    if (!sdcard_is_range_valid(hsdcard, start_block, block_count))
    {
        return sdcard_fail(hsdcard,
                           SDCARD_ERROR_OUT_OF_RANGE,
                           SDCARD_PORT_OK,
                           SDCARD_STATE_READY);
    }

    sdcard_clear_error(hsdcard);
    hsdcard->State = SDCARD_STATE_BUSY;

    port_status = hsdcard->PortOps->WriteBlocks(hsdcard->PortContext,
                                                data,
                                                start_block,
                                                block_count,
                                                SDCARD_TRANSFER_TIMEOUT_MS);
    if (port_status != SDCARD_PORT_OK)
    {
        return sdcard_fail(hsdcard,
                           SDCARD_ERROR_PORT_WRITE,
                           port_status,
                           sdcard_state_after_port_failure(port_status));
    }

    if (sdcard_wait_ready(hsdcard, SDCARD_ERROR_PORT_SYNC) != SDCARD_OK)
    {
        return SDCARD_ERROR;
    }

    hsdcard->State = SDCARD_STATE_READY;
    return SDCARD_OK;
}

/**
  * @brief  启动一次非阻塞 SD 块读取。
  * @param  hsdcard 已初始化且当前处于 READY 的 SD Card Device。
  * @param  data DMA 将写入的连续块缓冲区。
  * @param  start_block 第一个逻辑块编号。
  * @param  block_count 连续读取的逻辑块数量。
  * @retval SDCARD_OK DMA 请求已被 Port 接受，Device 进入 BUSY。
  * @retval SDCARD_ERROR 参数、状态、范围或底层启动操作失败。
  * @note   本函数刻意不轮询 Sync，也不把状态改回 READY。DMA 完成 IRQ 到达后，
  *         运行时上层必须在普通任务上下文调用 SDCard_CompleteTransfer()；DMA
  *         错误、超时或中止时则调用 SDCard_FailTransfer()。这样 ISR 不会修改
  *         Device 状态机，且发起者不会误把“DMA 已启动”当作“数据已可读取”。
  */
SDCard_StatusTypeDef SDCard_StartReadBlocks(SDCard_HandleTypeDef *hsdcard,
                                            uint8_t *data,
                                            uint32_t start_block,
                                            uint32_t block_count)
{
    SDCard_PortStatusTypeDef port_status;

    if (hsdcard == NULL)
    {
        return SDCARD_ERROR;
    }

    if ((data == NULL) || (block_count == 0u))
    {
        return sdcard_fail(hsdcard,
                           SDCARD_ERROR_INVALID_PARAM,
                           SDCARD_PORT_OK,
                           hsdcard->State);
    }

    if ((!sdcard_is_port_bound(hsdcard)) ||
        (hsdcard->PortOps->StartReadBlocks == NULL))
    {
        return sdcard_fail(hsdcard,
                           SDCARD_ERROR_PORT_NOT_BOUND,
                           SDCARD_PORT_ERROR,
                           SDCARD_STATE_ERROR);
    }

    if (!SDCard_IsPresent(hsdcard))
    {
        return sdcard_fail(hsdcard,
                           SDCARD_ERROR_NOT_PRESENT,
                           SDCARD_PORT_NOT_PRESENT,
                           SDCARD_STATE_NOT_PRESENT);
    }

    if (hsdcard->State != SDCARD_STATE_READY)
    {
        return sdcard_fail(hsdcard,
                           SDCARD_ERROR_NOT_READY,
                           SDCARD_PORT_OK,
                           hsdcard->State);
    }

    if (!sdcard_is_range_valid(hsdcard, start_block, block_count))
    {
        return sdcard_fail(hsdcard,
                           SDCARD_ERROR_OUT_OF_RANGE,
                           SDCARD_PORT_OK,
                           SDCARD_STATE_READY);
    }

    sdcard_clear_error(hsdcard);
    hsdcard->State = SDCARD_STATE_BUSY;

    port_status = hsdcard->PortOps->StartReadBlocks(hsdcard->PortContext,
                                                    data,
                                                    start_block,
                                                    block_count);
    if (port_status != SDCARD_PORT_OK)
    {
        return sdcard_fail(hsdcard,
                           SDCARD_ERROR_PORT_READ,
                           port_status,
                           sdcard_state_after_port_failure(port_status));
    }

    return SDCARD_OK;
}

/**
  * @brief  启动一次非阻塞 SD 块写入。
  * @param  hsdcard 已初始化且当前处于 READY 的 SD Card Device。
  * @param  data DMA 将读取的连续块缓冲区。
  * @param  start_block 第一个逻辑块编号。
  * @param  block_count 连续写入的逻辑块数量。
  * @retval SDCARD_OK DMA 请求已被 Port 接受，Device 进入 BUSY。
  * @retval SDCARD_ERROR 参数、状态、范围或底层启动操作失败。
  * @note   data 从本函数返回后仍被 DMA 使用，直至传输完成事件被普通任务处理。
  *         因此调用者必须维持缓冲区内容和生命周期；本函数不会调用 Sync 或把
  *         Device 提前改为 READY。
  */
SDCard_StatusTypeDef SDCard_StartWriteBlocks(SDCard_HandleTypeDef *hsdcard,
                                             const uint8_t *data,
                                             uint32_t start_block,
                                             uint32_t block_count)
{
    SDCard_PortStatusTypeDef port_status;

    if (hsdcard == NULL)
    {
        return SDCARD_ERROR;
    }

    if ((data == NULL) || (block_count == 0u))
    {
        return sdcard_fail(hsdcard,
                           SDCARD_ERROR_INVALID_PARAM,
                           SDCARD_PORT_OK,
                           hsdcard->State);
    }

    if ((!sdcard_is_port_bound(hsdcard)) ||
        (hsdcard->PortOps->StartWriteBlocks == NULL))
    {
        return sdcard_fail(hsdcard,
                           SDCARD_ERROR_PORT_NOT_BOUND,
                           SDCARD_PORT_ERROR,
                           SDCARD_STATE_ERROR);
    }

    if (!SDCard_IsPresent(hsdcard))
    {
        return sdcard_fail(hsdcard,
                           SDCARD_ERROR_NOT_PRESENT,
                           SDCARD_PORT_NOT_PRESENT,
                           SDCARD_STATE_NOT_PRESENT);
    }

    if (hsdcard->State != SDCARD_STATE_READY)
    {
        return sdcard_fail(hsdcard,
                           SDCARD_ERROR_NOT_READY,
                           SDCARD_PORT_OK,
                           hsdcard->State);
    }

    if (!sdcard_is_range_valid(hsdcard, start_block, block_count))
    {
        return sdcard_fail(hsdcard,
                           SDCARD_ERROR_OUT_OF_RANGE,
                           SDCARD_PORT_OK,
                           SDCARD_STATE_READY);
    }

    sdcard_clear_error(hsdcard);
    hsdcard->State = SDCARD_STATE_BUSY;

    port_status = hsdcard->PortOps->StartWriteBlocks(hsdcard->PortContext,
                                                     data,
                                                     start_block,
                                                     block_count);
    if (port_status != SDCARD_PORT_OK)
    {
        return sdcard_fail(hsdcard,
                           SDCARD_ERROR_PORT_WRITE,
                           port_status,
                           sdcard_state_after_port_failure(port_status));
    }

    return SDCARD_OK;
}

/**
  * @brief  在普通任务上下文确认一次 DMA 数据阶段成功完成。
  * @param  hsdcard 当前执行 DMA 传输的 SD Card Device。
  * @retval SDCARD_OK 卡已回到可继续发送命令的 READY 状态。
  * @retval SDCARD_ERROR 当前并无待完成传输，或卡在数据结束后未能回到 TRANSFER。
  * @details
  *          HAL 的读/写完成回调表示 SDMMC 的 DMA 数据阶段已经结束，但尤其对写入
  *          而言，SD 卡仍可能处于内部 PROGRAMMING 状态。故此函数仍通过 Port
  *          Sync 确认卡已回到 TRANSFER，再把 Device 从 BUSY 转为 READY。
  *
  *          该等待发生在任务上下文，而不是 IRQ 中；IRQ 只负责发布完成事件。
  */
SDCard_StatusTypeDef SDCard_CompleteTransfer(SDCard_HandleTypeDef *hsdcard)
{
    if (hsdcard == NULL)
    {
        return SDCARD_ERROR;
    }

    if (!sdcard_is_port_bound(hsdcard))
    {
        return sdcard_fail(hsdcard,
                           SDCARD_ERROR_PORT_NOT_BOUND,
                           SDCARD_PORT_ERROR,
                           SDCARD_STATE_ERROR);
    }

    if (!SDCard_IsPresent(hsdcard))
    {
        return sdcard_fail(hsdcard,
                           SDCARD_ERROR_NOT_PRESENT,
                           SDCARD_PORT_NOT_PRESENT,
                           SDCARD_STATE_NOT_PRESENT);
    }

    if (hsdcard->State != SDCARD_STATE_BUSY)
    {
        return sdcard_fail(hsdcard,
                           SDCARD_ERROR_NOT_READY,
                           SDCARD_PORT_OK,
                           hsdcard->State);
    }

    if (sdcard_wait_ready(hsdcard, SDCARD_ERROR_PORT_SYNC) != SDCARD_OK)
    {
        return SDCARD_ERROR;
    }

    hsdcard->State = SDCARD_STATE_READY;
    return SDCARD_OK;
}

/**
  * @brief  在普通任务上下文记录 DMA 失败或超时。
  * @param  hsdcard 当前执行 DMA 传输的 SD Card Device。
  * @retval SDCARD_ERROR 始终返回失败；详细原因记录在 Device 诊断快照中。
  * @note   IRQ 上报失败时不得直接写 Device 状态。上层在接收事件后调用本函数，
  *         由单一普通上下文完成状态转换；若此时卡已被移除，则优先进入
  *         NOT_PRESENT，而不是泛化 ERROR。
  */
SDCard_StatusTypeDef SDCard_FailTransfer(SDCard_HandleTypeDef *hsdcard)
{
    if (hsdcard == NULL)
    {
        return SDCARD_ERROR;
    }

    if (!SDCard_IsPresent(hsdcard))
    {
        return sdcard_fail(hsdcard,
                           SDCARD_ERROR_NOT_PRESENT,
                           SDCARD_PORT_NOT_PRESENT,
                           SDCARD_STATE_NOT_PRESENT);
    }

    return sdcard_fail(hsdcard,
                       SDCARD_ERROR_PORT_SYNC,
                       SDCARD_PORT_ERROR,
                       SDCARD_STATE_ERROR);
}

/**
  * @brief  显式等待 SD 卡完成所有内部传输。
  * @note   未来 FatFs 的 CTRL_SYNC 可以直接映射到本函数。
  */
SDCard_StatusTypeDef SDCard_Sync(SDCard_HandleTypeDef *hsdcard)
{
    if (hsdcard == NULL)
    {
        return SDCARD_ERROR;
    }

    if (!sdcard_is_port_bound(hsdcard))
    {
        return sdcard_fail(hsdcard,
                           SDCARD_ERROR_PORT_NOT_BOUND,
                           SDCARD_PORT_ERROR,
                           SDCARD_STATE_ERROR);
    }

    if (!SDCard_IsPresent(hsdcard))
    {
        return sdcard_fail(hsdcard,
                           SDCARD_ERROR_NOT_PRESENT,
                           SDCARD_PORT_NOT_PRESENT,
                           SDCARD_STATE_NOT_PRESENT);
    }

    if (hsdcard->State != SDCARD_STATE_READY)
    {
        return sdcard_fail(hsdcard,
                           SDCARD_ERROR_NOT_READY,
                           SDCARD_PORT_OK,
                           hsdcard->State);
    }

    sdcard_clear_error(hsdcard);
    hsdcard->State = SDCARD_STATE_BUSY;

    if (sdcard_wait_ready(hsdcard, SDCARD_ERROR_PORT_SYNC) != SDCARD_OK)
    {
        return SDCARD_ERROR;
    }

    hsdcard->State = SDCARD_STATE_READY;
    return SDCARD_OK;
}

/**
  * @brief  将缓存的介质信息复制给调用者。
  * @retval SDCARD_OK    信息有效并已复制。
  * @retval SDCARD_ERROR 参数非法或介质尚未就绪。
  */
SDCard_StatusTypeDef SDCard_GetInfo(const SDCard_HandleTypeDef *hsdcard,
                                    SDCard_InfoTypeDef *info)
{
    if ((hsdcard == NULL) ||
        (info == NULL) ||
        (!hsdcard->IsInfoValid) ||
        (hsdcard->State != SDCARD_STATE_READY))
    {
        return SDCARD_ERROR;
    }

    *info = hsdcard->Info;
    return SDCARD_OK;
}

/**
  * @brief  读取当前持续状态。
  * @retval SDCard_StateTypeDef；空句柄按 ERROR 处理。
  */
SDCard_StateTypeDef SDCard_GetState(const SDCard_HandleTypeDef *hsdcard)
{
    return (hsdcard != NULL) ? hsdcard->State : SDCARD_STATE_ERROR;
}

/**
  * @brief  通过当前 Port Adapter 读取原始卡检测状态。
  * @note   本函数不改变 Device State；状态转换由 Init/Refresh 完成。
  */
bool SDCard_IsPresent(const SDCard_HandleTypeDef *hsdcard)
{
    if ((!sdcard_is_port_bound(hsdcard)) ||
        (hsdcard->PortOps->IsPresent == NULL))
    {
        return false;
    }

    return hsdcard->PortOps->IsPresent(hsdcard->PortContext);
}
