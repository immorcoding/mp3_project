# Inbox: impl-179-open-by-locator

- 2026-11-01 · diagnostics · friction · DIAG-4：名称槽 INFO 的 `us=` 现为标签读取耗时（整行耗时减去 `open=`），窗口汇总行的 `max=`、`us=`、`top=` 仍含打开；汇总行新增 `omax=<本窗最长单次打开>`，其中一个触发条件仍是单行读取（含打开）> T_data/2 的防御阈值。建议 DIAG-4 的文字写明“`us=` 不含打开、`max/us/top` 含打开、新增 `omax=`”。汇总行 12 行窗口、十位数极端取值会超出 Service Log 单条 128 B 并丢尾部计数（现实取值约 120 字符）。
- 2026-11-01 · storage · proposed · STOR-17 / DIAG-5：`Stream` INFO 只在开启成功后记（`off=` 取请求的 `AudioOffset`，未钳位；`lseek=` 是最后一次定位到区间起点加偏移的耗时）；开启失败不记。如需失败也留痕，另开票。
- 2026-11-01 · storage · friction · STOR-17：核对不符的 `MISMATCH` 才退回按路径打开；`FAILED`（槽用尽、卷未就绪、介质错误）不退回（#178 决定）。STOR-17 现写“核对不符退回按路径打开”，可补一句“其他失败不退回”。
