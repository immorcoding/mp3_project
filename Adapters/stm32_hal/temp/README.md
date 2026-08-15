# STM32 HAL Temperature Adapter

本 Module 将 STM32H7 的 ADC3 内部温度传感器与 VREFINT 转换为毫摄氏度结温。它隐藏 HAL ADC 校准、启动/轮询/停止、双 Rank 原始采样、VDDA 修正和芯片工厂标定数据。

## 公开 Interface

- `Temp_STM32HAL_Calibrate()`：校准一个已由 CubeMX 配置的 ADC 单端输入偏移；
- `Temp_STM32HAL_Read()`：在校准后读取当前 MCU 结温，单位为毫摄氏度。

## 编译期依赖

- STM32H7 HAL ADC/ADCEx Interface；
- `stm32h7xx_ll_adc.h` 提供的内部通道工厂标定地址与常量；本 Module 不调用任何 LL 外设控制 Interface；
- 同目录的固定配置宏。

## 运行时请求路径

`Platform_Temp_Read()` 传入当前 PCB 选定的 `hadc3`；本 Adapter 读取 Temperature Sensor 与 VREFINT 的两个 Rank，并返回校正后的结温。

## 资源与约束

- 仅借用调用者传入的 ADC Handle，不拥有或保存 `hadc3`；
- CubeMX 必须配置 Rank 1 = Temperature Sensor、Rank 2 = VREFINT，以开启其内部模拟路径并保留各自的采样时间；
- CubeMX 必须启用 `LowPowerAutoWait`，并使用 `ADC_EOC_SINGLE_CONV`。每次 `HAL_ADC_GetValue()` 读取当前 Rank 后，硬件才开始下一 Rank，避免 `DR` 被覆盖；
- 温度传感器采样时间必须满足数据手册要求；当前 CubeMX 配置在 10 MHz ADC 时钟下为 810.5 cycles；
- 面向低频诊断，启动时校准一次；`HAL_ADC_Stop()` 后仍可直接读取，不可直接用于高频或 DMA 连续采样；
- 当前轮询方案不得切换为 ADC 中断或 DMA；若将来需要连续温度曲线，应建立独立 DMA 缓冲与完成路径，并关闭 Auto Wait；
- 输出是 MCU 结温，不是环境温度。

## 禁止依赖

- 不包含 `APP`、`Service`、`Platform` 或 Component；
- 不记录日志、不创建任务，也不决定采样周期。

## 命名

公开 Interface 使用 `Temp_STM32HAL_*`；文件内辅助函数使用 `temp_stm32_hal_*`。
