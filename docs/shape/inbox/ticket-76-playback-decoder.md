# Inbox: ticket/76-playback-decoder

> 以下是 2026-10-17 实施 [#76 播放模块（一）：minimp3、线性窗口与解码](https://github.com/immorcoding/mp3_project/issues/76) 时记下的 Signal，规则正文的改写尚未批准。drain 时提给用户。

- 2026-10-17 · routes · signal · 无对应规则 · `docs/shape/ROUTES.md` 缺 `Service/playback/` 一行（目标模块 README 为 `Service/playback/README.md`，Areas 同 `Service/log/`：Architecture · Code style，可再加 Hardware）；写入分支限制使本分支无法修改，drain 时补，并随 `APP/tasks/playback/` 一并加 [#71](https://github.com/immorcoding/mp3_project/issues/71) G 节要求的两行。
- 2026-10-17 · architecture · signal · ARC-13 · 新建 `Service/playback/README.md` 与 `Middlewares/Third_Party/minimp3/README.md`（固定提交 `ea99364`）；Service 层 README 已加 `Service_Playback_*` 条目。
- 2026-10-17 · harness · signal · 无对应规则 · 主机模块 `playback_service` 的路径规则含 `^Middlewares/Third_Party/minimp3/`，升级 minimp3 即选中该测试；`scripts/harness_helpers.Tests.ps1` 原先以 `Service/playback/player.c` 充当“未映射生产路径”的例子，现已映射，改用 `Service/unmapped/player.c`。
- 2026-10-17 · architecture · signal · 无对应规则 · 与 [#46](https://github.com/immorcoding/mp3_project/issues/46) 一致的 minimp3 行为在主机测试中得到验证：窗口不足即整窗丢弃、无解码器状态时需同格式帧连成串才认帧、流尾残余不足 4 字节会连带丢前一帧；这些写进了 `Service/playback/README.md` 的“已知的 minimp3 行为”，上层（播放任务 B13/B16）不应假定更强的保证。
