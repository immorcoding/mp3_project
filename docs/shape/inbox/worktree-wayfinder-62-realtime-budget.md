# Inbox: worktree-wayfinder-62-realtime-budget

> 以下是 2026-10-09 [实时预算：任务优先级、PCM 深度与最坏路径](https://github.com/immorcoding/mp3_project/issues/62) 会话记下的 Signal，规则正文的改写尚未批准。完整结论见该票的决结评论，由播放后端 spec 承载。drain 时提给用户。本票不改词典、不立 ADR。

- 2026-10-09 · architecture/binding · signal · ARC-17 · 播放任务定为优先级 3（播放 3 > Storage 2 > GUI/Log/Monitor 1 > Boot 0），独占该级、不依赖时间片；`FreeRTOSConfig.h` 不改（`configMAX_PRIORITIES` = 5）。ARC-17 只讲静态创建，没有写任务优先级的排序及其依据；drain 时决定是否把“播放任务高于 Storage、只靠优先级不加让出点”写成规则，或只留在 spec 与 `APP/tasks/README.md`。
- 2026-10-09 · architecture · signal · 无对应规则 · FreeRTOS 定时器服务任务的优先级也是 3（`configTIMER_TASK_PRIORITY`），目前应用代码里没有任何软件定时器，所以与播放任务同级没有影响。将来引入软件定时器时，回调必须很短，否则要重新评估播放任务或定时器任务的优先级。
- 2026-10-09 · gui · signal · 无对应规则 · GUI Task 是不让出的忙循环，只在等 LCD DMA 刷新时阻塞：Idle 几乎跑不到，运行时统计看不出 CPU 余量，Log/Monitor 只能靠时间片分到 CPU。播放任务的优先级高于它，所以对音频期限没有影响；要不要加帧间延时，留给 GUI 侧问题。
- 2026-10-09 · architecture/interrupts · confirm · ARC-10 · 播放任务每次唤醒的工作有结构上界（只补空出来的半区，最多两个；输入不够就结束本次唤醒去等块）；ISR 只计数并唤醒，与规则一致。I2S2 在 CubeMX 中还没有配置 DMA 流和 NVIC 优先级，实施时须从源工程导出；NVIC 优先级的数值须在 5–15 之间，才能调用 FromISR（ARC-2/ARC-3）。
- 2026-10-09 · hardware · signal · HWD-6 · PCM 保持两个半区，每半区 1152 帧，做成配置常量（须为 1152 的整数倍），期限按 48 kHz 下的 24 ms 计算。板测出现欠载时先改为 2304，AXI 多占 9 KiB（余约 210 KiB），不改 `Platform_Audio_*` 接口。
