# CURRENT.md

> 章程：工程现场。开场必读。进度跨会话；本场交接在任务替换或硬切前重写。合计约 80 行，只放指针。工作方式见 `AGENTS.md`，术语见 `CONTEXT.md`。

## 进度

- 刚收口：GUI Task `music/`；输入单槽；游标环形上一首/下一首；playing 假状态。准备提交。
- 下一活动：`MusicPlayingSlider` 假命令（运行时绑事件 → 单槽 → `gui_music_step`）。不解码、不真正 seek。
- 本主线必读：`APP/tasks/gui/music/README.md`、`Service/gui/main/transport/README.md`、ADR-0007 第 2 条、`docs/gui_ui_design.md` **10.5.2**。
- verify：上一刀 `PASS_HOST_ONLY`（FULL）。按需查词：**播放列表**。

## 本场交接

- 分支：`main`。禁止手改 `GUI/`。SquareLine 不加事件。不建 `Service/playback/`。
- 三键路径已通：`transport/` CLICKED → `ConsumeInput` → `gui_music_step` → `TransportApply`。进度条 `transport/` 明确不处理。
- 输入仍一个单槽。无点击 `SERVICE_GUI_INPUT_NONE` + `SERVICE_OK`。扩 Slider 命令，不要新 Queue。
- PLAY/PAUSE：SquareLine 已去掉 `x` 偏移，Label 为 `lv_font_montserrat_14`、居中。电量/时间、Library、打开文件仍不做。
- 提交：助手不代提交。工作区含 `music/`、`gui_service_input`、`main/transport/`；若一并提交 `GUI/`/`SquareLineProject/`，须当前会话 `$env:ALLOW_GENERATED_UPDATE='1'`。
- 下场第一刀：给 Slider 假 cmd（建议 `RELEASED` 再 post 百分比），`music/` 只记假进度；拖动不改 playing。
