# APP

`APP` 是固件的顶层编排 Module：调用 `Platform_Init()`、决定其失败是否致命、创建 FreeRTOS 任务并启动调度器。它不保存 HAL Handle、不构造 Adapter Ops，也不直接操作寄存器。LCD、LED 与触摸控制器的启动初始化属于 `Platform_Init()` 的内部顺序；其中 LED 失败只降级诊断能力，触摸初始化失败当前不阻止启动，GUI 的后续读失败会向 LVGL 报告释放。底层 HAL 延时不会阻塞普通任务。

## Core 启动接缝

- `app_init()`：由 `Core/Src/main.c` 的 USER CODE 接缝调用，初始化产品运行所需的 Module。
- `app_error()`：由 `Core/Src/main.c` 的 `Error_Handler()` USER CODE 接缝调用，保留应用级失效安全处理入口。

## APP 内部 Interface

- `app_task_start()`：由 `app_init()` 调用，创建 bootstrap task 并启动 FreeRTOS 调度器；正常情况下不返回。

## 编译期依赖

- `Platform_*` 产品硬件能力；
- `Service` 目录的公开 Interface；
- FreeRTOS 的任务创建和启动 Interface；
- CubeMX `Error_Handler()` 等明确的启动接缝。

## 运行时请求路径

APP 是启动与 Task 编排根。它通常发起 `APP → Service → Platform` 的产品请求，但不是每一笔请求的强制中转层；Task 内的后续流程由各自 Module 的公开 Interface 决定。

`Core/Src/main.c` 通过 USER CODE boot seam 调用 `app_init()` 和 `app_error()`；这是生成代码进入 APP 的运行时入口，不表示其他下层 Module 可以包含 APP。

## 事件/ISR 路径

APP Task 只消费 Service 或 Platform 经公开 Interface 发布的普通上下文结果；不拥有 HAL ISR 回调，也不直接处理硬件中断。

## 禁止依赖

- 不包含 Adapter 私有头、HAL Handle、GPIO/SDMMC/I2S 寄存器细节；
- 不直接调用 `HAL_SD_*`、`BSP_SD_*`、`f_*`；
- 不实现器件协议、总线时序或板级 Adapter Bind。

## 命名

`APP` 是一个完整顶层 Module，`APP/tasks` 只是其私有 Implementation 分区。`app_init()`、`app_error()` 是 Core 进入 APP 的启动接缝，`app_task_start()` 与各 Task 入口仅在 APP 内调用；它们都使用 `snake_case`。下层 Module 不得包含 APP 头文件或调用其函数；需要被多处复用的产品能力应下沉为 Service Interface。详情见 [../docs/coding_standard.md](../docs/coding_standard.md)。
