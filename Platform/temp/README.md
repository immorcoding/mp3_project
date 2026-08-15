# Platform Temperature

本 Module 向上层提供当前产品的 MCU 结温能力，并选择本 PCB 上由 CubeMX 初始化的 `hadc3`。它不暴露 HAL Handle、原始 ADC 值、工厂标定数据或采样顺序。

## 公开 Interface

- `Platform_Temp_Init()`：启动时校准当前 PCB 的 ADC3 温度采样通道；
- `Platform_Temp_Read()`：在初始化后读取当前 MCU 结温，输出单位为毫摄氏度。

## 编译期依赖与装配

- `Adapters/stm32_hal/temp` 的公开读取 Interface；
- CubeMX `adc.h` 的 `hadc3` Handle。

## 运行时请求路径

Monitor Task 启动时通过 `Platform_Temp_Init()` 校准一次；APP 或 Service 随后可通过 `Platform_Temp_Read()` 请求结温。本 Module 把当前 PCB 的 ADC3 Handle 交给 Adapter，Adapter 返回换算后的温度。

## 资源与约束

- `hadc3` 由 CubeMX/Core 拥有，本 Module 只借用它；
- `hadc3` 的双 Rank、`ADC_EOC_SINGLE_CONV` 与 `LowPowerAutoWait` 配置是 Adapter 的运行前提，具体配置约束见 `Adapters/stm32_hal/temp/README.md`；
- MCU 结温不等同于环境温度；本 Interface 不实现散热控制或过温策略；
- 当前只有 STM32H7 的一个实现，故不提前建立抽象 Component。出现第二种 MCU 或需要统一热管理能力时，再评估提取 `Components/temp`。

## 禁止依赖

- 不包含 `APP`、`Service` 或 FreeRTOS；
- 不创建任务、不决定采样周期、不输出日志。

## 命名

面向上层的 Interface 使用 `Platform_Temp_*`；文件内辅助对象使用 `snake_case`。
