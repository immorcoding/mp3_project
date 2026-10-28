# Inbox: impl/154-vinyl-artist (#154 唱盘页歌手行与 Now Playing 重排)

- 2026-10-28 · gui/music · cite · GUI-27 · 像素常量配置放在 `Service/gui/gui_service_config.h` 的 `SERVICE_GUI_MUSIC_LAYOUT_*` 一组（与 `SERVICE_GUI_MUSIC_TITLE_WIDTH` 同一份 config，STY-3），`service_gui_view_music_solve_layout()` 由它们与运行时读到的 Playing 内容区高度推出 S、D 与各元素 Y；模拟器实测盒子坐标与 #152 结论表逐项相同。
- 2026-10-28 · gui/music · friction · GUI-27 · “按字体完整字形高度计量”在实现里是两组手填常量（Noto 12：Label 内第 3 行起 13 行；Montserrat 10：第 1 行起 9 行），不随字体自动推出。资源包字体不可用时曲名与歌手回退 Montserrat 12，Label 高随行高变化，但字形上沿仍按 Noto 的 3 计算，字形与设计间距有像素级偏差（`music_title_font_fallback` 截图可见，未逐项实测）。以后换字体或字号须同步这两组常量；是否改为运行时从字形描述推出待定。
- 2026-10-28 · gui/music · cite · GUI-26 · 歌手的 `Unknown Artist` 与空串由 APP 音乐分区 `gui_music_title_text()` 决定（与 Queue 同一个 `GUI_MUSIC_TEXT_UNKNOWN_ARTIST`），Service 只画；曲名为空白或 `--` 时歌手为空串。
- 2026-10-28 · gui/ownership · cite · GUI-4 · #154 有 31 个既有场景 132 帧变化（另新增 `music_artist` 9 帧）。核对办法：`git archive cae66d0` 到临时目录渲染改动前截图，逐帧左右拼图比对：唱盘页帧的播放状态、时间、进度、停止短语、唱盘角度与改动前一致，差异只有版面（唱盘 144 → 122、曲名下移并加歌手行、进度条与三键位置）；Queue 帧只有位置 6 的歌手行（新增多歌手假标签）。三键与进度条的位置变了，场景里的点按坐标随之改为新中心（上一首 60 → 42、下一首 180 → 198、拖进度条 y 224 → 212）。
