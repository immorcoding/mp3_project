# 项目领域上下文

> 章程：稳定领域词典。按条查阅，禁止开场整本阅读。不写进度、本场文件名单或实现参数。单条超过约 25 行且夹带寄存器/坐标/缓存大小时，下沉到对应 `docs/*_architecture.md` 或 Module README。现场见 `CURRENT.md`，工作方式见 `AGENTS.md`。

本文档只定义 Module 之间反复使用的领域词汇、职责边界和产品语义。路径、字段、调用链、寄存器和硬件参数由 `docs/` 与各 Module README 维护；长期取舍由 `docs/adr/` 记录。新增 Module、改变职责归属或改变产品语义时，必须同步核对本文档与对应技术文档；若同时形成可复用的长期架构取舍，还必须新增或更新对应 ADR。

## 播放器应用（Player application）

**播放器应用**是固件的产品行为入口，负责组织系统服务、板级强依赖设备和可移除介质的启动顺序，并持续推进播放器运行所需的周期任务。

它决定“什么时候初始化、失败是否致命、事件如何影响播放器行为”，但不实现器件寄存器协议、总线时序或芯片厂商接口。

相关术语：**平台电源**、**延后启动日志**、**平台 SD**。

示例：

> 播放器应用把 SD 卡视为可选介质，因此未插卡不会阻止系统启动。首版曲库只扫描 SD 上的 `Music/`；未插卡或没有该目录时播放列表为空。

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

**AXP2101 I2C 适配器**是 AXP2101 Device 与具体 I2C 后端之间的 Adapter，当前是 SoftI2C 到 AXP2101 Bus Ops 的跨 Component Bridge；未来可替换为硬件 I2C 或测试 Adapter。

它把具体后端的传输结果转换成 AXP2101 Device 的 Bus Ops。Bridge 不依赖 HAL 或 FreeRTOS，也不拥有板级引脚和 I2C 实例；实例始终由 Platform 注入。

相关术语：**AXP2101 设备**、**归一化传输状态**。

示例：

> 硬件 I2C 的超时状态应在 Adapter 内转换成统一的 AXP2101 总线超时。

## 软件 I2C 组件（SoftI2C Component）

**软件 I2C 组件**是与具体 MCU 无关的开漏 I2C 时序算法。

它只通过 GPIO Ops 表达 SCL/SDA 的主动拉低、释放和实际电平读取，不拥有具体引脚或芯片协议。总线恢复属于 I2C 算法的一部分，因此仍由该组件负责。

相关术语：**STM32 GPIO 适配器**、**AXP2101 I2C 适配器**。

示例：

> 把软件 I2C 移植到另一款 MCU 时，只替换 GPIO Adapter，不修改 START、ACK 和恢复算法。

## STM32 GPIO 适配器（STM32 GPIO Adapter）

**STM32 GPIO 适配器**把软件 I2C 的最小 GPIO Ops 映射到 STM32 HAL GPIO。

它认识 GPIO 端口与引脚，但不认识 AXP2101 地址、寄存器和电源策略。具体 SCL/SDA 引脚由 Platform 注入。

相关术语：**软件 I2C 组件**、**平台电源**。

示例：

> `RELEASED` 在 STM32 开漏输出上表现为写入 SET，但其真实高电平仍由外部上拉产生。

## Cortex-M7 Cache 适配器（Cortex-M7 Cache Adapter）

**Cortex-M7 Cache 适配器**封装 Cacheable 内存范围的 D-Cache 维护。它是无状态 Adapter：提供按范围的 Clean、Invalidate 与 Clean + Invalidate；DMA Adapter 和 Platform SDRAM 诊断按各自语义组合调用。

它不拥有外设 Handle、DMA 等待、任务通知或中转缓冲区。对齐与范围所有权见 [ADR-0006](docs/adr/0006-cache-range-ownership.md)。

相关术语：**SD 卡端口**、**文件系统 Module**。

示例：

> SDMMC Adapter 在启动读 DMA 前维护 Cache，DMA 完成后由任务上下文再使 CPU 读取 RAM 中的新数据。

## Cortex-M7 周期计数器适配器（Cortex-M7 Cycle Counter Adapter）

**Cortex-M7 周期计数器适配器**向性能诊断提供核心周期数与当前核心频率，使 Platform 不需要认识核心调试寄存器。

周期计数器是全局硬件资源：测量者必须在普通上下文串行使用，并在回绕前完成单次测量。它不拥有 SDRAM、DMA、任务或缓冲区。测速用法见 [sdram_architecture.md](docs/sdram_architecture.md)。

相关术语：**平台 SDRAM**、**Cortex-M7 Cache 适配器**。

示例：

> 平台 SDRAM 在写入和冷读测速前复位周期计数器，随后只用周期差和核心频率换算吞吐。

## 平台 LED（Platform LED）

**平台 LED**表示当前 PCB 上一个或多个可供上层使用的板级诊断 LED。它长期持有 LED Device 和 Adapter Context，并通过稳定逻辑编号装配当前 LED 后端；闪烁周期、任务调度和产品状态机仍由调用者决定。

当前状态灯使用 GPIO；逻辑 ON/OFF 与物理电平的映射属于 Adapter Context。未来新增 I2C、PWM 或扩展 IO LED 时，Platform 只增加编号与实例装配，上层调用方式不变。

相关术语：**LED 设备**、**STM32 HAL GPIO LED 适配器**、**播放器应用**。

示例：

> Monitor Task 每个监控周期翻转状态灯，但无需包含 HAL 或板级引脚头。

## LED 设备（LED Device）

**LED 设备**是可复用的逻辑亮灭 Device Module。它通过 PortOps 请求 ON/OFF，维护最后一次成功提交的逻辑状态；它不认识 GPIO 电平、I2C 地址、PWM、芯片寄存器或板级编号。

`Toggle` 由设备根据最后一次成功的逻辑状态调用 `Set` 实现。Port 失败时设备保留旧状态。多灯的编号、实例表与启动失败策略均不属于该 Device。

相关术语：**平台 LED**、**STM32 HAL GPIO LED 适配器**。

示例：

> 低有效 GPIO LED 和高有效 GPIO LED 都接收相同的逻辑 ON 请求，由各自 Adapter Context 决定输出电平。

## STM32 HAL GPIO LED 适配器（STM32 HAL GPIO LED Adapter）

**STM32 HAL GPIO LED 适配器**把 LED Device 的逻辑 ON/OFF PortOps 映射到 STM32 HAL GPIO 写操作。

它认识端口、引脚和 ON 对应电平，但不选择当前 PCB 的引脚、LED 编号、任务周期或启动策略。

相关术语：**LED 设备**、**平台 LED**。

示例：

> 低有效 LED 只需把 Adapter Context 的导通电平配成 RESET，Device 与 Monitor Task 都无需修改。

## 平台电源启动配置（Platform power boot profile）

**平台电源启动配置**是 AXP2101 完成器件识别后，由 Platform Power 按顺序应用的一组 PCB 供电策略。

它控制输入限制、充电、中断、ADC、DCDC/LDO 使能和预设电压。修改该配置属于硬件电源策略变更，必须同时核对数据手册、原理图、负载允许电压、上电顺序和实测结果。

相关术语：**平台电源**、**播放器应用**。

示例：

> 关闭某一路 LDO 的启动使能，不代表应同时清除它的电压预设。

## 平台 LCD（Platform LCD）

**平台 LCD**表示当前 PCB 上唯一的 ST7789V 显示模组及其可见显示能力。

它负责组合 LCD 电源轨、SPI、片选、命令/数据、复位、背光等板级资源，并向上层提供不泄漏 HAL Handle 和 GPIO 细节的显示语义。DMA 完成只经已注册的轻量回调上报；Platform 不认识任务通知、帧缓冲或 LVGL。

相关术语：**ST7789 设备**、**STM32 HAL ST7789 SPI 适配器**、**平台电源**。

示例：

> GUI Service 请求读取显示 ID 时，只调用平台 LCD；SPI 和控制线的具体时序仍由下层承担。

## 平台触摸（Platform Touch）

**平台触摸**表示当前 PCB 上的 FocalTech FT6X36 系列电容触摸控制器及其可供上层使用的触摸硬件能力。

它负责组合 FT6X36 Device、当前 I2C/RESET Adapter 和本板地址，并对上提供启动初始化、通信确认和触摸数据读取。它不等同于 LCD，不拥有 LVGL、GUI Task、坐标旋转、手势、中断任务通知或低功耗唤醒。Platform 只发布原始按下状态和 X/Y；坐标方向由 GUI Service 按显示方向解释。轮询与中断的取舍见 [touch_architecture.md](docs/touch_architecture.md)。

相关术语：**FT6X36 设备**、**STM32 HAL FT6X36 I2C 适配器**、**平台 LCD**。

示例：

> Platform Init 在调度器启动前按 LCD、Touch 顺序完成硬件初始化；GUI Service 不直接包含 I2C 句柄或复位脚。

## FT6X36 设备（FT6X36 Device）

**FT6X36 设备**是面向 FocalTech FT6X36 系列触摸控制器的可复用器件协议 Module。

它负责初始化成功后应具备的 I2C 就绪语义、寄存器地址和第一触点帧的读取语义，并通过 PortOps 使用外部能力。它不拥有 I2C Handle、GPIO、具体地址、LVGL、坐标变换或中断策略。当前不建立名为“通用 Touch”但实际泄漏 FT6X36 寄存器语义的虚假抽象。

相关术语：**平台触摸**、**STM32 HAL FT6X36 I2C 适配器**。

示例：

> 移植到另一款 MCU 时，FT6X36 Device 无需修改；只替换实现其 PortOps 的 Adapter，并由新 Platform 绑定 I2C 与 RESET Context。

## STM32 HAL FT6X36 I2C 适配器（STM32 HAL FT6X36 I2C Adapter）

**STM32 HAL FT6X36 I2C 适配器**把 STM32 HAL I2C 探测、寄存器读取和 RESET GPIO 初始化序列转换为 FT6X36 Device 的 PortOps。

它处理 HAL 地址表示与归一化传输状态，不拥有板级 I2C 实例、复位脚、GUI Task 或触摸中断。所有具体硬件资源由平台触摸长期持有并注入。地址表示细节见 [touch_architecture.md](docs/touch_architecture.md)。

相关术语：**FT6X36 设备**、**平台触摸**、**归一化传输状态**。

示例：

> FT6X36 Device 始终以 7-bit 地址表达设备；只有该 Adapter 在调用 HAL 前才做厂商要求的地址变换。

## 平台 SDRAM（Platform SDRAM）

**平台 SDRAM**表示当前 PCB 上兼容 `MT48LC16M16A2-6A` 与 `AS4C16M16SA-7TCN` 的 32 MiB x16 外部同步动态随机存储器及其 FMC 初始化状态。

它负责把已配置的 FMC 控制器推进到可访问状态，并在启动阶段提供破坏性硬件诊断；它不拥有链接器段、动态堆、LVGL 帧缓冲、DMA 传输或任务策略。时钟与时序见 [sdram_architecture.md](docs/sdram_architecture.md)。

相关术语：**存储任务**、**平台 LCD**。

示例：

> Storage Task 可以在任何帧缓冲开始使用前请求一次 SDRAM 诊断，但不能在已有业务数据时重复执行全容量测试。

## 平台温度（Platform Temperature）

**平台温度**表示当前 MCU 的片内温度传感器所测得的结温诊断能力。

它组合当前 PCB 的 ADC 实例、内部温度传感器、基准和芯片工厂标定数据，但只向上层提供统一的结温结果。它不表示环境温度，也不拥有采样任务、日志输出、散热控制或过温策略。

相关术语：**MCU 结温**、**STM32 HAL 温度适配器**、**监控任务**。

示例：

> 监控任务可周期性记录平台温度；播放器的过温降频策略若以后需要，应由 Service 或 APP 消费该结果后决定。

## ST7789 设备（ST7789 Device）

**ST7789 设备**是面向 ST7789 显示控制器的可复用协议 Device。

它定义复位、最小显示初始化、地址窗口、RGB565 传输和异步写窗生命周期，并通过 PortOps 使用外部能力；它不拥有屏幕电源、SPI 实例、GPIO、帧缓冲、DMA、Cache 或 UI 状态。

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

**日志投递**是普通任务之间的异步日志转交 Module。Log Task 是唯一消费者，负责把就绪消息转交给日志核心。

它不拥有 USB 输出、颜色或格式化策略；这些仍属于日志核心和 USB CDC 日志适配器。普通任务发布已经形成的文本，不能在 ISR 中投递。初始化必须早于所有可能投递日志的任务。队列与任务接缝见 [log_architecture.md](docs/log_architecture.md)。

相关术语：**延后启动日志**、**USB CDC 日志适配器**、**存储任务**。

示例：

> Storage Task 在卡插入后只投递“已挂载”一类短消息；Log Task 再把它交给日志核心和 USB 输出后端。

## 平台 SD（Platform SD）

**平台 SD**表示本 PCB 上唯一的可移除 SD 卡槽及其平台行为。

它组合通用 SD Card Device、当前 Port、卡检测事件和 SDMMC DMA 完成事件，并向播放器应用报告稳定的插入或拔出。边沿和传输完成 IRQ 都只作为轻量通知。Storage Task 决定消抖和卷生命周期，Filesystem Module 在该任务上下文中决定 DMA 等待与后续文件系统时序。机械触点产生了多少次边沿不属于播放器业务语义。

相关术语：**SD 卡设备**、**SD 卡端口**、**STM32 HAL GPIO EXTI 适配器**、**STM32 SDMMC IRQ 适配器**。

示例：

> 平台 SD 可以处于“未插卡”状态，同时整机仍然正常运行。

## SD 卡设备（SD Card Device）

**SD 卡设备**是可复用、面向逻辑块的介质模块。

它维护介质生命周期、容量信息、块范围校验、轮询块访问、DMA 忙状态推进和稳定错误语义，不依赖某个 MCU 或厂商 HAL。文件系统挂载、DMA 缓冲区 Cache 维护和 USB MSC 所有权不属于该 Device。

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

**存储任务**是 SD 热插拔、块传输完成、FatFs 卷生命周期和 Flash 异步操作的唯一执行上下文。

它拥有 SD 与本地文件系统的访问时序，但不拥有 SDMMC、GPIO EXTI 或卡座引脚。中断只通知该任务，不能在 ISR 中执行消抖、FatFs、日志格式化或块访问。通知槽的划分见 [storage README](APP/tasks/storage/README.md) 与 [sd_architecture.md](docs/sd_architecture.md)。

相关术语：**平台 SD**、**文件系统 Module**、**日志投递**。

示例：

> 卡检测引脚产生多次抖动边沿时，存储任务只在最后一次边沿后的静默窗口结束后刷新一次平台 SD。

## 文件系统 Module（Filesystem Module）

**文件系统 Module**封装 FatFs 逻辑卷的就绪检查、挂载、注销，以及仅针对内部 Flash 卷的显式格式化/恢复，并持有同步 DMA 与 Flash 执行器。它只在存储任务已经取得介质独占权且对应 Platform 能力可访问时调用 FatFs，不负责卡检测、消抖或控制器初始化。设备不格式化 SD 卡。

公开 Interface 按卷生命周期、卷感知文件、卷感知目录和启动诊断切开。调用者只持有 Volume、UTF-8 相对路径和不透明句柄。同步接口仅唯一 Storage Task 调用。卷分工见 [ADR-0015](docs/adr/0015-volume-roles-and-resource-install.md)；路径与句柄规则见 [ADR-0014](docs/adr/0014-filesystem-volume-aware-file-interface.md) 和 [filesystem README](Service/filesystem/README.md)。

相关术语：**存储任务**、**平台 SD**、**平台 Flash**、**SD 卡设备**。

示例：

> 插入一张未格式化卡时，平台 SD 仍可 READY；随后文件系统 Module 返回无文件系统。设备不格式化 SD，只记录可诊断状态，由用户在电脑上格式化。

## STM32 HAL GPIO EXTI 适配器（STM32 HAL GPIO EXTI adapter）

**STM32 HAL GPIO EXTI 适配器**占有 STM32 HAL 的 GPIO 外部中断回调入口，并按引脚掩码把事件交给已注册的调用者回调。

它不认识 SD、PMIC 或其他产品语义，也不处理非 GPIO 外设中断。调用者在普通上下文注册和注销；ISR 中只匹配并调用处理函数。回调只能发布通知或置位。分发细节见 [irq README](Adapters/stm32_hal/irq/README.md)。

相关术语：**平台 SD**。

示例：

> 平台 SD 持有卡检测回调并按本板检测脚注册；更换 MCU 的 GPIO EXTI 后端只替换该 Adapter。

## STM32 SDMMC IRQ 适配器（STM32 SDMMC IRQ Adapter）

**STM32 SDMMC IRQ 适配器**占有某个 SD Handle 的 HAL 完成、错误和中止回调，并按 Handle 把传输事件交给调用者持有的节点。

它不认识卡槽、Storage Task、FatFs 或 DMA 缓冲区。ISR 回调只能向上发布事件，不能推进 Device 状态机或访问文件系统。见 [sd_architecture.md](docs/sd_architecture.md)。

相关术语：**平台 SD**、**SD 卡设备**、**存储任务**。

示例：

> SDMMC 读 DMA 完成后，Adapter 发布“读完成”；Filesystem 私有执行器在 Storage Task 上下文收到通知后才让 Device 从忙回到就绪。

## STM32 QSPI IRQ 适配器（STM32 QSPI IRQ Adapter）

**STM32 QSPI IRQ 适配器**占有某个 QSPI Handle 的 HAL 读完成、自动轮询匹配、错误和中止回调，并按 Handle 把事件交给调用者持有的节点。

它不认识 W25Qxx、平台 Flash、Storage Task 或 DMA 缓冲区。ISR 只能发布轻量通知；Cache 维护和 Device 状态推进属于普通上下文。见 [w25q256_architecture.md](docs/w25q256_architecture.md)。

相关术语：**平台 Flash**、**W25Qxx 设备**、**Cortex-M7 Cache 适配器**。

示例：

> QSPI 接收完成或状态匹配后，平台 Flash 先让 Adapter 记录结果，再通知 Storage Task；任务被唤醒后才处理缓冲区或提交下一页。

## W25Qxx 设备（W25Qxx Device）

**W25Qxx 设备**是面向 Winbond W25Q 系列串行 NOR Flash 的可复用芯片协议 Module。

它负责识别、状态、Quad 使能，以及原始数组读取、页编程和扇区擦除的协议生命周期；通过自身拥有的 Bus Ops 使用具体总线后端。FTL 映射、任意长度拆页、FatFs、USB MSC 和媒体业务均不属于它；它也不拥有 QSPI Handle 或板级引脚。命令、时序与映射协议见 [w25q256_architecture.md](docs/w25q256_architecture.md)。

相关术语：**Flash FTL**、**平台 Flash**。

示例：

> 更换 MCU 时，W25Qxx Device 可以不修改，只需替换 Bus Ops 的 Adapter，并由 Platform 注入本板识别条件。

## Flash FTL（Flash Translation Layer）

**Flash FTL**是把原始 NOR Flash 转换为稳定逻辑扇区的可复用 Module。它拥有 Raw Ops，但不认识 W25Qxx、STM32 HAL、Platform、FatFs、USB MSC 或媒体业务。W25Qxx 到 FTL 的转换由跨 Component Bridge 承担。

**FTL 逻辑组**是一组共同映射并提交版本的相邻逻辑扇区；组内局部更新保留其他扇区，跨组请求不具备整体原子性。

**已提交组版本**是校验和提交记录都满足卷格式的组记录；最新已提交版本损坏时报告损坏，不静默回退旧版。

**已擦除空闲块**是确认整块擦除、可安全接收新记录的物理块；只看到空头不能把它归为可分配空闲块。

**FTL 格式代次**标识一次显式底层格式化产生的卷世代；它不同于格式协议版本和单组更新版本。

**FTL 格式化**建立底层卷与空闲块，**FatFs 格式化**在其逻辑扇区上建立文件系统；两者都不因挂载失败而自动执行。

规则见 [flash_ftl_design.md](docs/flash_ftl_design.md) 与 [ADR-0011](docs/adr/0011-ftl-copy-on-write-and-recovery.md)。板级验收状态见 `CURRENT.md`。

相关术语：**W25Qxx 设备**、**平台 Flash**。

## 平台 Flash（Platform Flash）

**平台 Flash**代表当前 PCB 上 W25Q256 外部 NOR Flash 的板级装配 Module。它长期持有芯片 Handle、QSPI Adapter 与中断回调节点，完成本板识别与受限的原始/逻辑访问接缝，并唯一拥有只读内存映射的生命周期。

它不实现芯片协议、FTL 映射、FatFs 挂载、USB MSC 或媒体扫描。CubeMX 管理实际控制器、引脚与时钟；Platform 只注入借用的实例并决定本板启动识别策略。分区与自检区见 [ADR-0009](docs/adr/0009-w25q256-firmware-slots-and-diagnostic-reservation.md)；命令与映射窗口见 [w25q256_architecture.md](docs/w25q256_architecture.md)。

相关术语：**W25Qxx 设备**、**Flash FTL**。

示例：

> 文件系统或诊断请求 Flash 操作时只调用平台 Flash；QSPI 句柄和引脚仍由下层持有。

## 可回滚固件镜像（Rollback Firmware Image）

**可回滚固件镜像**是保存在外部 Flash 中、供未来 Bootloader 校验并恢复或安装到 MCU 内部 Flash 的完整固件二进制，不是工程源码或普通文件系统文件。

它属于启动与升级路径的固定原始分区，必须和 **Flash FTL** 的逻辑容量、模型资源及用户文件区物理隔离；镜像格式、完整性校验和安装时机由未来 Bootloader 协议定义。

相关术语：**平台 Flash**、**Flash FTL**。

## 固件镜像槽（Firmware Image Slot）

**固件镜像槽**是外部 Flash 中容纳一份完整可启动固件及其镜像头的固定原始分区；本产品使用一个 **候选槽** 和一个 **回滚槽**，均不属于 **Flash FTL**。

候选槽保存已下载且待安装的新版本，回滚槽保存已验证的旧版本；两槽和首尾 Flash 自检区共同从 FTL 的逻辑容量中永久扣除。

相关术语：**可回滚固件镜像**、**平台 Flash**、**Flash FTL**。

## 资源包（Resource Pack）

**资源包**是外部 Flash 中连续、原始且只读的数据镜像，承载字库、模型等需要按地址高吞吐读取的系统资源。它有独立的镜像头、版本和完整性校验，由烧录工具在开发/发布时写入固定物理范围；运行时经 QSPI 内存映射读取，不经过 FatFs、USB MSC 或 Flash FTL。

其固定范围必须位于两个固件槽之后、FTL 分区之前。发布默认包仍由 PC 烧录；设备侧更新壁纸或小模型时，生效位置只能是本包，安装路径见 [ADR-0015](docs/adr/0015-volume-roles-and-resource-install.md)。格式与加载见 [resource_pack_design.md](docs/resource_pack_design.md)。

相关术语：**平台 Flash**、**Flash FTL**、**固件镜像槽**、**资源安装**、**Cortex-M7 Cache 适配器**。

## 曲库（Catalog）

**曲库**是已扫描曲目的事实表：来源卷加上 UTF-8 相对路径。它不是播放顺序，也不等于 GUI Queue 窗口。首版只枚举已挂载 SD 的 `Music/`，收录 `.mp3`；没有该目录或未插卡时曲库为空，不自动创建目录。重扫或拔卡后旧下标作废，问询必须带代次。

相关术语：**播放列表**、**文件系统 Module**。

## 播放列表（Playback List）

**播放列表**是曲库下标的排列，不是路径的第二份拷贝，也不是 GUI 可见行。首版按曲库扫描顺序生成恒等序列；上一首/下一首沿播放列表下标移动。随机播放或以后的心动模式只替换这条序列，不重新遍历文件系统。有效长度就是当时的曲库条数。Storage Task 在曲库扫描成功后立即重建播放列表，拔卡时与曲库一并作废。不向 GUI 暴露整表指针；问询带代次。

GUI Queue 只读取可见窗口（外加少量行预取以免滚动空白）。预取的是曲名等行数据，不是解码缓冲。Playback 打开当前曲，至多再预开下一首，与窗口滑到哪一行无关。

相关术语：**曲库**、**文件系统 Module**、**资源安装**。

## 资源安装（Resource Install）

**资源安装**是把 SD 上的壁纸包或模型包装进机内 Resource Pack 的事务，不是把普通文件挪到 FTL 目录供 GUI 长期引用。Storage Task 推进暂存、校验与 Pack 写入；Settings 只发起并显示阶段。中途拔卡不得破坏正在使用的旧包；暂存完整后不插卡也可重试写入 Pack。

以后只读资源管理器的跨卷复制（歌、书）与资源安装分开，暂存路径不进入曲库扫描。

相关术语：**资源包**、**文件系统 Module**、**播放列表**。
