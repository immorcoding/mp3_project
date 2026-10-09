# STM32H7 I2S DMA 暂停（Pause）、恢复（Resume）与停止（Stop）行为调研

- 票：[#47 STM32H7 HAL I2S DMA 的 Pause/Resume/Stop 行为与勘误](https://github.com/immorcoding/mp3_project/issues/47)（`wayfinder:research`）
- 日期：2026-10-08
- 对象：本仓库 vendored HAL（版本 1.11.5，见 §3）；I2S2 = SPI2，主发送，接 PCM5102A（见 §2）
- 用途：供票「DMA 完成事实、欠载与 Stop 失败语义」使用
- 证据标记：
  - 【确认】本仓库源码，或与其逐字一致的 ST 上游源码，可直接读到的行为（给出文件与行号）
  - 【佐证】第三方驱动源码或提交说明（Zephyr、Linux），与 ST 代码互证，不是 ST 手册原文
  - 【二手】社区帖、工单或搜索摘要，只作线索，不作依据
  - 【推断】由以上证据推导，未经手册原文或上板验证
  - 【未核对】需要 RM0433 或 ES0392 原文，本次无法访问

## 0. 结论摘要

1. **Stop 不等待播放完成。** `HAL_I2S_DMAStop`（`stm32h7xx_hal_i2s.c` 1795–1836）不读 TXC、EOT，不等待 FIFO 排空，只清 DMA 请求、Abort 两个 DMA 流、清 SPE 并置 READY。`TxCplt` 只说明 DMA 已把最后一个采样写入 TXDR（进入 FIFO），不说明已经输出。I2S HAL 全文不读取 TXC、EOT、SUSP 标志，唯一的 SUSP 相关操作是 1713 行置 CSUSP。
2. **Stop 的超时只存在于 DMA 层。** 每个 DMA 流的 `HAL_DMA_Abort` 最多等 5 ms（`dma.c` 133、835–851）。超时后流可能仍使能，DMA 状态为 ERROR；此时 `HAL_I2S_DMAStop` 仍清 SPE、把 I2S 置 READY，并返回 HAL_ERROR。之后 `HAL_DMA_Start_IT` 因 DMA 非 READY 拒绝启动，需要重新初始化 DMA。
3. **Pause 超时很长，且状态不闭合。** `HAL_I2S_DMAPause` 轮询 CSTART 清零，超时阈值 `I2S_TIMEOUT = 0xFFFF` ms，约 65.5 s，期间忙等（1715–1729，阈值见 198 行）。超时后 CSUSP 未撤销、SPE 仍开、DMA 仍 BUSY，而 I2S 状态已是 READY。成功路径同样把 I2S 置 READY，DMA 仍 BUSY。Pause 不清 SUSP 标志；ST 的 SPI HAL 在挂起后会清除（`stm32h7xx_hal_spi.c` 2611）。
4. **Pause 期间时钟停止，DAC 进入保护状态。** Pause 清 SPE（1732）。HAL 注释称保持 I2S 使能是为避免主从时钟失步（1411–1412），由此推断挂起期间 BCK、WS 停止【推断】。PCM5102A 在时钟停止时进入 standby，BCK 与 LRCK 低电平超过 1 s 进入 power-down；恢复时在 16 个 LRCK 周期后才启动内部 PLL（TI 数据手册 §11.5.2、§9.3.5.3）。
5. **循环模式回调与错误上报。** 半区（HT）触发 `TxHalfCplt`，全区（TC）触发 `TxCplt`，每圈各一次，状态与 TXDMAEN 保持不变（2249–2288）。DMA 的 TE、DME 错误经 `I2S_DMAError` 上报为 `HAL_I2S_ERROR_DMA`，具体原因要读 `hdmatx->ErrorCode`。主机发送的欠载在 DMA 模式下不会上报：DMA 启动函数不使能 UDR 中断（1415–1485），且 CMSIS 将 UDR 注释为「Slave transmission」（`stm32h743xx.h` 18455–18457）。【推断：主机模式下 UDR 不置位，需 RM0433 确认】
6. **Stop 之后的回调。** v1.11.5 的 `HAL_DMA_Abort` 先屏蔽 TC、HT、TE、DME、FE 中断再停流（`stm32h7xx_hal_dma.c` 812–832），成功后 DMA 中断不再派发回调。I2S HAL 的注释却称 Abort 会触发 TC 回调（1798–1802），两者不一致。v1.11.6 改为先停流后屏蔽中断，窗口方向相反（§8）。两个版本的硬件行为均未上板验证。
7. **勘误。** ES0392 原文与 RM0433 无法访问（st.com 连接超时）。可用的线索均为二手或佐证：(a) SPI/I2S「单工发送后启用 Rx DMA 产生虚假读请求」，本项目纯 TX，推断不触发；(b) SPE 关闭时 TXP 相关问题，规避是先关 TXP、EOT 中断（Zephyr 注释引用 ES0392），HAL 的 Pause、Stop 未做此步；(c) 高系统时钟下使能 SPI 后立即启动可能停滞，Zephyr 加 1 µs 等待（引用 ES0392），HAL 的 Transmit_DMA 与 DMAResume 均未等待。
8. **运行中改采样率需要完整重配。** `HAL_I2S_Init` 在调用时一次性读取 SPI123 时钟计算分频（378、382–400），不跟随后续 PLL 变化，也不停 DMA。流程是静音、停 DMA、必要时改 PLL3、重新 Init、重新 `Transmit_DMA`，不能用 Resume。按推算，SPI123 = PLL3P ≈ 170 MHz，48 kHz 的整数分频实际约 47.86 kHz（−0.29%），44.1 kHz 约 44.27 kHz（+0.39%）【推断，需实测】。SPI1（LCD）与 I2S2 共用 SPI123 时钟。
9. **版本。** vendored HAL 1.11.5 的 I2S 与 DMA 源文件与上游 v1.11.5 逐字一致。v1.11.6 的 I2S 文件无改动，DMA 文件有 Abort 顺序与双缓冲 CT 判定两处改动。升级前必须重新评估 Stop 相关行为。

## 1. 范围与来源

- 【确认】本仓库：`Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_i2s.c`（2637 行）、`Src/stm32h7xx_hal_dma.c`（2062 行）、`Src/stm32h7xx_hal_spi.c`、`Inc/stm32h7xx_hal_i2s.h`、`Inc/stm32h7xx_hal_spi.h`、`Inc/stm32h7xx_hal_dma.h`、`Src/stm32h7xx_hal.c`（版本宏），以及 CMSIS `Drivers/CMSIS/Device/ST/STM32H7xx/Include/stm32h743xx.h`。
- 【确认】ST 上游：GitHub `STMicroelectronics/stm32h7xx-hal-driver` 的标签 v1.11.5、v1.11.6（`Src/stm32h7xx_hal_i2s.c`、`Src/stm32h7xx_hal_dma.c`、`Release_Notes.html`）。比对方式为 `diff --strip-trailing-cr`。
- 【佐证】Zephyr：`drivers/spi/spi_stm32.h`（提交 80c360e2）、`drivers/spi/spi_stm32.c`（提交 ef37420f）。Linux：提交 dc6620c3「spi: stm32h7: don't wait for EOT and flush fifo on disable」（作者 Alain Volmat），以及 `drivers/spi/spi-stm32.c`（2026-10-08 抓取）。
- 【二手】ST 社区 2019 年 1 月帖，引用勘误 2.10.1 标题与 workaround；ChibiOS 工单 1268，引用 Rev 9 的 2.14.1；WebSearch 摘要称 Rev 15 含 SPI/I2S 条目（未核对）。
- 【确认】TI PCM5102A 数据手册 SLAS859C（May 2012，revised May 2015），`https://www.ti.com/lit/ds/symlink/pcm5102a.pdf`，本地用 pdftotext 提取后核对章节。
- 【未核对】ST RM0433（SPI/I2S、DMA 章节）与 ST ES0392（勘误表）：`st.com` 与 `st.com.cn` 从本环境连接超时（curl 与 WebFetch 均失败），无法读取原文。

## 2. 当前固件现状

- I2S2 配置（`Core/Src/i2s.c` 40–50）：SPI2，`I2S_MODE_MASTER_TX`，Philips，16 位数据（`DATAFORMAT_16B`），MCLK 输出关闭，48 kHz，CPOL low，`MasterKeepIOState = DISABLE`（即 CFG2.AFCNTR = 0）。
- 当前没有 DMA：`Core/Src/i2s.c` 的 `HAL_I2S_MspInit` 只配置时钟与 GPIO，不绑定 `hdmatx`，不配置 NVIC。
- 当前播放路径是阻塞发送：`Adapters/stm32_hal/audio_i2s/audio_i2s_stm32_hal_adapter.c` 94–97 调用 `HAL_I2S_Transmit(…, 1000U)`。`Platform/audio/README.md` 第 18 行与 `Adapters/stm32_hal/audio_i2s/README.md` 第 17 行都写明尚无 DMA 或 IRQ 完成路径。
- 因此，本文关于 Pause、Resume、Stop 的结论针对未来的 DMA 设计，当前代码不调用这些接口。
- 时钟（【确认】配置，【推断】数值）：
  - HSE 25 MHz（`io_sheet.ioc` 596 行，`RCC.HSE_VALUE=25000000`）。
  - SYSCLK 取 PLL1P，M=5、N=192、P=2，推算约 480 MHz（`Core/Src/main.c` 168–171、187）。
  - SPI123 内核时钟选 PLL3（`main.c` 230）；PLL3 M=5、N=102、P=3（221–224），推算 PLL3P ≈ 170 MHz。
  - SPI1（LCD 通道，`Platform/lcd/platform_lcd.c` 与 `Adapters/stm32_hal/st7789_spi/` 使用；`Core/Src/spi.c` 100–115 配置其 DMA1_Stream0，NORMAL 模式）与 I2S2 共用 SPI123 时钟。

## 3. HAL 版本核对

- 版本宏：`Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal.c` 52–59，MAIN=0x01、SUB1=0x0B、SUB2=0x05、RC=0x00，即 **1.11.5**。
- 与上游标签 v1.11.5 比对：`Src/stm32h7xx_hal_i2s.c` 与 `Src/stm32h7xx_hal_dma.c` 忽略行尾 CR 后无任何差异。
- 上游 v1.11.6 的 `stm32h7xx_hal_i2s.c` 与 v1.11.5 无差异；`stm32h7xx_hal_dma.c` 的差异见 §8。
- 发布说明：v1.11.5（2024-12-06）的 I2S 相关项是 IOSwap 接口（实际属于 v1.11.4，2024-10-30）；v1.11.5 与 v1.11.6 的发布说明均无 I2S DMA 暂停、恢复或停止的修改。

## 4. 逐条回答

### 4.1 Pause、Resume、Stop 的执行步骤与返回条件

以下行号指 vendored 1.11.5 的文件，与上游 v1.11.5 逐字一致。

| 接口 | 执行步骤（按顺序） | 返回条件 | 证据 |
|---|---|---|---|
| `HAL_I2S_DMAPause` | ① `__HAL_LOCK`；② 非主模式则 NOT_SUPPORTED，置 READY，返回 ERROR；③ CR1.CSTART 为 0 则 NO_OGT，置 READY，返回 ERROR；④ 置 CR1.CSUSP；⑤ 轮询 CSTART 直至清零，超过 `I2S_TIMEOUT`（0xFFFF ms）则 TIMEOUT，置 READY，返回 TIMEOUT；⑥ 清 SPE，置 READY，解锁，返回 OK | OK：CSTART 已清零且 SPE 已清。ERROR：非主模式或未在传输。TIMEOUT：约 65.5 s 内 CSTART 未清零 | 1688–1752；阈值 198 |
| `HAL_I2S_DMAResume` | ① 若 `State != READY`，置 READY 并返回 ERROR；② 置 BUSY，清 ErrorCode；③ 置 SPE；④ 置 CSTART；⑤ 返回 OK | OK：只检查 I2S 状态为 READY，不检查 DMA 状态、TXDMAEN、CSUSP 或 SUSP | 1760–1787 |
| `HAL_I2S_DMAStop` | ① 清 CFG1.TXDMAEN 与 RXDMAEN；② 若 `hdmatx != NULL`，调用 `HAL_DMA_Abort`，失败则置 ErrorCode.DMA 并记 ERROR；③ `hdmarx` 同理；④ 清 SPE；⑤ 置 READY；⑥ 返回 errorcode | OK：两个 Abort 都成功。ERROR：任一 Abort 失败，包括 DMA 非 BUSY 时的直接失败 | 1795–1836；注释 1798–1802 |
| `HAL_DMA_Abort`（被 Stop 调用） | ① `State != BUSY` 时，ErrorCode 赋值为 NO_XFER，返回 ERROR，不动流与中断；② 屏蔽 CR 的 TC、TE、DME、HT 与 FCR 的 FE；③ 屏蔽 DMAMUX 溢出中断；④ 清 EN，轮询 EN 为 0，超过 5 ms 则 ErrorCode 赋值为 TIMEOUT，State 置 ERROR，返回 ERROR；⑤ 清标志，State 置 READY，返回 OK | 见上 | dma.c 781–889；阈值 133 |

补充要点（均为【确认】代码路径）：

- Pause 的 NO_OGT 分支（1707）与超时分支（1720、1726）把 I2S 置 READY，但不修改 DMA 状态。
- Resume 的错误分支把 State 强制置为 READY（1767）。若 DMA 仍在运行时误调，会造成状态不一致。
- Stop 之后误调 Resume，会在 DMA 已停、TXDMAEN 已清的情况下置位 SPE 与 CSTART（1765–1781 无检查）。【推断：I2S 空转输出时钟】
- `HAL_DMA_Abort` 对 `hdma->ErrorCode` 做的是赋值而非按位或，会覆盖此前的 TE 等错误原因（dma.c 799、841）。要读取 DMA 错误原因，必须在 Stop 之前完成。

### 4.2 Stop 是否等待 FIFO、TXC，是否超时，超时后各部分的状态

- 等待：不等待。`stm32h7xx_hal_i2s.c` 中没有读取 TXC、EOT、SUSP 标志的代码（唯一 SUSP 相关为 1713 行置 CSUSP）。对照 ST 的 SPI HAL，其发送完成路径会等待 EOT（`stm32h7xx_hal_spi.c` 1016、1282、1614）。
- 超时：Stop 本身没有超时参数，耗时只来自各 DMA 流 Abort 的 5 ms 轮询（dma.c 133、838）。
- 各种情形的状态（代码路径为【确认】，硬件后果为【推断】）：

| 情形 | DMA 流 | DMA 状态与错误码 | SPI/I2S 外设 | I2S State 与 ErrorCode | 返回 |
|---|---|---|---|---|---|
| 正常 | EN=0，中断全部屏蔽，标志清零 | READY | DMA 请求清零，SPE=0 | READY，ErrorCode 不变 | OK |
| DMA 非 BUSY（例如 TE 之后已 READY） | 未被改动 | READY，ErrorCode 被覆盖为 NO_XFER | SPE=0 | READY，ErrorCode 置 DMA | ERROR，尽管流实际已停 |
| Abort 超时 | EN 可能仍为 1，中断已屏蔽 | ERROR，ErrorCode 为 TIMEOUT | SPE=0 | READY，ErrorCode 置 DMA | ERROR |

- 无论哪种情形，`HAL_I2S_DMAStop` 都不写 CSTART，其清零行为未核对（RM0433）。

### 4.3 Stop 之后是否仍可能触发 TxHalfCplt、TxCplt、Error 回调

- 【确认】v1.11.5：`HAL_DMA_Abort` 在停流之前屏蔽 CR 的 TC、TE、DME、HT 与 FCR 的 FE（dma.c 812–813）。DMA 中断处理函数在派发每类回调前都检查对应的中断使能（dma.c 1227、1242、1254、1266、1313）。因此 Abort 成功后，DMA 中断不再派发 HT、TC、TE、DME 回调。
- 【确认】`XferAbortCallback` 在仓库内（Core、Platform、Adapters、APP、Components、Service、Middlewares）与 HAL I2S 源文件中都没有赋值，所以 dma.c 1318–1341 的 Abort 回调路径对 I2S 不生效。
- 【推断】残留窗口：
  - Stop 调用前已进入或挂起的 DMA 中断，可能在 Stop 执行过程中派发回调（ISR 抢占）。
  - `HAL_DMA_Abort` 在 `State != BUSY` 时直接返回，不屏蔽中断也不停流（dma.c 797–805）。若此时流仍使能且中断仍开，回调仍可能派发。NORMAL 模式结束后流已自动关闭，风险较低；循环模式下 DMA 状态始终为 BUSY，不走这条分支。
- 【确认】I2S 注释（1798–1802）写道「when calling HAL_DMA_Abort() ... the DMA TX or RX Transfer complete interrupt is generated and the correspond call back is executed」，即声称 Abort 会产生 TC 中断并执行 `TxCplt`。这与 v1.11.5 的屏蔽顺序不符。v1.11.6 改为先停流（§8），与该注释的描述更接近【推断】。
- 【确认】`HAL_I2S_DMAStop` 不加锁（1798–1802 注释），设计上允许在 `TxCplt` 或 `ErrorCallback` 中调用，应用层需防止重入。

### 4.4 循环模式下半区、全区回调与错误上报

- 配置：`HAL_I2S_Transmit_DMA` 设置 `XferHalfCpltCallback`、`XferCpltCallback`、`XferErrorCallback`（1445–1451）。DMA 启动时只在存在半区回调时才使能 HT（dma.c 714–720）。
- 半区（HT）：`HAL_DMA_IRQHandler` 的 HT 分支（非双缓冲）调用 `I2S_DMATxHalfCplt`，再调用用户 `HAL_I2S_TxHalfCpltCallback`（dma.c 1264–1306；i2s.c 2277–2288）。循环模式下 HT 中断不被关闭（dma.c 1296–1300）。
- 全区（TC）：非双缓冲、循环模式下调用 `I2S_DMATxCplt`（dma.c 1367–1385；i2s.c 2249–2269）。只有 NORMAL 模式才清 TXDMAEN 并置 READY（2255–2262）。循环模式下这两项保持不变，只调用 `TxCpltCallback`。
- 错误（TE、DME）：TE 时 DMA 层先停流并等待 EN 清零（dma.c 1397–1418），再调用 `I2S_DMAError`（i2s.c 2393–2413）。该函数清 TXDMAEN 与 RXDMAEN，置 READY，I2S ErrorCode 置 DMA，调用 ErrorCallback。SPE 不被清除（【确认】），I2S 外设仍在运行时钟（【推断】）。DME 走同一条回调路径，但 DMA 层不停流（dma.c 1252–1262）。
- FE：DMA 启动不使能 FE 中断（dma.c 714 只开 TC、TE、DME、HT，FCR 的 FE 未设），因此 HAL 不会因 FE 调用错误回调【确认】。FE 只有在该中断被手动开启时才会被识别【推断】。
- 粘滞错误码：DMA 中断末尾（dma.c 1390–1429）只要 `hdma->ErrorCode != NONE` 就调用 `XferErrorCallback`，而 ErrorCode 只在 `HAL_DMA_Start_IT` 中清零（dma.c 703）。FE 与 DME 错误不停流，因此此后每一次 DMA 中断（包括 HT、TC）都会再次进入 `I2S_DMAError`【推断：代码路径可读，未经测试】。
- UDR 与 OVR：DMA 启动不使能 UDR 中断（1415–1485 无 `__HAL_I2S_ENABLE_IT`；该宏只出现在 IT 收发函数 1232、1307、1385）。因此 DMA 模式下 `HAL_I2S_IRQHandler` 的 UDR 分支（1992–2011）不会运行。若应用手动开启 UDR，该分支只屏蔽 TXP 与 ERR 中断、置 READY、报错，不关 TXDMAEN 与 SPE【确认】，从而造成与 DMA 状态不一致【推断】。
- 错误区分方法：ErrorCallback 中用 `HAL_I2S_GetError()` 判断位。0x08（DMA）时再读 `hdmatx->ErrorCode`：TE=0x01、FE=0x02、DME=0x04、TIMEOUT=0x20、NO_XFER=0x80（`stm32h7xx_hal_dma.h` 197–204）。0x04（UDR）只来自 IT 路径；0x02（OVR）仅在接收方向出现（`stm32h7xx_hal_i2s.h` 186–194）。
- 主机欠载：CMSIS 注释 UDR 为「UDR at Slave transmission」（`stm32h743xx.h` 18455–18457）。因此在主机发送模式下，UDR 不是欠载指示【推断】。阻塞发送中的 UDR 检查（`stm32h7xx_hal_i2s.c` 896–903）同样可能不会命中【推断】。可靠的欠载判据需要 RM0433 确认，或依靠应用层缓冲水位。

### 4.5 Pause 后 Resume 是否从原位继续；时钟与静音

- DMA 位置：Pause 不改 DMA 流与 NDTR，也不清 TXDMAEN（1688–1752 未写 CFG1）。理想情况下 DMA 指针保持原位【推断】。
- 挂起期间的 DMA：TXDMAEN 仍然置位。若 SPE 为 0 时 TXP 请求仍能产生，DMA 会在挂起期间继续搬运数据，DMA 指针就不再是原位【推断，未核对】。Zephyr 对 ES0392 的注释（SPE 关闭时 TXP 相关问题）提示此类行为存在【佐证，未核对条目】。
- FIFO 内容：Linux 提交 dc6620c3 的说明写道，SPE 清零时「clear of all internal FIFO」（作者 Alain Volmat，ST 工程师）。Linux 驱动 `stm32h7_spi_disable` 的注释写道「RX-Fifo is flushed when SPI controller is disabled」（`spi-stm32.c` 883）【佐证】。这表明 Pause 清 SPE 后，TX FIFO 中尚未发出的样本可能被丢弃。若成立，Resume 之后会出现相当于 FIFO 深度的样本跳变【推断】。TX FIFO 深度及丢弃行为未核对（RM0433）。
- 恢复无延时：`HAL_I2S_DMAResume` 先置 SPE 再置 CSTART，中间没有等待（1778、1781）。Zephyr 在使能 SPI 后等待 1 µs，注释引用 ES0392，称用于防止高系统时钟下传输停滞（`spi_stm32.c` 907–909）【佐证，条目未核对】。本项目 SYSCLK 推算约 480 MHz，属于高频情形【推断】。
- SUSP 标志：Pause 后 SR.SUSP 保持置位，因为 HAL 不写 SUSPC。ST 的 SPI HAL 在挂起完成后清除 SUSP，并等待其清零（`stm32h7xx_hal_spi.c` 2611–2620）。恢复前是否必须清除 SUSP，未核对（RM0433）。
- 挂起完成判据：`HAL_I2S_DMAPause` 以 CSTART 清零作为挂起完成的判据（1715–1729），与 ST SPI HAL 的挂起流程相同（`stm32h7xx_hal_spi.c` 2599–2611）【确认】。挂起完成的硬件时序未核对。
- 时钟：Pause 清 SPE（1732）之后，I2S 主时钟输出停止【推断】。HAL 注释称保持使能是为避免主从时钟失步（1411–1412）。
- 引脚：本项目 `MasterKeepIOState = DISABLE`（`Core/Src/i2s.c` 50），即 CFG2.AFCNTR = 0。CMSIS 将其定义为「Alternate function GPIOs control」（`stm32h743xx.h` 18400–18402）。SPE 清零后 SCK、WS 的电平取决于 AF 释放后的 GPIO 状态【推断，未核对】。
- 静音：PCM5102A 的 XSMT 为软静音，拉低后按 104 个采样衰减（TI §9.3.3）。计划暂停前先拉低 XSMT，等待衰减完成后再 Pause，可以减轻硬断时钟的影响【推断，未上板】。本项目 Adapter 已有 XSMT 静音接口（`audio_i2s_stm32_hal_adapter.c` 107–123）。
- DAC 行为（TI 数据手册）：
  - §9.1：时钟错误或系统掉电时，器件先衰减数据（或保持最后有效值），再对模拟输出静音。
  - §11.2「Clock Error Detect」：检测到时钟错误时切换到内部振荡器，继续驱动输出并衰减，之后硬静音。
  - §11.5.2：SCK、BCK、LRCK 时钟错误或时钟停止（clock halt）时进入 standby，DAC 与线路驱动断电；BCK 与 LRCK 保持低电平超过 1 s 进入 power-down；时钟重新施加后自动上电。
  - §9.3.5.3（无 SCK 的三线模式）：BCK 与 LRCK 正常开始后，若 SCK 保持地电平 16 个 LRCK 周期，内部 PLL 启动并从 BCK 生成内部 SCK；需要 BCK 位于表 11 的 PLL 工作点。
- 结论：Pause、Resume 不是无缝的。时钟停止会让 DAC 进入保护状态。恢复时间至少是 16 个 LRCK 周期（48 kHz 约 0.33 ms）加上 PLL 锁定时间，后者未量化。

### 4.6 运行中切换采样率

- `HAL_I2S_Init` 的行为（267–460）：
  - 若 SPE 为 1 则清除（325–330），清 I2SCFGR（333）。
  - 主模式且 `AudioFreq != I2S_AUDIOFREQ_DEFAULT` 时：`i2sclk = HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_SPI123)`（373–378，SPI1、SPI2、SPI3 共用）。随后计算 `tmp = (i2sclk / (32 × packetlength)) × 10 / AudioFreq + 5`，`tmp /= 10`，`odd = tmp & 1`，`div = (tmp − odd) / 2`（385–400）。
  - 非法组合（odd==1 且 div==1，或 div>255）返回 PRESCALER 错误（410–415）。
  - 写入 I2SCFGR 的分频与格式位（423–438），清 IFCR（440），设置 CFG2（445–454）。
  - 不修改 CFG1.TXDMAEN，也不停 DMA 流【确认】。
- 推算（依据 HSE 25 MHz 与 PLL3 设置，【推断】）：
  - PLL3 输入 25/5 = 5 MHz；VCO = 5 × 102 = 510 MHz；PLL3P = 510/3 = 170 MHz；SPI123 = PLL3P。
  - 48 kHz：`tmp = ⌊(170e6/32) × 10 / 48000 + 5⌋ = 1111`，故 `tmp = 111`，odd = 1，div = 55。按 HAL 的分频关系，Fs ≈ i2sclk / (32 × (2·div + odd)) = 170e6 / (32 × 111) ≈ 47.86 kHz，偏差 −0.29%。BCK ≈ 1.5315 MHz，名义值 1.536 MHz。
  - 44.1 kHz：`tmp = 120`，odd = 0，div = 60，Fs ≈ 44.27 kHz，偏差 +0.39%。
  - 上述 Fs 与 BCK 由 HAL 的取整算术推得，实际以 RM0433 的 I2S 时钟章节和示波器测量为准。
- PCM5102A 在无 MCLK（MCLKOutput 关闭）时依赖 BCK 驱动内部 PLL（TI §9.3.5.3）。表 11 给出 PLL 工作点的 BCK 频率，48 kHz、32·fS 对应 1.536 MHz。当前分频给出的 BCK 偏差 −0.29%，是否仍在 PLL 容差之内，手册摘录中未给出，未核对。
- 推荐流程（【推断】，基于上述源码）：
  1. 拉低 XSMT 静音，等待软静音完成。
  2. 调用 `HAL_I2S_DMAStop`。若返回 ERROR，检查 `hdmatx->State` 与流的 EN 位；必要时 `HAL_DMA_DeInit` 后重新初始化 DMA。
  3. 如需改变 PLL3，调用 `HAL_RCCEx_PeriphCLKConfig`。注意 SPI1（LCD DMA）共用该时钟，须保证 LCD 无传输进行。
  4. 调用 `HAL_I2S_Init`（新的 AudioFreq），检查返回值；PRESCALER 错误表示该采样率不可实现。
  5. 用 `HAL_I2S_Transmit_DMA` 重新启动，不使用 Resume。
  6. 解除静音。

### 4.7 ES0392 中与 SPI、I2S、DMA 相关的条目

ES0392 原文未能读取（§1）。下表条目全部来自二手或佐证来源，需以 ES0392 最新修订版原文核对。

| 条目 | 来源等级 | 现象与触发条件 | 规避方法 | 对本项目的适用性 |
|---|---|---|---|---|
| 「Spurious DMA Rx transaction after simplex Tx traffic」（SPI 与 I2S） | 二手。ST 社区 2019 帖引用为 2.10.1；ChibiOS 工单引用为 Rev 9 的 2.14.1；WebSearch 摘要称见 Rev 15（未核对） | RXFIFO 为空时，单工发送完成后置 RXDMAEN，会产生虚假 DMA 读请求 | 发送完成后、启用 Rx DMA 之前对 SPI/I2S 做硬件复位 | 纯 TX 不置 RXDMAEN（`HAL_I2S_Transmit_DMA` 1466–1478），推断不触发。若将来启用 I2Sext 或全双工，必须处理 |
| 「TXP interrupt occurring while SPI disabled」（ES0392、ES0445、ES0491、ES0478） | 佐证。Zephyr `spi_stm32.h` 315–322 引用 | SPE 关闭时出现 TXP 中断 | 清 SPE 之前先关闭 TXP 与 EOT 中断，清 SPE 后等待 disabled | HAL 的 Pause、Stop 未做此步（1731–1732、1830–1831）。DMA 模式不开 TXP 中断，默认不触发。IT 发送（1232）会开 TXPIE，需在 Pause、Stop 前关闭 |
| 高系统时钟下使能后传输停滞 | 佐证。Zephyr `spi_stm32.c` 907–909 引用 ES0392，条目正文未核对 | 高 SYSCLK 下，SPI 使能后立即启动可能停滞 | 使能后等待约 1 µs（Zephyr 做法） | 本项目 SYSCLK 推算约 480 MHz。`HAL_I2S_Transmit_DMA`（1477–1481）与 `HAL_I2S_DMAResume`（1778–1781）无此等待，需核对条目后决定是否在应用层加入 |

另有一条不适用项：Zephyr 注释指出 H7 SPI 在 9、17、25 位传输时 TXC 不置位（`spi_stm32.c` 534–536）。本项目为 16 位数据（`Core/Src/i2s.c` 43），不适用。

## 5. 对票「DMA 完成事实、欠载与 Stop 失败语义」的建议

- DMA 完成事实：`TxCplt` 表示 DMA 已把最后一个采样写入 TXDR（进入 FIFO），不表示已播出。若需要「播完」事件，需要 FIFO 排空判据。TXC 与 EOT 的语义需 RM0433 确认，HAL I2S 没有提供。SPI HAL 的做法是等待 EOT（`stm32h7xx_hal_spi.c` 1016）。
- 欠载：HAL 在 DMA 模式下不报 UDR。建议以应用层缓冲水位和 DMA HT、TC 节拍检测欠载。上板时用逻辑分析仪观察缺数据时 BCK、WS、SD 是否仍连续输出。
- Stop 失败语义：
  - `HAL_I2S_DMAStop` 返回 ERROR 不等于「未停止」。可能是流已停而 Abort 因非 BUSY 失败，也可能是流未停而超时。应同时检查 `hdmatx->State` 与流的 EN 位。
  - 超时后 DMA 流可能仍使能，状态为 ERROR，需要 `HAL_DMA_DeInit` 与重新 `Init` 才能再次 `Transmit_DMA`。
  - 无论 Abort 结果如何，Stop 都会清 SPE，即停止 I2S 时钟。
- Pause 超时：Pause 最长阻塞约 65.5 s。应禁止在 ISR 或临界区调用。`HAL_GetTick` 由 TIM17 提供（计数时钟 1 MHz，`stm32h7xx_hal_timebase_tim.c` 71–72；中断优先级为 `TICK_INT_PRIORITY` = 15，`stm32h7xx_hal_conf.h` 168；`main.c` 285 调用 `HAL_IncTick`）。调用上下文优先级高于 TIM17 时，超时不会前进【推断】。
- 不要用 `HAL_I2S_GetState() == READY` 判断「可以发送」。Pause 之后 I2S 为 READY，而 DMA 仍为 BUSY（1707、1734）。当前 Adapter 的 `prepare` 正是用该判据（`audio_i2s_stm32_hal_adapter.c` 63），迁移到 DMA 时需要改。

## 6. 未能确认的点

1. RM0433 原文：I2S 主发送模式下 SUSP、TXC、EOT、UDR 的定义与置位条件；CSUSP 的完成时机；SPE 清零后 CSTART 的状态；清 SPE 对 TX FIFO 的影响（Linux 维护者称 FIFO 被清空，未经手册核对）；AFCNTR 对 SCK、WS 电平的影响；DMA 流禁用时是否产生 TC 或 TE 标志。
2. ES0392 原文：三条 SPI、I2S、DMA 相关条目的正文、适用修订版与状态。
3. 上板验证：Pause、Stop 后的爆音与静音；FIFO 样本丢失量；Resume 后的相位连续性；改采样率后的 PLL 锁定时间与实际采样率；DMA 超时与 ISR 上下文的实际表现。
4. 实际 HSE 与 PLL3 运行频率：25 MHz 与 170 MHz 为推算值，需用 `HAL_RCCEx_GetPeriphCLKFreq` 或示波器实测。
5. PCM5102A 对 BCK 偏差的容差（表 11 的 PLL 工作点及其容差）。
6. v1.11.6 的 DMA Abort 顺序变化对 Stop 回调的实际影响。
7. 论坛中关于 TXC、EOT 与 CSTART 的说法，仅作线索，未采信。

## 7. 证据索引

- 本仓库（vendored HAL 1.11.5）：
  - `Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_i2s.c`：Init 267–460；Transmit_DMA 1415–1485；Pause 1688–1752；Resume 1760–1787；Stop 1795–1836（注释 1798–1802）；IRQ 1945–2078；DMA 回调 2249–2413；I2S_TIMEOUT 198。
  - `Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_dma.c`：Start_IT 681–767；Abort 781–889（超时 133）；IRQHandler 1208–1430。
  - `Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_spi.c`：EOT 等待 1016、1282、1614；挂起与 SUSPC 2599–2620。
  - `Drivers/STM32H7xx_HAL_Driver/Inc/stm32h7xx_hal_i2s.h` 186–194、326–333、365–401；`stm32h7xx_hal_dma.h` 197–204、585–589；`stm32h7xx_hal_spi.h` 632、809。
  - `Drivers/CMSIS/Device/ST/STM32H7xx/Include/stm32h743xx.h` 18245–18518（SPI 位定义及注释）。
  - `Core/Src/i2s.c` 40–50、`Core/Src/spi.c` 100–115、`Core/Src/main.c` 168–171、187、221–230、285、`Core/Inc/stm32h7xx_hal_conf.h` 109、168、196、`Core/Src/stm32h7xx_hal_timebase_tim.c` 51–72、`io_sheet.ioc` 596。
  - `Adapters/stm32_hal/audio_i2s/audio_i2s_stm32_hal_adapter.c` 63、94–97、107–123；`Platform/audio/README.md` 18；`Adapters/stm32_hal/audio_i2s/README.md` 17。
- ST 上游（已比对）：`https://github.com/STMicroelectronics/stm32h7xx-hal-driver`，标签 v1.11.5、v1.11.6；`Release_Notes.html`（v1.11.5、v1.11.6）。
- 第三方（佐证）：
  - Zephyr `drivers/spi/spi_stm32.h`，提交 80c360e2，315–322。
  - Zephyr `drivers/spi/spi_stm32.c`，提交 ef37420f，534–536、907–909。
  - Linux 提交 dc6620c3「spi: stm32h7: don't wait for EOT and flush fifo on disable」，`https://github.com/torvalds/linux/commit/dc6620c31326bc50fa22fd8900a9f995d0a04bc1`。
  - Linux `drivers/spi/spi-stm32.c`（2026-10-08 抓取），883、1400–1402。
- 二手：ST 社区帖 `https://community.st.com/stm32-mcus-products-25/stm32h743ii-spi-dma-malfunction-66441`；ChibiOS 工单 `https://sourceforge.net/p/chibios/bugs/1268/`。
- TI：PCM5102A 数据手册 SLAS859C，§9.1、§9.3.2.1、§9.3.2.3、§9.3.3、§9.3.5.3（表 11）、§11.2、§11.2.1、§11.5.2。
- 未能访问：ST RM0433 与 ES0392（`st.com`、`st.com.cn` 连接超时）。

## 8. HAL 1.11.5 与 1.11.6 的差异

- I2S 文件：两版无差异。
- DMA 文件（`stm32h7xx_hal_dma.c`）两处实质改动：
  1. `HAL_DMA_Abort`：1.11.6 先执行 `__HAL_DMA_DISABLE`（约 805 行），再屏蔽中断（约 811 行起）。1.11.5 的顺序相反（812 行屏蔽中断，832 行停流）。发布说明：「Disable channel before disabling interrupts in abort functions」。
  2. `HAL_DMA_IRQHandler` 双缓冲路径的 CT 判定取反修正（发布说明：「Fix incorrect callback execution due to inverted CT bit check condition」）。仅影响双缓冲（DBM）分支，本项目未使用 DBM【推断】。
- 其余 DMA 改动（注释、LL 层、BDMA）与本项目无关。
- 升级 HAL 到 1.11.6 会改变 Stop 时的中断与停流顺序，回调行为需重新评估。
