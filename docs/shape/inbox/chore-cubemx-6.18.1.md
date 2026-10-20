# Shape inbox：chore/cubemx-6.18.1

- 链接脚本改用 CubeMX 默认文件名 `STM32H743xG_flash.ld`（旧名 `stm32h743zgtx_flash.ld` 删除），自定义段并入其中。
- Signal（2026-10-20）：HAR 的 Agent Hook 按 `\.ld$` 拒绝所有链接脚本编辑，合并自定义段时只能在 scratch 目录生成、由用户拷入；用 `ld` 重链接（`-T` 指向 scratch 文件）可在不动仓库文件的情况下验证各自定义段地址不变。
- Signal（2026-10-20）：链接脚本改名后，`hardware.md`（HWD-6 来源、相关链接）与 `sources/hardware-facts.md` 的 3 处链接在本分支就地改为 `STM32H743xG_flash.ld`；HAR-8 要求领域文件只在 `main` 改，但链接目标重命名与链接必须同一提交，否则 `check-docs` 断链、FULL 失败。用户批准按此例外处理（2026-10-20）。建议 HAR-8 补"仅链接目标重命名可随引起重命名的分支同步"（level 待用户定）。
