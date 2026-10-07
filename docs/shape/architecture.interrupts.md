# Architecture · interrupts

中断发布事件与任务执行的交界。

[返回 Architecture](architecture.md)

### Rules

- **ARC-10** · settled · ISR 只发布轻量事件/FromISR 通知，不格式化日志、发 USB、访问 I2C/SD/文件系统、延时消抖或调用普通 RTOS API；任务执行状态机与业务，通知槽编号只在所属任务内有意义。GPIO EXTI 使用调用者长期持有、普通上下文注册注销的 Callback 链表；其他外设用按 Handle 的强类型回调，不扩成 `IRQ_ID + void *` 通用分发器。 _Why:_ 中断上下文不能承担阻塞业务，通知只唤醒拥有状态的任务。 _Source:_ [architecture_standard（迁移基线）](https://github.com/immorcoding/mp3_project/blob/232502bd4b66a728d26d58d39555f34bb3faedf2/docs/architecture_standard.md) §8 _Check:_ `docs/shape/REVIEW.md` 的架构审阅入口。
