# 工程分层、依赖与装配标准

> 适用工程：`version0.2.3` 及后续版本
> 状态：当前工程的规范性架构文档  
> 目的：避免同一类模块在后续扩展时使用不同的分层、命名和装配方式

## 1. 总体规则

工程从底层到上层分为：

```text
Vendor / HAL
      ↑
Adapters
      ↑
Components
      ↑
Platform
      ↑
Service
      ↑
APP
```

图中箭头表示功能抽象和职责所有权从底层向上提升，不表示 C 头文件依赖或一次普通请求的调用方向。普通请求通常由上向下进入硬件：

```text
APP -> Service -> Platform -> Components -> Adapters -> Vendor / HAL
```

硬件事件则由底层向已注册的上层回调发布，例如：

```text
Vendor IRQ -> Adapter -> Platform 持有的转发回调 -> Service 持有的回调 -> Task
```

编译期为了实现依赖倒置，Interface 所有权更准确地表示为：

```text
Component 定义 Ops Interface
        ↑                 ↑
Adapter 实现 Interface    Platform 绑定 Ops + Context
```

因此 Adapter `.c` 包含 Component 头文件以实现其 Interface 是正常的；Platform `.c` 包含 Adapter 头文件以完成装配也是正常的。禁止的是 Component 反向包含 Adapter 头文件，或低层模块包含 `Service` / `APP` 头文件并主动调用产品业务。

核心原则：

1. 可复用算法、芯片协议和稳定状态机属于 `Components`；
2. HAL、USB Device、具体 MCU Handle 和 GPIO 类型只属于 `Adapters` 或 Vendor；
3. 本 PCB 的实例、引脚、供电轨映射和启动策略属于 `Platform`；
4. 跨多个 Platform 能力的产品流程属于 `Service`；
5. `APP` 只决定启动顺序、顶层策略和任务入口，不直接装配底层 Handle；
6. Ops Interface 由使用它的 Component 拥有，而不是由 Adapter 自己发明；
7. Ops 与 Context 必须成对绑定，且 Context 生命周期必须覆盖全部调用期。

## 2. 顶层目录

```text
APP/                         产品入口、顶层启动和任务入口
Service/                     产品级流程 Module
Platform/                    本板实例装配和产品可见硬件能力
Components/                  可复用、与具体 MCU 无关的组件
Adapters/                    STM32 HAL、Cortex-M 与跨 Component Bridge 的具体实现
FATFS/                       CubeMX 生成的 FatFs 卷对象与 DiskIO Glue
Core/                        CubeMX 生成的 MCU 初始化代码
Drivers/                     ST HAL/CMSIS 等 Vendor 代码
Middlewares/                 ST/第三方中间件及其项目级配置接缝
USB_DEVICE/                  CubeMX 生成的 USB Device glue
cmake/                       工具链与 CubeMX 生成的构建描述
docs/                        中文技术文档和本规范
```

`Components` 当前保持扁平，不再人为分成 `Bus`、`Devices`、`Libraries`：

```text
Components/
  axp2101/
  audio/
  log/
  sd/
  soft_i2c/
```

这些模块虽然用途不同，但在架构上都是可独立复用的 Component。只有目录规模显著
增长并出现稳定分类需求时，才考虑增加新的分组。

`Service` 不属于 `Components` 的子目录。当前已有日志投递和文件系统 Module；未来还可能出现：

```text
Service/
  log/
  filesystem/
  playback/
  storage/
  media_library/
```

Service 目录中的 Module 可以组织多个 Component 和 Platform 能力，表达播放器产品行为，因此不能
伪装成底层可复用组件。

## 3. 各层职责

### 3.1 Vendor / HAL

包含 ST HAL、CMSIS、FreeRTOS Kernel、USB Device Library 和 CubeMX 生成代码。

允许：

- 公开厂商规定的 Handle、状态码和回调；
- 在生成器允许的 `USER CODE` 区调用自维护入口。

禁止：

- 为了迁就上层架构直接修改 ST 官方实现；
- 把产品策略写进 CubeMX 生成函数；
- 让自维护 Component 依赖生成目录中的具体全局实例。

### 3.2 Adapters

Adapter 实现 Component 定义的 Ops，把具体 SDK 语义转换为稳定语义。

允许：

- 包含 STM32 HAL、USB Device、FreeRTOS 头文件；
- 认识 `I2S_HandleTypeDef`、`SD_HandleTypeDef`、`GPIO_TypeDef`；
- 调用 `HAL_I2S_Transmit()`、`HAL_SD_ReadBlocks()`、`CDC_Transmit_FS()`；
- 把厂商原始状态转换为 Component 的归一化状态；
- 定义具体实现需要的 Context 类型。

禁止：

- 决定本板到底使用哪个 Handle、哪个引脚或哪一路供电；
- 保存产品启动策略；
- 让 Component 反向包含 Adapter 头文件；
- 在 Adapter 内偷偷绑定 `hi2s2`、`hsd1` 等全局对象。

当前 Adapter：

| 目录 | 职责 |
| --- | --- |
| `Adapters/bridge/axp2101_soft_i2c` | SoftI2C Component 到 AXP2101 Bus Ops 的跨 Component Bridge。 |
| `Adapters/cortex/cache` | Cortex-M7 DMA 缓冲区的 D-Cache 一致性维护。 |
| `Adapters/stm32_hal/audio_i2s` | STM32 HAL I2S/GPIO 到 Audio Ops。 |
| `Adapters/stm32_hal/sd` | STM32 HAL SDMMC/GPIO 到 SD Port Ops，并在 DMA 前后委托 Cortex Cache Adapter。 |
| `Adapters/stm32_hal/soft_i2c` | STM32 HAL GPIO 到 SoftI2C GPIO Ops。 |
| `Adapters/stm32_hal/irq/stm32_gpio_exti_irq` | 独占 STM32 HAL GPIO EXTI 全局入口，并按 GPIO PinMask 管理调用者回调链表。 |
| `Adapters/stm32_hal/irq/stm32_sdmmc_irq` | 按 `SD_HandleTypeDef` 注册 HAL SD 完成、错误和中止回调，并发布强类型传输事件。 |
| `Adapters/stm32_hal/log_usb_cdc` | USB CDC 非阻塞输出和 HAL 毫秒时间源。 |

`bridge/` 只转换两个 Component Interface，不引入具体 MCU 依赖；`cortex/` 只封装 Cortex-M 架构能力，不持有外设或任务状态；`stm32_hal/` 则集中所有必须认识 STM32 HAL、CubeMX Handle 或 HAL 全局回调的实现。

FreeRTOS 内核源码、项目配置与 Hook 的边界为：

```text
Middlewares/Third_Party/FreeRTOS/
  Source/                     未修改的 FreeRTOS Kernel 与 Cortex-M Port
  Config/                     本项目的 FreeRTOSConfig、Hook 和断言处理
```

`Config/` 不属于 Adapter：它不实现 Component Ops，而是定义本固件如何使用
FreeRTOS。不要把项目代码写入 `Source/`。

### 3.3 Components

Component 是可复用核心。它可以是算法、芯片驱动、状态机或基础服务核心。

允许：

- 定义公共类型、Handle、状态机和 Ops Interface；
- 保存稳定的归一化状态和设备语义错误；
- 通过 Ops + Context 调用外部能力；
- 在测试中绑定模拟 Adapter。

禁止：

- 包含 `main.h`、`i2s.h`、`sdmmc.h`、STM32 HAL 或 USB Device 头文件；
- 直接引用 `hi2s2`、`hsd1` 和具体 GPIO；
- 决定某块 PCB 的启动配置；
- 使用 FreeRTOS API 作为核心逻辑的硬依赖。

### 3.4 Platform

Platform 表示当前产品板卡向上提供的稳定硬件能力，也是唯一的对象装配层。

它负责：

- 持有当前 PCB 的 Component Handle 和 Adapter Context；
- 注入 CubeMX Handle、GPIO、极性、总线参数；
- 执行 `Adapter_Bind()`；
- 初始化 Component；
- 把芯片能力映射成 Audio、LCD、SD、Power 等产品语义；
- 为板级设备持有并注册 GPIO EXTI 回调对象，并向上层发布轻量检测通知；
- 选择日志输出 Adapter。

Platform 对 APP 隐藏：

- AXP2101 寄存器；
- SoftI2C 引脚和时序；
- `hi2s2`、`hsd1`；
- USB CDC 状态；
- Component 内部 Handle。

### 3.5 Service

Service 表达跨模块产品流程，例如：

- Playback Module：文件读取、解码、音频缓冲和播放状态机；
- 未来的 Storage Module：跨任务存储命令、文件打开状态和 USB MSC 所有权仲裁；
- Media Library：扫描、索引和曲目元数据。

Service 可以依赖 Platform 和 Components，但不直接依赖 HAL。

当前的 `Service/log` 负责 RTOS 日志消息块的投递与消费，`Service/filesystem`
负责 Storage Task 独占期间的 FatFs 操作。SD 生命周期、热插拔消抖与卷调用时序目前
属于 `APP/tasks/storage` 的 Storage Task，不等同于未来的 `Service/storage` Module。
它们的公开 Interface、资源所有权和调用约束必须在各自 README 中明确；新 Module 不创建
空目录或占位 Interface。

### 3.6 APP

APP 是固件顶层入口，负责：

- 调用 Platform 初始化；
- 决定可选设备失败是否致命；
- 创建并启动当前 FreeRTOS Tasks；
- 启动和连接 Service Module；
- 执行顶层产品策略。

APP 不负责：

- 构造 Ops 表；
- 填写 Component Handle 字段；
- 解释 HAL 状态；
- 访问芯片寄存器。

## 4. 统一装配模式

所有可替换实现统一使用：

```text
Component Handle
  + Component-owned Ops
  + Adapter Context
  + Adapter_Bind()
  + Platform-owned instance
```

典型代码形态：

```c
static Component_HandleTypeDef hplatform_component;
static ConcreteAdapterTypeDef hplatform_adapter = {
    /* 当前 PCB 的 Handle、GPIO 和极性 */
};

Platform_StatusTypeDef Platform_Module_Init(void)
{
    if (ConcreteAdapter_Bind(&hplatform_component,
                             &hplatform_adapter) != COMPONENT_OK)
    {
        return PLATFORM_MODULE_ERROR;
    }

    return (Component_Init(&hplatform_component) == COMPONENT_OK)
        ? PLATFORM_OK
        : PLATFORM_MODULE_ERROR;
}
```

要求：

1. Component Handle 和 Adapter Context 由 Platform 用 `static` 长期持有；
2. Bind 只完成依赖装配，不进行业务操作；
3. Component 的 `Init` 才执行状态检查或硬件访问；
4. Bind 必须检查 Handle、Ops、Context 和必要资源；
5. Component 的 Init 必须再次防御性检查必需回调；
6. APP 不得直接调用 Bind。

## 5. SoftI2C 的 GPIO Ops

SoftI2C 是纯 Component，不再包含 STM32 类型。它只认识：

```text
Line  = SCL / SDA
State = LOW / RELEASED
Write(context, line, state)
Read(context, line)
```

`RELEASED` 不是“强推高电平”，而是释放开漏输出，由外部上拉产生高电平。

装配链：

```text
Platform_Power_Init
  -> SoftI2C_STM32HALAdapter_Bind
       -> GPIOOps + STM32 GPIO Context
  -> AXP2101_SoftI2CAdapter_Bind
       -> AXP2101 BusOps + SoftI2C Context
  -> AXP2101_Init
       -> SoftI2C_Init
       -> GPIO Adapter
       -> HAL_GPIO_*
```

因此：

- `soft_i2c.c` 可移植到其他 MCU；
- 更换 GPIO SDK只需新增 Adapter；
- AXP2101 Driver 不需要知道 GPIO 层存在。

## 6. 日志装配

日志分成：

```text
Components/log
  格式化、等级过滤、RAM 队列、非阻塞消费

Adapters/stm32_hal/log_usb_cdc
  CDC 就绪判断、异步 TxBuffer、ANSI 颜色、HAL tick

Platform/log
  持有 USB Adapter 实例，生成绑定并调用 LOG_Init
```

`LOG_Init()` 接受已完成的 `LOG_OutputTypeDef` 和 `LOG_TimeSourceTypeDef`，不再在
核心内部调用所谓“默认 USB Backend”。这样 Log Component 可以替换成 UART、RTT、
文件或测试 Adapter，而无需修改 `log.c`。

## 7. 状态和错误

统一区分：

| 类型 | 表达内容 |
| --- | --- |
| 函数返回值 | 本次调用是否完成。 |
| `State` | 对象当前持续生命周期。 |
| `ErrorCode` | Component 的哪个语义步骤失败。 |
| `LastBusStatus` / `LastPortStatus` | 归一化后的底层原因。 |
| Vendor Handle `ErrorCode` | 具体后端原始错误，仅深入调试时查看。 |

Adapter 的状态转换函数入口统一使用 `int32_t native_status`，内部再转换成当前 SDK
类型。这样 Component Interface 不认识 HAL，未来也可接收以负数表示错误的 SDK。

上层业务不得依据 HAL 原始错误码作控制决策。

## 8. 中断与 RTOS

中断路径必须短：

```text
GPIO EXTI IRQHandler
  -> HAL Callback
  -> GPIO EXTI STM32 HAL Adapter
  -> 按 GPIO PinMask 遍历已注册回调
  -> 使用者持有的 ISR callback
  -> 只置位或发送 FromISR 通知
```

GPIO EXTI Adapter 采用与 Zephyr `gpio_callback` 相近的调用者持有模式：

- Adapter 独占 `HAL_GPIO_EXTI_Callback()`，但不认识 SD、PMIC 等产品语义；
- 使用者长期持有一个 Callback 对象，并在普通上下文注册或注销；
- Adapter 维护侵入式单向链表，允许不同模块订阅不同或相同 GPIO PinMask；
- Platform SD 等模块只在自己的回调中发布轻量通知，后续工作留给普通上下文。

GPIO EXTI Adapter 不是通用 NVIC 中断框架。SDMMC、DMA、USB、UART、定时器等
外设中断继续由 CubeMX 向量函数调用相应 `HAL_*_IRQHandler()`，再由对应 Adapter
或 Platform 模块处理其完成事件，不能注册到 GPIO EXTI 回调链表。

SDMMC DMA 的当前路径为：

```text
SDMMC1_IRQHandler()
  -> HAL_SD_IRQHandler(&hsd1)
  -> STM32 SDMMC IRQ Adapter（按 Handle 匹配）
  -> Platform SD 私有转发
  -> Filesystem Service 注册的传输 callback
  -> Storage Task 的通知索引 1
  -> FatFs 同步桥接等待返回后调用 Platform_SD_CompleteTransfer()
```

卡检测边沿保留通知索引 0，DMA 完成使用索引 1；两类事件的含义、消抖规则和等待方式不同，
不得共用同一个无类型通知。`Adapters/stm32_hal/irq` 只集中 HAL 全局回调的唯一所有权，GPIO EXTI 和
SDMMC 仍保留各自的强类型 Interface，不能收敛为 `IRQ_ID + void *` 的通用分发器。

ISR 禁止：

- 日志格式化和 USB 发送；
- I2C、SD 和文件系统访问；
- 延时和机械触点消抖；
- 普通 FreeRTOS API。

接入 RTOS 后，模块状态机和 Component API 保持不变，只把裸机通知接缝换成
`vTaskNotifyGiveFromISR()`、队列或事件组，并由普通 Task 执行实际工作。

## 9. 命名标准

命名的唯一规范见 [coding_standard.md](coding_standard.md)。摘要如下：

- 文件内和同 Module 的私有 Implementation 使用 `snake_case`；
- 跨 Module 的公开 Interface 使用 Pascal 分段命名，例如 `Platform_SD_GetInfo()`、
  `SDCard_ReadBlocks()` 与 `LogService_Post()`；
- FreeRTOS Task 入口保留 `storage_task()` 这类 `snake_case`；
- 不删除能表达 Adapter 后端或资源语义的名称，仅移除重复的层级词；
- 不再新增 `Board_*`、`BSP/*`、`*_port` 作为模糊层级名称。已有 Component 内部的
  `PortOps` 表示稳定的设备端口 Interface，不等同于目录层级。

## 10. 头文件与注释

按本工程约定：

- `.h` 中函数声明保持干净，不写逐函数 Doxygen；公共枚举、结构体和关键字段可写类型说明；
- 公开函数的完整 Doxygen 只写在对应 `.c` 的定义处，避免声明和定义的重复说明漂移；
- 复杂私有函数和关键时序内部写必要注释，重点说明资源、并发和时序约束；
- include 使用工程根目录起始的完整路径；
- 避免无意义地在 `=` 后立即换行；
- 不在注释中保留已失效的目录名和旧层名。

## 11. CMake

当前根 CMake 递归收集：

```text
Components/*.c
Adapters/*.c
Platform/*.c
APP/*.c
Service/*.c
Middlewares/Third_Party/FreeRTOS/Config/*.c
USB_DEVICE/App/*.c
USB_DEVICE/Target/*.c
```

`FATFS/App/fatfs.c` 与 `FATFS/Target/*.c` 由 `cmake/stm32cubemx/CMakeLists.txt` 显式加入；其中生成的 DiskIO Glue 通过 `BSP_SD_*` Override Seam 调用 `Service/filesystem` 的强定义，不将 DMA 等待或任务通知写回生成目录。

工程根目录是项目自维护代码的统一 include 根，因此使用：

```c
#include "Components/axp2101/axp2101.h"
#include "Adapters/bridge/axp2101_soft_i2c/axp2101_soft_i2c_adapter.h"
#include "Platform/power/platform_power.h"
```

当某个 Component 需要跨仓库复用时，再为它增加独立 `CMakeLists.txt` 和
`add_library()`。成熟组件目标应：

- 只公开自己的公共 include；
- 用 `PRIVATE` 链接内部实现依赖；
- 用 `PUBLIC` 表达公共头文件确实暴露的依赖；
- 不依赖当前固件可执行目标中的隐式 include。

## 12. 新模块归属检查

新增代码前依次回答：

1. 换一块 MCU 后这份代码是否仍应原样复用？是：优先 Component；
2. 是否包含 HAL、USB、RTOS 或具体 SDK？是：Adapter；
3. 是否描述本 PCB 的引脚、实例、供电轨或启动顺序？是：Platform；
4. 是否组织多个硬件能力形成播放器产品流程？是：Service；
5. 是否只负责顶层启动、任务创建和产品策略？是：APP；
6. 接口由谁消费？Ops 应由消费方 Component 定义；
7. 对象由谁拥有？具体实例通常由 Platform 持有并装配。

每个自维护目录还必须提供 README，写明 Module 职责、资源所有权、公开 Interface、
允许调用的 Interface、禁止依赖及任务/ISR 约束。README 用于缩短理解路径；它不能
替代本文件的全局规则。

无法清楚回答时，不要急着新建目录，先在本文中补充边界决定。
