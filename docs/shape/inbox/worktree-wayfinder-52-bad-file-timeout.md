# Inbox: worktree-wayfinder-52-bad-file-timeout

> 以下是 2026-10-09 [坏文件与超时：有界退出与超时口径](https://github.com/immorcoding/mp3_project/issues/52) 会话记下的 Signal，规则正文的改写尚未批准。完整结论见该票的解决评论，由播放后端 spec 承载。drain 时提给用户。词典“坏文件”的改写已获用户当场批准，直接写在本分支。

- 2026-10-09 · storage · signal · STOR-8 · 插卡消抖结束后，`storage_sd_process()` 同步完成挂载和曲库扫描（最多 32000 首，可能耗时数秒），期间主循环不处理流请求信箱。播放任务的数据等待超时 T_data（初值 2 s）因此会在“插卡后立即按播放”时触发停止；用户已接受这一行为。若将来把扫描改为可分步插入请求，需要改写本规则描述的主循环结构，并同步修订 T_data 的依据。扫描耗时进入板测。
- 2026-10-09 · architecture/interrupts · confirm · ARC-10 · 输出无进展看门狗（T_out，初值 500 ms）由播放任务按等待超时判定，ISR 仍然只累加计数并唤醒，不承担计时或判定，与规则一致。
- 2026-10-09 · storage · signal · storage 待决问题 · FatFs DiskIO 等一次 SDMMC DMA 事件的超时是 30 s（`FILESYSTEM_FATFS_BSP_DMA_TIMEOUT_MS`），远大于播放侧的 T_data。播放任务超时停止后，Storage 可能仍阻塞在这次读取中，之后写入的请求要等它返回才处理。本票不改这个值；它是否需要按 SD 规范的读访问上限（SDHC/SDXC 100 ms）收紧，留给存储侧复核。
