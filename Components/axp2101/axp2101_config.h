/**
  ******************************************************************************
  * @file    axp2101_config.h
  * @brief   AXP2101 Component 的私有固定定义。
  *
  * @details
  *          本文件集中保存 AXP2101 的默认地址、芯片 ID、寄存器映射和固定
  *          位掩码，不执行任何总线访问。寄存器名称和地址依据 AXP2101
  *          Datasheet V1.4；位级配置应在 Device Implementation 中使用语义
  *          明确的掩码完成，避免在应用层直接写入裸寄存器值。
  *
  * @note    这里多数宏只定义“寄存器地址”，不等于该寄存器可安全整字节
  *          覆盖。保留位、写 1 清零位和动作位必须按数据手册采用读改写或
  *          专用语义接口处理。
  ******************************************************************************
  */

#ifndef AXP2101_CONFIG_H
#define AXP2101_CONFIG_H

/* Device identification ----------------------------------------------------*/
/** @brief AXP2101 默认 7 位 I2C 地址，不包含读写位。 */
#define AXP2101_SLAVE_ADDRESS                            (0x34)

/** @brief REG03H 应返回的 AXP2101 芯片 ID。 */
#define XPOWERS_AXP2101_CHIP_ID1                         (0x47)
#define XPOWERS_AXP2101_CHIP_ID2                         (0x4A)

/* Status and data buffer registers -----------------------------------------*/
/** @note 状态寄存器反映电源输入、充电和工作状态；读取不会修改配置。 */
#define XPOWERS_AXP2101_STATUS1                          (0x00)
#define XPOWERS_AXP2101_STATUS2                          (0x01)
/** @brief 芯片型号寄存器，正常值为 XPOWERS_AXP2101_CHIP_ID。 */
#define XPOWERS_AXP2101_IC_TYPE                          (0x03)

/** @brief 4 字节通用数据缓冲区，可用于掉电保持的小量用户数据。 */
#define XPOWERS_AXP2101_DATA_BUFFER1                     (0x04)
#define XPOWERS_AXP2101_DATA_BUFFER2                     (0x05)
#define XPOWERS_AXP2101_DATA_BUFFER3                     (0x06)
#define XPOWERS_AXP2101_DATA_BUFFER4                     (0x07)
#define XPOWERS_AXP2101_DATA_BUFFER_SIZE                 (4u)

/* Common, protection and input control registers ---------------------------*/
/** @note 本组包含软件关机/重启等动作位，写入前必须核对位定义。 */
#define XPOWERS_AXP2101_COMMON_CONFIG                    (0x10)
#define XPOWERS_AXP2101_BATFET_CTRL                      (0x12)
#define XPOWERS_AXP2101_DIE_TEMP_CTRL                    (0x13)
#define XPOWERS_AXP2101_MIN_SYS_VOL_CTRL                 (0x14)
#define XPOWERS_AXP2101_INPUT_VOL_LIMIT_CTRL             (0x15)
#define XPOWERS_AXP2101_INPUT_CUR_LIMIT_CTRL             (0x16)
#define XPOWERS_AXP2101_RESET_FUEL_GAUGE                 (0x17)
#define XPOWERS_AXP2101_CHARGE_GAUGE_WDT_CTRL            (0x18)

/* Watchdog and battery warning registers -----------------------------------*/
#define XPOWERS_AXP2101_WDT_CTRL                         (0x19)
#define XPOWERS_AXP2101_LOW_BAT_WARN_SET                 (0x1A)

/* Power-on, power-off and sequence control registers -----------------------*/
/** @note 本组决定开关机条件和电源时序，属于高风险策略寄存器。 */
#define XPOWERS_AXP2101_PWRON_STATUS                     (0x20)
#define XPOWERS_AXP2101_PWROFF_STATUS                    (0x21)
#define XPOWERS_AXP2101_PWROFF_EN                        (0x22)
#define XPOWERS_AXP2101_DC_OVP_UVP_CTRL                  (0x23)
#define XPOWERS_AXP2101_VOFF_SET                         (0x24)
#define XPOWERS_AXP2101_PWROK_SEQU_CTRL                  (0x25)
#define XPOWERS_AXP2101_SLEEP_WAKEUP_CTRL                (0x26)
#define XPOWERS_AXP2101_IRQ_OFF_ON_LEVEL_CTRL            (0x27)

/* Fast power-on configuration registers ------------------------------------*/
#define XPOWERS_AXP2101_FAST_PWRON_SET0                  (0x28)
#define XPOWERS_AXP2101_FAST_PWRON_SET1                  (0x29)
#define XPOWERS_AXP2101_FAST_PWRON_SET2                  (0x2A)
#define XPOWERS_AXP2101_FAST_PWRON_CTRL                  (0x2B)

/* ADC control and result registers -----------------------------------------*/
/** @note ADC 结果通常跨多个寄存器组合，换算公式应由设备层语义函数实现。 */
#define XPOWERS_AXP2101_ADC_CHANNEL_CTRL                 (0x30)
#define XPOWERS_AXP2101_ADC_DATA_RESULT0                 (0x34)
#define XPOWERS_AXP2101_ADC_DATA_RESULT1                 (0x35)
#define XPOWERS_AXP2101_ADC_DATA_RESULT2                 (0x36)
#define XPOWERS_AXP2101_ADC_DATA_RESULT3                 (0x37)
#define XPOWERS_AXP2101_ADC_DATA_RESULT4                 (0x38)
#define XPOWERS_AXP2101_ADC_DATA_RESULT5                 (0x39)
#define XPOWERS_AXP2101_ADC_DATA_RESULT6                 (0x3A)
#define XPOWERS_AXP2101_ADC_DATA_RESULT7                 (0x3B)
#define XPOWERS_AXP2101_ADC_DATA_RESULT8                 (0x3C)
#define XPOWERS_AXP2101_ADC_DATA_RESULT9                 (0x3D)

/* Interrupt enable registers -----------------------------------------------*/
#define XPOWERS_AXP2101_INTEN1                           (0x40)
#define XPOWERS_AXP2101_INTEN2                           (0x41)
#define XPOWERS_AXP2101_INTEN3                           (0x42)

/* Interrupt status registers -----------------------------------------------*/
/** @note 向状态位写 1 清除对应的中断挂起状态。 */
#define XPOWERS_AXP2101_INTSTS1                          (0x48)
#define XPOWERS_AXP2101_INTSTS2                          (0x49)
#define XPOWERS_AXP2101_INTSTS3                          (0x4A)
#define XPOWERS_AXP2101_INTSTS_CNT                       (3)

/* LDO control register bit definitions -------------------------------------*/
/** @brief REG90H bit0：ALDO1 输出使能位，1 为开启，0 为关闭。 */
#define AXP2101_LDO_CTRL0_ALDO1_ENABLE_MASK              (1u << 0)
/** @brief REG90H bit1：ALDO2 输出使能位，1 为开启，0 为关闭。 */
#define AXP2101_LDO_CTRL0_ALDO2_ENABLE_MASK              (1u << 1)

/* Battery temperature sensing registers ------------------------------------*/
#define XPOWERS_AXP2101_TS_PIN_CTRL                      (0x50)
#define XPOWERS_AXP2101_TS_HYSL2H_SET                    (0x52)
#define XPOWERS_AXP2101_TS_LYSL2H_SET                    (0x53)

/* Charge/work temperature threshold registers ------------------------------*/
#define XPOWERS_AXP2101_VLTF_CHG_SET                     (0x54)
#define XPOWERS_AXP2101_VHLTF_CHG_SET                    (0x55)
#define XPOWERS_AXP2101_VLTF_WORK_SET                    (0x56)
#define XPOWERS_AXP2101_VHLTF_WORK_SET                   (0x57)

/* JEITA temperature charge control registers -------------------------------*/
#define XPOWERS_AXP2101_JIETA_EN_CTRL                    (0x58)
#define XPOWERS_AXP2101_JIETA_SET0                       (0x59)
#define XPOWERS_AXP2101_JIETA_SET1                       (0x5A)
#define XPOWERS_AXP2101_JIETA_SET2                       (0x5B)

/* Charger configuration registers ------------------------------------------*/
/** @note 修改充电电流/电压前必须核对电池规格、热设计和输入能力。 */
#define XPOWERS_AXP2101_IPRECHG_SET                      (0x61)
#define XPOWERS_AXP2101_ICC_CHG_SET                      (0x62)
#define XPOWERS_AXP2101_ITERM_CHG_SET_CTRL               (0x63)

#define XPOWERS_AXP2101_CV_CHG_VOL_SET                   (0x64)

#define XPOWERS_AXP2101_THE_REGU_THRES_SET               (0x65)
#define XPOWERS_AXP2101_CHG_TIMEOUT_SET_CTRL             (0x67)

#define XPOWERS_AXP2101_BAT_DET_CTRL                     (0x68)
#define XPOWERS_AXP2101_CHGLED_SET_CTRL                  (0x69)

/* Button battery charge voltage conversion range ---------------------------*/
/** @brief 纽扣电池充电电压最小值，单位 mV。 */
#define XPOWERS_AXP2101_BTN_VOL_MIN                      (2600)
/** @brief 纽扣电池充电电压最大值，单位 mV。 */
#define XPOWERS_AXP2101_BTN_VOL_MAX                      (3300)
/** @brief 纽扣电池充电电压步进，单位 mV。 */
#define XPOWERS_AXP2101_BTN_VOL_STEPS                    (100)


#define XPOWERS_AXP2101_BTN_BAT_CHG_VOL_SET              (0x6A)

/* DCDC enable, mode and voltage registers ----------------------------------*/
/** @note DC_ONOFF 与 DC_VOLx 分别控制使能和目标电压，二者语义不能混淆。 */
#define XPOWERS_AXP2101_DC_ONOFF_DVM_CTRL                (0x80)
#define XPOWERS_AXP2101_DC_FORCE_PWM_CTRL                (0x81)
#define XPOWERS_AXP2101_DC_VOL0_CTRL                     (0x82)
#define XPOWERS_AXP2101_DC_VOL1_CTRL                     (0x83)
#define XPOWERS_AXP2101_DC_VOL2_CTRL                     (0x84)
#define XPOWERS_AXP2101_DC_VOL3_CTRL                     (0x85)
#define XPOWERS_AXP2101_DC_VOL4_CTRL                     (0x86)

/* LDO enable and voltage registers -----------------------------------------*/
/** @note LDO_ONOFF 与 LDO_VOLx 分别控制使能和预设电压，设电压不会自动开启。 */
#define XPOWERS_AXP2101_LDO_ONOFF_CTRL0                  (0x90)
#define XPOWERS_AXP2101_LDO_ONOFF_CTRL1                  (0x91)
#define XPOWERS_AXP2101_LDO_VOL0_CTRL                    (0x92)
#define XPOWERS_AXP2101_LDO_VOL1_CTRL                    (0x93)
#define XPOWERS_AXP2101_LDO_VOL2_CTRL                    (0x94)
#define XPOWERS_AXP2101_LDO_VOL3_CTRL                    (0x95)
#define XPOWERS_AXP2101_LDO_VOL4_CTRL                    (0x96)
#define XPOWERS_AXP2101_LDO_VOL5_CTRL                    (0x97)
#define XPOWERS_AXP2101_LDO_VOL6_CTRL                    (0x98)
#define XPOWERS_AXP2101_LDO_VOL7_CTRL                    (0x99)
#define XPOWERS_AXP2101_LDO_VOL8_CTRL                    (0x9A)

/* Fuel-gauge registers ------------------------------------------------------*/
/** @note 电量计状态依赖芯片内部算法，不能只凭单次百分比读数判断电池健康。 */
#define XPOWERS_AXP2101_BAT_PARAM                        (0xA1)
#define XPOWERS_AXP2101_FUEL_GAUGE_CTRL                  (0xA2)
#define XPOWERS_AXP2101_BAT_PERCENT_DATA                 (0xA4)


#endif /* AXP2101_CONFIG_H */
