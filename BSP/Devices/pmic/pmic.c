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
static const PMIC_RegisterValueTypeDef pmic_boot_profile[] =
{
    {XPOWERS_AXP2101_MIN_SYS_VOL_CTRL,     0x50u}, /* VSYSDPM：4.6 V。 */
    {XPOWERS_AXP2101_INPUT_VOL_LIMIT_CTRL, 0x06u}, /* VBUS 电压限制：4.36 V。 */
    {XPOWERS_AXP2101_INPUT_CUR_LIMIT_CTRL, 0x01u}, /* 输入电流限制：500 mA。 */
    {XPOWERS_AXP2101_ADC_CHANNEL_CTRL,     0x1Fu},
    {XPOWERS_AXP2101_INTEN1,               0x00u},
    {XPOWERS_AXP2101_INTEN2,               0x00u},
    {XPOWERS_AXP2101_INTEN3,               0x00u},
    {XPOWERS_AXP2101_INTSTS1,              0xFFu},
    {XPOWERS_AXP2101_INTSTS2,              0xFFu},
    {XPOWERS_AXP2101_INTSTS3,              0xFFu},
    {XPOWERS_AXP2101_ICC_CHG_SET,          0x09u}, /* 充电电流：300 mA。 */
    {XPOWERS_AXP2101_ITERM_CHG_SET_CTRL,   0x15u}, /* 终止电流：125 mA。 */
    {XPOWERS_AXP2101_CV_CHG_VOL_SET,       0x03u}, /* 充电电压：4.2 V。 */
    {XPOWERS_AXP2101_DC_ONOFF_DVM_CTRL,    0x01u}, /* 保持 DCDC1 开启。 */
    {XPOWERS_AXP2101_LDO_ONOFF_CTRL0,      0x04u}, /* 关闭 ALDO1/2，保持 ALDO3。 */
    {XPOWERS_AXP2101_LDO_ONOFF_CTRL1,      0x00u},
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
    hpmic->ErrorCode = PMIC_ERROR_NONE;
    hpmic->LastFailedRegister = 0u;
    hpmic->BusErrorDetail = 0u;
}

/**
  * @brief  将 PMIC 句柄切换为错误状态并保存完整错误上下文。
  * @param  hpmic PMIC 句柄指针。
  * @param  error PMIC 驱动层错误类型。
  * @param  reg 失败寄存器地址；总线准备或参数错误时为 0。
  * @param  bus_detail 底层总线原始错误码。
  * @retval PMIC_ERROR
  */
static PMIC_StatusTypeDef pmic_fail(PMIC_HandleTypeDef *hpmic,
                                    PMIC_ErrorTypeDef error,
                                    uint8_t reg,
                                    uint32_t bus_detail)
{
    hpmic->State = PMIC_STATE_ERROR;
    hpmic->ErrorCode = error;
    hpmic->LastFailedRegister = reg;
    hpmic->BusErrorDetail = bus_detail;
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
    PMIC_BusResultTypeDef bus_result =
        hpmic->BusOps->MemRead(hpmic->BusContext,
                               hpmic->Address7Bit,
                               reg,
                               value,
                               1u);

    if (bus_result.Status != PMIC_BUS_OK)
    {
        return pmic_fail(hpmic, PMIC_ERROR_BUS_READ, reg, bus_result.Detail);
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
    PMIC_BusResultTypeDef bus_result =
        hpmic->BusOps->MemWrite(hpmic->BusContext,
                                hpmic->Address7Bit,
                                reg,
                                &value,
                                1u);

    if (bus_result.Status != PMIC_BUS_OK)
    {
        return pmic_fail(hpmic, PMIC_ERROR_BUS_WRITE, reg, bus_result.Detail);
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
    PMIC_BusResultTypeDef bus_result;
    uint8_t chip_id = 0u;

    if (hpmic == NULL)
    {
        return PMIC_ERROR;
    }

    hpmic->Address7Bit = PMIC_DEFAULT_ADDRESS_7BIT;
    hpmic->ApplyBootConfig = PMIC_DEFAULT_BOOT_CONFIG;
    hpmic->State = PMIC_STATE_RESET;
    pmic_clear_error(hpmic);

    if ((hpmic->BusOps == NULL) ||
        (hpmic->BusOps->Prepare == NULL) ||
        (hpmic->BusOps->MemRead == NULL) ||
        (hpmic->BusOps->MemWrite == NULL) ||
        (hpmic->BusContext == NULL) ||
        (hpmic->Address7Bit > 0x7Fu) ||
        ((hpmic->ApplyBootConfig != PMIC_BOOT_CONFIG_DISABLED) &&
         (hpmic->ApplyBootConfig != PMIC_BOOT_CONFIG_ENABLED)))
    {
        return pmic_fail(hpmic, PMIC_ERROR_INVALID_PARAM, 0u, 0u);
    }

    hpmic->State = PMIC_STATE_BUSY;

    //初始化i2c，硬件/软件i2c都可以
    bus_result = hpmic->BusOps->Prepare(hpmic->BusContext);

    if (bus_result.Status != PMIC_BUS_OK)
    {
        return pmic_fail(hpmic, PMIC_ERROR_BUS_PREPARE, 0u, bus_result.Detail);
    }

    //读取芯片ID
    if (pmic_read_reg(hpmic, XPOWERS_AXP2101_IC_TYPE, &chip_id) != PMIC_OK)
    {
        return PMIC_ERROR;
    }
    //检查芯片ID是否正确
    if (chip_id != XPOWERS_AXP2101_CHIP_ID)
    {
        return pmic_fail(hpmic,
                         PMIC_ERROR_WRONG_CHIP_ID,
                         XPOWERS_AXP2101_IC_TYPE,
                         0u);
    }

    if ((hpmic->ApplyBootConfig == PMIC_BOOT_CONFIG_ENABLED) &&
        (pmic_apply_boot_config(hpmic) != PMIC_OK))
    {
        return PMIC_ERROR;
    }

    hpmic->State = PMIC_STATE_READY;
    return PMIC_OK;
}
