# FT6X36 Device

本 Module 封装 FT6X36 系列电容触摸控制器的器件级复位时序、I2C 就绪检查和 Chip ID 寄存器读取。当前最小能力只用于确认硬件通信；不预设控制器返回的具体 ID，也不包含触点坐标、手势、LVGL、IRQ 或任务策略。

## 公开 Interface

- `FT6X36_Init()`：执行低有效硬件复位时序，并确认指定 7-bit I2C 地址可应答；
- `FT6X36_ReadID()`：读取寄存器 `0xA3` 的原始 Chip ID；
- `FT6X36_PortOpsTypeDef`：由具体 Adapter 实现的复位、延时、I2C 探测与 8-bit 寄存器读取 Interface；
- `FT6X36_HandleTypeDef`：由 Platform 长期持有的 Device 状态、7-bit 地址及已绑定 PortOps。

## 编译期依赖

仅依赖标准 C 和自身私有配置；不包含 HAL、CubeMX、FreeRTOS、LVGL、GPIO 或 I2C Handle。

## 运行时请求与事件路径

`FT6X36_Init()`、`FT6X36_ReadID()` 通过自身拥有的 PortOps 发起复位、延时和 I2C 访问；STM32 HAL Adapter 实现 Ops，Platform Touch 绑定 Context。当前没有异步事件路径。

## 禁止依赖与约束

- 不持有板级 I2C 实例、TP_RST、TP_IRQ、供电轨或具体 I2C 地址选择；
- 不调用 LVGL 或 FreeRTOS，也不解释 GUI 轮询周期；
- 读 `0xA3` 成功只证明当前控制器通信正常，不能仅根据文件名把返回值写死为某个 FT6X36 子型号。

## 命名

跨 Module Interface 使用 `FT6X36_*`；文件内私有 Implementation 使用 `ft6x36_*`。
