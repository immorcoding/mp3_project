# Inbox: ticket/85-cursor-retire

> 以下是 2026-10-19 实施 [#85 游标退场](https://github.com/immorcoding/mp3_project/issues/85) 时记下的待 drain 项；`docs/shape/*.md` 只在 `main` 修改，规则正文未改。drain 时提给用户。

## Drain

- 2026-10-19 · storage · drain · STOR-6 / STOR-8 / STOR-12 · `docs/shape/storage.md` Open questions 的“过渡说明（2026-10-09）”（关于播放游标、音频流与回收门的目标态说明）应删除：Storage 侧游标 `storage_playback_cursor.*` 及 `storage_catalog` 中的 init/invalidate 调用已在本票移除，游标只由播放任务持有（ADR-0020 第 4 条），STOR-6 等条款已与实现一致。删除前核对该说明里是否还有未完成的流/回收门部分，有则只删游标相关句。

## Signal

- 2026-10-19 · harness · signal · 无对应规则 · `Tests/storage_catalog` 只含游标用例，随游标删除后整个主机模块 `storage_catalog` 退场：`verification.psd1` 的 `AllHostModules` 与规则块、`test-host.ps1` / `verify.ps1` 的 `ValidateSet` 同步去掉，主机测试由 11 个工程 40 个 CTest 降为 10 个、39 个。`APP/tasks/storage/catalog/` 现在不属于任何主机模块，改动该目录会走“未知生产路径”保守回退（全部主机测试）。若希望 Catalog 小改不触发全量，需要另立票为 Catalog 补主机测试。
- 2026-10-19 · harness · signal · 无对应规则 · #82/#84 inbox 与 ADR-0007/0018/0020 的历史叙述里仍出现 `storage_playback_cursor` 名称；它们是决策与过程记录，未改。
