# Inbox: impl-183

- 2026-11-01 · harness · proposed · HAR-5：`verify.ps1`（FULL）在仓库根超过 56 个字符时自动 `subst` 到空闲盘符运行并在结束（含失败）卸载，证据目录与 metadata 仍写真实路径；不再靠手动 `subst`（#183，机制在 `scripts/short_root.ps1`，说明在 `docs/shape/sources/verification.md`）。建议 HAR-5 的 Check 补一句“工作树路径过长时 FULL 自动映射短盘符”。
- 2026-11-01 · harness · proposed · HAR-11：`verification.psd1` 的主机模块规则新增可选键 `Kind = 'Python'`（unittest 模块，如 `board_test_card`）：`test-host.ps1` 按 Kind 走 `python -m unittest`，`check-test-paths.ps1` 不对它做 gcc 依赖检查；慢用例由 `MP3_SKIP_SLOW_TESTS=1`（`test-host.ps1 -SkipSlow`，CHANGED 传）跳过，FULL 全跑。建议 HAR-11 提一句“Kind = 'Python' 的模块只由 `Tools/<模块>/` 与登记的夹具路径决定选择”。
