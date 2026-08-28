# Platform Flash

本 Module 预留给当前 PCB 上的 W25Q256 外部 NOR Flash 装配与产品可见硬件能力。当前仅建立目录和架构约束，尚未创建源代码、配置 QSPI 或暴露上层 Interface。

## 预期公开 Interface

后续仅在出现真实上层使用者后，提供稳定的 `Platform_Flash_*` Interface；其具体范围由 W25Qxx 原始验证和 Flash FTL 设计共同决定。

## 编译期依赖与装配

- `Components/w25qxx`、`Components/flash_ftl` 的公开 Interface；
- `Adapters/stm32_hal/w25qxx_qspi` 的 QSPI Bind Interface；
- `Adapters/bridge/flash_ftl_w25qxx` 的跨 Component Bind Interface；
- CubeMX 管理的 QSPI Handle、GPIO 与时钟配置。

Platform 将长期持有 W25Qxx Handle、QSPI Adapter Context 与 Flash FTL Handle，并依次完成 HAL QSPI Adapter Bind、W25Qxx 初始化、FTL Bridge Bind 与 FTL 初始化。

## 运行时请求与约束

- 上层将来只使用 `Platform_Flash_*` 所表达的板级 Flash 能力，不访问 QSPI Handle、W25Qxx Handle 或 FTL 私有元数据；
- 本 Module 不实现 W25Q 指令、FTL 映射、FatFs 挂载、USB MSC 所有权、媒体扫描或任务策略；
- QSPI 实例、引脚、时钟和启动顺序是本 PCB 的事实，必须由 Platform 管理并与 CubeMX 配置同步核对。
