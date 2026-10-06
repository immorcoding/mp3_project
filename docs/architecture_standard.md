# 工程架构阅读入口

当前分层与开发规则统一在 [Architecture](shape/architecture.md)，按改动范围读取；本页保留旧入口，不重复发布规范。

| 改动 | 必读正文 |
| --- | --- |
| 跨层依赖、生成器边界、Module 归属 | [layering](shape/architecture.md#layering)，[ADR-0002](adr/0002-layering-and-interface-ownership.md) |
| Ops 装配、状态/错误、ISR、FTL/文件生命周期 | [lifecycle](shape/architecture.md#lifecycle) |
| 新模块、README 与独立构建 | [maintenance](shape/architecture.md#maintenance) |
| 目录职责与模块阅读路线 | [ROUTES](shape/ROUTES.md) |
| 命名、Doxygen、config | [代码审阅入口](coding_standard.md) |

## 技术解释

- SoftI2C GPIO 的 LOW/RELEASED、桥接与板级电源装配：[PMIC 架构](pmic_i2c_architecture.md)。RELEASED 是释放开漏输出，由外部上拉形成高电平。
- 日志的格式化核心、输出 Adapter 与绑定：[日志架构](log_architecture.md)。
- 状态与归一化错误的调试含义：[错误模型](error_model.md)。
- GPIO/SD/QSPI/LCD 完成事件和任务通知：[运行路径](runtime_paths.md)。
- SD/FatFs：[SD 架构](sd_architecture.md)；Flash/FTL：[NOR 架构](w25q256_architecture.md)、[FTL 设计](flash_ftl_design.md)。
- Filesystem 当前公开接缝：[接缝说明](filesystem_service_reshape.md)。
- 曲库、播放列表及解析/解码边界：[曲库架构](catalog_architecture.md)。

构建源集合以根 `CMakeLists.txt` 与生成的 `cmake/stm32cubemx/CMakeLists.txt` 为事实源。后者纳入 FATFS App/Target；生成 DiskIO 经声明的 BSP Override 接缝进入 Service，不承载产品执行逻辑。
