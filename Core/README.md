# Core

`Core` 由 CubeMX 管理 MCU 启动、时钟、外设初始化、IRQ 向量和 HAL MSP。自维护产品逻辑只能经 USER CODE 区进入 `APP` 或已有 Adapter。

## 接缝

- `main.c`：调用 `app_init()`；
- `stm32h7xx_it.c`：将外设中断交给相应 `HAL_*_IRQHandler()`；
- `gpio.c`、`sdmmc.c`、`i2s.c`：提供 CubeMX 的全局 Handle 与引脚配置。

`Core` 是生成代码的出站/入站接缝：它在 USER CODE 处进入 APP，IRQ 向量则把硬件事件交给 HAL。该运行时关系是明确例外，不允许据此让自维护低层普遍包含 APP。

## 约束

- 不将产品策略、FatFs 流程或 FreeRTOS 任务直接写入生成区；
- 更新 `.ioc` 并重新生成后，必须验证 USER CODE 接缝、NVIC 优先级和 HAL 配置；
- `Core` 可以调用 App 启动入口，但其余下层不应包含 `APP`。
