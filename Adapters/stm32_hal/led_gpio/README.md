# STM32 HAL GPIO LED Adapter

本 Module 实现 LED Device 所拥有的 PortOps，把逻辑 `ON/OFF` 映射为 STM32 HAL GPIO 输出。

## 公开 Interface

- `LED_GPIO_STM32HALAdapter_Bind()`：把 GPIO Adapter Context 与 LED Handle 成对绑定；
- `LED_GPIO_STM32HALAdapterTypeDef`：保存由 Platform 注入的 GPIO Port、Pin 与逻辑 ON 电平。

## 编译期依赖

- `Components/led` 的 PortOps 与归一化状态；
- STM32 HAL 的 `GPIO_TypeDef`、`GPIO_PinState` 和 `HAL_GPIO_WritePin()`。

## 运行时请求与事件路径

`LED_Set()` 经 PortOps 调用本 Adapter，Adapter 按 `OnState` 把逻辑 ON/OFF 写入对应 GPIO。没有
DMA、IRQ、回调或任务通知路径。

## 资源与约束

- GPIO 的模式、速度和初始值由 CubeMX `MX_GPIO_Init()` 负责；Adapter 不重复配置引脚；
- `HAL_GPIO_WritePin()` 无错误返回，因此 Adapter 只能确认写入请求已提交，不能检测外部短路、断线或 LED 失效；
- 当前 GPIO Context 由 Platform 长期持有；Adapter 不选择 `USER_LED` 或其他具体板级资源。

## 禁止依赖

- 不包含 APP、Service 或 FreeRTOS；
- 不决定 LED 逻辑编号、启动失败是否致命或心跳周期；
- 不反向调用 Platform。

## 命名

跨 Module Interface 使用 `LED_GPIO_STM32HALAdapter_*`；私有 Implementation 使用
`led_gpio_stm32_hal_*`。
