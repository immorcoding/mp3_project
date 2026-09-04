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

/* axp2101.h */
#define AXP2101_SLAVE_ADDRESS                  (0x34)     /* AXP2101 默认 7 位 I2C 地址，不包含读写位。 */

/* axp2101.c */
#define XPOWERS_AXP2101_CHIP_ID1               (0x47)     /* REG03H 应返回的 AXP2101 芯片 ID。 */
#define XPOWERS_AXP2101_CHIP_ID2               (0x4A)     /* REG03H 备选芯片 ID。 */

#define XPOWERS_AXP2101_STATUS1                (0x00)     /* 状态寄存器 1：电源输入、充电和工作状态；读取不修改配置。 */
#define XPOWERS_AXP2101_STATUS2                (0x01)     /* 状态寄存器 2。 */
#define XPOWERS_AXP2101_IC_TYPE                (0x03)     /* 芯片型号寄存器，正常值为 XPOWERS_AXP2101_CHIP_ID。 */

#define XPOWERS_AXP2101_DATA_BUFFER1           (0x04)     /* 通用数据缓冲第 1 字节，可用于掉电保持的小量用户数据。 */
#define XPOWERS_AXP2101_DATA_BUFFER2           (0x05)     /* 通用数据缓冲第 2 字节。 */
#define XPOWERS_AXP2101_DATA_BUFFER3           (0x06)     /* 通用数据缓冲第 3 字节。 */
#define XPOWERS_AXP2101_DATA_BUFFER4           (0x07)     /* 通用数据缓冲第 4 字节。 */
#define XPOWERS_AXP2101_DATA_BUFFER_SIZE       (4u)       /* 通用数据缓冲区字节数。 */

#define XPOWERS_AXP2101_COMMON_CONFIG          (0x10)     /* 公共配置；含软件关机/重启等动作位，写入前必须核对位定义。 */
#define XPOWERS_AXP2101_BATFET_CTRL            (0x12)     /* REG12H：BATFET 控制。 */
#define XPOWERS_AXP2101_DIE_TEMP_CTRL          (0x13)     /* REG13H：芯片温度控制。 */
#define XPOWERS_AXP2101_MIN_SYS_VOL_CTRL       (0x14)     /* REG14H：最小系统电压控制。 */
#define XPOWERS_AXP2101_INPUT_VOL_LIMIT_CTRL   (0x15)     /* REG15H：输入电压限制。 */
#define XPOWERS_AXP2101_INPUT_CUR_LIMIT_CTRL   (0x16)     /* REG16H：输入电流限制。 */
#define XPOWERS_AXP2101_RESET_FUEL_GAUGE       (0x17)     /* REG17H：复位电量计。 */
#define XPOWERS_AXP2101_CHARGE_GAUGE_WDT_CTRL  (0x18)     /* REG18H：充电电量计与看门狗控制。 */

#define XPOWERS_AXP2101_WDT_CTRL               (0x19)     /* REG19H：看门狗控制。 */
#define XPOWERS_AXP2101_LOW_BAT_WARN_SET       (0x1A)     /* REG1AH：低电量告警阈值。 */

#define XPOWERS_AXP2101_PWRON_STATUS           (0x20)     /* 开机状态；本组决定开关机条件和电源时序，属于高风险策略寄存器。 */
#define XPOWERS_AXP2101_PWROFF_STATUS          (0x21)     /* REG21H：关机状态。 */
#define XPOWERS_AXP2101_PWROFF_EN              (0x22)     /* REG22H：关机使能。 */
#define XPOWERS_AXP2101_DC_OVP_UVP_CTRL        (0x23)     /* REG23H：DCDC 过压/欠压保护。 */
#define XPOWERS_AXP2101_VOFF_SET               (0x24)     /* REG24H：关机电压设定。 */
#define XPOWERS_AXP2101_PWROK_SEQU_CTRL        (0x25)     /* REG25H：PWROK 时序控制。 */
#define XPOWERS_AXP2101_SLEEP_WAKEUP_CTRL      (0x26)     /* REG26H：睡眠唤醒控制。 */
#define XPOWERS_AXP2101_IRQ_OFF_ON_LEVEL_CTRL  (0x27)     /* REG27H：IRQ 开关机电平控制。 */

#define XPOWERS_AXP2101_FAST_PWRON_SET0        (0x28)     /* REG28H：快速开机配置 0。 */
#define XPOWERS_AXP2101_FAST_PWRON_SET1        (0x29)     /* REG29H：快速开机配置 1。 */
#define XPOWERS_AXP2101_FAST_PWRON_SET2        (0x2A)     /* REG2AH：快速开机配置 2。 */
#define XPOWERS_AXP2101_FAST_PWRON_CTRL        (0x2B)     /* REG2BH：快速开机控制。 */

#define XPOWERS_AXP2101_ADC_CHANNEL_CTRL       (0x30)     /* ADC 通道控制；结果通常跨多个寄存器，换算应由设备层语义函数实现。 */
#define XPOWERS_AXP2101_ADC_DATA_RESULT0       (0x34)     /* ADC 结果寄存器 0。 */
#define XPOWERS_AXP2101_ADC_DATA_RESULT1       (0x35)     /* ADC 结果寄存器 1。 */
#define XPOWERS_AXP2101_ADC_DATA_RESULT2       (0x36)     /* ADC 结果寄存器 2。 */
#define XPOWERS_AXP2101_ADC_DATA_RESULT3       (0x37)     /* ADC 结果寄存器 3。 */
#define XPOWERS_AXP2101_ADC_DATA_RESULT4       (0x38)     /* ADC 结果寄存器 4。 */
#define XPOWERS_AXP2101_ADC_DATA_RESULT5       (0x39)     /* ADC 结果寄存器 5。 */
#define XPOWERS_AXP2101_ADC_DATA_RESULT6       (0x3A)     /* ADC 结果寄存器 6。 */
#define XPOWERS_AXP2101_ADC_DATA_RESULT7       (0x3B)     /* ADC 结果寄存器 7。 */
#define XPOWERS_AXP2101_ADC_DATA_RESULT8       (0x3C)     /* ADC 结果寄存器 8。 */
#define XPOWERS_AXP2101_ADC_DATA_RESULT9       (0x3D)     /* ADC 结果寄存器 9。 */

#define XPOWERS_AXP2101_INTEN1                 (0x40)     /* 中断使能寄存器 1。 */
#define XPOWERS_AXP2101_INTEN2                 (0x41)     /* 中断使能寄存器 2。 */
#define XPOWERS_AXP2101_INTEN3                 (0x42)     /* 中断使能寄存器 3。 */

#define XPOWERS_AXP2101_INTSTS1                (0x48)     /* 中断状态寄存器 1；向状态位写 1 清除对应挂起。 */
#define XPOWERS_AXP2101_INTSTS2                (0x49)     /* 中断状态寄存器 2。 */
#define XPOWERS_AXP2101_INTSTS3                (0x4A)     /* 中断状态寄存器 3。 */
#define XPOWERS_AXP2101_INTSTS_CNT             (3)        /* 中断状态寄存器个数。 */

#define AXP2101_LDO_CTRL0_ALDO1_ENABLE_MASK    (1u << 0)  /* REG90H bit0：ALDO1 输出使能，1 为开启，0 为关闭。 */
#define AXP2101_LDO_CTRL0_ALDO2_ENABLE_MASK    (1u << 1)  /* REG90H bit1：ALDO2 输出使能，1 为开启，0 为关闭。 */

#define XPOWERS_AXP2101_TS_PIN_CTRL            (0x50)     /* REG50H：TS 引脚控制。 */
#define XPOWERS_AXP2101_TS_HYSL2H_SET          (0x52)     /* REG52H：TS 高阈值设定。 */
#define XPOWERS_AXP2101_TS_LYSL2H_SET          (0x53)     /* REG53H：TS 低阈值设定。 */

#define XPOWERS_AXP2101_VLTF_CHG_SET           (0x54)     /* REG54H：充电低温阈值。 */
#define XPOWERS_AXP2101_VHLTF_CHG_SET          (0x55)     /* REG55H：充电高温阈值。 */
#define XPOWERS_AXP2101_VLTF_WORK_SET          (0x56)     /* REG56H：工作低温阈值。 */
#define XPOWERS_AXP2101_VHLTF_WORK_SET         (0x57)     /* REG57H：工作高温阈值。 */

#define XPOWERS_AXP2101_JIETA_EN_CTRL          (0x58)     /* REG58H：JEITA 使能。 */
#define XPOWERS_AXP2101_JIETA_SET0             (0x59)     /* REG59H：JEITA 设定 0。 */
#define XPOWERS_AXP2101_JIETA_SET1             (0x5A)     /* REG5AH：JEITA 设定 1。 */
#define XPOWERS_AXP2101_JIETA_SET2             (0x5B)     /* REG5BH：JEITA 设定 2。 */

#define XPOWERS_AXP2101_IPRECHG_SET            (0x61)     /* 预充电流；修改充电电流/电压前必须核对电池规格、热设计和输入能力。 */
#define XPOWERS_AXP2101_ICC_CHG_SET            (0x62)     /* REG62H：恒流充电电流。 */
#define XPOWERS_AXP2101_ITERM_CHG_SET_CTRL     (0x63)     /* REG63H：终止充电电流。 */
#define XPOWERS_AXP2101_CV_CHG_VOL_SET         (0x64)     /* REG64H：恒压充电电压。 */
#define XPOWERS_AXP2101_THE_REGU_THRES_SET     (0x65)     /* REG65H：热调节阈值。 */
#define XPOWERS_AXP2101_CHG_TIMEOUT_SET_CTRL   (0x67)     /* REG67H：充电超时。 */
#define XPOWERS_AXP2101_BAT_DET_CTRL           (0x68)     /* REG68H：电池检测控制。 */
#define XPOWERS_AXP2101_CHGLED_SET_CTRL        (0x69)     /* REG69H：充电指示灯控制。 */

#define XPOWERS_AXP2101_BTN_VOL_MIN            (2600)     /* 纽扣电池充电电压最小值，单位 mV。 */
#define XPOWERS_AXP2101_BTN_VOL_MAX            (3300)     /* 纽扣电池充电电压最大值，单位 mV。 */
#define XPOWERS_AXP2101_BTN_VOL_STEPS          (100)      /* 纽扣电池充电电压步进，单位 mV。 */
#define XPOWERS_AXP2101_BTN_BAT_CHG_VOL_SET    (0x6A)     /* REG6AH：纽扣电池充电电压设定。 */

#define XPOWERS_AXP2101_DC_ONOFF_DVM_CTRL      (0x80)     /* DCDC 使能；与 DC_VOLx 分别控制使能和目标电压，二者语义不能混淆。 */
#define XPOWERS_AXP2101_DC_FORCE_PWM_CTRL      (0x81)     /* REG81H：DCDC 强制 PWM。 */
#define XPOWERS_AXP2101_DC_VOL0_CTRL           (0x82)     /* REG82H：DCDC0 电压。 */
#define XPOWERS_AXP2101_DC_VOL1_CTRL           (0x83)     /* REG83H：DCDC1 电压。 */
#define XPOWERS_AXP2101_DC_VOL2_CTRL           (0x84)     /* REG84H：DCDC2 电压。 */
#define XPOWERS_AXP2101_DC_VOL3_CTRL           (0x85)     /* REG85H：DCDC3 电压。 */
#define XPOWERS_AXP2101_DC_VOL4_CTRL           (0x86)     /* REG86H：DCDC4 电压。 */

#define XPOWERS_AXP2101_LDO_ONOFF_CTRL0        (0x90)     /* LDO 使能 0；与 LDO_VOLx 分别控制使能和预设电压，设电压不会自动开启。 */
#define XPOWERS_AXP2101_LDO_ONOFF_CTRL1        (0x91)     /* REG91H：LDO 使能 1。 */
#define XPOWERS_AXP2101_LDO_VOL0_CTRL          (0x92)     /* REG92H：LDO0 电压。 */
#define XPOWERS_AXP2101_LDO_VOL1_CTRL          (0x93)     /* REG93H：LDO1 电压。 */
#define XPOWERS_AXP2101_LDO_VOL2_CTRL          (0x94)     /* REG94H：LDO2 电压。 */
#define XPOWERS_AXP2101_LDO_VOL3_CTRL          (0x95)     /* REG95H：LDO3 电压。 */
#define XPOWERS_AXP2101_LDO_VOL4_CTRL          (0x96)     /* REG96H：LDO4 电压。 */
#define XPOWERS_AXP2101_LDO_VOL5_CTRL          (0x97)     /* REG97H：LDO5 电压。 */
#define XPOWERS_AXP2101_LDO_VOL6_CTRL          (0x98)     /* REG98H：LDO6 电压。 */
#define XPOWERS_AXP2101_LDO_VOL7_CTRL          (0x99)     /* REG99H：LDO7 电压。 */
#define XPOWERS_AXP2101_LDO_VOL8_CTRL          (0x9A)     /* REG9AH：LDO8 电压。 */

#define XPOWERS_AXP2101_BAT_PARAM              (0xA1)     /* 电量计参数；不能只凭单次百分比读数判断电池健康。 */
#define XPOWERS_AXP2101_FUEL_GAUGE_CTRL        (0xA2)     /* REGA2H：电量计控制。 */
#define XPOWERS_AXP2101_BAT_PERCENT_DATA       (0xA4)     /* REGA4H：电池百分比。 */

#endif /* AXP2101_CONFIG_H */
