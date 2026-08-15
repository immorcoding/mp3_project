# FT6X36 触摸架构

## 1. 当前范围

本阶段只建立触摸控制器的最小通信链路：复位触摸模组、探测 I2C 地址，并读取 Chip ID 寄存器 `0xA3`。

读取成功仅说明当前地址和寄存器通信正常；不得在代码中预先假定某个固定 Chip ID 值。旧验证工程中的模组曾使用 FT6336U 命名，而本板资料使用 FT6X36 系列命名，应先记录实际读值后再决定是否需要更严格的型号校验。

本阶段不包含：LVGL 输入设备、触点坐标读取、手势识别、屏幕坐标旋转、TP_IRQ 注册、低功耗唤醒和 DMA。

## 2. 模块职责与装配

```text
APP/tasks/lcd
       │ Platform_Touch_Init / Platform_Touch_ReadID
       ▼
Platform/touch
       │ 绑定 I2C2、TP_RST、7-bit 地址与超时
       ▼
Components/ft6x36
       │ FT6X36_PortOps
       ▼
Adapters/stm32_hal/ft6x36_i2c
       │ HAL_I2C / HAL_GPIO / HAL_Delay
       ▼
CubeMX HAL 与硬件
```

`Components/ft6x36` 只拥有 FT6X36 的协议语义：复位时序、寄存器地址、I2C 就绪检查和错误状态。它通过自己定义的 `FT6X36_PortOpsTypeDef` 请求外部能力，因而不知道 HAL、`hi2c2`、GPIO 或具体 PCB。

`Adapters/stm32_hal/ft6x36_i2c` 实现该 Ops：内部把 Component 使用的 7-bit 地址左移为 STM32 HAL 的地址形式，执行 I2C 探测/寄存器读取，并控制 RESET GPIO。Adapter 只借用由 Platform 注入的硬件对象，不保存 Platform 或 Task 的所有权。

`Platform/touch` 是本板的装配与对上能力接口：长期持有 `hi2c2`、`TP_RST`、地址 `0x38`、探测次数与超时，并向 APP 提供不泄漏 HAL 类型的 `Platform_Touch_*` 接口。

## 3. 编译期依赖与运行时路径

功能所有权自下而上为：

```text
CubeMX HAL → STM32 HAL Adapter → FT6X36 Device → Platform Touch → LCD Task
```

编译期中，Adapter 包含并实现 Component 声明的 PortOps；这是依赖倒置的正常形式，并不意味着 Component 反向依赖 Adapter。Platform 在装配时包含 Adapter 与 Component 的公开头；APP 只包含 Platform Touch 公开头。

运行时最小请求路径为：

```text
LCD Task
  → Platform_Touch_Init()
  → FT6X36_Init()
  → SetReset(false) → Delay(5 ms) → SetReset(true) → Delay(200 ms)
  → IsReady(0x38)

LCD Task
  → Platform_Touch_ReadID()
  → FT6X36_ReadID()
  → MemRead(0x38, 0xA3, 1 byte)
```

其中 `0x38` 始终是 Device/Platform 中表达的 7-bit 地址。只有 STM32 HAL Adapter 调用 `HAL_I2C_IsDeviceReady()` 或 `HAL_I2C_Mem_Read()` 前才执行左移一位转换。

## 4. TP_IRQ 的当前状态与后续演进

CubeMX 已将 `TP_IRQ` 配为 EXTI 下降沿，但当前不注册 GPIO EXTI 回调，也不依赖它完成 Chip ID 或未来常规触点采样。

后续接入 LVGL 时，输入 `read_cb` 在 LCD/GUI Task 的普通任务上下文中经 Platform Touch 读取 `TD_STATUS (0x02)` 与坐标寄存器即可。若将来需要以 IRQ 降低空闲轮询或实现触摸唤醒，事件链必须为：

```text
GPIO EXTI IRQ → GPIO EXTI Adapter 分发 → 已注册的平台/任务回调 → 仅通知 GUI Task
                                                        ↓
                                             GUI Task 普通上下文读取 FT6X36
```

中断回调不得直接调用 LVGL、执行 I2C 寄存器读取或处理手势业务。

## 5. 当前验收标准

LCD Task 成功初始化 LCD 后执行触摸最小测试。串口出现以下形式的日志即表示链路通过：

```text
I (...) TOUCH: Chip ID: 0xNN.
```

失败时由 Platform 记录错误语义、最后一个 Port 状态和失败寄存器；先检查 TP_RST、I2C2 上拉、地址、供电和模组连接，再扩展上层功能。
