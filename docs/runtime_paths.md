# 硬件完成事件与任务路径

本页解释跨模块事件到任务的实际运行路径。分层与 ISR 规则见 [Architecture](shape/architecture.md#lifecycle)；它不代替各模块的公开接口说明。

## GPIO EXTI、SD 与 Flash

GPIO EXTI 由 Adapter 独占 HAL 全局回调，调用者长期持有回调对象并在普通上下文注册/注销；PinMask 链表可有多个订阅者。它不是 NVIC 通用框架。

- SD 插拔与 SDMMC 完成路径、消抖/传输两个通知槽：[SD 架构](sd_architecture.md#5-热插拔事件路径)。
- Flash 的 QSPI/MDMA、状态匹配、任务收尾、映射退出与恢复：[NOR 架构](w25q256_architecture.md#53-运行时请求路径)。
- FTL/USER DiskIO 和同步 Service 等待：[FTL 运行路径](flash_ftl_design.md#23-运行路径)。

QSPI IRQ、MDMA IRQ 使用可调用 FreeRTOS FromISR 的优先级 5；ISR 不访问数据缓冲或提交下一条 Flash 命令。完成通知只唤醒拥有操作的任务，任务调用 ProcessOperation 完成 Cache/设备状态收尾。卡检测/Queue 请求的 EVENT 与 SD DMA 的 TRANSFER 语义分开，通知槽由 Storage Task 枚举并在 InitSD 注入 Service。

## LCD DMA

SPI LCD DMA 的当前路径为：

```text
GUI Task 调用 Service_GUI_Process()
  -> LVGL v8 渲染到 GUI Service 持有的 SDRAM 双绘制缓冲
  -> GUI Service flush callback 调用 Platform_LCD_StartWrite()
  -> ST7789 Device (CASET/RASET/RAMWR，保持 CS)
  -> STM32 HAL ST7789 SPI Adapter (Clean Cache、DMA 分块)
  -> DMA1 Stream0 IRQ -> HAL_DMA_IRQHandler()
  -> SPI1 EOT IRQ -> HAL_SPI_IRQHandler()
  -> 已注册 HAL Tx complete / error callback
  -> Adapter 续发下一块或发布最终结果
  -> ST7789 Device (释放 CS、恢复 READY)
  -> Platform LCD 转发强类型事件
  -> GUI Service callback（FromISR：lv_disp_flush_ready() + GUI_NOTIFY_LCD_TRANSFER）
  -> GUI Task 中 LVGL wait callback 结束等待并继续处理
```

DMA Stream TC 只表示 DMA 已把数据交给 SPI FIFO，不能作为本次 RAMWR 的最终完成；必须等待
SPI EOT，才可安全续发下一块或释放 CS。当前只有一个 SPI1 异步使用者，STM32 HAL ST7789 SPI
Adapter 可直接注册该 Handle 的回调；第二个真实异步使用者出现后，才按 Handle 提取强类型 SPI
IRQ 分发 Module。任务通知槽属于每个 Task 自己的数组；GUI Task 的 `GUI_NOTIFY_LCD_TRANSFER`
与 Storage Task 的 `STORAGE_NOTIFY_EVENT` 数值都可以是 0，语义互不相关。


## QSPI 只读窗口

Platform 在 WIP=0 且无在飞操作时打开只读窗口；映射本身不触发 IRQ、不检查 WIP。间接操作先退出映射，成功收尾后才恢复，失败保持关闭。资源启动加载按 [Resource Service 链路](resource_pack_design.md#22-启动链路) 使用受控窗口，不向调用者发布长期 NOR View。

Flash benchmark 先将窗口首个 4 KiB 与间接 0xEC 读回逐字节比较，再做 1 MiB volatile 顺序读取/checksum；它属于诊断，不能替代板级 Cache、WIP 和中断时序验收。
