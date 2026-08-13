/**
  ******************************************************************************
  * @file    platform_power.c
  * @brief   本板 AXP2101 实例装配和板级电源策略实现。
  *
  * @details
  *          本模块拥有 AXP2101 Device 与 SoftI2C 的具体实例，并把 Audio、
  *          LCD 等板级电源语义映射到实际电源轨。AXP2101 Device 不保存
  *          本 PCB 的启动策略，SoftI2C Adapter 也不保存板级引脚。
  ******************************************************************************
  */

#include "Platform/power/platform_power.h"
#include "Platform/power/platform_power_config.h"

#include <stddef.h>

#include "Adapters/bridge/axp2101_soft_i2c/axp2101_soft_i2c_adapter.h"
#include "Adapters/stm32_hal/soft_i2c/soft_i2c_stm32_hal_adapter.h"
#include "Components/axp2101/axp2101.h"
#include "main.h"

/** @brief 本板 AXP2101 通信使用的软件 I2C 算法实例。 */
static SoftI2C_HandleTypeDef hplatform_power_i2c;

/**
  * @brief 软件 I2C 使用的 STM32 GPIO Adapter Context。
  * @note  Platform 拥有 SCL/SDA 的板级装配关系，但 GPIO Port 仅为对
  *        Vendor 寄存器映射的借用引用。
  */
static SoftI2C_STM32HALAdapterTypeDef hplatform_power_gpio = {
    .SCLPort = AXP2101_SCL_GPIO_Port,
    .SCLPin = AXP2101_SCL_Pin,
    .SDAPort = AXP2101_SDA_GPIO_Port,
    .SDAPin = AXP2101_SDA_Pin
};

/** @brief 本板唯一 AXP2101 Device 实例。 */
static AXP2101_HandleTypeDef hplatform_power;

/**
  * @brief 本板 AXP2101 上电后需要按顺序应用的寄存器配置。
  * @note  Mask 为 0xFF 的项目直接写入；其他项目由 Device 执行读改写。
  *        修改本表属于板级电源策略变更，必须同步核对原理图、数据手册
  *        和负载允许电压。
  */
static const AXP2101_RegisterConfigTypeDef platform_power_boot_profile[] = {
    {
        XPOWERS_AXP2101_COMMON_CONFIG,
        PLATFORM_POWER_COMMON_BOOT_MASK,
        PLATFORM_POWER_COMMON_BOOT_VALUE
    },
    {XPOWERS_AXP2101_MIN_SYS_VOL_CTRL,     AXP2101_REGISTER_FULL_MASK, 0x50u},
    {XPOWERS_AXP2101_INPUT_VOL_LIMIT_CTRL, AXP2101_REGISTER_FULL_MASK, 0x06u},
    {XPOWERS_AXP2101_INPUT_CUR_LIMIT_CTRL, AXP2101_REGISTER_FULL_MASK, 0x01u},
    {XPOWERS_AXP2101_ADC_CHANNEL_CTRL,     AXP2101_REGISTER_FULL_MASK, 0x1Fu},
    {XPOWERS_AXP2101_INTEN1,               AXP2101_REGISTER_FULL_MASK, 0x00u},
    {XPOWERS_AXP2101_INTEN2,               AXP2101_REGISTER_FULL_MASK, 0x00u},
    {XPOWERS_AXP2101_INTEN3,               AXP2101_REGISTER_FULL_MASK, 0x00u},
    {XPOWERS_AXP2101_INTSTS1,              AXP2101_REGISTER_FULL_MASK, 0xFFu},
    {XPOWERS_AXP2101_INTSTS2,              AXP2101_REGISTER_FULL_MASK, 0xFFu},
    {XPOWERS_AXP2101_INTSTS3,              AXP2101_REGISTER_FULL_MASK, 0xFFu},
    {XPOWERS_AXP2101_ICC_CHG_SET,          AXP2101_REGISTER_FULL_MASK, 0x09u},
    {XPOWERS_AXP2101_ITERM_CHG_SET_CTRL,   AXP2101_REGISTER_FULL_MASK, 0x15u},
    {XPOWERS_AXP2101_CV_CHG_VOL_SET,       AXP2101_REGISTER_FULL_MASK, 0x03u},
    {XPOWERS_AXP2101_DC_ONOFF_DVM_CTRL,    AXP2101_REGISTER_FULL_MASK, 0x01u},
    {XPOWERS_AXP2101_LDO_ONOFF_CTRL0,      AXP2101_REGISTER_FULL_MASK, 0x04u},
    {XPOWERS_AXP2101_LDO_ONOFF_CTRL1,      AXP2101_REGISTER_FULL_MASK, 0x00u},
    {XPOWERS_AXP2101_LDO_VOL0_CTRL,        AXP2101_REGISTER_FULL_MASK, 0x1Cu},
    {XPOWERS_AXP2101_LDO_VOL1_CTRL,        AXP2101_REGISTER_FULL_MASK, 0x1Cu}
};

/**
  * @brief  初始化本板唯一的 AXP2101，并应用板级启动电源策略。
  * @retval PLATFORM_OK 初始化和配置成功。
  * @retval PLATFORM_PMIC_ERROR Adapter 绑定、芯片识别或配置失败。
  */
Platform_StatusTypeDef Platform_Power_Init(void)
{
    /*
     * Platform 持有具体引脚并依次完成两级装配：
     * STM32 GPIO -> SoftI2C Component -> AXP2101 Component。
     */
    if (SoftI2C_STM32HALAdapter_Bind(
            &hplatform_power_i2c,
            &hplatform_power_gpio,
            PLATFORM_POWER_I2C_DELAY_CYCLES,
            PLATFORM_POWER_I2C_STRETCH_TIMEOUT) != SOFT_I2C_OK)
    {
        return PLATFORM_PMIC_ERROR;
    }

    if (AXP2101_SoftI2CAdapter_Bind(&hplatform_power, &hplatform_power_i2c) != AXP2101_OK)
    {
        return PLATFORM_PMIC_ERROR;
    }

    if (AXP2101_Init(&hplatform_power) != AXP2101_OK)
    {
        return PLATFORM_PMIC_ERROR;
    }

    uint32_t profile_count = sizeof(platform_power_boot_profile) /
                             sizeof(platform_power_boot_profile[0]);

    if (AXP2101_ApplyConfiguration(&hplatform_power,
                                   platform_power_boot_profile,
                                   profile_count) != AXP2101_OK)
    {
        return PLATFORM_PMIC_ERROR;
    }

    return PLATFORM_OK;
}

/**
  * @brief  开启或关闭本板音频电源。
  * @param  enabled true 开启，false 关闭。
  * @retval PLATFORM_OK 操作成功。
  * @retval PLATFORM_AUDIO_ERROR AXP2101 ALDO1 操作失败。
  */
Platform_StatusTypeDef Platform_Power_SetAudio(bool enabled)
{
    return (AXP2101_SetALDO1Enabled(&hplatform_power, enabled) == AXP2101_OK)
        ? PLATFORM_OK
        : PLATFORM_AUDIO_ERROR;
}

/**
  * @brief  开启或关闭本板 LCD 电源。
  * @param  enabled true 开启，false 关闭。
  * @retval PLATFORM_OK 操作成功。
  * @retval PLATFORM_LCD_ERROR AXP2101 ALDO2 操作失败。
  */
Platform_StatusTypeDef Platform_Power_SetLCD(bool enabled)
{
    return (AXP2101_SetALDO2Enabled(&hplatform_power, enabled) == AXP2101_OK)
        ? PLATFORM_OK
        : PLATFORM_LCD_ERROR;
}

/**
  * @brief  复制最近一次板级电源错误诊断。
  * @param  diagnostics 接收只读快照的调用者缓冲区。
  * @retval PLATFORM_OK 复制成功。
  * @retval PLATFORM_PMIC_ERROR 参数为空。
  */
Platform_StatusTypeDef Platform_Power_GetDiagnostics(
    Platform_Power_DiagnosticsTypeDef *diagnostics)
{
    if (diagnostics == NULL)
    {
        return PLATFORM_PMIC_ERROR;
    }

    diagnostics->DeviceState = (uint32_t)hplatform_power.State;
    diagnostics->DeviceError = (uint32_t)hplatform_power.ErrorCode;
    diagnostics->BusStatus = (uint32_t)hplatform_power.LastBusStatus;
    diagnostics->FailedRegister = hplatform_power.LastFailedRegister;
    return PLATFORM_OK;
}
