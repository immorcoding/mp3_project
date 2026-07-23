/**
  ******************************************************************************
  * @file    pmic.c
  * @brief   AXP2101 芯片识别、寄存器访问和启动配置实现。
  *
  * @details
  *          本模块只依赖 PMIC_BusOpsTypeDef，不直接访问 GPIO 或具体 I2C
  *          驱动。所有寄存器地址、位定义和启动配置均封装在设备驱动内部。
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "pmic.h"

#include <stddef.h>

#include "axp2101_regs.h"

/* Private defines -----------------------------------------------------------*/
/** @defgroup AXP2101_Common_Config_Bits REG10H 公共配置寄存器位定义
  * @{
  */
/** @brief bit5：DCDC、LDO 和 SWITCH 关闭后启用内部放电。 */
#define AXP2101_COMMON_OFF_DISCHARGE_MASK       (1u << 5)
/** @brief bit3：PWROK 被外部拉低时重启系统。 */
#define AXP2101_COMMON_PWROK_RESTART_MASK       (1u << 3)
/** @brief bit2：PWRON 持续按下 16 秒时关闭 PMIC。 */
#define AXP2101_COMMON_PWRON_16S_SHUTDOWN_MASK  (1u << 2)
/** @brief bit1：写 1 立即重启系统；配置写入时必须保持为 0。 */
#define AXP2101_COMMON_RESTART_ACTION_MASK      (1u << 1)
/** @brief bit0：写 1 立即软件关机；配置写入时必须保持为 0。 */
#define AXP2101_COMMON_POWEROFF_ACTION_MASK     (1u << 0)
/** @} */

/**
  * @brief 启动阶段允许修改的 REG10H 位集合。
  * @note  bit1/bit0 也进入掩码，从而在写回时被明确清零，避免误触发动作。
  */
#define AXP2101_COMMON_BOOT_MASK \
    (AXP2101_COMMON_OFF_DISCHARGE_MASK | \
     AXP2101_COMMON_PWROK_RESTART_MASK | \
     AXP2101_COMMON_PWRON_16S_SHUTDOWN_MASK | \
     AXP2101_COMMON_RESTART_ACTION_MASK | \
     AXP2101_COMMON_POWEROFF_ACTION_MASK)

/**
  * @brief REG10H 启动目标值：开启内部放电和 PWRON 16 秒关机。
  * @note  未在此值中出现但属于 BOOT_MASK 的位会被清零。
  */
#define AXP2101_COMMON_BOOT_VALUE \
    (AXP2101_COMMON_OFF_DISCHARGE_MASK | \
     AXP2101_COMMON_PWRON_16S_SHUTDOWN_MASK)

/** @brief REG90H bit0：ALDO1 输出使能位，1 为开启，0 为关闭。 */
#define AXP2101_LDO_CTRL0_ALDO1_ENABLE_MASK  (1u << 0)
/** @brief REG90H bit1：ALDO2 输出使能位，1 为开启，0 为关闭。 */
#define AXP2101_LDO_CTRL0_ALDO2_ENABLE_MASK  (1u << 1)

/* Private types -------------------------------------------------------------*/
/**
  * @brief 单字节 PMIC 寄存器配置项。
  */
typedef struct
{
    uint8_t Register; /**< 寄存器地址。 */
    uint8_t Value;    /**< 写入值。 */
} PMIC_RegisterValueTypeDef;

/* Private variables ---------------------------------------------------------*/
/**
  * @brief AXP2101 启动配置表。
  * @note  寄存器表仅在驱动内部可见；是否应用由
  *        PMIC_HandleTypeDef::ApplyBootConfig 显式控制。
  */
static const PMIC_RegisterValueTypeDef pmic_boot_profile[] = {
    {XPOWERS_AXP2101_MIN_SYS_VOL_CTRL,     0x50u}, /* VSYSDPM：4.6 V。 */
    {XPOWERS_AXP2101_INPUT_VOL_LIMIT_CTRL, 0x06u}, /* VBUS 电压限制：4.36 V。 */
    {XPOWERS_AXP2101_INPUT_CUR_LIMIT_CTRL, 0x01u}, /* 输入电流限制：500 mA。 */
    {XPOWERS_AXP2101_ADC_CHANNEL_CTRL,     0x1Fu}, /* 启用当前选定的 5 路 ADC 测量。 */
    {XPOWERS_AXP2101_INTEN1,               0x00u}, /* 禁用第 1 组 PMIC 中断源。 */
    {XPOWERS_AXP2101_INTEN2,               0x00u}, /* 禁用第 2 组 PMIC 中断源。 */
    {XPOWERS_AXP2101_INTEN3,               0x00u}, /* 禁用第 3 组 PMIC 中断源。 */
    {XPOWERS_AXP2101_INTSTS1,              0xFFu}, /* 写 1 清除第 1 组挂起状态。 */
    {XPOWERS_AXP2101_INTSTS2,              0xFFu}, /* 写 1 清除第 2 组挂起状态。 */
    {XPOWERS_AXP2101_INTSTS3,              0xFFu}, /* 写 1 清除第 3 组挂起状态。 */
    {XPOWERS_AXP2101_ICC_CHG_SET,          0x09u}, /* 充电电流：300 mA。 */
    {XPOWERS_AXP2101_ITERM_CHG_SET_CTRL,   0x15u}, /* 终止电流：125 mA。 */
    {XPOWERS_AXP2101_CV_CHG_VOL_SET,       0x03u}, /* 充电电压：4.2 V。 */
    {XPOWERS_AXP2101_DC_ONOFF_DVM_CTRL,    0x01u}, /* 保持 DCDC1 开启。 */
    {XPOWERS_AXP2101_LDO_ONOFF_CTRL0,      0x04u}, /* 关闭 ALDO1/2，保持 ALDO3。 */
    {XPOWERS_AXP2101_LDO_ONOFF_CTRL1,      0x00u}, /* 关闭该寄存器控制的全部 LDO。 */
    {XPOWERS_AXP2101_LDO_VOL0_CTRL,        0x1Cu}, /* ALDO1 预设为 3.3 V。 */
    {XPOWERS_AXP2101_LDO_VOL1_CTRL,        0x1Cu}  /* ALDO2 预设为 3.3 V。 */
};

/* Private functions ---------------------------------------------------------*/
/**
  * @brief  清除句柄中保存的上一次错误诊断信息。
  * @param  hpmic PMIC 句柄指针。
  * @retval None
  */
static void pmic_clear_error(PMIC_HandleTypeDef *hpmic)
{
    /* 只清理诊断字段，不触碰总线绑定、地址、启动策略或 State。 */
    hpmic->ErrorCode = PMIC_ERROR_NONE;
    hpmic->LastBusStatus = PMIC_BUS_OK;
    hpmic->LastFailedRegister = 0u;
}

/**
  * @brief  保存 PMIC 层错误阶段和归一化总线原因。
  * @param  hpmic PMIC 句柄指针。
  * @param  error PMIC 驱动层错误类型。
  * @param  reg 失败寄存器地址；总线准备或参数错误时为 0。
  * @param  bus_status 与本次错误关联的归一化总线状态。
  * @param  next_state 失败后应进入的持续状态。
  * @retval PMIC_ERROR
  */
static PMIC_StatusTypeDef pmic_fail(PMIC_HandleTypeDef *hpmic,
                                    PMIC_ErrorTypeDef error,
                                    uint8_t reg,
                                    PMIC_BusStatusTypeDef bus_status,
                                    PMIC_StateTypeDef next_state)
{
    /* Device 只保存稳定语义；SoftI2C/HAL 原始错误留在具体后端句柄中。 */
    hpmic->State = next_state;
    hpmic->ErrorCode = error;
    hpmic->LastBusStatus = bus_status;
    hpmic->LastFailedRegister = reg;
    return PMIC_ERROR;
}

/**
  * @brief  通过抽象总线读取一个 AXP2101 寄存器。
  * @param  hpmic PMIC 句柄指针。
  * @param  reg 寄存器地址。
  * @param  value 接收寄存器值的指针。
  * @retval PMIC_OK    读取成功。
  * @retval PMIC_ERROR 读取失败，句柄中已记录寄存器和底层错误。
  */
static PMIC_StatusTypeDef pmic_read_reg(PMIC_HandleTypeDef *hpmic,
                                        uint8_t reg,
                                        uint8_t *value)
{
    /* Device 层只调用抽象 Ops；context 的实际类型由 Port 决定。 */
    PMIC_BusStatusTypeDef bus_status = hpmic->BusOps->MemRead(hpmic->BusContext,
                                                              hpmic->Address7Bit,
                                                              reg,
                                                              value,
                                                              1u);

    if (bus_status != PMIC_BUS_OK)
    {
        return pmic_fail(hpmic,
                         PMIC_ERROR_BUS_READ,
                         reg,
                         bus_status,
                         (bus_status == PMIC_BUS_BUSY)
                             ? PMIC_STATE_READY
                             : PMIC_STATE_ERROR);
    }
    return PMIC_OK;
}

/**
  * @brief  通过抽象总线写入一个 AXP2101 寄存器。
  * @param  hpmic PMIC 句柄指针。
  * @param  reg 寄存器地址。
  * @param  value 待写入值。
  * @retval PMIC_OK    写入成功。
  * @retval PMIC_ERROR 写入失败，句柄中已记录寄存器和底层错误。
  */
static PMIC_StatusTypeDef pmic_write_reg(PMIC_HandleTypeDef *hpmic,
                                         uint8_t reg,
                                         uint8_t value)
{
    /* value 是局部变量，但当前所有总线 MemWrite 都是同步阻塞调用。 */
    PMIC_BusStatusTypeDef bus_status = hpmic->BusOps->MemWrite(hpmic->BusContext,
                                                               hpmic->Address7Bit,
                                                               reg,
                                                               &value,
                                                               1u);

    if (bus_status != PMIC_BUS_OK)
    {
        return pmic_fail(hpmic,
                         PMIC_ERROR_BUS_WRITE,
                         reg,
                         bus_status,
                         (bus_status == PMIC_BUS_BUSY)
                             ? PMIC_STATE_READY
                             : PMIC_STATE_ERROR);
    }
    return PMIC_OK;
}

/**
  * @brief  对寄存器执行读-改-写，只修改 mask 指定的位。
  * @param  hpmic PMIC 句柄指针。
  * @param  reg 寄存器地址。
  * @param  mask 需要修改的位集合；未选中的位保持原值。
  * @param  value 目标位值，仅 mask 覆盖的位有效。
  * @retval PMIC_OK    读写均成功。
  * @retval PMIC_ERROR 读取或写入失败。
  */
static PMIC_StatusTypeDef pmic_update_bits(PMIC_HandleTypeDef *hpmic,
                                           uint8_t reg,
                                           uint8_t mask,
                                           uint8_t value)
{
    uint8_t current = 0u;

    if (pmic_read_reg(hpmic, reg, &current) != PMIC_OK)
    {
        return PMIC_ERROR;
    }

    /*
     * 清除 old 中 mask 选中的位，再写入 value 对应位：
     * new = (old & ~mask) | (value & mask)。mask 外所有位保持芯片原值。
     */
    current = (uint8_t)((current & (uint8_t)(~mask)) | (value & mask));
    return pmic_write_reg(hpmic, reg, current);
}

/**
  * @brief  应用 AXP2101 内置启动配置。
  * @param  hpmic PMIC 句柄指针。
  * @retval PMIC_OK    所有配置写入成功。
  * @retval PMIC_ERROR 任一寄存器访问失败，函数立即停止。
  * @note   REG10H 使用读-改-写以保留未管理位，并明确清零重启/关机动作位；
  *         其他寄存器按照 pmic_boot_profile 顺序写入。
  */
static PMIC_StatusTypeDef pmic_apply_boot_config(PMIC_HandleTypeDef *hpmic)
{
    /* 只修改明确使用的位，并将重启/关机动作位写 0。 */
    if (pmic_update_bits(hpmic,
                         XPOWERS_AXP2101_COMMON_CONFIG,
                         AXP2101_COMMON_BOOT_MASK,
                         AXP2101_COMMON_BOOT_VALUE) != PMIC_OK)
    {
        return PMIC_ERROR;
    }

    /*
     * 配置顺序即数组顺序。出现首个 NACK/超时即停止，避免在总线状态不明时
     * 继续修改后续电源寄存器；失败寄存器由 pmic_write_reg 保存。
     */
    for (uint32_t i = 0u;
         i < (sizeof(pmic_boot_profile) / sizeof(pmic_boot_profile[0]));
         ++i)
    {
        if (pmic_write_reg(hpmic,
                           pmic_boot_profile[i].Register,
                           pmic_boot_profile[i].Value) != PMIC_OK)
        {
            return PMIC_ERROR;
        }
    }

    return PMIC_OK;
}

/**
  * @brief  修改 REG90H 中指定 LDO 的使能位，同时保留其他电源轨状态。
  * @param  hpmic PMIC 句柄指针。
  * @param  enable_mask 目标 LDO 在 REG90H 中对应的使能位掩码。
  * @param  enabled true 表示置位并开启，false 表示清零并关闭。
  * @retval PMIC_OK 操作成功，句柄恢复为 PMIC_STATE_READY。
  * @retval PMIC_ERROR 参数、状态、总线绑定或寄存器访问失败；详情保存在句柄中。
  * @note   本函数采用读-改-写，禁止直接覆盖整个 REG90H，以免改变其他 LDO 状态。
  */
static PMIC_StatusTypeDef pmic_set_ldo_enabled(PMIC_HandleTypeDef *hpmic,
                                                uint8_t enable_mask,
                                                bool enabled)
{
    if (hpmic == NULL)
    {
        return PMIC_ERROR;
    }
    if (hpmic->State != PMIC_STATE_READY)
    {
        return pmic_fail(hpmic,
                         PMIC_ERROR_NOT_READY,
                         0u,
                         PMIC_BUS_OK,
                         hpmic->State);
    }

    if ((hpmic->BusOps == NULL) ||
        (hpmic->BusOps->MemRead == NULL) ||
        (hpmic->BusOps->MemWrite == NULL) ||
        (hpmic->BusContext == NULL))
    {
        return pmic_fail(hpmic,
                         PMIC_ERROR_PORT_NOT_BOUND,
                         0u,
                         PMIC_BUS_OK,
                         PMIC_STATE_ERROR);
    }

    if (hpmic->Address7Bit > 0x7Fu)
    {
        return pmic_fail(hpmic,
                         PMIC_ERROR_INVALID_PARAM,
                         0u,
                         PMIC_BUS_OK,
                         PMIC_STATE_ERROR);
    }

    pmic_clear_error(hpmic);
    hpmic->State = PMIC_STATE_BUSY;

    if (pmic_update_bits(hpmic,
                         XPOWERS_AXP2101_LDO_ONOFF_CTRL0,
                         enable_mask,
                         enabled ? enable_mask : 0u) != PMIC_OK)
    {
        return PMIC_ERROR;
    }

    hpmic->State = PMIC_STATE_READY;
    return PMIC_OK;
}

/**
  * @brief  修改 AXP2101 ALDO1 输出使能位。
  * @param  hpmic 已初始化且处于 READY 状态的 PMIC Handle。
  * @param  enabled true 开启 ALDO1，false 关闭 ALDO1。
  * @retval PMIC_OK 寄存器更新成功。
  * @retval PMIC_ERROR Handle、状态或总线操作失败。
  */
PMIC_StatusTypeDef PMIC_SetALDO1Enabled(PMIC_HandleTypeDef *hpmic,
                                        bool enabled)
{
    return pmic_set_ldo_enabled(hpmic,
                                AXP2101_LDO_CTRL0_ALDO1_ENABLE_MASK,
                                enabled);
}

/**
  * @brief  修改 AXP2101 ALDO2 输出使能位。
  * @param  hpmic 已初始化且处于 READY 状态的 PMIC Handle。
  * @param  enabled true 开启 ALDO2，false 关闭 ALDO2。
  * @retval PMIC_OK 寄存器更新成功。
  * @retval PMIC_ERROR Handle、状态或总线操作失败。
  */
PMIC_StatusTypeDef PMIC_SetALDO2Enabled(PMIC_HandleTypeDef *hpmic,
                                        bool enabled)
{
    return pmic_set_ldo_enabled(hpmic,
                                AXP2101_LDO_CTRL0_ALDO2_ENABLE_MASK,
                                enabled);
}

/* Exported functions --------------------------------------------------------*/
/**
  * @brief  初始化 PMIC、校验 AXP2101 芯片 ID，并按需应用启动配置。
  * @param  hpmic PMIC 句柄指针。BusOps 和 BusContext 必须已由端口绑定。
  * @note   默认 I2C 地址和启动配置策略由本函数装载，调用者不应直接
  *         修改句柄中的设备配置及运行状态。
  * @retval PMIC_OK    初始化成功，句柄进入 PMIC_STATE_READY。
  * @retval PMIC_ERROR 参数、总线、芯片 ID 或配置写入失败；句柄保存诊断信息。
  */
PMIC_StatusTypeDef PMIC_Init(PMIC_HandleTypeDef *hpmic)
{
    PMIC_BusStatusTypeDef bus_status;
    uint8_t chip_id = 0u;

    if (hpmic == NULL)
    {
        return PMIC_ERROR;
    }

    /*
     * Init 是设备配置的唯一装配入口：每次调用都恢复本驱动的默认地址和
     * 启动策略，而不是要求 Board 直接填写 Handle 内部字段。
     */
    hpmic->Address7Bit = PMIC_DEFAULT_ADDRESS_7BIT;
    hpmic->ApplyBootConfig = PMIC_DEFAULT_BOOT_CONFIG;
    hpmic->State = PMIC_STATE_RESET;
    pmic_clear_error(hpmic);

    /*
     * BusOps 与 BusContext 应已由 PMIC_I2C_Port_Bind() 成对注入。检查所有
     * 必需回调，防止通过 NULL 函数指针跳转。
     */
    if ((hpmic->BusOps == NULL) ||
        (hpmic->BusOps->Prepare == NULL) ||
        (hpmic->BusOps->MemRead == NULL) ||
        (hpmic->BusOps->MemWrite == NULL) ||
        (hpmic->BusContext == NULL))
    {
        return pmic_fail(hpmic,
                         PMIC_ERROR_PORT_NOT_BOUND,
                         0u,
                         PMIC_BUS_OK,
                         PMIC_STATE_ERROR);
    }

    if ((hpmic->Address7Bit > 0x7Fu) ||
        ((hpmic->ApplyBootConfig != PMIC_BOOT_CONFIG_DISABLED) &&
         (hpmic->ApplyBootConfig != PMIC_BOOT_CONFIG_ENABLED)))
    {
        return pmic_fail(hpmic,
                         PMIC_ERROR_INVALID_PARAM,
                         0u,
                         PMIC_BUS_OK,
                         PMIC_STATE_ERROR);
    }

    /* 从此处开始会访问硬件；BUSY 防止观察者把初始化中的对象当作可用。 */
    hpmic->State = PMIC_STATE_BUSY;

    /*
     * Prepare 的含义由后端决定：软件 I2C 会检查/恢复物理总线；未来 HAL
     * 硬件 I2C 可检查 HAL Handle 状态，或在已由 CubeMX 初始化时仅返回 OK。
     */
    bus_status = hpmic->BusOps->Prepare(hpmic->BusContext);

    if (bus_status != PMIC_BUS_OK)
    {
        return pmic_fail(hpmic,
                         PMIC_ERROR_BUS_PREPARE,
                         0u,
                         bus_status,
                         PMIC_STATE_ERROR);
    }

    /* 读取只读芯片 ID，先确认地址上的器件确实是期望的 AXP2101。 */
    if (pmic_read_reg(hpmic, XPOWERS_AXP2101_IC_TYPE, &chip_id) != PMIC_OK)
    {
        return PMIC_ERROR;
    }
    /* 总线成功但 ID 错误属于 Device 层错误，不携带底层总线错误码。 */
    if (chip_id != XPOWERS_AXP2101_CHIP_ID)
    {
        return pmic_fail(hpmic,
                         PMIC_ERROR_WRONG_CHIP_ID,
                         XPOWERS_AXP2101_IC_TYPE,
                         PMIC_BUS_OK,
                         PMIC_STATE_ERROR);
    }

    /* 只有芯片身份确认后才允许写电源配置，避免向未知器件写寄存器。 */
    if ((hpmic->ApplyBootConfig == PMIC_BOOT_CONFIG_ENABLED) &&
        (pmic_apply_boot_config(hpmic) != PMIC_OK))
    {
        return PMIC_ERROR;
    }

    /* 所有步骤完成后才进入 READY；任何总线失败已由 pmic_fail 置 ERROR。 */
    hpmic->State = PMIC_STATE_READY;
    return PMIC_OK;
}
