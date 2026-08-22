# 项目领域上下文

本文档统一记录工程中的稳定术语、职责边界和产品语义。具体文件路径、字段、调用链和硬件参数由 `docs/` 下的技术文档维护，避免同一实现事实被重复记录后逐渐不一致。

本文档不是目录导航：根目录与各源码目录的 `README.md` 说明 Module 的位置、公开 Interface 和局部约束；`docs/architecture_standard.md` 是功能/抽象所有权、编译期依赖与运行时路径的唯一总则；本文档只定义这些 Module 之间反复使用的领域词汇。新增 Module、改变职责归属或改变产品语义时，必须同步核对本文档与对应技术文档。

## 播放器应用（Player application）

**播放器应用**是固件的产品行为入口，负责组织系统服务、板级强依赖设备和可移除介质的启动顺序，并持续推进播放器运行所需的周期任务。

它决定“什么时候初始化、失败是否致命、事件如何影响播放器行为”，但不实现器件寄存器协议、总线时序或芯片厂商接口。

相关术语：**平台电源**、**延后启动日志**、**平台 SD**。

示例：

> 播放器应用把 SD 卡视为可选介质，因此未插卡不会阻止系统启动。

## 平台电源（Platform Power）

**平台电源**表示本 PCB 的供电轨、实际电源管理器件及其产品用途。

它负责组合 AXP2101 Device、当前通信 Adapter 和本板供电策略，并向上层提供 Audio、LCD 等板级电源语义。它不等同于芯片驱动，也不等同于某一种 I2C 实现。

相关术语：**AXP2101 设备**、**AXP2101 I2C 适配器**、**平台电源启动配置**。

示例：

> 平台电源可以改用硬件 I2C，但上层看到的 Audio 和 LCD 电源语义不应变化。

## AXP2101 设备（AXP2101 Device）

**AXP2101 设备**是明确面向 AXP2101 芯片的可复用 Device Driver。

它负责芯片识别、寄存器协议、芯片电源轨控制和稳定诊断，不拥有本板引脚、通信后端实例或 PCB 启动策略。当前不建立名为“通用 PMIC”但实际暴露 AXP2101 语义的虚假抽象。

相关术语：**平台电源**、**AXP2101 I2C 适配器**。

示例：

> 同一份 AXP2101 Device 可以通过不同 Adapter 使用软件 I2C、硬件 I2C 或测试后端。

## AXP2101 I2C 适配器（AXP2101 I2C Adapter）

**AXP2101 I2C 适配器**是 AXP2101 Device 与具体 I2C 后端之间的 Adapter。当前实现位于 `Adapters/bridge/`，是 SoftI2C Component 到 AXP2101 Bus Ops 的跨 Component Bridge；未来可替换为硬件 I2C 或测试 Adapter。

它把具体后端的 Context、寄存器读写和状态码转换成 AXP2101 Device 的 Bus Ops。Bridge 不依赖 HAL、CMSIS 或 FreeRTOS，也不拥有板级引脚和 I2C 实例；实例始终由 Platform 注入。

相关术语：**AXP2101 设备**、**归一化传输状态**。

示例：

> 硬件 I2C 的超时状态应在 Adapter 内转换成统一的 AXP2101 总线超时。

## 软件 I2C 组件（SoftI2C Component）

**软件 I2C 组件**是与具体 MCU 无关的开漏 I2C 时序算法。

它只通过 GPIO Ops 表达 SCL/SDA 的主动拉低、释放和实际电平读取，不拥有
`GPIO_TypeDef`、具体引脚或芯片协议。总线恢复属于 I2C 算法的一部分，因此仍由该
组件负责。

相关术语：**STM32 GPIO 适配器**、**AXP2101 I2C 适配器**。

示例：

> 把软件 I2C 移植到另一款 MCU 时，只替换 GPIO Adapter，不修改 START、ACK 和恢复算法。

## STM32 GPIO 适配器（STM32 GPIO Adapter）

**STM32 GPIO 适配器**把软件 I2C 的最小 GPIO Ops 映射到 STM32 HAL GPIO。

它认识 GPIO Port、Pin 和 HAL 电平类型，但不认识 AXP2101 地址、寄存器和电源策略。
具体 SCL/SDA 引脚由 Platform 注入。

相关术语：**软件 I2C 组件**、**平台电源**。

示例：

> `RELEASED` 在 STM32 开漏输出上表现为写入 SET，但其真实高电平仍由外部上拉产生。

## Cortex-M7 Cache 适配器（Cortex-M7 Cache Adapter）

**Cortex-M7 Cache 适配器**封装 Cacheable 内存范围的 Cortex-M7 D-Cache 维护。它是无状态 Adapter，提供 Clean、Invalidate 与 Clean + Invalidate 三项通用 Interface；DMA Adapter 按传输方向组合调用，Platform SDRAM 诊断也通过同一 Interface 强制提交或重新读取外部存储器。

它不拥有外设 Handle、DMA 等待、FreeRTOS 通知或中转缓冲区。`*_Aligned()` Interface 要求调用者在普通任务上下文传入首地址与长度均按 32 字节 Cache line 对齐的范围，并拥有其覆盖的全部 Cache line。`Clean_Rounded()` 仅用于 DMA 读取内存前的 Clean：首地址仍须对齐，Implementation 会向后覆盖到 Cache line 末尾，调用者必须拥有补齐后的范围；不提供对应的非严格 Invalidate，避免丢弃相邻脏数据。用于 DMA 时还需保证缓冲区可被 DMA 访问，且传输期间 CPU 不并发读写该缓冲区。

相关术语：**SD 卡端口**、**文件系统 Module**。

示例：

> SDMMC Adapter 在启动读 DMA 前维护 Cache，DMA 完成后由任务上下文的 `Sync()` 再使 CPU 读取 RAM 中的新数据。

## Cortex-M7 周期计数器适配器（Cortex-M7 Cycle Counter Adapter）

**Cortex-M7 周期计数器适配器**封装 CoreDebug 与 DWT `CYCCNT`，向性能诊断提供核心周期
数与当前核心频率。它把周期计数器使能、复位、寄存器访问与必要执行屏障收敛在一个
无状态 Adapter 中，使 Platform 不需要认识 Cortex-M 核心寄存器。

`CYCCNT` 是全局 32 位硬件资源：测量者必须在普通上下文串行使用，且在完整回绕前完成
单次测量。它不拥有 SDRAM、DMA、任务、缓冲区或任何板级策略。

相关术语：**平台 SDRAM**、**Cortex-M7 Cache 适配器**。

示例：

> 平台 SDRAM 在写入和冷读测速前复位周期计数器，随后只使用周期差和核心频率换算吞吐。

## 平台 LED（Platform LED）

**平台 LED**表示当前 PCB 上一个或多个可供上层使用的板级诊断 LED。它长期持有 LED Device 和
Adapter Context，并通过稳定逻辑编号装配当前 LED 后端；闪烁周期、任务调度和产品状态机仍由调用者决定。

当前 `STATUS` LED 使用 GPIO；逻辑 ON/OFF 与物理高低电平的映射属于 Adapter Context。未来新增
I2C、PWM 或扩展 IO LED 时，Platform 只增加编号与实例装配，上层调用方式不变。

相关术语：**LED 设备**、**STM32 HAL GPIO LED 适配器**、**播放器应用**。

示例：

> Monitor Task 每个监控周期请求 `Platform_LED_Toggle(PLATFORM_LED_ID_STATUS)`，但无需包含 `main.h` 或 STM32 HAL。

## LED 设备（LED Device）

**LED 设备**是可复用的逻辑亮灭 Device Module。它通过 PortOps 初始化并请求 ON/OFF，维护最后一次
成功提交的逻辑状态；它不认识 GPIO 电平、I2C 地址、PWM、具体芯片寄存器或板级编号。

`Toggle` 由设备根据最后一次成功的逻辑状态调用 `Set` 实现。Port 失败时设备保留旧状态，允许调用者
在瞬态后端故障后重试；多灯的编号、实例表与启动失败策略均不属于该 Device。

相关术语：**平台 LED**、**STM32 HAL GPIO LED 适配器**。

示例：

> 低有效 GPIO LED 和高有效 GPIO LED 都接收相同的逻辑 ON 请求，由各自 Adapter Context 决定输出电平。

## STM32 HAL GPIO LED 适配器（STM32 HAL GPIO LED Adapter）

**STM32 HAL GPIO LED 适配器**把 LED Device 的逻辑 ON/OFF PortOps 映射到 STM32 HAL GPIO 写操作。
它认识 `GPIO_TypeDef`、Pin 和 ON 对应电平，但不选择当前 PCB 的引脚、LED 编号、任务周期或启动策略。

相关术语：**LED 设备**、**平台 LED**。

示例：

> 低有效 LED 只需把 Adapter Context 的 `OnState` 配置为 RESET，Device 与 Monitor Task 都无需修改。

## 平台电源启动配置（Platform power boot profile）

**平台电源启动配置**是 AXP2101 完成器件识别后，由 Platform Power 按顺序应用的一组 PCB 供电策略。

它控制输入限制、充电、中断、ADC、DCDC/LDO 使能和预设电压。修改该配置属于硬件电源策略变更，不只是软件重构，必须同时核对数据手册、原理图、负载允许电压、上电顺序和实测结果。

相关术语：**平台电源**、**播放器应用**。

示例：

> 关闭某一路 LDO 的启动使能，不代表应同时清除它的电压预设。

## 平台 LCD（Platform LCD）

**平台 LCD**表示当前 PCB 上唯一的 ST7789V 显示模组及其可见显示能力。

它负责组合 LCD 电源轨、SPI1、CS、D/C、RESET、背光等板级资源，并向上层提供不泄漏
HAL Handle 和 GPIO 细节的显示诊断或绘制语义。当前阶段已完成 RGB565 最小亮屏、
`RDDID`、阻塞画点/填充和异步 SPI DMA 矩形写入；DMA 完成只经已注册的轻量回调
上报，Platform 不认识任务通知、帧缓冲或 LVGL。

相关术语：**ST7789 设备**、**STM32 HAL ST7789 SPI 适配器**、**平台电源**。

示例：

> LCD Task 请求读取显示 ID 时，只调用平台 LCD；SPI1 和三根控制线的具体时序仍由下层承担。

## 平台触摸（Platform Touch）

**平台触摸**表示当前 PCB 上的 FocalTech FT6X36 系列电容触摸控制器及其可供上层使用的触摸硬件能力。

它负责组合 FT6X36 Device、当前 I2C2/RESET Adapter 和本板 I2C 地址，并对上提供启动初始化、通信确认和触摸数据读取语义。它不等同于 LCD，不拥有 LVGL、GUI Task、坐标旋转、手势解释、TP IRQ 的任务通知或低功耗唤醒策略。

当前最小实现只读取 Chip ID，用于确认触摸模组的实际控制器。后续 LVGL 输入回调可在同一 GUI Task 内经平台触摸轮询 `TD_STATUS` 和坐标寄存器；TP IRQ 仅在确有降低轮询或低功耗唤醒需求时才注册。

相关术语：**FT6X36 设备**、**STM32 HAL FT6X36 I2C 适配器**、**平台 LCD**。

示例：

> Platform Init 在调度器启动前按 LCD、Touch 顺序完成硬件初始化；LCD Task 不直接包含 `hi2c2` 或 TP_RST，只读取 Chip ID。

## FT6X36 设备（FT6X36 Device）

**FT6X36 设备**是面向 FocalTech FT6X36 系列触摸控制器的可复用器件协议 Module。

它负责初始化成功后应具备的 I2C 就绪语义、寄存器地址和未来触点帧的读取语义，并通过 PortOps 使用外部能力。它不拥有 I2C Handle、GPIO、具体地址选择、LVGL、触摸坐标变换或中断策略。当前不建立名为“通用 Touch”但实际泄漏 FT6X36 寄存器语义的虚假抽象。

相关术语：**平台触摸**、**STM32 HAL FT6X36 I2C 适配器**。

示例：

> 移植到另一款 MCU 时，FT6X36 Device 无需修改；只替换实现其 PortOps 的 Adapter，并由新 Platform 绑定 I2C 与 RESET Context。

## STM32 HAL FT6X36 I2C 适配器（STM32 HAL FT6X36 I2C Adapter）

**STM32 HAL FT6X36 I2C 适配器**把 STM32 HAL I2C 探测、8-bit 寄存器读取和 RESET GPIO 初始化序列转换为 FT6X36 Device 的 PortOps。其 HAL 时基只在调度器启动前用于一次性硬件稳定等待。

它在内部处理 STM32 HAL 的左移一位地址表示与归一化传输状态，不拥有 `hi2c2`、TP_RST、板级地址、GUI Task 或 TP IRQ。所有具体硬件资源由平台触摸长期持有并注入。

相关术语：**FT6X36 设备**、**平台触摸**、**归一化传输状态**。

示例：

> FT6X36 Device 始终以 7-bit 地址表达设备；只有 STM32 HAL FT6X36 I2C 适配器调用 HAL 前才把它左移一位。

## 平台 SDRAM（Platform SDRAM）

**平台 SDRAM**表示当前 PCB 上兼容 `MT48LC16M16A2-6A` 与 `AS4C16M16SA-7TCN` 的 32 MiB x16
外部同步动态随机存储器及其 FMC 初始化状态。

它负责把 CubeMX 已配置的 FMC 控制器推进到可访问状态，并在启动阶段提供破坏性硬件诊断；它不拥有链接器段、动态堆、LVGL 帧缓冲、DMA 传输或任务策略。

相关术语：**存储任务**、**平台 LCD**。

示例：

> Storage Task 可以在任何帧缓冲开始使用前请求一次 SDRAM 诊断，但不能在已有业务数据时重复执行全容量测试。

## 平台温度（Platform Temperature）

**平台温度**表示当前 MCU 的片内温度传感器所测得的结温诊断能力。

它组合当前 PCB 的 ADC3 实例、内部温度传感器、VREFINT 和芯片工厂标定数据，但只向上层提供统一的结温结果。它不表示环境温度，也不拥有采样任务、日志输出、散热控制或过温策略。

相关术语：**MCU 结温**、**STM32 HAL 温度适配器**、**监控任务**。

示例：

> 监控任务可周期性记录平台温度；播放器的过温降频策略若以后需要，应由 Service 或 APP 消费该结果后决定。

## ST7789 设备（ST7789 Device）

**ST7789 设备**是面向 ST7789 显示控制器的可复用协议 Device。

它定义复位、最小显示初始化、地址窗口、RGB565 画点/填充、命令/数据选择、串行读写和
异步 RAMWR 事务生命周期，并通过 PortOps 使用外部能力；它不拥有屏幕电源、SPI 实例、
GPIO、帧缓冲、DMA Handle、Cache 或 UI 状态。

相关术语：**平台 LCD**、**STM32 HAL ST7789 SPI 适配器**。

示例：

> 更换 MCU 时，ST7789 设备可以不修改，只需重新实现它的 PortOps。

## 归一化传输状态（Normalized transport status）

**归一化传输状态**是跨越 Port 边界后仍保持稳定的底层结果类别，例如成功、忙、超时、未应答或介质不存在。

Device 错误表示“哪个语义步骤失败”，归一化传输状态表示“底层大致因为什么失败”。芯片厂商或具体后端专属的原始错误只用于深入诊断，不应成为上层业务逻辑的一部分。

相关术语：**AXP2101 I2C 适配器**、**SD 卡端口**。

示例：

> “寄存器读取失败”表示失败阶段，“从机未应答”表示可移植的底层原因。

## 延后启动日志（Deferred startup log）

**延后启动日志**是在输出端尚未就绪时先保存在 RAM 中，并在输出端可以接收后再提交的启动消息。

消息成功进入日志队列不代表主机已经显示。该机制用于解耦 MCU 的初始化时刻与 USB 串口终端的打开时刻。

相关术语：**播放器应用**、**USB CDC 日志适配器**。

示例：

> 即使 PC 晚于 MCU 打开虚拟串口，初始化日志仍应在端口就绪后显示。

## USB CDC 日志适配器（USB CDC log adapter）

**USB CDC 日志适配器**是日志核心与 USB 虚拟串口之间的输出适配器。

它负责判断 USB 输出是否就绪、维护异步发送期间的数据生命周期，并把厂商 USB 状态转换为日志系统的统一输出结果。日志核心不应依赖 USB 枚举、控制请求或端点状态等具体概念。

相关术语：**延后启动日志**、**归一化传输状态**。

示例：

> USB 正忙属于可重试状态，日志核心应保留消息并稍后再次提交。

## 日志投递（Service Log）

**日志投递**是 FreeRTOS 普通任务之间的异步日志转交 Module。它把固定数量的静态消息块在 `free queue`、生产者局部变量和 `ready queue` 之间转移；Log Task 是唯一消费者，负责把 ready 消息转交给日志核心。

日志投递不拥有 USB 输出、ANSI 颜色或日志格式化策略；这些仍属于日志核心和 USB CDC 日志适配器。普通任务使用 `Service_Log_Post()` 发布已经形成的文本，不能在 ISR 中调用它。`Service_Log_Init()` 必须早于所有可能投递日志的任务。

相关术语：**延后启动日志**、**USB CDC 日志适配器**、**存储任务**。

示例：

> Storage Task 在卡插入后只投递“Filesystem mounted.”；Log Task 再把该消息交给日志核心和 USB 输出后端。

## 平台 SD（Platform SD）

**平台 SD**表示本 PCB 上唯一的可移除 SD 卡槽及其平台行为。

它组合通用 SD Card Device、当前 Port、卡检测事件和 SDMMC DMA 完成事件，并向播放器应用报告稳定的插入或拔出。GPIO EXTI 边沿和 SDMMC 完成 IRQ 都只作为轻量通知；Platform 只把 DMA 事件转交给其唯一订阅者。Storage Task 决定消抖和卷生命周期，Filesystem Module 在该任务上下文中决定 DMA 等待与后续文件系统时序，机械触点产生了多少次边沿不属于播放器业务语义。

相关术语：**SD 卡设备**、**SD 卡端口**、**STM32 HAL GPIO EXTI 适配器**、**STM32 SDMMC IRQ 适配器**。

示例：

> 平台 SD 可以处于“未插卡”状态，同时整机仍然正常运行。

## SD 卡设备（SD Card Device）

**SD 卡设备**是可复用、面向逻辑块的介质模块。

它维护介质生命周期、容量信息、块范围校验、轮询块访问、DMA 的 `BUSY` 状态推进和稳定错误语义，不依赖某个 MCU 或厂商 HAL。文件系统挂载、DMA 缓冲区 Cache 维护和 USB MSC 所有权不属于该 Device。

相关术语：**平台 SD**、**SD 卡端口**。

示例：

> 文件系统应使用 SD Card Device 的逻辑块语义，而不是直接调用厂商 SD 驱动。

## SD 卡端口（SD Card Port）

**SD 卡端口**是 SD Card Device 与具体 SD 控制器、卡检测信号及厂商驱动之间的适配边界。

它负责执行具体硬件访问并归一化后端结果，使 Device 不需要认识 MCU 句柄、GPIO 极性或 HAL 状态码。

相关术语：**平台 SD**、**SD 卡设备**、**归一化传输状态**。

示例：

> SPI SD 后端可以替换 SDMMC 后端，同时保持 SD Card Device 的公共语义不变。

## 存储任务（Storage Task）

**存储任务**是 SD 热插拔、SDMMC DMA 完成和当前 FatFs 卷生命周期的唯一执行上下文。它通过索引 0 接收 GPIO EXTI 的轻量事件并完成机械触点消抖、平台 SD 生命周期推进；Filesystem Module 在同一任务上下文中通过索引 1 等待 SDMMC DMA 结果，并在插卡时挂载、拔卡时注销文件系统卷。

存储任务拥有 SD 卡与本地 FatFs 的访问时序，但不拥有 SDMMC、GPIO EXTI 或卡座引脚。中断回调只通知该任务，不能在 ISR 中执行消抖、FatFs、日志格式化或 SD 块访问。

相关术语：**平台 SD**、**文件系统 Module**、**日志投递**。

示例：

> 卡检测引脚产生多次抖动边沿时，存储任务只在最后一次边沿后的静默窗口结束后刷新一次平台 SD。

## 文件系统 Module（Filesystem Module）

**文件系统 Module**封装当前 FatFs 逻辑卷的驱动就绪检查、挂载、注销和显式格式化，并持有 FatFs 所需的同步 DMA 执行器。它只在存储任务已经取得 SD 独占权且平台 SD 已处于可访问状态时调用 FatFs，不负责卡检测、消抖或 SDMMC 初始化。

该 Module 的工作缓冲区和 DMA 中转缓冲区均属于静态存储期，以避免长文件名、格式化工作区和大块中转区挤占任务栈。它经 FatFs 声明的 `BSP_SD_*` Override Seam 间接使用 Platform SD；DMA 等待与 Cache 一致性细节见 `docs/sd_architecture.md`。它返回 FatFs 的 `FRESULT`，使存储任务能够区分“介质通信失败”和“介质上没有可挂载文件系统”等结果。

相关术语：**存储任务**、**平台 SD**、**SD 卡设备**。

示例：

> 插入一张未格式化卡时，平台 SD 仍可 READY；随后文件系统 Module 返回 `FR_NO_FILESYSTEM`，由存储任务决定是否响应明确的格式化请求。

## STM32 HAL GPIO EXTI 适配器（STM32 HAL GPIO EXTI adapter）

**STM32 HAL GPIO EXTI 适配器**占有 STM32 HAL 唯一的 `HAL_GPIO_EXTI_Callback()` 入口，并按 GPIO 引脚位掩码把事件交给已注册的调用者回调。

它维护由调用者长期持有的侵入式回调链表，但不认识 SD、PMIC 或其他产品语义，也不处理 SDMMC、DMA、USB、UART 等非 GPIO 外设中断。调用者在普通上下文注册和注销回调；Adapter 在 ISR 中只匹配 PinMask 并调用处理函数。回调只能发布通知或置位，消抖、日志、设备通信和文件系统操作必须延后到普通执行上下文。

相关术语：**平台 SD**。

示例：

> 平台 SD 持有卡检测 Callback，使用 `SD_CD_Pin` 注册；更换 MCU GPIO EXTI 后端只替换 GPIO EXTI Adapter。

## STM32 SDMMC IRQ 适配器（STM32 SDMMC IRQ Adapter）

**STM32 SDMMC IRQ 适配器**占有某个 `SD_HandleTypeDef` 的 HAL SD 完成、错误和中止回调注册，并按 Handle 把传输事件交给调用者持有的回调节点。

它不定义新的通用中断 ID，也不认识卡槽、Storage Task、FatFs 或 DMA 缓冲区。CubeMX 向量函数先调用 `HAL_SD_IRQHandler()`；Adapter 只把 HAL 回调翻译为强类型读完成、写完成、错误或中止事件。普通上下文负责注册和注销节点；ISR 回调只能向上发布事件，不能推进 Device 状态机或访问文件系统。

相关术语：**平台 SD**、**SD 卡设备**、**存储任务**。

示例：

> SDMMC1 的读 DMA 完成后，Adapter 发布“读完成”；Filesystem Module 的私有执行器在 Storage Task 上下文收到通知后才调用 Platform SD，使 SD Card Device 从 `BUSY` 回到 `READY`。
