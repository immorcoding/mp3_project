# APP

`APP` 是固件的顶层编排 Module：调用 `Platform_Init()`、决定其失败是否致命、创建 FreeRTOS 任务并启动调度器。它不保存 HAL Handle、不构造 Adapter Ops，也不直接操作寄存器。LCD 与触摸控制器的启动初始化属于 `Platform_Init()` 的内部顺序；因此底层 HAL 延时不会阻塞普通任务。

## 公开 Interface

- `app_init()`：由 `Core/Src/main.c` 的 USER CODE 接缝调用，初始化产品运行所需的 Module。
- `app_task_start()`：创建 bootstrap task 并启动 FreeRTOS 调度器；正常情况下不返回。

## 编译期依赖

- `Platform_*` 产品硬件能力；
- `Service` 目录的公开 Interface；
- FreeRTOS 的任务创建和启动 Interface；
- CubeMX `Error_Handler()` 等明确的启动接缝。

## 运行时请求路径

APP 是启动与 Task 编排根。它通常发起 `APP → Service → Platform` 的产品请求，但不是每一笔请求的强制中转层；Task 内的后续流程由各自 Module 的公开 Interface 决定。

`Core/Src/main.c` 通过 USER CODE boot seam 调用 `app_init()`，这是生成代码进入 APP 的运行时入口，不表示其他下层 Module 可以包含 APP。

## 事件/ISR 路径

APP Task 只消费 Service 或 Platform 经公开 Interface 发布的普通上下文结果；不拥有 HAL ISR 回调，也不直接处理硬件中断。

## 禁止依赖

- 不包含 Adapter 私有头、HAL Handle、GPIO/SDMMC/I2S 寄存器细节；
- 不直接调用 `HAL_SD_*`、`BSP_SD_*`、`f_*`；
- 不实现器件协议、总线时序或板级 Adapter Bind。

## 命名

顶层编排和 Task 入口使用 `snake_case`。仅向其他 Module 暴露的能力使用本工程的 Pascal 分段规则；详情见 [../docs/coding_standard.md](../docs/coding_standard.md)。
