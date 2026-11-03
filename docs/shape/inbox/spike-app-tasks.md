# Inbox: spike-app-tasks

- 2026-11-03 · harness · friction · HAR-11：`Tests/filesystem_locator` 的“锁表满”用例依赖产品 `ffconf.h` 的 `_FS_LOCK`（原值 2）。#191 把 `_FS_LOCK` 调到 4 之后，该用例在没有任何生产代码改动的情况下失败（锁表与 4 个文件槽同时用尽，测不出锁表满）。本分支把测试用的 `ffconf.h` 副本里的 `_FS_LOCK` 固定为 2。建议：主机测试复制产品配置时，凡用例依赖某个配置数值，就在测试的 CMake 里显式固定它，并注释原因。
- 2026-11-03 · diagnostics · friction · DIAG-3、WF-12：板测要的数字多（本票约 20 行每 10 s），而 Service Log 消息池只有 30 条，Monitor 每 5 s 一次连发约 13 行，两者撞上就挤掉 PLAYBACK 等日志。本分支靠“每行之后让出 6 ms”绕过（Log Task 每 5 ms 消费一条），没有改池深度。建议：WF-12 的汇总类日志注明节拍要求，或日志池深度随汇总需求复核。
