# Inbox: impl-178-fatfs-locator

- 2026-10-31 · routes · route · ROUTES 需要为仓库根新目录 `patches/`（上游补丁，见 HAR-15 / ADR-0022）补一条路由：Entry `patches/fatfs/README.md`，Areas [Architecture](../architecture.md) · [Harness](../harness.md) · [Storage](../storage.md)；`scripts/apply-vendor-patches.ps1` 属于 `scripts/` 既有路由。
- 2026-10-31 · harness · proposed · 新主机测试模块 `Tests/filesystem_locator`（`Tests/` 路由已覆盖目录）：`docs/shape/sources/verification.md` 的主机模块清单与命令表需要加 `filesystem_locator`（`scripts/test-host.ps1`、`verify.ps1`、`rules/verification.psd1`、`harness_helpers.Tests.ps1`、`Tests/README.md` 已同步）。
- 2026-10-31 · architecture/storage-seams · friction · ARC-19：定位符里“文件夹起始簇 + 字节偏移”要求打开时沿目录簇链走到目标簇（`dir_sdi`），读扇区数随目标所在的目录簇序号增长（每个目录簇至多一次 FAT 扇区读），CPU 侧与位置无关但读扇区数不严格无关。32 KiB 簇、12000 首同文件夹的内存镜像实测：第 1 首 1 个扇区，最后一首 54 个扇区，按路径打开最后一首 3485 个扇区；真实卡上目录簇与文件数据簇交错，最坏约等于目录簇数次 FAT 扇区读，约 30 ms 的预算需板测确认；若超预算，可把目录项所在簇（或扇区）也放进定位符（16 B 变 20 B，或以它替换长度核对）。
- 2026-10-31 · harness · friction · HAR-15：补丁文件必须以 LF 存放，否则 `git apply` 在 `core.autocrlf=true` 的 Windows 工作树上无法与 CRLF 文件比对；已在 `.gitattributes` 加 `patches/**/*.patch text eol=lf`，可补进 HAR-15。
- 2026-10-31 · harness · friction · HAR-15：在仓库子目录（如 `build/host/<模块>/`）里对副本 `git apply` 会把补丁路径当作仓库根相对路径而 Skipped；主机测试用 `GIT_CEILING_DIRECTORIES` 绕开，且副本已含补丁（用户提交 Vendor 改动之后）时先 `--reverse --check` 跳过。
