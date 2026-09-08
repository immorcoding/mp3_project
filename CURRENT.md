# CURRENT.md

> 章程：工程现场。开场必读。进度跨会话；本场交接在任务替换或硬切前重写。合计约 80 行，只放指针。工作方式见 `AGENTS.md`，术语见 `CONTEXT.md`。

## 进度

- 刚收口：假唱盘第一帧（纯色圆合成 + `MusicPlayerVinylImage`）；`PLATFORM_DCACHE_LINE_SIZE` 别名 Adapter config。已提交 `2026/9/8 15:49`。工作树干净。
- 下一活动：板上确认 Now Playing 假唱盘观感（深灰盘 + 青中心）；通过后再接线旋转。禁止手改 `GUI/`。TimeLabel 仍不接线。
- 本主线必读：`docs/gui_ui_design.md` **10.5.4**、`Service/gui/main/vinyl/README.md`、`Service/gui/canvas/README.md`、ADR-0007。
- verify：提交时 FAST `PASS`（索引，含 `ALLOW_GENERATED_UPDATE`）。FULL 未作本刀收口。HEX 已 DFU 写入并校验，观感未报。按需查词：**资源包**。

## 本场交接

- 分支：`main`。禁止手改 `GUI/`。SquareLine 不加事件。不建 `Service/playback/`。助手不代提交。
- 导出对象：`MusicPlayerVinylImage` 144×144。`canvas/` 独立 SDRAM 缓冲画盘 `#202020`、封面 `#00B0DE`；`main/vinyl/` `set_src` 第一帧。旋转未做。
- 直径 `SERVICE_GUI_MUSIC_VINYL_DIAMETER` 须与 Image 宽高一致。不读 ID3、不导入 PNG。唱片底图下一版进 Resource Pack。
- D-Cache 行只在 `cortex_m7_dcache_adapter_config.h`；`platform.h` 别名，不对上泄漏 Clean/Invalidate。
- Slider 假 SEEK、切歌/CLEAR 归零仍有效。电量/时间栏、Library、真解码/seek 仍不做。
- 下场：确认第一帧后，对 `MusicPlayerVinylImage` 做 playing 转 / paused 停；不要塞进 `transport/`。
