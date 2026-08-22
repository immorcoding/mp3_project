/**
  ******************************************************************************
  * @file    platform_sdram_config.h
  * @brief   MT48LC16M16A2-6A 与 AS4C16M16SA-7TCN 共用的 SDRAM 固定配置。
  ******************************************************************************
  */

#ifndef PLATFORM_SDRAM_CONFIG_H
#define PLATFORM_SDRAM_CONFIG_H

/** @brief FMC SDRAM Bank1 的 Cortex-M 可访问基地址。 */
#define PLATFORM_SDRAM_BASE_ADDRESS                (0xC0000000UL)

/** @brief 两种兼容 4 Meg × 16 × 4 Bank SDRAM 的可用总容量，单位为字节。 */
#define PLATFORM_SDRAM_CAPACITY_BYTES              (32UL * 1024UL * 1024UL)

/** @brief SDRAM 的物理数据总线宽度，单位为字节。 */
#define PLATFORM_SDRAM_DATA_WIDTH_BYTES            (2UL)

/** @brief 上电后、发出 PRECHARGE ALL 前的最小等待时间，单位为毫秒。 */
#define PLATFORM_SDRAM_POWER_UP_DELAY_MS           (1UL)

/** @brief JEDEC 初始化阶段执行的 AUTO REFRESH 次数。 */
#define PLATFORM_SDRAM_INITIAL_AUTO_REFRESH_COUNT  (8UL)

/** @brief FMC 刷新计数：135 MHz SDRAM 时钟对应 64 ms / 8192 行刷新要求。 */
#define PLATFORM_SDRAM_REFRESH_RATE                (995UL)

/** @brief 模式寄存器：Burst 4、顺序寻址、CAS 3、单位置写突发。 */
#define PLATFORM_SDRAM_MODE_REGISTER_VALUE         (0x232UL)

/** @brief 数据总线测试的基准图样。 */
#define PLATFORM_SDRAM_DATA_BUS_PATTERN            (0x0001U)

/** @brief 地址总线测试的正常图样。 */
#define PLATFORM_SDRAM_ADDRESS_PATTERN              (0xA55AU)

/** @brief 全容量读写校验使用的初始异或图样。 */
#define PLATFORM_SDRAM_MEMORY_PATTERN_SEED          (0x5A5AU)

#endif /* PLATFORM_SDRAM_CONFIG_H */
