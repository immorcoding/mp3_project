# 冻结包：真唱片底图（slug `vinyl-id4`）

> 本刀合同，合计约 50 行。现场索引见根 `CURRENT.md`，板上事实见 [board.md](board.md)。不抄 `docs/` 与 `CONTEXT.md`。

## 目标

固件解包 ResourceID 4，替换 `MusicPlayerVinylImage` 的假纯色圆：Resource 加载 ID 4 到 SDRAM 槽 → Canvas 用底图合成再叠假封面 → `Service/gui/main/vinyl/` `set_src`。

## 非目标

旋转（另刀，不进 `transport/`）；TimeLabel、电量/时间栏、Library；真解码/seek；读 ID3；假封面换真封面。

## 接缝与禁止

禁止手改 `GUI/`；SquareLine 不加事件；不新建 `Service/playback/`。直径 `SERVICE_GUI_MUSIC_VINYL_DIAMETER` 须与 Image 宽高一致。不把唱盘 C 数组编进内部 Flash；不要 `KEEP` 该数组。

## 资源账

- 入包：ID 1/2 BINARY CP936；ID 3 IMAGE 壁纸 240×320；ID 4 IMAGE `vinyl_original_144px` 144×144 TRUE_COLOR_ALPHA，62208 B（`0xF300`），stride 432。扫描 `Resources/imgs/*.c` 文件名排序，忽略 PNG。
- 起点：固件只加载 ID 1–3；`canvas/` 仍假圆 `#202020` / `#00B0DE`。
- 唱盘槽：`stm32h743zgtx_flash.ld` 的 `__external_resource_vinyl_*` 用 `. += 0xF300` 预留，不 KEEP。该 `.ld` 属生成目录，助手不改。

## 必读

`docs/gui_ui_design.md` 10.5.4、`docs/resource_pack_design.md`、`Service/resource/README.md`、`Service/gui/canvas/README.md`、`Service/gui/main/vinyl/README.md`、ADR-0007。查词：**资源包**。

## 验收

打包器 host 全通过；`./scripts/verify.ps1` FULL 通过；上板见真底图叠假封面，证据记 [board.md](board.md)。
