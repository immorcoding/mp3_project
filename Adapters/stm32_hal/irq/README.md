# STM32 IRQ Adapters

本目录只负责占有 STM32 HAL 的全局回调入口，并按**中断源自己的类型**分发事件。它不是通用 NVIC 框架：向量函数仍由 CubeMX `Core/Src/stm32h7xx_it.c` 调用相应的 `HAL_*_IRQHandler()`。

## 公开 Interface

- `stm32_gpio_exti_irq.h`：GPIO EXTI 的 PinMask 回调节点及注册、注销 Interface；
- `stm32_sdmmc_irq.h`：SDMMC 的 `SD_HandleTypeDef` 传输完成、错误和中止事件节点及注册、注销 Interface。
- `stm32_qspi_irq.h`：QSPI 的 `QSPI_HandleTypeDef` 接收完成、错误和中止事件节点及注册、注销 Interface。

三个 Interface 保持源特定、强类型；不得为了统一形式创建 `IRQ_ID + void *` 的泛化回调，也不得让 GPIO 调用者包含 SDMMC 或 QSPI 类型。

## 编译期依赖

- STM32 HAL GPIO EXTI、SD 与 QSPI 回调注册 Interface；
- CMSIS `PRIMASK` 临界区原语；
- 调用者持有的回调函数。

## 运行时事件路径

CubeMX 向量函数先进入相应 `HAL_*_IRQHandler()`；本目录随后从 HAL 全局回调或 Handle 回调接收事件，匹配源特定节点并调用调用者持有的轻量回调。此路径不是通用 NVIC 分发，也不允许 Adapter 直接知道 Task 或产品业务。

## 约束

- GPIO 模块独占 `HAL_GPIO_EXTI_Callback()`，使用调用者持有的侵入式链表按 PinMask 分发；
- SDMMC 模块通过每个 `SD_HandleTypeDef` 的 HAL 回调注册分发读完成、写完成、错误和中止；QSPI 模块同样通过每个 `QSPI_HandleTypeDef` 的读完成、错误和中止回调分发 MDMA 接收生命周期；两者均避免定义与 CubeMX/BSP 冲突的全局 HAL 回调符号；
- 注册和注销只能在普通上下文进行；ISR 回调只发布轻量事件，不能访问 FatFs、执行 SD 块读写、格式化日志或消抖；
- Adapter 不包含 `Platform`、`Service` 或 `APP` 头文件，不拥有任务、卡槽引脚或业务状态机。

命名上，跨 Module 的注册 Interface 保留 Pascal 分段命名；链表操作、匹配和 HAL 回调内部实现使用 `snake_case`。
