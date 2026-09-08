# CURRENT.md

> 章程：工程现场。开场必读。进度跨会话；本场交接在任务替换或硬切前重写。合计约 80 行，只放指针。工作方式见 `AGENTS.md`，术语见 `CONTEXT.md`。

## 进度

- 刚收口：Slider 假 SEEK；切歌/CLEAR 归零；墙钟假走表已裁。板上拖条/切歌/拔卡已确认。
- 下一活动：Now Playing 唱片旋转（playing 转 / paused 停）+ 中心假封面（无当前曲只转唱片）。先 SquareLine 导出对象，禁止手改 `GUI/`。`MusicPlayerTimeLabel` 已导出，本刀不接线。
- 本主线必读：`docs/gui_ui_design.md` **10.5.2 / 10.5.4**、`APP/tasks/gui/music/README.md`、`Service/gui/main/transport/README.md`、ADR-0007。
- verify：上一刀 `PASS_HOST_ONLY`（FULL）。按需查词：**播放列表**。

## 本场交接

- 分支：`main`。禁止手改 `GUI/`。SquareLine 不加事件。不建 `Service/playback/`。
- 已导出：`MusicPlayerVinylImage` 144×144。Canvas 纯色圆假合成（盘 `#202020`、封面 `#00B0DE`）写入独立唱盘缓冲，`vinyl/` 绑第一帧。旋转未做。
- 直径宏 `SERVICE_GUI_MUSIC_VINYL_DIAMETER` 须与 Image 宽高一致。不读 ID3、不导入 PNG。
- Slider 假 SEEK、切歌/CLEAR 归零仍有效。TimeLabel 仍不接线。
- 电量/时间栏、Library、真解码/seek、ID3 封面仍不做。助手不代提交。
- 下场：板上确认 Now Playing 假唱盘第一帧；再接线旋转。
