# Main Queue Module

`queue/` 是 Main Screen 的私有 Queue 行 Module。`view/` 只创建空的 `QueueTab`（竖向 Flex、
行距 2 px）；本 Module 的行工厂按 `Service_GUI_QueueApply()` 的 `Length` 在其中构造行。
窗口滑动时在已有 panel 上转 head 改字，不按整表无限 `create`。

本 Module 不包含 `storage_listbuffer.h`，也不向 APP 公开 Interface。曲名与歌手由 GUI Task
把窗口行的 {曲目显示名, 歌手} 原样传入（GUI-25），见 `docs/shape/storage.md#catalog`。
`Length` 为 0 则没有可见行。

行结构：Wash 薄底、圆角 8 的 Panel（左侧 4 px Accent 边条，按下时提高底 Opa 并加 1 px Ink
Outline）→ 80% 宽文字组（曲名、歌手 Ink Opa 180）+ 右侧 `LV_SYMBOL_AUDIO` 符号（montserrat_14，
整行垂直居中）。颜色按角色引用 `theme/` 共享 style。

曲名与歌手显示曲目元数据，都取 GUI 字体子模块 `service_gui_font_title()` 的字体（GUI-24）：
资源包 Noto Sans CJK SC 12 px，不可用时 Montserrat 12，缺字画占位框。曲名循环滚动（非当前行
省略号），歌手高度锁成一行、截断加省略号。行排布按 #147 方案 B：行高 = 2 × 字体行高 + 两行
间距 2 px + 上下内边距各 2 px + 上下边框各 4 px（LVGL 8.3 内容区按四边减边框宽，虽然只画左边条），
Noto 下为 46 px，曲名 Y 6、歌手 Y 24（从 Panel 上沿量起），一屏约 5 行；Montserrat 回退时随其行高变矮。
取值在 `gui_service_main_queue_config.h`。整行滚动步长取首行实际高度加 `QueueTab` 行距，不写死像素（GUI-16）。

歌手 Label 原样显示传入的文本；歌手为空串时的占位文案 `Unknown Artist` 由 GUI Task 的
`gui_music_queue_rows()` 备好（GUI-25，文案定义在 `APP/tasks/gui/music/gui_music_text.h`），本 Module 不替换。

当前/非当前只改 Border Opa、曲名颜色角色（Accent ↔ Ink，经 `service_gui_theme_style_replace()`）、
Long mode 和右侧符号 Opa。不改 Border Width，以免文字漂移。点按 `CLICKED` 立刻只刷新这些样式，
并把窗内槽位换成播放列表下标后 `post` 为 `MUSIC_QUEUE_SELECT`；空窗丢掉未取走的 Queue 选曲。
非当前行的右侧符号用透明度隐藏并占位，不 HIDDEN。Queue 行不放封面。
正在播放行由 GUI Task 传入的 `current_index` 判定；无当前曲为 `SERVICE_GUI_QUEUE_NO_CURRENT`。

## 私有 Interface

- `service_gui_main_queue_prepare()`：仅由 `main/gui_service_main.c` 在界面创建后、
  Background 准备前调用。只打开 `QueueTab` 滚动。
- `service_gui_main_queue_scroll_lead()`：仅由 `Service_GUI_QueueScrollLead()` 转发。
  返回顶部已滚出的整行数。
- `service_gui_main_queue_apply()`：仅由 `Service_GUI_QueueApply()` 转发。按 `titles[i]` / `artists[i]` 填曲名与歌手（`artists` 为 NULL 时歌手全空）；按 `Length`
  补造或 Hidden 已有行；`window_index` 变化时转 head，并从当前 `scroll_y` 扣整行高度，
  不把列表吸回整页。`current_index` 决定哪一行是当前曲。

## 运行时路径

~~~text
view/ 创建空 QueueTab
  -> GUI Task 按 ScrollLead 改 Index 并 request
  -> 看见 READY
  -> Service_GUI_QueueApply(titles, artists, Length, Index, current_index)
  -> 已有行转 head，不够的行由行工厂补造，按 Length 填曲名与歌手
  -> Length 之后已构造行 Hidden
  -> current_index 落在本窗则高亮该行
  -> Process() 内 CLICKED 只改行样式并 post 选曲
  -> 下一圈 Service_GUI_ConsumeInput()
~~~

## 配置与依赖

`gui_service_main_queue_config.h` 保存行数上限与行排布取值，属于本 Module 私有配置；
其他 Module 不得包含。Implementation 依赖 LVGL、`view/` 句柄、`theme/` 共享 style 与 `font/` 曲目文本字体，不依赖 Platform、
Canvas、FreeRTOS 或 APP。
