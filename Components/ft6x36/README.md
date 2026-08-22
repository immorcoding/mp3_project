# FT6X36 Device

本 Module 封装 FT6X36 系列电容触摸控制器的器件级初始化完成语义、I2C 就绪检查、Chip ID 和第一触点原始坐标读取。它不预设控制器返回的具体 ID，也不包含屏幕坐标转换、手势、LVGL、IRQ 或任务策略。

## 公开 Interface

- `FT6X36_Init()`：请求已绑定 Adapter 完成一次初始化，并确认指定 7-bit I2C 地址可应答；
- `FT6X36_ReadID()`：读取寄存器 `0xA3` 的原始 Chip ID；
- `FT6X36_ReadRawPoint()`：从 `TD_STATUS (0x02)` 连续读取状态和第一触点的 12 位原始 X/Y；无触摸不是错误。
- `FT6X36_PortOpsTypeDef`：由具体 Adapter 实现的初始化、I2C 探测与 8-bit 寄存器读取 Interface；
- `FT6X36_HandleTypeDef`：由 Platform 长期持有的 Device 状态、7-bit 地址及已绑定 PortOps。

## 编译期依赖

仅依赖标准 C 和自身私有配置；不包含 HAL、CubeMX、FreeRTOS、LVGL、GPIO 或 I2C Handle。

## 运行时请求与事件路径

`FT6X36_Init()`、`FT6X36_ReadID()` 和 `FT6X36_ReadRawPoint()` 通过自身拥有的 PortOps 发起 I2C 访问；STM32 HAL Adapter 实现 Ops，Platform Touch 绑定 Context。只有 `FT6X36_Init()` 在启动阶段执行；触点读取由 GUI Task 的普通任务上下文轮询，没有异步事件路径。

## 禁止依赖与约束

- 不持有板级 I2C 实例、TP_RST、TP_IRQ、供电轨或具体 I2C 地址选择；
- 不调用 LVGL 或 FreeRTOS，也不解释 GUI 轮询周期；
- 多触点时仅返回第一触点，手势和多指语义留给上层；
- 读 `0xA3` 成功只证明当前控制器通信正常，不能仅根据文件名把返回值写死为某个 FT6X36 子型号。

## 命名

跨 Module Interface 使用 `FT6X36_*`；文件内私有 Implementation 使用 `ft6x36_*`。
