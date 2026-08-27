# FT6X36 触摸架构

## 1. 当前范围

本阶段建立触摸控制器的轮询式单指输入链路：复位触摸模组、探测 I2C 地址、读取 Chip ID 寄存器 `0xA3`，并把第一触点交给 LVGL Pointer 输入设备。

读取成功仅说明当前地址和寄存器通信正常；不得在代码中预先假定某个固定 Chip ID 值。旧验证工程中的模组曾使用 FT6336U 命名，而本板资料使用 FT6X36 系列命名，应先记录实际读值后再决定是否需要更严格的型号校验。

`Components/ft6x36` 从 `TD_STATUS (0x02)` 连续读取 5 字节：低 4 位为触点数，后 4 字节为第一触点的 12 位原始 X/Y。无触摸是成功结果，多个触点时首版只使用第一点。`Platform/touch` 原样发布这一结果；坐标方向是 GUI 语义，留在 `Service/gui` 的 LVGL `read_cb`。

本阶段不包含：手势识别、多指交互、TP_IRQ 注册、低功耗唤醒和 DMA。

## 2. 模块职责与装配

```text
启动装配：

APP/app_init
       │ Platform_Init（先 LCD，后 Touch）
       ▼
Platform/platform
       │ Platform_Touch_Init
       ▼
Platform/touch
       │ 绑定 I2C2、TP_RST、7-bit 地址与超时
       ▼
Components/ft6x36
       │ FT6X36_PortOps
       ▼
Adapters/stm32_hal/ft6x36_i2c
       │ HAL_I2C / HAL_GPIO / HAL_Delay（仅调度器启动前）
       ▼
CubeMX HAL 与硬件

运行时触点读取：

APP/GUI Task
       │ Service_GUI_Process → LVGL Pointer read_cb
       ▼
Service/gui
       │ Platform_Touch_ReadRawPoint
       ▼
Platform/touch → Components/ft6x36 → Adapter → CubeMX HAL
```

`Components/ft6x36` 只拥有 FT6X36 的协议语义：初始化成功后必须可 I2C 应答、寄存器地址、I2C 就绪检查和错误状态。它通过自己定义的 `FT6X36_PortOpsTypeDef` 请求外部能力，因而不知道 HAL、`hi2c2`、GPIO 或具体 PCB。

`Adapters/stm32_hal/ft6x36_i2c` 实现该 Ops：内部把 Component 使用的 7-bit 地址左移为 STM32 HAL 的地址形式，执行 I2C 探测/寄存器读取，并将 TP_RST 的逻辑断言、保持、释放和稳定等待收敛为一次 `Initialize()`。Adapter 只借用由 Platform 注入的硬件对象，不保存 Platform 或 Task 的所有权。

`Platform/touch` 是本板的装配与对上能力接口：长期持有 `hi2c2`、`TP_RST`、地址 `0x38`、探测次数与超时，并向上层提供不泄漏 HAL 类型的 `Platform_Touch_*` 接口。当前触摸模组使用 LCD 的 ALDO2 供电，因此 `Platform_Init()` 必须先完成 LCD 初始化。

## 3. 编译期依赖与运行时路径

功能所有权自下而上为：

```text
CubeMX HAL → STM32 HAL Adapter → FT6X36 Device → Platform Touch → APP
```

编译期中，Adapter 包含并实现 Component 声明的 PortOps；这是依赖倒置的正常形式，并不意味着 Component 反向依赖 Adapter。Platform 在装配时包含 Adapter 与 Component 的公开头；GUI Service 只包含 Platform Touch 的公开头。

启动初始化在 FreeRTOS 调度器启动前执行：

```text
APP app_init
  → Platform_Init()
  → Platform_LCD_Init()
  → Platform_Touch_Init()
  → FT6X36_Init()
  → STM32 HAL Adapter Initialize()
  → 逻辑断言 TP_RST → HAL_Delay(5 ms) → 逻辑释放 TP_RST → HAL_Delay(200 ms)
  → IsReady(0x38)
  → app_task_start()
  → FreeRTOS 调度器
```

因此 `HAL_Delay()` 不会在 LCD/GUI 等普通 Task 中忙等待；它仅用于一次性的启动硬件稳定时间。复位有效电平在 Platform 注入为本板低有效，但 Component 只知道 Adapter 初始化成功或失败。

调度器运行后的触点读取路径为：

```text
GUI Task
  → Service_GUI_Process()
  → lv_timer_handler()
  → LVGL Pointer read_cb
  → Platform_Touch_IsAvailable()
  ├─ false：向 LVGL 报告 RELEASED，不访问 I2C
  └─ true：Platform_Touch_ReadRawPoint()
             → FT6X36_ReadRawPoint()
             → MemRead(0x38, 0x02, 5 bytes)
```

其中 `0x38` 始终是 Device/Platform 中表达的 7-bit 地址。只有 STM32 HAL Adapter 调用 `HAL_I2C_IsDeviceReady()` 或 `HAL_I2C_Mem_Read()` 前才执行左移一位转换。I2C 读取失败时 GUI Service 向 LVGL 报告释放，避免界面永久保留上一次按下状态；Component 同时记录失败语义，需在下一次系统初始化时重新建立通信。

`Platform_Touch_IsAvailable()` 是 Platform Touch 的稳定可用性 Interface：启动初始化成功后为 true；初始化失败或一次实际读点失败后为 false。该查询本身不访问 I2C。GUI Service 的 Pointer 回调每次先查询它；不可用时持续向 LVGL 报告释放，不再调用 `Platform_Touch_ReadRawPoint()`。Platform 的读点 Interface 也会在不可用时短路返回，作为第二层保护，确保任意上层调用者不会在触摸缺失或通信故障后反复占用 I2C 或重复记录同一错误。

## 4. TP_IRQ 的当前状态与后续演进

CubeMX 已将 `TP_IRQ` 配为 EXTI 下降沿，但当前不注册 GPIO EXTI 回调；常规触点采样已由 GUI Task 中的 LVGL `read_cb` 轮询完成。

若将来需要以 IRQ 降低空闲轮询或实现触摸唤醒，事件链必须为：

```text
GPIO EXTI IRQ → GPIO EXTI Adapter 分发 → 已注册的平台/任务回调 → 仅通知 GUI Task
                                                        ↓
                                             GUI Task 普通上下文读取 FT6X36
```

中断回调不得直接调用 LVGL、执行 I2C 寄存器读取或处理手势业务。

## 5. 当前验收标准

`Platform_Init()` 成功完成触摸初始化后，GUI Task 创建 LVGL Pointer 输入设备。烧录后应验证界面上的可点击控件能收到点击；若触点方向不匹配显示方向，只在 `Service/gui` 的 `read_cb` 中交换或镜像 X/Y，不能把 UI 方向下沉到 Platform 或 FT6X36 Device。

启动阶段的 Chip ID 日志仍用于确认基础链路：

```text
I (...) TOUCH: Chip ID: 0xNN.
```

失败时由 Platform 记录错误语义、最后一个 Port 状态和失败寄存器；先检查 TP_RST、I2C2 上拉、地址、供电和模组连接，再扩展上层功能。
