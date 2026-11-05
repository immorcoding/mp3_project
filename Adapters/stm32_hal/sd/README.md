# STM32 SD Adapter

本 Adapter 将 STM32 HAL SDMMC、卡检测 GPIO 和 HAL 状态转换为 SD Card Component 的 Port Ops。

## 公开 Interface

- `SDCard_STM32HALAdapter_Bind()`（同时安装 Port 操作表，其中含 `AbortTransfer`，见“显式中止”）；
- `SDCard_STM32HALAdapterTypeDef`，其中保存注入的 SD Handle、卡检测 GPIO，以及当前 DMA 的 Cache 维护元数据。

## 编译期依赖

- `SDCard_*` Port Ops 和状态类型；
- `HAL_SD_*`、`HAL_GPIO_ReadPin()`；显式中止另用 LL 的 `SDMMC_SendCommand()`、`SDMMC_GetCommandResponse()`、`SDMMC_GetResponse()` 与 `__SDMMC_*` 宏、`NVIC_ClearPendingIRQ()`，直接读写 `Instance` 的 `MASK`、`STA`、`ICR`、`CMD`、`DCTRL`、`IDMACTRL`；
- `Adapters/cortex/cache` 的 D-Cache 一致性维护 Interface。

## 运行时请求与事件路径

SD Card Device 经已绑定 PortOps 发起块访问时，本 Adapter 调用 HAL SDMMC；DMA 完成由独立 SDMMC IRQ Adapter 发布，随后由 Service 在任务上下文调用 `Sync()`，本 Adapter 不拥有通知等待。

## 约束

- Context 由 Platform 注入 SD Handle、检测 GPIO 与有效电平；
- 不固定使用 `hsd1`，不注册 EXTI，也不执行 FatFs 操作；
- `ReadBlocks()` / `WriteBlocks()` 保留阻塞轮询路径；`StartReadBlocks()` / `StartWriteBlocks()` 只启动 DMA，`Sync()` 在任务上下文确认传输结果并完成读取方向的 Cache 维护；
- 不拥有 DMA 等待、任务通知、超时策略或中转缓冲区；这些属于 Filesystem Service。
- `sd_stm32_hal_adapter_config.h` 只保存 HAL DMA 字节数换算所用的固定逻辑块大小；它是本 Adapter 私有实现参数，不是 Component 的块设备契约。

## 显式中止（`AbortTransfer`，#209，ADR-0024 第 7 条）

Service 等 DMA 完成超时后，要让迟到的 DMA 不可能再被下一次传输误当作完成，也不会有旧 DMA 覆盖 bounce buffer。**不调用 `HAL_SD_Abort()`**：它的数据超时常量 `SDMMC_SWDATATIMEOUT` 是 `0xFFFFFFFF` ms，三处等待都没有可预期的上界，而且超时分支会把 `State` 写回 READY 却不关 IDMA。本 Adapter 自己写序列，全部等待共用一个从调用起算的预算 `timeout_ms`（Component 传 `SDCARD_ABORT_TIMEOUT_MS`，100 ms；`HAL_GetTick()` 精度 1 ms，实际上界在 99–100 ms 之间；预算为 0 时立刻判定没停住）。

契约（Port 操作 `AbortTransfer(context, timeout_ms)`）：返回 `SDCARD_PORT_OK` 时传输已停住，Port 仍可继续使用；**返回其他任何值之前，本 Adapter 已经 `HAL_SD_DeInit()`**（`NOT_PRESENT`：开始时或结束时卡已不在；`TIMEOUT`：预算内没停住；`ERROR`：Context 无效，或停住之后作废 D-Cache 失败），Device 据此把 Port 当作未初始化。

### 序列（`sd_stm32_hal_abort_transfer`、`sd_stm32_hal_quiesce_data_path`）

1. `MASK = 0`：外设不再为这笔传输请求中断。`HAL_SD_IRQHandler()` 读的是原始 `STA` 而不是被屏蔽后的状态，所以此前已经挂起的中断要靠最后的 `NVIC_ClearPendingIRQ()`；单核上任务不会和 ISR 同时运行，ISR 要么在这一步之前整个跑完，要么永远不会为这笔传输再跑。
2. 卡不在：不发任何命令（CMD12 无人响应，DPSM 也等不到数据，只会白烧预算），直接走“没停住”分支，结果 `NOT_PRESENT`。
3. `CMD.CMDTRANS` 清零。DPSM 仍活动或 IDMA 仍使能（传输没有自己结束）时：发 CMD12，`CMDSTOP` 置位、`CMDTRANS` 清零，等命令通路结束（`STA` 里 `CCRCFAIL | CMDREND | CTIMEOUT | BUSYD0END` 之一置位且 `CPSMACT` 为 0）。响应超时与 CRC 错误**不算失败**（传输可能刚好自己结束，卡不再响应 CMD12，HAL 同样容忍响应超时），是否停住由后面的检查判断。`SDMMC_GetCmdResp1()` 的有上界版本是自己写的：HAL 的循环次数是 `Timeout * (SystemCoreClock / 8 / 1000)`，STOP_TRANSMISSION 的 `SDMMC_STOPTRANSFERTIMEOUT` 是 `100000000`，480 MHz 下这个乘法在 32 位里回绕，结果约 4.23e9 次循环（约 70.4 s），不能当作可预期的上界。
4. 等 `STA.DPSMACT` 回到 0（**DPSM** 停止）。IDMA 使能位只能在 DPSM 静止之后写——`HAL_SD_Abort()` 里的注释说得明白：不等标志，后面的 `IDMACTRL` 清零不会生效，下一笔读写会失败——所以这一步必须先于下一步。
5. `IDMACTRL = 0`（**IDMA** 停止）并回读确认 `IDMAEN` 为 0，仍为 1 算没停住；`DCTRL |= FIFORST` 复位 FIFO（尽力而为：HAL 的错误分支同样这么做，但参考手册里它“只在传输错误或 hold 时生效”的说法没有在仓库里核对过）；清全部静态标志（`SDMMC_STATIC_FLAGS`，含 `DABORT`、`DATAEND`、`BUSYD0END`、`IDMATE`、`IDMABTC`）。
6. 轮询 CMD13（`RelCardAdd << 16`）直到卡状态为 TRANSFER（**CPSM** 每条命令用完即回空闲；写方向 CMD12 的 R1b 忙由这一步等掉）。每条命令的等待同样受预算约束，命令通路卡住（`CPSMACT` 不退）算没停住。
7. 停住了：`ErrorCode = NONE`、`State = READY`、`Context = NONE`（与 `HAL_SD_Abort()` 成功路径一致）；读方向且 `DMAReadPending` 时对 DMA 缓冲区 `CortexM7DCache_Invalidate_Aligned()`；清 Cache 维护元数据；清 NVIC 挂起位。没停住：清元数据、`HAL_SD_DeInit()`、清 NVIC 挂起位。

### D-Cache 顺序

读方向的 Cache 维护顺序是：启动前 Clean+Invalidate → DMA 写 RAM → 完成后 Invalidate → CPU 读。中止后新增一步“确认 IDMA 已关（上面第 5 步回读）→ Invalidate → 恢复 State”，这个顺序不能反：先 Invalidate 再关 IDMA，迟到的 DMA 写入会落在刚作废过的行之后，CPU 随后读到旧行或者半新半旧的行。写方向不需要作废（启动前已 Clean，CPU 在 DMA 期间不改 bounce buffer；真正的危险是旧写 DMA 还在读 bounce buffer 时 CPU 又 `memcpy` 新数据进去，旧 DMA 会把错数据写到卡的旧地址上——中止先于 Service 的下一次 `memcpy` 就关掉了这条路径）。没停住的分支不作废：硬件状态未定，缓冲区的一致性由下一笔传输开始前的 Clean+Invalidate 重新建立，而下一笔传输要等卡重新初始化才可能发生。

### HAL 句柄与升级复核（读 `stm32h7xx_hal_sd.c` / `stm32h7xx_ll_sdmmc.c` 的结论，不是实测）

- **在飞时**：`HAL_SD_ReadBlocks_DMA/WriteBlocks_DMA` 把 `State` 置 BUSY、`Context` 置 `READ/WRITE_{SINGLE,MULTIPLE}_BLOCK | DMA`，`DCTRL` 清零后 `SDMMC_ConfigData`（数据超时 `DTIMER = 0xFFFFFFFF` 个 SDMMC_CK 周期，SDMMC 核时钟 48 MHz（PLL1Q）、`ClockDiv = 0` 时约 89.5 s，远大于 Service 的 500 ms），`CMDTRANS` 置位，`IDMABASE0`/`IDMACTRL` 写入，发 CMD17/18/24/25，最后开 `DCRCFAIL | DTIMEOUT | RXOVERR（写为 TXUNDERR） | DATAEND` 中断。
- **正常完成**（`HAL_SD_IRQHandler`，`DATAEND` 且 `Context & DMA`）：`DLEN`、`DCTRL`、`IDMACTRL` 清零，多块的再发 CMD12（在 ISR 里阻塞轮询），`State = READY`、`Context = NONE`，再回调。
- **错误分支**（`DCRCFAIL/DTIMEOUT/RXOVERR/TXUNDERR`）：清标志、关中断、`CMDTRANS` 清零、`DCTRL |= FIFORST`、`CMD |= CMDSTOP`、`SDMMC_CmdStopTransfer`（ISR 里阻塞）、`IDMACTRL = 0`，`State = READY`，**`Context` 不清**（下一次 Start 会覆盖），再 `ErrorCallback`。
- **拔卡时**：HAL 不看卡检测脚，拔卡本身不触发 HAL 的任何路径。数据线浮空：读到垃圾则走上面的错误分支（IRQ 适配层发 ERROR 事件，Service 以 ERROR 事件收尾，Device 看到卡不在进 NOT_PRESENT）；读不到起始位则要等到上面的数据超时（约 89.5 s），所以 Service 的 500 ms 先到——旧做法是 `SDCard_FailTransfer` 把 Device 置 NOT_PRESENT，HAL 的 `State` 仍是 BUSY、IDMA/DPSM 仍使能，直到 `Refresh` 的 `HAL_SD_DeInit()`；新做法是第 2 步直接 DeInit。`HAL_SD_DeInit()` 只做 `State = BUSY`、`POWER.PWRCTRL = 0`、`MspDeInit`（CubeMX：关 SDMMC1 的 RCC 时钟、`HAL_GPIO_DeInit`、关 `SDMMC1_IRQn`）、`ErrorCode = NONE`、`State = RESET`；**不复位 SDMMC1 的寄存器**，也不动 `Context`。
- **DeInit 之后再 Init**：`HAL_SD_Init()` 在 `State == RESET` 时才调 `MspInit`（开时钟、GPIO、NVIC），并把 HAL 回调指针复位成弱默认函数——所以 Platform 必须在 Init 成功后重新 `STM32SDMMCIRQ_Register`（`HAL_SD_RegisterCallback` 还要求 `State == READY`），`Platform_SD_AbortTransfer` 在状态不是 READY 时注销 IRQ 节点正是为了这个；`HAL_SD_InitCard` 里 `SDMMC_Init` 重写 `CLKCR`、`PowerState_ON`、走 CMD0 起的初始化；`HAL_SD_Init` 结尾 `ErrorCode = NONE`、`Context = NONE`、`State = READY`。IDMA/DPSM 真的卡死时，断电加关时钟能不能把它们复位**没有核实**（仓库里没有参考手册）：DeInit 不做 RCC 复位，是否要加 `__HAL_RCC_SDMMC1_FORCE_RESET()` 脉冲留给上板观察（这条分支在健康卡上构造不出来，主机测试只覆盖到 Component 的状态转换）。
- **HAL 升级时复核**：`HAL_SD_Abort()`、`HAL_SD_IRQHandler()`、两个 `*_DMA()` 启动函数里 `State`、`Context`、`ErrorCode` 的写法和中断掩码；`SDMMC_CmdStopTransfer()` 的 CMDSTOP/CMDTRANS 次序（本 Adapter 照它排）；`SdCard.RelCardAdd` 仍是 CMD13 的参数来源；`SDMMC_STATIC_FLAGS`、`SDMMC_FLAG_*`、`SDMMC_IDMA_IDMAEN`、`SDMMC_DCTRL_FIFORST` 的定义；`HAL_SD_RegisterCallback` 的 `State` 条件。这些是本 Adapter 越过 HAL 公开函数直接依赖的事实。
