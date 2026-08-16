# Platform Touch

本 Module 装配当前 PCB 唯一的 FT6X36 系列触摸控制器、CubeMX I2C2、`TP_RST` GPIO 和本板 7-bit I2C 地址。它对上隐藏 HAL Handle、复位电平和地址表示，当前只提供初始化与 Chip ID 读取能力。

## 公开 Interface

- `Platform_Touch_Init()`：绑定当前板的 I2C2/TP_RST Context，执行一次启动初始化并确认 I2C 可访问；
- `Platform_Touch_ReadID()`：读取当前控制器的原始 Chip ID。

## 编译期依赖与装配

- `Components/ft6x36` 的 Device Interface；
- `Adapters/stm32_hal/ft6x36_i2c` 的 Bind Interface 与 Context；
- CubeMX 的 `hi2c2`、`TP_RST` GPIO 定义和 HAL 类型。

## 运行时请求与事件路径

`Platform_Init()` 在 LCD 初始化完成后、FreeRTOS 调度器启动前调用 `Platform_Touch_Init()`；Platform 向下调用 FT6X36 Device，Device 经已绑定 STM32 HAL Adapter 完成 TP_RST 时序和 I2C2 地址探测。LCD Task 只读取 Chip ID。`TP_IRQ` 只保留 CubeMX EXTI 配置，尚未注册回调；触点读取与 LVGL 输入将在后续由 GUI 任务轮询处理。

## 禁止依赖与约束

- 不创建 FreeRTOS Task、不调用 LVGL、不解释触点坐标或手势；
- 不设置 LCD 电源轨；`Platform_Init()` 必须先完成 LCD 初始化以开启并稳定 ALDO2，再调用初始化；
- 不直接向 APP 泄漏 `hi2c2`、GPIO、Adapter Context 或 HAL 状态。

## 私有配置

`platform_touch_config.h` 保存本板触摸地址、I2C 探测次数和同步超时；复位时序属于 STM32 HAL FT6X36 I2C Adapter 私有配置，芯片寄存器属于 FT6X36 Device 私有配置。

## 命名

跨层公开 Interface 使用 `Platform_Touch_*`；文件内私有实例使用 `hplatform_touch*`。
