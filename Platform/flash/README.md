# Platform Flash

本 Module 装配当前 PCB 上的 W25Q256 外部 NOR Flash 与 CubeMX QSPI。当前已在启动阶段绑定 STM32 HAL QSPI Adapter，读取 JEDEC ID，并按本板配置校验 Winbond 厂商码 `EF` 与 256 Mbit 容量码 `19`；完整三字节 ID 仍会缓存并对上公开。Flash FTL、擦写、逻辑扇区、FatFs 和 USB MSC 均未接入。

## 预期公开 Interface

- `Platform_Flash_Init()`：绑定 QSPI Adapter，同步读取并校验 JEDEC ID；
- `Platform_Flash_GetJedecID()`：取得初始化阶段缓存的三字节 ID，不重新访问 QSPI。

## 编译期依赖与装配

- `Components/w25qxx` 的公开 Interface；
- `Adapters/stm32_hal/w25qxx_qspi` 的 QSPI Bind Interface；
- CubeMX 管理的 QSPI Handle、GPIO 与时钟配置。

Platform 当前长期持有 W25Qxx Handle、`EF / 19` 的期望标识和 QSPI Adapter Context，完成 HAL QSPI Adapter Bind 与 W25Qxx 初始化。`MemoryType`（例如 `40` 或 `70`）只保留为诊断信息，不影响 W25Q256 兼容性判定。FTL 实现后才会增加 Flash FTL Handle 与 Bridge Bind。

## 运行时请求与约束

- 上层只使用 `Platform_Flash_*` 所表达的板级 Flash 能力，不访问 QSPI Handle、W25Qxx Handle 或未来 FTL 私有元数据；
- 本 Module 不实现 W25Q 指令、FTL 映射、FatFs 挂载、USB MSC 所有权、媒体扫描或任务策略；
- QSPI 实例、引脚、时钟和启动顺序是本 PCB 的事实，必须由 Platform 管理并与 CubeMX 配置同步核对。
