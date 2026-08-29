/**
  ******************************************************************************
  * @file    w25qxx.c
  * @brief   W25Q 系列串行 NOR Flash Device 的最小识别实现。
  *
  * @details
 *          首版只定义初始化成功的最小语义：经已绑定的总线读取并缓存 JEDEC
 *          三字节 ID，再校验实例注入的厂商与容量。页编程、擦除、状态轮询、
 *          4-byte 地址和内存映射均尚未属于本 Module 的实现范围。
  ******************************************************************************
  */

#include "Components/w25qxx/w25qxx.h"
#include "Components/w25qxx/w25qxx_config.h"

#include <stddef.h>

/**
 * @brief 记录一次语义失败并把 Device 置为错误状态。
 * @param hflash 已验证非空的 W25Qxx Handle。
 * @param error 本次失败对应的 Device 语义阶段。
 * @param bus_status Adapter 返回的归一化底层总线状态。
 * @retval W25QXX_ERROR。
 */
static W25Qxx_StatusTypeDef w25qxx_record_failure(
    W25Qxx_HandleTypeDef *hflash,
    W25Qxx_ErrorTypeDef error,
    W25Qxx_BusStatusTypeDef bus_status)
{
    hflash->ErrorCode = error;
    hflash->LastBusStatus = bus_status;
    hflash->State = W25QXX_STATE_ERROR;
    return W25QXX_ERROR;
}

/**
 * @brief  初始化 W25Qxx Device 并读取 JEDEC 三字节芯片标识。
 * @param  hflash 已由 Platform 绑定 BusOps 和 BusContext 的 W25Qxx Handle。
 * @retval W25QXX_OK 已成功读取且缓存 JEDEC ID，Device 进入 READY。
 * @retval W25QXX_ERROR Handle、配置或绑定无效，JEDEC 读取失败，或芯片标识
 *         与当前实例的预期厂商和容量不符。
 * @note   本函数同步阻塞于一次极短的无地址 QSPI 命令，仅应在调度器启动前的
 *         Platform 初始化路径调用。ExpectedJedecID 由 Platform 注入当前 PCB
 *         的厂商与容量要求；MemoryType 仅缓存，不作为匹配条件。
 */
W25Qxx_StatusTypeDef W25Qxx_Init(W25Qxx_HandleTypeDef *hflash)
{
    W25Qxx_BusStatusTypeDef bus_status;
    uint8_t raw_jedec_id[W25QXX_JEDEC_ID_LENGTH];

    if (hflash == NULL)
    {
        return W25QXX_ERROR;
    }

    hflash->State = W25QXX_STATE_RESET;
    hflash->ErrorCode = W25QXX_ERROR_NONE;
    hflash->LastBusStatus = W25QXX_BUS_OK;

    if ((hflash->BusOps == NULL) ||
        (hflash->BusContext == NULL) ||
        (hflash->BusOps->ReadCommand == NULL))
    {
        return w25qxx_record_failure(hflash,
                                     W25QXX_ERROR_BUS_NOT_BOUND,
                                     W25QXX_BUS_ERROR);
    }

    if (hflash->ExpectedJedecID == NULL)
    {
        return w25qxx_record_failure(hflash,
                                     W25QXX_ERROR_INVALID_PARAM,
                                     W25QXX_BUS_OK);
    }

    hflash->State = W25QXX_STATE_BUSY;
    bus_status = hflash->BusOps->ReadCommand(
        hflash->BusContext,
        W25QXX_COMMAND_READ_JEDEC_ID,
        raw_jedec_id,
        W25QXX_JEDEC_ID_LENGTH);
    if (bus_status != W25QXX_BUS_OK)
    {
        return w25qxx_record_failure(hflash,
                                     W25QXX_ERROR_READ_JEDEC_ID,
                                     bus_status);
    }

    hflash->JedecID.ManufacturerID = raw_jedec_id[0];
    hflash->JedecID.MemoryType = raw_jedec_id[1];
    hflash->JedecID.CapacityID = raw_jedec_id[2];

    if ((hflash->JedecID.ManufacturerID !=
         hflash->ExpectedJedecID->ManufacturerID) ||
        (hflash->JedecID.CapacityID !=
         hflash->ExpectedJedecID->CapacityID))
    {
        return w25qxx_record_failure(hflash,
                                     W25QXX_ERROR_CHIP_MISMATCH,
                                     W25QXX_BUS_OK);
    }

    hflash->ErrorCode = W25QXX_ERROR_NONE;
    hflash->LastBusStatus = W25QXX_BUS_OK;
    hflash->State = W25QXX_STATE_READY;
    return W25QXX_OK;
}

/**
 * @brief  获取初始化阶段成功缓存的 JEDEC 三字节芯片标识。
 * @param  hflash 已完成初始化的 W25Qxx Device Handle。
 * @param  jedec_id 接收制造商、存储器类型和容量代码的有效地址。
 * @retval W25QXX_OK 已复制缓存的 JEDEC ID。
 * @retval W25QXX_ERROR 参数无效，或 Device 尚未处于 READY。
 * @note   本函数不重新访问总线；若以后需要诊断性重复读取，应以单独的显式 API
 *         表达并定义其并发与失败语义。
 */
W25Qxx_StatusTypeDef W25Qxx_GetJedecID(
    W25Qxx_HandleTypeDef *hflash,
    W25Qxx_JedecIDTypeDef *jedec_id)
{
    if ((hflash == NULL) || (jedec_id == NULL))
    {
        if (hflash != NULL)
        {
            hflash->ErrorCode = W25QXX_ERROR_INVALID_PARAM;
        }

        return W25QXX_ERROR;
    }

    if (hflash->State != W25QXX_STATE_READY)
    {
        hflash->ErrorCode = W25QXX_ERROR_NOT_READY;
        return W25QXX_ERROR;
    }

    *jedec_id = hflash->JedecID;
    hflash->ErrorCode = W25QXX_ERROR_NONE;
    return W25QXX_OK;
}
