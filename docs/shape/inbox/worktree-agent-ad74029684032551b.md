# Inbox: worktree-agent-ad74029684032551b (#115 字体资源与资源包 v3)

- 2026-10-26 · resources/container · proposed · `sources/rpkc1-extensions.md` 的 FONT V1 需更新为 0.7.0 实际字节契约：FONT 解码器已启用（页首「当前解码器未启用」对 FONT 过时）；MetadataSize 固定 32，无格式专用尾部，条目 Data 即 `lv_font_conv` 1.5.3 未压缩 bin 原样字节；FontFormat = 1 表示该 bin，CodepointEncoding = 1 表示 Unicode；GlyphCount 为 bin 的字形条目数（loca 条目数，含索引 0 的 notdef，当前 7541）；NominalPixelHeight、Ascent、Descent 取自 bin head（当前 12、13、−3，Descent 为 int32 负数）；Component 解码时核对这四项与 bin 一致，并拒绝位图总量（不计字形 0 与空盒子字形）≥ 1 MiB 的字体，打包器同口径。
- 2026-10-26 · resources/packaging · proposed · RES-7 的 Source 可补 `Resources/fonts/`：已入库的生成二进制由 `generate_font.py --check` 复现；打包器的 ResourceID 3、4 由扫描分配，JSON 用 `scanned_image_first_id` 固定其起点，使 ID 5 的 FONT 不挤占图片编号。
- 2026-10-26 · routes · route · `Resources/fonts/` 是字体生成输入与生成结果（README、`generate_font.py`、`OFL.txt`、bin），改动前读其 README；与 `Tools/package_maker/`（Areas: Resources）和 `Components/resource_pack/` 同属 Resources 领域。
