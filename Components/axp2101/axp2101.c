/**
  ******************************************************************************
  * @file    axp2101.c
  * @brief   AXP2101 芯片识别、寄存器访问和电源轨控制实现。
  *
  * @details
  *          本模块只依赖 AXP2101_BusOpsTypeDef，不直接访问 GPIO 或具体 I2C
  *          驱动。本板采用的启动寄存器配置由 Platform Power Module 提供，
  *          本模块负责按掩码安全应用配置并保存稳定的错误诊断。
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "Components/axp2101/axp2101.h"
#include <stddef.h>
#include "Components/axp2101/axp2101_regs.h"

/* Private defines -----------------------------------------------------------*/
/** @brief REG90H bit0：ALDO1 输出使能位，1 为开启，0 为关闭。 */
#define AXP2101_LDO_CTRL0_ALDO1_ENABLE_MASK  (1u << 0)
/** @brief REG90H bit1：ALDO2 输出使能位，1 为开启，0 为关闭。 */
#define AXP2101_LDO_CTRL0_ALDO2_ENABLE_MASK  (1u << 1)

/* Private functions ---------------------------------------------------------*/
/**
  * @brief  清除句柄中保存的上一次错误诊断信息。
  * @param  haxp2101 AXP2101 句柄指针。
  * @retval None
  */
static void axp2101_clear_error(AXP2101_HandleTypeDef *haxp2101)
{
    /* 只清理诊断字段，不触碰总线绑定、设备地址或 State。 */
    haxp2101->ErrorCode = AXP2101_ERROR_NONE;
    haxp2101->LastBusStatus = AXP2101_BUS_OK;
    haxp2101->LastFailedRegister = 0u;
}

/**
  * @brief  保存 AXP2101 层错误阶段和归一化总线原因。
  * @param  haxp2101 AXP2101 句柄指针。
  * @param  error AXP2101 驱动层错误类型。
  * @param  reg 失败寄存器地址；总线准备或参数错误时为 0。
  * @param  bus_status 与本次错误关联的归一化总线状态。
  * @param  next_state 失败后应进入的持续状态。
  * @retval AXP2101_ERROR
  */
static AXP2101_StatusTypeDef axp2101_fail(AXP2101_HandleTypeDef *haxp2101,
                                    AXP2101_ErrorTypeDef error,
                                    uint8_t reg,
                                    AXP2101_BusStatusTypeDef bus_status,
                                    AXP2101_StateTypeDef next_state)
{
    /* Device 只保存稳定语义；SoftI2C/HAL 原始错误留在具体后端句柄中。 */
    haxp2101->State = next_state;
    haxp2101->ErrorCode = error;
    haxp2101->LastBusStatus = bus_status;
    haxp2101->LastFailedRegister = reg;
    return AXP2101_ERROR;
}

/**
  * @brief  通过抽象总线读取一个 AXP2101 寄存器。
  * @param  haxp2101 AXP2101 句柄指针。
  * @param  reg 寄存器地址。
  * @param  value 接收寄存器值的指针。
  * @retval AXP2101_OK    读取成功。
  * @retval AXP2101_ERROR 读取失败，句柄中已记录寄存器和底层错误。
  */
static AXP2101_StatusTypeDef axp2101_read_reg(AXP2101_HandleTypeDef *haxp2101,
                                        uint8_t reg,
                                        uint8_t *value)
{
    /* Device 层只调用抽象 Ops；context 的实际类型由 Port 决定。 */
    AXP2101_BusStatusTypeDef bus_status = haxp2101->BusOps->MemRead(haxp2101->BusContext,
                                                              haxp2101->Address7Bit,
                                                              reg,
                                                              value,
                                                              1u);

    if (bus_status != AXP2101_BUS_OK)
    {
        return axp2101_fail(haxp2101,
                         AXP2101_ERROR_BUS_READ,
                         reg,
                         bus_status,
                         (bus_status == AXP2101_BUS_BUSY)
                             ? AXP2101_STATE_READY
                             : AXP2101_STATE_ERROR);
    }
    return AXP2101_OK;
}

/**
  * @brief  通过抽象总线写入一个 AXP2101 寄存器。
  * @param  haxp2101 AXP2101 句柄指针。
  * @param  reg 寄存器地址。
  * @param  value 待写入值。
  * @retval AXP2101_OK    写入成功。
  * @retval AXP2101_ERROR 写入失败，句柄中已记录寄存器和底层错误。
  */
static AXP2101_StatusTypeDef axp2101_write_reg(AXP2101_HandleTypeDef *haxp2101,
                                         uint8_t reg,
                                         uint8_t value)
{
    /* value 是局部变量，但当前所有总线 MemWrite 都是同步阻塞调用。 */
    AXP2101_BusStatusTypeDef bus_status = haxp2101->BusOps->MemWrite(haxp2101->BusContext,
                                                               haxp2101->Address7Bit,
                                                               reg,
                                                               &value,
                                                               1u);

    if (bus_status != AXP2101_BUS_OK)
    {
        return axp2101_fail(haxp2101,
                         AXP2101_ERROR_BUS_WRITE,
                         reg,
                         bus_status,
                         (bus_status == AXP2101_BUS_BUSY)
                             ? AXP2101_STATE_READY
                             : AXP2101_STATE_ERROR);
    }
    return AXP2101_OK;
}

/**
  * @brief  对寄存器执行读-改-写，只修改 mask 指定的位。
  * @param  haxp2101 AXP2101 句柄指针。
  * @param  reg 寄存器地址。
  * @param  mask 需要修改的位集合；未选中的位保持原值。
  * @param  value 目标位值，仅 mask 覆盖的位有效。
  * @retval AXP2101_OK    读写均成功。
  * @retval AXP2101_ERROR 读取或写入失败。
  */
static AXP2101_StatusTypeDef axp2101_update_bits(AXP2101_HandleTypeDef *haxp2101,
                                           uint8_t reg,
                                           uint8_t mask,
                                           uint8_t value)
{
    uint8_t current = 0u;

    if (axp2101_read_reg(haxp2101, reg, &current) != AXP2101_OK)
    {
        return AXP2101_ERROR;
    }

    /*
     * 清除 old 中 mask 选中的位，再写入 value 对应位：
     * new = (old & ~mask) | (value & mask)。mask 外所有位保持芯片原值。
     */
    current = (uint8_t)((current & (uint8_t)(~mask)) | (value & mask));
    return axp2101_write_reg(haxp2101, reg, current);
}

/**
  * @brief  修改 REG90H 中指定 LDO 的使能位，同时保留其他电源轨状态。
  * @param  haxp2101 AXP2101 句柄指针。
  * @param  enable_mask 目标 LDO 在 REG90H 中对应的使能位掩码。
  * @param  enabled true 表示置位并开启，false 表示清零并关闭。
  * @retval AXP2101_OK 操作成功，句柄恢复为 AXP2101_STATE_READY。
  * @retval AXP2101_ERROR 参数、状态、总线绑定或寄存器访问失败；详情保存在句柄中。
  * @note   本函数采用读-改-写，禁止直接覆盖整个 REG90H，以免改变其他 LDO 状态。
  */
static AXP2101_StatusTypeDef axp2101_set_ldo_enabled(AXP2101_HandleTypeDef *haxp2101,
                                                      uint8_t enable_mask,
                                                      bool enabled)
{
    if (haxp2101 == NULL)
    {
        return AXP2101_ERROR;
    }
    if (haxp2101->State != AXP2101_STATE_READY)
    {
        return axp2101_fail(haxp2101,
                            AXP2101_ERROR_NOT_READY,
                            0u,
                            AXP2101_BUS_OK,
                            haxp2101->State);
    }

    if ((haxp2101->BusOps == NULL) ||
        (haxp2101->BusOps->MemRead == NULL) ||
        (haxp2101->BusOps->MemWrite == NULL) ||
        (haxp2101->BusContext == NULL))
    {
        return axp2101_fail(haxp2101,
                            AXP2101_ERROR_PORT_NOT_BOUND,
                            0u,
                            AXP2101_BUS_OK,
                            AXP2101_STATE_ERROR);
    }

    if (haxp2101->Address7Bit > 0x7Fu)
    {
        return axp2101_fail(haxp2101,
                            AXP2101_ERROR_INVALID_PARAM,
                            0u,
                            AXP2101_BUS_OK,
                            AXP2101_STATE_ERROR);
    }

    axp2101_clear_error(haxp2101);
    haxp2101->State = AXP2101_STATE_BUSY;

    if (axp2101_update_bits(haxp2101,
                            XPOWERS_AXP2101_LDO_ONOFF_CTRL0,
                            enable_mask,
                            enabled ? enable_mask : 0u) != AXP2101_OK)
    {
        return AXP2101_ERROR;
    }

    haxp2101->State = AXP2101_STATE_READY;
    return AXP2101_OK;
}

/**
  * @brief  修改 AXP2101 ALDO1 输出使能位。
  * @param  haxp2101 已初始化且处于 READY 状态的 AXP2101 Handle。
  * @param  enabled true 开启 ALDO1，false 关闭 ALDO1。
  * @retval AXP2101_OK 寄存器更新成功。
  * @retval AXP2101_ERROR Handle、状态或总线操作失败。
  */
AXP2101_StatusTypeDef AXP2101_SetALDO1Enabled(AXP2101_HandleTypeDef *haxp2101,
                                               bool enabled)
{
    return axp2101_set_ldo_enabled(haxp2101,
                                    AXP2101_LDO_CTRL0_ALDO1_ENABLE_MASK,
                                    enabled);
}

/**
  * @brief  修改 AXP2101 ALDO2 输出使能位。
  * @param  haxp2101 已初始化且处于 READY 状态的 AXP2101 Handle。
  * @param  enabled true 开启 ALDO2，false 关闭 ALDO2。
  * @retval AXP2101_OK 寄存器更新成功。
  * @retval AXP2101_ERROR Handle、状态或总线操作失败。
  */
AXP2101_StatusTypeDef AXP2101_SetALDO2Enabled(AXP2101_HandleTypeDef *haxp2101,
                                               bool enabled)
{
    return axp2101_set_ldo_enabled(haxp2101,
                                    AXP2101_LDO_CTRL0_ALDO2_ENABLE_MASK,
                                    enabled);
}

/* Exported functions --------------------------------------------------------*/
/**
  * @brief  准备底层总线并校验 AXP2101 芯片 ID。
  * @param  haxp2101 AXP2101 句柄指针。BusOps 和 BusContext 必须已由端口绑定。
  * @note   本函数只完成 Device 初始化，不应用任何板级供电策略。
  * @retval AXP2101_OK    初始化成功，句柄进入 AXP2101_STATE_READY。
  * @retval AXP2101_ERROR 参数、总线或芯片 ID 校验失败；句柄保存诊断信息。
  */
AXP2101_StatusTypeDef AXP2101_Init(AXP2101_HandleTypeDef *haxp2101)
{
    AXP2101_BusStatusTypeDef bus_status;
    uint8_t chip_id = 0u;

    if (haxp2101 == NULL)
    {
        return AXP2101_ERROR;
    }

    /* 设备地址是 AXP2101 固有配置，Platform 不需要直接装配 Handle 字段。 */
    haxp2101->Address7Bit = AXP2101_DEFAULT_ADDRESS_7BIT;
    haxp2101->State = AXP2101_STATE_RESET;
    axp2101_clear_error(haxp2101);

    /*
     * BusOps 与 BusContext 应已由某个 Adapter 成对注入。检查所有
     * 必需回调，防止通过 NULL 函数指针跳转。
     */
    if ((haxp2101->BusOps == NULL) ||
        (haxp2101->BusOps->Prepare == NULL) ||
        (haxp2101->BusOps->MemRead == NULL) ||
        (haxp2101->BusOps->MemWrite == NULL) ||
        (haxp2101->BusContext == NULL))
    {
        return axp2101_fail(haxp2101,
                         AXP2101_ERROR_PORT_NOT_BOUND,
                         0u,
                         AXP2101_BUS_OK,
                         AXP2101_STATE_ERROR);
    }

    if (haxp2101->Address7Bit > 0x7Fu)
    {
        return axp2101_fail(haxp2101,
                            AXP2101_ERROR_INVALID_PARAM,
                            0u,
                            AXP2101_BUS_OK,
                            AXP2101_STATE_ERROR);
    }

    /* 从此处开始会访问硬件；BUSY 防止观察者把初始化中的对象当作可用。 */
    haxp2101->State = AXP2101_STATE_BUSY;

    /*
     * Prepare 的含义由后端决定：软件 I2C 会检查/恢复物理总线；未来 HAL
     * 硬件 I2C 可检查 HAL Handle 状态，或在已由 CubeMX 初始化时仅返回 OK。
     */
    bus_status = haxp2101->BusOps->Prepare(haxp2101->BusContext);

    if (bus_status != AXP2101_BUS_OK)
    {
        return axp2101_fail(haxp2101,
                            AXP2101_ERROR_BUS_PREPARE,
                            0u,
                            bus_status,
                            AXP2101_STATE_ERROR);
    }

    /* 读取只读芯片 ID，先确认地址上的器件确实是期望的 AXP2101。 */
    if (axp2101_read_reg(haxp2101, XPOWERS_AXP2101_IC_TYPE, &chip_id) != AXP2101_OK)
    {
        haxp2101->State = AXP2101_STATE_ERROR;
        return AXP2101_ERROR;
    }
    /* 总线成功但 ID 错误属于 Device 层错误，不携带底层总线错误码。 */
    if (chip_id != XPOWERS_AXP2101_CHIP_ID)
    {
        return axp2101_fail(haxp2101,
                            AXP2101_ERROR_WRONG_CHIP_ID,
                            XPOWERS_AXP2101_IC_TYPE,
                            AXP2101_BUS_OK,
                            AXP2101_STATE_ERROR);
    }

    /* 所有步骤完成后才进入 READY；任何总线失败已由 axp2101_fail 置 ERROR。 */
    haxp2101->State = AXP2101_STATE_READY;
    return AXP2101_OK;
}

/**
  * @brief  按顺序应用调用者提供的 AXP2101 寄存器配置。
  * @param  haxp2101 已初始化且处于 READY 状态的 AXP2101 Handle。
  * @param  configuration 配置项数组；Mask 为 0xFF 时直接写入，否则读改写。
  * @param  configuration_count 数组元素数量。
  * @retval AXP2101_OK 所有配置项成功应用。
  * @retval AXP2101_ERROR 参数、状态或总线访问失败。
  * @note   配置顺序即执行顺序。任一项失败后立即停止，避免继续改变后续电源轨。
  */
AXP2101_StatusTypeDef AXP2101_ApplyConfiguration(
    AXP2101_HandleTypeDef *haxp2101,
    const AXP2101_RegisterConfigTypeDef *configuration,
    uint32_t configuration_count)
{
    if (haxp2101 == NULL)
    {
        return AXP2101_ERROR;
    }

    if ((configuration == NULL) || (configuration_count == 0u))
    {
        return axp2101_fail(haxp2101,
                            AXP2101_ERROR_INVALID_PARAM,
                            0u,
                            AXP2101_BUS_OK,
                            haxp2101->State);
    }

    if (haxp2101->State != AXP2101_STATE_READY)
    {
        return axp2101_fail(haxp2101,
                            AXP2101_ERROR_NOT_READY,
                            0u,
                            AXP2101_BUS_OK,
                            haxp2101->State);
    }

    axp2101_clear_error(haxp2101);
    haxp2101->State = AXP2101_STATE_BUSY;

    for (uint32_t i = 0u; i < configuration_count; ++i)
    {
        const AXP2101_RegisterConfigTypeDef *item = &configuration[i];
        AXP2101_StatusTypeDef status;

        if (item->Mask == 0u)
        {
            return axp2101_fail(haxp2101,
                                AXP2101_ERROR_INVALID_PARAM,
                                item->Register,
                                AXP2101_BUS_OK,
                                AXP2101_STATE_READY);
        }

        if (item->Mask == AXP2101_REGISTER_FULL_MASK)
        {
            status = axp2101_write_reg(haxp2101, item->Register, item->Value);
        }
        else
        {
            status = axp2101_update_bits(
                haxp2101,
                item->Register,
                item->Mask,
                item->Value);
        }

        if (status != AXP2101_OK)
        {
            return AXP2101_ERROR;
        }
    }

    haxp2101->State = AXP2101_STATE_READY;
    return AXP2101_OK;
}
