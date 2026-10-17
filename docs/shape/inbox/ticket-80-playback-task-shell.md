# Inbox: ticket/80-playback-task-shell

> 以下是 2026-10-17 实施 [#80 播放任务外壳与接线](https://github.com/immorcoding/mp3_project/issues/80) 时记下的 Signal 与必须在 writer branch（`main`）完成的文档修改，规则正文的改写尚未批准。drain 时提给用户。

## 必须在 main 上完成的修改（本票 AC 里的文档项，本分支无权写入）

- 2026-10-17 · routes · signal · 无对应规则 · `docs/shape/ROUTES.md` 需加两行（#71 G 节、#80 AC），格式同相邻行：
  - `APP/tasks/playback/`：说明“播放状态机与任务外壳”；Entry 指向 `APP/tasks/playback/README.md`（链接写法同相邻 `APP/tasks/*/` 行，文字“说明”）；Areas 为 Architecture。
  - `Service/playback/`：说明“跨模块产品流程（playback）”；Entry 指向 `Service/playback/README.md`（文字“模块说明”）；Areas 为 Architecture · Code style，与 `Service/log/` 行同式。
  - 插入位置：`APP/tasks/playback/` 放在 `APP/tasks/storage/` 之后、`APP/tasks/gui/` 之前（同属 APP/tasks 组），`Service/playback/` 放在 `Service/log/` 之后、`Service/resource/` 之前（按字母序）。`APP/tasks/playback/` 的 Areas 可再加 Hardware（I2S 与 PCM5102A 的板级约束），由 drain 时决定。#76、#78 的 inbox 已记过同一缺口。
- 2026-10-17 · architecture · signal · ARC-12、ARC-18 · `docs/shape/architecture.md` 的 `## Open questions` 里整条“过渡说明（2026-10-09）”（ARC-12 中的音频流专用交接与 ARC-18 中“停止确认成功才交还、锁存故障时留在传输模块”是 0.6.0 真播放的目标态……实施完成后删除本条。）应删除；该小节只有这一条，删除后连同 `## Open questions` 标题一并删除（`scripts/check-docs.ps1` 不要求该小节存在）。依据：#71 G 节“在本 spec 实施完成时删除”，#77 inbox 已说明“待播放任务落地后再删”；本票落地了播放任务外壳，后端 spec 的实现部分已齐（Storage 音频流 #75、播放模块 #76/#77、状态机 #78、外壳 #80），仍未板测。#79 与板级验收若要先于删除，请在 drain 时确认。
- 2026-10-17 · architecture · signal · ARC-17 · `docs/shape/architecture.binding.md` 的 2026-10-09 两条 cite Signal（播放任务静态创建与优先级、定时器服务任务同级 3）现已有落地事实：`APP/tasks/README.md` 的“任务与优先级”一节承载排序与依据，`app_tasks.c` 以 `xTaskCreateStatic` 创建。可考虑把 ARC-17 从 provisional 的检查依据补上“`APP/tasks/README.md` 任务与优先级表”，由 drain 时决定。

## Signal

- 2026-10-17 · architecture · signal · 无对应规则 · #71 F 节写“堆新增命令队列与状态信箱两个对象”，实现改为 `xQueueCreateStatic`，两个队列的控制块与存储在 DTCM `.bss`（`playback_task_command_*`、`playback_task_snapshot_*`，合计约 360 B），不占内核堆。理由：与 Storage 的信箱同法（#75）；创建不会失败、没有运行期分配（B2 的字面要求）。代价是 DTCM 多约 0.35 KiB、堆余量比 F 节估算更多；已在 #80 的 map 校核评论里记入。
- 2026-10-17 · architecture · signal · B1、B7、B8 · 外壳在无事件时最长 100 ms 唤醒一次（`PLAYBACK_TASK_WAKE_PERIOD_MS`），否则 #79 的 T_data（2 s）、T_out（500 ms）这类“没有任何 FLAG 也要到期”的判定没有触发源。#71 没有写这个周期。100 ms 相对 T_out 500 ms 最多多出 100 ms 的检出延迟，板级验收“T_out 故障注入：T_out + 一次唤醒延迟内触发”据此仍成立；#79 若要更细的粒度，只改这个常量。空闲时每秒 10 次空唤醒的开销待板测评估。
- 2026-10-17 · architecture · cite · #78 inbox · 状态机 1:1 副本的漂移风险用外壳里的 `_Static_assert` 固定（结构体大小、字段宽度与偏移、枚举值），没有改 #75 的公开头；Storage 一侧增删字段或枚举会在固件构建时报错，主机测试不会（状态机的 Fake 不引入 Storage 头）。若以后要主机侧也能发现漂移，才需要把 Storage 的请求/记录类型拆到不含 FreeRTOS 的头。
- 2026-10-17 · architecture · signal · 无对应规则 · `playback_task()` 在队列创建、播放模块初始化或 Platform 音频回调注册失败时记日志并 `vTaskDelete(NULL)`，快照保持全零、`playback_command_send()` 返回 `PLAYBACK_QUEUE_FULL`。选 `vTaskDelete` 是因为 `FreeRTOSConfig.h` 没有开 `INCLUDE_vTaskSuspend`（该文件受保护，未改）；这条失败路径主机无法覆盖，也不会被 GUI 以外的任何东西看见。若希望启动期这类失败直接 `Error_Handler()`（同 `app_tasks.c` 创建任务失败），请在 drain 时决定。
- 2026-10-17 · hardware · signal · HWD-2、E10 · Platform 音频事件回调（`playback_task_audio_event`）在 ISR 里调用 `xTaskNotifyIndexedFromISR`，I2S2 DMA 的 NVIC 抢占优先级必须落在 `configMAX_SYSCALL_INTERRUPT_PRIORITY` 对应的 5–15。I2S2 的 DMA 流与 NVIC 由用户在 CubeMX 导出（E10），本票没有核对其当前值；`hw:pending`。
- 2026-10-17 · harness · cite · 无对应规则 · 外壳 `playback_task.c` 没有主机测试；`scripts/rules/verification.psd1` 里 `^APP/tasks/playback/` 会让它的改动选中 `playback_task` 主机模块，但该模块只编译 `playback_machine.c`，不覆盖外壳。固件 Debug/Release 构建会编译并链接外壳，静态断言在那里生效。
- 2026-10-17 · storage · signal · STOR-6 · `APP/tasks/storage/README.md` 的“音频流分区”小节仍写“播放任务本身由后续票引入”，播放任务外壳落地后该句过时；按 #71 G 节本应由 Storage 侧文档更新（“音频流分区、游标退场、新 FLAG”），本票按任务分工只改 `APP/tasks/README.md` 与 `APP/tasks/playback/README.md`，该句留给交互 spec 的游标退场票或 drain 时顺手改。
