# Shape inbox：integration/0.6.1

- Signal 处理（#108，2026-10-22）：`diagnostics.md` 2026-10-19 的 friction（播放路径只有错误日志）由 #108 解决——播放任务（`APP/tasks/playback`）经 `LogInfo` 记 INFO 事件，标签 `PLAYBACK`；`Platform/audio` 与 `Service/playback` 不加 INFO；固件版本经 `cmake/fw_version.cmake` 生成，启动日志 `BOOT` 标签打印。drain 时把该 Signal 标为已处理。
- 草稿（provisional，待用户定）：DIAG 新规则——ISR 回调与每帧路径不调用 `Service_Log_Post`，INFO 只在命令与曲目边界记，文本不含文件名。_Why:_ 日志投递占消息池且不属实时预算；#108 靠主机测试 `playback_task_log` 与代码核对遵守。_Check:_ code-review 规范轴核对 ISR 回调与每帧路径无日志调用。
- Open question 处理（#107，2026-10-22）：`storage.md` Open questions 中「FatFs DiskIO 单次 SDMMC DMA 等待超时为 30 s ……是否收紧」一条已由 #107 解决——`FILESYSTEM_FATFS_BSP_DMA_TIMEOUT_MS` 改为 500 ms（按每个 32 KiB DMA 分块计；依据 SD 读访问上限 100 ms、SDHC 写忙 250 ms、SDXC 写忙 500 ms；超时经 `filesystem_sd_transfer_finish` 以 ERROR 收尾，BSP 返回 `MSD_ERROR`，Storage 回到主循环）。drain 时移除该条。板上 ≥ 3 张卡播放与扫描不误触发属 `hw:pending`。
