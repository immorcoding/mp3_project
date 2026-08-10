# SD Card Component

SD Card Component 是面向逻辑块的可复用介质状态机。它维护生命周期、容量缓存、块范围检查、同步和归一化错误，不认识 SDMMC、GPIO 或 FatFs。

## 公开 Interface

- 生命周期：`SDCard_Init()`、`SDCard_DeInit()`、`SDCard_Refresh()`；
- 块访问：`SDCard_ReadBlocks()`、`SDCard_WriteBlocks()`、`SDCard_Sync()`；
- 查询：`SDCard_GetInfo()`、`SDCard_GetState()`、`SDCard_IsPresent()`；
- `SDCard_PortOpsTypeDef` 与相关状态、错误、Handle 类型。

## 调用的 Interface

- Platform 绑定的 SD Card Port Ops。

## 约束

- 不包含 HAL、SDMMC 或卡检测 GPIO；
- `Refresh()` 不做机械消抖且不得在 ISR 调用；
- 文件系统、USB MSC 所有权和任务策略属于上层。
