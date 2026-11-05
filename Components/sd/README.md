# SD Card Component

SD Card Component 是面向逻辑块的可复用介质状态机。它维护生命周期、容量缓存、块范围检查、同步、DMA 传输状态和归一化错误；它不认识 SDMMC、GPIO、FreeRTOS 或 FatFs。

## 公开 Interface

- `SDCard_Init()`、`SDCard_DeInit()`、`SDCard_Refresh()`；
- `SDCard_ReadBlocks()`、`SDCard_WriteBlocks()`：阻塞轮询路径；
- `SDCard_StartReadBlocks()`、`SDCard_StartWriteBlocks()`：只启动 DMA，并进入 `BUSY`；
- `SDCard_CompleteTransfer()`、`SDCard_FailTransfer()`：由普通上下文在传输事件后推进状态；
- `SDCard_AbortTransfer()`：等 DMA 完成事件超时后，在上界内显式中止在飞的传输（见下节）；
- `SDCard_GetInfo()`、`SDCard_GetState()`、`SDCard_GetDiagnostics()`；
- `SDCard_PortOpsTypeDef`：由需要块介质能力的 SD Card Component 定义、由具体 Adapter 实现的端口契约。

## 编译期依赖

- 调用注入的 `SDCard_PortOpsTypeDef`；
- 仅使用 ISO C，不包含 HAL、Adapter、RTOS 或 FatFs Interface。

## 私有配置

- `sd_config.h` 集中保存阻塞块传输、介质同步和显式中止（`SDCARD_ABORT_TIMEOUT_MS`，100 ms）的默认超时；仅本 Component 的 Implementation 可包含它，Adapter 与上层不可据此决定超时策略。

## 运行时请求与事件路径

SD Card Device 经自身拥有的 `SDCard_PortOpsTypeDef` 发起介质请求；Adapter 实现 PortOps，Platform 装配 Context。DMA 完成事件由上层普通上下文调用 `SDCard_CompleteTransfer()` 或 `SDCard_FailTransfer()` 推进，ISR 不直接进入 Device。

## 显式中止（#209，ADR-0024 第 7 条）

`SDCard_AbortTransfer()` 只在 Device 处于 `BUSY`（`SDCard_Start*Blocks()` 已受理、完成事件没有按时到）时受理，调用 Port 的可选操作 `AbortTransfer(context, timeout_ms)`，上界是 `SDCARD_ABORT_TIMEOUT_MS`（100 ms，传给 Port）。Port 契约：返回 `SDCARD_PORT_OK` 表示传输已停止、卡回到可传输状态且 Port 仍可使用；返回其他任何值之前 Port 必须已经自行反初始化。Device 按结果转换状态：

| Port 返回 | Device 状态 | 函数返回 | 诊断快照（`ErrorCode` / `LastPortStatus`） | 恢复 |
|---|---|---|---|---|
| `OK`（停住） | `READY` | `SDCARD_OK` | `TRANSFER_ABORTED` / `TIMEOUT` | 无需恢复，可以立即再启动下一笔，不重新初始化 |
| `NOT_PRESENT` | `NOT_PRESENT`，缓存的介质信息失效，`IsPortInitialized` 清零 | `SDCARD_ERROR` | `NOT_PRESENT` / `NOT_PRESENT` | 重新插卡（`Refresh` 不会再对已反初始化的 Port 反初始化） |
| `TIMEOUT`（上界内没停住）或其他失败 | `ERROR`，介质信息失效，`IsPortInitialized` 清零 | `SDCARD_ERROR` | `PORT_ABORT` / Port 的状态 | 保持 `ERROR`：`Refresh` 不自动重试，新的 `Start*` 被拒；显式 `SDCard_Init()` 或拔卡后重新插卡才恢复 |
| 不在 `BUSY` | 不变 | `SDCARD_ERROR` | `NOT_READY` | Port 不被调用 |
| Port 没有安装 `AbortTransfer` | `ERROR` | `SDCARD_ERROR` | `PORT_NOT_BOUND` | 与其他可选 Port 操作缺失时一致 |

卡不在时本函数与 `SDCard_FailTransfer()` 一样先于“比较前后状态”把 Device 写成 `NOT_PRESENT`，所以 `Platform_SD_Process()` 之后看到的前后状态相同，拔出事件会被吞掉——这是 ADR-0024 背景里记的同一个已知问题，由卷任务票（对账而不是靠状态变化事件）解决，不在本票范围。

## 约束

- Start 接口返回成功不代表 DMA 完成；同一 Device 同时只允许一笔 `BUSY` 传输；`BUSY` 的传输要么等到完成/失败事件、要么被 `SDCard_AbortTransfer()` 收尾，调用者不得把等待超时的 Device 留在 `BUSY` 或只把它标成 ERROR 而放着硬件不管；
- `CompleteTransfer()` / `FailTransfer()` 不能从 ISR 调用；ISR 只应通知上层任务；
- 轮询与 DMA 路径都保留：前者便于启动诊断和低复杂度场景，后者由调用方负责等待完成事件与 Cache 一致性；
- 不拥有卡检测引脚、HAL Handle、DMA 缓冲区或文件系统挂载状态。

跨 Module Interface 使用 `SDCard_*`，状态机与参数检查辅助函数使用 `sdcard_*`。
