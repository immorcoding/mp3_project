# Architecture runtime reference

[Architecture](architecture.md) ARC-10 的跨模块完成事件参考；公开接口与局部调用链仍由模块 README 维护。

## gpio-storage

GPIO EXTI Adapter 独占 HAL 全局回调，调用者持有订阅对象、普通上下文注册/注销；PinMask 可有多个订阅者，不是通用 NVIC 框架。

SD 的插拔 EVENT 与 DMA TRANSFER 用不同通知槽，枚举属于 Storage Task并经初始化注入 Service。QSPI/MDMA IRQ 当前使用允许 FreeRTOS FromISR 的优先级 5；通知只唤醒操作拥有者，任务推进 ProcessOperation 完成 Cache/设备状态收尾，ISR 不访问数据缓冲或提交下一条 Flash 命令。具体链路见 [Platform SD](../../Platform/sd/README.md)、[Platform Flash](../../Platform/flash/README.md) 与 [Filesystem Service](../../Service/filesystem/README.md)。

## lcd-dma

LCD 的完成链是：GUI Task → LVGL flush → Platform LCD → ST7789 RAMWR（保持 CS）→ SPI Adapter（Clean Cache、分块 DMA）→ DMA IRQ → SPI EOT IRQ → HAL 回调 → Adapter 续块或发布最终结果 → Device 释放 CS/恢复 READY → Platform 强类型事件 → GUI Service `lv_disp_flush_ready()` 与 FromISR 通知 → GUI Task wait callback 结束等待。

DMA Stream TC 只表明数据到达 SPI FIFO，SPI EOT 才允许续块或释放 CS。把 DMA TC 当最终完成会提前复用缓冲或截断事务。当前只有一个 SPI1 异步使用者，Adapter 直接注册 Handle 回调；第二个真实使用者出现时才按 Handle 提取强类型分发模块。

GUI 的 LCD_TRANSFER 与 Storage 的 EVENT 可以都为数值 0，但属于不同 Task 的通知数组。缓冲与回调接口见 [GUI Service](../../Service/gui/README.md)、[LCD Platform](../../Platform/lcd/README.md) 和 [ST7789 SPI Adapter](../../Adapters/stm32_hal/st7789_spi/README.md)。

## qspi-window

Platform 只在 WIP=0 且无在飞操作时打开只读映射；映射本身不检查 WIP也不触发 IRQ。间接操作先退映射，成功收尾才恢复，失败保持关闭。资源启动加载经受控窗口读取，不向调用者发布长期 NOR View，见 [Resource Service](../../Service/resource/README.md)。

Flash benchmark 用间接读校对窗口后做顺序读取校验，它只是诊断，不能替代板级 Cache、WIP 与中断时序证据。
