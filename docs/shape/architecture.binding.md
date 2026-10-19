# Architecture · binding

实例装配与调用期间的对象寿命。

[返回 Architecture](architecture.md)

### Rules

- **ARC-8** · settled · Ops 与 Context 成对绑定且生命周期覆盖调用；Platform 用 static 长期持有 Handle/Context；Bind 只装配且检查 Handle/Ops/Context/资源，Init 再检查必需回调后访问硬件，APP 不调用 Bind。 _Why:_ 调用期间悬空 Context 或装配时访问硬件都会破坏生命周期。 _Source:_ [architecture_standard（迁移基线）](https://github.com/immorcoding/mp3_project/blob/232502bd4b66a728d26d58d39555f34bb3faedf2/docs/architecture_standard.md) §4 _Check:_ `CODING_STANDARDS.md` 的架构审阅入口。
- **ARC-17** · provisional · 长期运行的任务及其任务控制块、栈由 APP 装配时静态创建（`xTaskCreateStatic`），其余任务沿用动态创建；新增堆占用须按 HWD-2 审查。 _Why:_ 解码栈约 17.6 KB，超出 32 KiB 堆的余量，扩堆与补丁各有成本。 _Source:_ [ADR-0019](../adr/0019-freertos-static-task-stack.md) _Check:_ code-review 规范轴核对任务创建方式与 FreeRTOS 静态分配开关，以 [`APP/tasks/README.md`](../../APP/tasks/README.md)「任务与优先级」（排序与依据：播放任务 3 高于 Storage 2、GUI 1，只靠优先级不加让出点；定时器服务任务同为 3，引入软件定时器时回调须很短，否则复核两者优先级）与 `APP/tasks/app_tasks.c` 的 `xTaskCreateStatic` 调用为依据。

### Signals

- 2026-10-17 · cite · ARC-17 · 播放任务的命令队列与状态信箱也用 `xQueueCreateStatic`，控制块与存储在 DTCM `.bss`（约 360 B），不占内核堆，创建不会失败（#80）。
- 2026-10-19 · cite · ARC-17 · 板测 Playback Task 栈峰值 84.4%（最小余量 3184 B / 20480 B），堆最小余量 15792 B。
- 2026-10-19 · cite · new · 新任务须在 `monitor_get_task_stack_words()`（`APP/tasks/monitor/monitor_task.c`）登记栈深，否则栈占用不出现在监控日志；Playback Task 曾因此缺失，已在 859f307 补登记。
- 2026-10-17 · cite · new · 播放状态机为不依赖 FreeRTOS，在 `playback_machine.h` 自带与 `Storage_Audio*TypeDef` 1:1 的 `Playback_Machine*TypeDef` 与 Ops，由任务外壳映射，并用 `_Static_assert` 固定结构体大小、字段宽度与偏移、枚举值；漂移只在固件构建报错，主机测试不会。用户决定不把 Storage 的请求/记录类型拆到无 FreeRTOS 的头。
- 2026-10-17 · cite · new · `playback_task()` 在队列创建、播放模块初始化或音频回调注册失败时记日志并 `vTaskDelete(NULL)`（`FreeRTOSConfig.h` 未开 `INCLUDE_vTaskSuspend`），快照保持全零；用户可能日后改为 `Error_Handler()`（同 `app_tasks.c` 创建失败）。该路径主机测试无法覆盖。
