# Inbox: ticket-203-freertos-stubs

- 2026-11-04 · harness · proposed · HAR-11：主机工程共用的 FreeRTOS 桩在 `Tests/stubs/freertos/`（原 `Tests/flash_ftl/stubs/`，#203），按产品 include 路径摆放头文件，附脚本化接口（`freertos_stub.h`：拿不到锁、阻塞钩子、tick 与通知观察）；桩自测是 `Tests/flash_ftl` 里的 `freertos_stub_behavior`，`verification.psd1` 里 `flash_ftl` 与 `filesystem_locator` 的 `PathPatterns` 都登记 `^Tests/stubs/freertos/`。建议 HAR-11 提一句“新增依赖 FreeRTOS 的主机工程把 `../stubs/freertos` 放在 include 路径最前，并登记该路径模式”。
- 2026-11-04 · harness · proposed · harness.md 2026-11-03 的 Signal（`_FS_LOCK` 依赖）已落实：`Tests/filesystem_locator/CMakeLists.txt` 在复制 `ffconf.h` 时把 `_FS_LOCK` 固定为 2（正则匹配不到则配置失败），注释写明原因。临时把 `ffconf.h` 改成 `_USE_LFN 2`、`_FS_LOCK 9` 后全部 14 个 CMake 主机工程通过，未发现其他依赖，所以 #201 K 节“依赖旧数值的主机测试由第 4 步的票修”这句可以改成“#203 已修，导出后不需要再修测试”。
