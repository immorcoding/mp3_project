# LED Device

本 Module 表示可复用的单路逻辑 LED Device。它只定义初始化、逻辑 ON/OFF 设置与翻转行为；不认识
GPIO 电平、I2C 地址、PWM、LED 控制器寄存器、板级编号或 FreeRTOS Task。

## 公开 Interface

- `LED_Init()`：验证已绑定 Port，初始化后强制提交逻辑 `OFF`；
- `LED_Set()`：请求逻辑 `ON` 或 `OFF`，仅在 Port 成功时更新 Handle 的已确认状态；
- `LED_Toggle()`：基于最后一次成功的逻辑状态请求相反状态；
- `LED_PortOpsTypeDef`：由具体后端实现的 `Initialize` 与 `SetState` Interface。

## 编译期依赖

只依赖标准 C。Device 自己拥有 PortOps Interface；GPIO、I2C、PWM 或扩展 IO Adapter 可以包含本
Module 的公开头实现该 Interface，但本 Module 不反向包含任何 Adapter。

## 运行时请求路径

`Platform_LED_Set/Toggle()` → `LED_Set/Toggle()` → 已绑定 `LED_PortOpsTypeDef` → 具体 Adapter。
当前 GPIO 后端由 `Adapters/stm32_hal/led_gpio` 实现；未来出现具体 I2C LED 控制器后，应实现该器件
语义对应的 Adapter 或 Bridge，而不是仅因使用 I2C 预建空泛的“通用 I2C LED”。

## 资源与约束

- `LED_HandleTypeDef` 及其 Adapter Context 必须由 Platform 长期持有；
- `LED_Toggle()` 不读取物理输出寄存器，依据最后一次成功的 `OnOffState`；
- Port 设置失败时，Device 保留此前成功状态并回到 READY，调用者可按自身策略重试；
- Component 不具有运行时全局注册表。多灯的逻辑编号与实例映射属于当前 PCB 的 Platform。

## 禁止依赖

- 不包含 STM32 HAL、CubeMX `main.h`、GPIO/I2C/PWM Handle；
- 不包含 Platform、Service、APP 或 FreeRTOS；
- 不决定 LED 编号、闪烁频率或产品状态机。

## 命名

跨 Module Interface 使用 `LED_*`；文件内辅助 Implementation 使用 `led_*`。
