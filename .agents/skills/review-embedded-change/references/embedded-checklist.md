# 嵌入式审阅清单

只检查与当前 diff 有关的项目，并给出文件、行为路径和可复现证据。

- ISR 是否只做有界通知，是否使用正确的 `FromISR` API、优先级和唤醒语义；
- DMA 完成事件是否等同于真实外设完成，回调所有权与 Handle 匹配是否唯一；
- DMA 缓冲区的地址、长度、对齐、生命周期及 D-Cache clean/invalidate 时机是否正确；
- Task 通知槽、队列、锁和状态机是否会丢事件、重复消费或在错误上下文阻塞；
- Adapter、Platform、Service、APP 的资源与策略所有权是否仍符合 `docs/architecture_standard.md`；
- 超时、错误恢复、取消和重复初始化是否把硬件与软件状态重新收敛；
- 链接段、SDRAM、栈、静态缓冲和 Vendor Handle 的生命周期是否覆盖异步操作；
- 主机 fake/mock 能证明哪些语义，哪些结论仍必须通过真机日志、示波器或掉电测试确认。
