# CURRENT.md

> 章程：工程现场。开场必读。进度跨会话；本场交接在任务替换或硬切前重写。合计约 80 行，只放指针。工作方式见 `AGENTS.md`，术语见 `CONTEXT.md`。

## 进度

- 刚收口：Slider 假 SEEK；切歌/CLEAR 归零；墙钟假走表已裁。板上拖条/切歌/拔卡已确认。
- 下一活动：Now Playing 唱片旋转（playing 转 / paused 停）+ 中心假封面（无当前曲只转唱片）。先 SquareLine 导出对象，禁止手改 `GUI/`。`MusicPlayerTimeLabel` 已导出，本刀不接线。
- 本主线必读：`docs/gui_ui_design.md` **10.5.2 / 10.5.4**、`APP/tasks/gui/music/README.md`、`Service/gui/main/transport/README.md`、ADR-0007。
- verify：上一刀 `PASS_HOST_ONLY`（FULL）。按需查词：**播放列表**。

## 本场交接

- 分支：`main`。禁止手改 `GUI/`。SquareLine 不加事件。不建 `Service/playback/`。
- Slider 假命令已收口：`transport/` `RELEASED` → 单槽 `MUSIC_SEEK` → `gui_music_step` → `ProgressApply`。拖动不改 playing。切歌 / CLEAR 把进度写回 0；无当前曲的 SEEK 忽略并写回已存值。
- 墙钟假走表已裁（`fake_accum_ms` / `advance_fake` 不进结构体）。自动刷新留在 `gui_music_step` 注释框，等 Playback 用解码器时间对接。
- 输入仍一个单槽。`param`：QUEUE_SELECT=播放列表下标，SEEK=0..100。
- 已导出：`MusicPlayerTimeLabel`（Slider 上方，占位 `1:00/3:14`）。控制区高 `34%`，Slider `y=20%`。运行时尚未 Apply。
- 电量/时间栏、Library、打开文件、真解码/seek、ID3 封面仍不做。助手不代提交。
- 唱片底图下一版 PC 烧进 Resource Pack，本版 SquareLine 不导入大 PNG。假封面仍可占位。
- 下场：用户按 10.5.4 导出圆形 `MusicVinylDisc` + 假封面后，再接线旋转与封面显隐。
