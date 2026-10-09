# Architecture · binding

实例装配与调用期间的对象寿命。

[返回 Architecture](architecture.md)

### Rules

- **ARC-8** · settled · Ops 与 Context 成对绑定且生命周期覆盖调用；Platform 用 static 长期持有 Handle/Context；Bind 只装配且检查 Handle/Ops/Context/资源，Init 再检查必需回调后访问硬件，APP 不调用 Bind。 _Why:_ 调用期间悬空 Context 或装配时访问硬件都会破坏生命周期。 _Source:_ [architecture_standard（迁移基线）](https://github.com/immorcoding/mp3_project/blob/232502bd4b66a728d26d58d39555f34bb3faedf2/docs/architecture_standard.md) §4 _Check:_ `CODING_STANDARDS.md` 的架构审阅入口。
- **ARC-17** · provisional · 长期运行的任务及其任务控制块、栈由 APP 装配时静态创建（`xTaskCreateStatic`），其余任务沿用动态创建；新增堆占用须按 HWD-2 审查。 _Why:_ 解码栈约 17.6 KB，超出 32 KiB 堆的余量，扩堆与补丁各有成本。 _Source:_ [ADR-0019](../adr/0019-freertos-static-task-stack.md) _Check:_ code-review 规范轴核对任务创建方式与 FreeRTOS 静态分配开关。

### Signals

- 2026-10-09 · cite · ARC-17 · 播放任务静态创建、优先级 3（高于 Storage 2、GUI 1），只靠优先级不加让出点；排序与依据由后端 spec 和 `APP/tasks/README.md` 承载，`FreeRTOSConfig.h` 不改。
- 2026-10-09 · cite · new · FreeRTOS 定时器服务任务优先级也是 3（`configTIMER_TASK_PRIORITY`），与播放任务同级；应用暂无软件定时器，引入时回调须很短，否则复核两者优先级。
