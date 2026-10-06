# Hardware reference

[Hardware](hardware.md) 的硬件事实与调试依据；模块接口和寄存器/宏定义由链接目标维护。

## power

当前链路是 Platform Power → AXP2101 → SoftI2C Bridge → SoftI2C → STM32 GPIO Adapter；决定依据在 [ADR-0003](../adr/0003-pmic-softi2c-bridge.md)。Bind 只装配；AXP2101 Init 的 Prepare 才初始化总线、读取并校验芯片 ID，随后按 Platform 启动表顺序写入配置。局部接口见 [AXP2101](../../Components/axp2101/README.md) 和 [Platform Power](../../Platform/power/README.md)。

SoftI2C 的 LOW 是拉低，RELEASED 是释放开漏线让外部上拉产生高电平，不是推挽输出高。它使用忙等待且无锁；多个使用者需要在现有实例外串行化，不向 Component 加 RTOS 依赖。

本板音频接 ALDO1、LCD 接 ALDO2，触摸共享 LCD 供电。启动表中的输入限制、充电、电压和使能是 PCB 策略；寄存器位定义在芯片 config，具体值在 [Platform 实现](../../Platform/power/platform_power.c)，不在文档另存一张表。当前没有低电量、关机恢复或通用电源服务；它们不应由驱动默认承担。

## memory

兼容器件为 MT48LC16M16A2-6A 与 AS4C16M16SA-7TCN：32 MiB、x16、4 bank、13 行/9 列地址、8192 行/64 ms 刷新。FMC 时序按较慢的 AS4 -7 等级取整，当前 PLL2/分频得到 130 MHz；刷新计数依据 `floor(fSDRAM × 64 ms / 8192) - 20`，频率变化需重算。具体控制器字段在 CubeMX 源和 [Platform 配置](../../Platform/sdram/platform_sdram_config.h)。

初始化分两段（[ADR-0008](../adr/0008-sdram-early-init-and-noload-ownership.md)）：

1. `SystemInit()` 后、C 运行库 `.data/.bss` 循环前，`fmc.c` 的 `SDRAM_EarlyInit()` 只用局部状态建立约 32 MHz 临时时序。CLK ENABLE 后至少等待 200 µs；复位约 64 MHz 下的 12,800 次 NOP 满足较慢器件的稳定要求。
2. `main()` 中 CubeMX 切换最终 FMC 时钟，Platform 正式执行 CLK ENABLE → 稳定等待 → PRECHARGE ALL → 8 次 AUTO REFRESH → LOAD MODE REGISTER → 刷新计数。两阶段之间的数据不作为有效内容。

`.sdram_framebuffer (NOLOAD)` 位于外部 SDRAM，边界按 Cache line 对齐；不从 Flash 加载也不由启动代码清零。其首次填充职责见 Hardware memory 规则，早期接缝授权边界仍是 ARC-3。

[Platform SDRAM](../../Platform/sdram/README.md) 的诊断依次检查数据线、地址线、全容量地址图样；写入测速包含 D-Cache Clean，读取前 Invalidate，测到的是外存提交/冷读而非 Cache 命中。DWT 计时基准是核心时钟。Cache 和周期计数统一由 Cortex Adapter 提供（[ADR-0006](../adr/0006-cache-range-ownership.md)）。诊断覆写全容量，默认关闭；启用前核对实际宏在调用翻译单元中的生效情况与所有业务缓冲占用。

## input

本板 FT6X36 通过 I2C2 轮询第一触点。地址在 Component/Platform 中为 7-bit，仅 STM32 HAL Adapter 调用前左移。初始化先复位并等待稳定、再探测 I2C；具体延时和地址在各拥有方配置，不另维护数值副本。

Chip ID 读取只证明寄存器通信，不能预设固定型号 ID；启动当前不读取或打印它。协议从 TD_STATUS 起取触点数和第一点原始坐标，无触摸是成功结果，多点只用第一点；细节见 [FT6X36](../../Components/ft6x36/README.md)。

启动失败或一次真实读点失败后，Platform 可用性变为 false；查询本身不访问 I2C。GUI 先查询再读取，Platform 的读点接口也短路保护；失效后持续 RELEASED，下一次系统初始化重新建立通信。GUI 负责坐标交换/镜像，模组缺失不能留下旧的按下状态。

TP_IRQ 当前仅有 CubeMX EXTI 配置、没有订阅回调；未来唤醒仍按 ARC-10 在任务中读点。当前没有 DMA、多指/手势或低功耗唤醒实现。真机核对点击和方向；故障从供电、TP_RST、I2C2 上拉、地址、接线及诊断状态排查。

## temperature

[Platform Temperature](../../Platform/temp/README.md) 输出 MCU 结温（毫摄氏度），不是环境温度；Monitor 决定采样与日志，Adapter 只借用 ADC Handle。当前低频轮询不承担热管理。

ADC3 顺序转换温度和 VREFINT。Auto Wait 在当前 Rank 结果被读取前暂停后续转换，避免单个 DR 被覆盖。温度采样时间必须满足器件要求；当前采样配置及约束见 [Adapter](../../Adapters/stm32_hal/temp/README.md)，不与 ADC DMA/中断混用。

VREFINT 工厂值先估算实际 VDDA，再把温度原始值折算到工厂校准电压，最后用 TS_CAL1/TS_CAL2 插值得到结温；否则供电波动会被误当成温变。启动前 CubeMX 已开启内部通道并提供稳定时间，Monitor 启动校准一次；Stop 保留校准，DeInit、deep-power-down 或复位后重校准。

校准、启动、轮询、停止、原始值或工厂数据异常都返回 Platform 错误。Monitor 连续失败只记录首条，成功一次后恢复正常记录。扩展连续采样前重新评估资源仲裁、DMA 缓冲与完成路径。
