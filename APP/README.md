# APP

`APP` 是固件的顶层编排 Module：决定初始化顺序、创建 FreeRTOS 任务并启动调度器。它不保存 HAL Handle、不构造 Adapter Ops，也不直接操作寄存器。

## 公开 Interface

- `app_init()`：由 `Core/Src/main.c` 的 USER CODE 接缝调用，初始化产品运行所需的 Module。
- `app_task_start()`：创建 bootstrap task 并启动 FreeRTOS 调度器；正常情况下不返回。

## 调用的 Interface

- `Platform_*` 产品硬件能力；
- `Service` 目录的公开 Interface；
- FreeRTOS 的任务创建和启动 Interface；
- CubeMX `Error_Handler()` 等明确的启动接缝。

## 禁止依赖

- 不包含 Adapter 私有头、HAL Handle、GPIO/SDMMC/I2S 寄存器细节；
- 不直接调用 `HAL_SD_*`、`BSP_SD_*`、`f_*`；
- 不实现器件协议、总线时序或板级 Adapter Bind。

## 命名

顶层编排和 Task 入口使用 `snake_case`。仅向其他 Module 暴露的能力使用本工程的 Pascal 分段规则；详情见 [../docs/coding_standard.md](../docs/coding_standard.md)。
