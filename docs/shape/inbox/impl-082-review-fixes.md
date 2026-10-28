# Inbox: impl/082-review-fixes (0.8.2 集成分支审阅修复)

- 2026-10-28 · gui/music · cite · GUI-27 · 上一条 friction（字形范围手填）已解决：`service_gui_view_music_solve_layout()` 按字体行高、基线扫描参考字形（曲名、歌手 `Hdg`，时间为数字与 `:/-`）推出字形首行与高度。Noto 12 推出第 3 行起 13 行、Montserrat 10 第 1 行起 9 行，与原常量相同，Noto 下全部场景基线不变；回退 Montserrat 12 推出第 2 行起 13 行，曲名、歌手盒子下移 1 px，字形上沿回到设计位置。参考字符里 `d` 比 `H` 高 1 行，只用 `Hg` 会得到第 4 行起 12 行。
- 2026-10-28 · gui/ownership · friction · GUI-4 · 模拟器上电外观是 Solid（`SERVICE_GUI_THEME_STARTUP`），按 T 后才是 Default。`music_artist` 原先把按 T 后的帧命名为 `*_solid`，审阅据此误判缺了哪些外观；本分支已改名为 `*_default`。`music_title_cjk_short`（注释写“按 T 切到 Solid”、帧名 `cjk_short_solid`）同样命名反了，未改。
