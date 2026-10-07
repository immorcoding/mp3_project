# Architecture · generation

生成代码与自维护代码的修改边界。

[返回 Architecture](architecture.md)

### Rules

- **ARC-2** · settled · CubeMX（`io_sheet.ioc` 产物）和 Vendor（HAL/CMSIS/中间件）只经源工程重新导出或上游更新；HAL 缺陷在 `Adapters/stm32_hal/` 绕过。界面按 GUI-1 手写；`FATFS/Target/bsp_driver_user_diskio.*` 为自维护契约。_Why:_ 重新导出覆盖手改。_Check:_ Agent Hook 保护全部生成/Vendor 产物；`check-generated-write.ps1` 保护 Vendor 与 `cmake/stm32cubemx/`。
- **ARC-3** · settled · CubeMX `USER CODE` 修改先获用户确认，只调用或转发自维护入口，不放产品逻辑；链接段要求 C 运行库前访问外部存储时，`fmc.c` 的早期初始化接缝可使用局部状态与 Vendor/HAL，但不进入 APP/Service/Platform/FreeRTOS，不作为产品 Interface。 _Why:_ 生成接缝必须可再次导出，早期内存初始化尚无运行库和任务环境。 _Source:_ [ADR-0008](../adr/0008-sdram-early-init-and-noload-ownership.md) _Check:_ Agent Hook 对 USER CODE 文件走 ask（Codex deny）；例外范围按 `CODING_STANDARDS.md` 审阅。
