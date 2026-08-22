# Platform LED

本 Module 表示当前 PCB 向上公开的 LED 能力。它长期持有 LED Device 与具体 Adapter Context，并以
稳定的 `Platform_LED_IdTypeDef` 将板级编号映射到实例；当前仅装配 `PLATFORM_LED_ID_STATUS` GPIO
LED。

## 公开 Interface

- `Platform_LED_Init()`：绑定并初始化所有已公开 LED，失败时返回 `PLATFORM_LED_ERROR`；
- `Platform_LED_Set()`：按板级编号请求逻辑 `PLATFORM_LED_ON/OFF`；
- `Platform_LED_Toggle()`：按板级编号翻转 LED。

编号与状态均是逻辑语义：上层不知道 GPIO 高低电平、I2C 地址或具体驱动器件。新增 LED 时只扩展
Platform 的静态装配表和编号枚举；不创建运行时注册表或动态分配。

## 编译期依赖与装配

- `Components/led` 的 Device Interface；
- `Adapters/stm32_hal/led_gpio` 的 Bind Interface 和 GPIO Context；
- CubeMX `main.h` 的 `USER_LED` Port/Pin。

Platform 将本板 STATUS LED 的逻辑 ON 映射为 `GPIO_PIN_SET`。若 PCB 改为低有效，只修改 Adapter
Context 的 `OnState`；Component、上层 Interface 和 Monitor Task 均无需改变。

## 运行时请求与事件路径

`Platform_Init()` 在调度器启动前初始化 LED，但其失败只降级诊断能力。Monitor Task 每个周期调用
`Platform_LED_Toggle(PLATFORM_LED_ID_STATUS)`；Platform → LED Device → GPIO Adapter → HAL GPIO。
本 Module 没有 IRQ、DMA 或回调路径。

## 禁止依赖

- 不包含 APP、Service 或 FreeRTOS；
- 不直接保存产品心跳周期、任务调度或状态机；
- 不向上泄漏 LED Handle、Adapter Context、GPIO 或 HAL 状态。

## 命名

公开 Interface 使用 `Platform_LED_*`；文件内装配对象与辅助 Implementation 使用 `hplatform_led_*`、
`platform_led_*`。
