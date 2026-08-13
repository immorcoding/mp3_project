/**
  ******************************************************************************
  * @file    platform_power_config.h
  * @brief   当前 PCB 的 Platform Power 启动策略参数。
  *
  * @details
  *          本文件保存 SoftI2C 时序及 AXP2101 COMMON_CONFIG 的板级目标值。
  *          修改这些参数会改变本板供电行为，必须同时核对原理图、数据手册和
  *          实测结果；它们不属于 Platform Power 的公开 Interface。
  ******************************************************************************
  */

#ifndef PLATFORM_POWER_CONFIG_H
#define PLATFORM_POWER_CONFIG_H

/** @brief SoftI2C 每个逻辑时序阶段执行的忙等待循环次数。 */
#define PLATFORM_POWER_I2C_DELAY_CYCLES           800u
/** @brief 等待 AXP2101 释放 SCL 的最大轮询次数。 */
#define PLATFORM_POWER_I2C_STRETCH_TIMEOUT        1000u

/** @brief COMMON_CONFIG bit5：关机后主动对输出电容放电。 */
#define PLATFORM_POWER_COMMON_OFF_DISCHARGE_MASK       (1u << 5)
/** @brief COMMON_CONFIG bit3：PWROK 拉低时自动重启。 */
#define PLATFORM_POWER_COMMON_PWROK_RESTART_MASK       (1u << 3)
/** @brief COMMON_CONFIG bit2：PWRON 长按 16 秒执行关机。 */
#define PLATFORM_POWER_COMMON_PWRON_16S_SHUTDOWN_MASK  (1u << 2)
/** @brief COMMON_CONFIG bit1：软件重启动作控制位。 */
#define PLATFORM_POWER_COMMON_RESTART_ACTION_MASK      (1u << 1)
/** @brief COMMON_CONFIG bit0：软件关机动作控制位。 */
#define PLATFORM_POWER_COMMON_POWEROFF_ACTION_MASK     (1u << 0)

/** @brief 启动时允许 Platform 修改的 COMMON_CONFIG 位集合。 */
#define PLATFORM_POWER_COMMON_BOOT_MASK \
    (PLATFORM_POWER_COMMON_OFF_DISCHARGE_MASK | \
     PLATFORM_POWER_COMMON_PWROK_RESTART_MASK | \
     PLATFORM_POWER_COMMON_PWRON_16S_SHUTDOWN_MASK | \
     PLATFORM_POWER_COMMON_RESTART_ACTION_MASK | \
     PLATFORM_POWER_COMMON_POWEROFF_ACTION_MASK)

/** @brief 启动时写入 COMMON_CONFIG 掩码范围内的目标值。 */
#define PLATFORM_POWER_COMMON_BOOT_VALUE \
    (PLATFORM_POWER_COMMON_OFF_DISCHARGE_MASK | \
     PLATFORM_POWER_COMMON_PWRON_16S_SHUTDOWN_MASK)

#endif /* PLATFORM_POWER_CONFIG_H */
