# Main Queue Module

`queue/` 是 Main Screen 的私有 Queue 行 Module。`view/` 只创建空的 `QueueTab`（竖向 Flex、
行距 2 px）；本 Module 的行工厂按 `Service_GUI_QueueApply()` 的 `Length` 在其中构造行。
窗口滑动时在已有 panel 上转 head 改字，不按整表无限 `create`。

本 Module 不包含 `storage_listbuffer.h`，也不向 APP 公开 Interface。曲名由 GUI Task
把窗口 `Buffer` 原样传入；当前仍是路径。标题/歌手等 `load` 调解析器后，窗口载荷改成
曲名与歌手，见 `docs/shape/storage.catalog.md` 第 5 节。`Length` 为 0 则没有可见行。

行结构：Wash 薄底、圆角 8 的 Panel（左侧 4 px Accent 边条，按下时提高底 Opa 并加 1 px Ink
Outline）→ 80% 宽文字组（曲名 montserrat_12、歌手 montserrat_10 Ink 半透明）+ 右侧
`LV_SYMBOL_AUDIO` 符号（montserrat_14）。颜色按角色引用 `theme/` 共享 style。

当前/非当前只改 Border Opa、曲名颜色角色（Accent ↔ Ink，经 `service_gui_theme_style_replace()`）、
Long mode 和右侧符号 Opa。不改 Border Width，以免文字漂移。点按 `CLICKED` 立刻只刷新这些样式，
并把窗内槽位换成播放列表下标后 `post` 为 `MUSIC_QUEUE_SELECT`；空窗丢掉未取走的 Queue 选曲。
非当前行的右侧符号用透明度隐藏并占位，不 HIDDEN。Queue 行不放封面。
正在播放行由 GUI Task 传入的 `current_index` 判定；无当前曲为 `SERVICE_GUI_QUEUE_NO_CURRENT`。
歌手 Label 写空串，直到窗口载荷带上解析器给出的歌手。

## 私有 Interface

- `service_gui_main_queue_prepare()`：仅由 `main/gui_service_main.c` 在界面创建后、
  Background 准备前调用。只打开 `QueueTab` 滚动。
- `service_gui_main_queue_scroll_lead()`：仅由 `Service_GUI_QueueScrollLead()` 转发。
  返回顶部已滚出的整行数。
- `service_gui_main_queue_apply()`：仅由 `Service_GUI_QueueApply()` 转发。按 `Length`
  补造或 Hidden 已有行；`window_index` 变化时转 head，并从当前 `scroll_y` 扣整行高度，
  不把列表吸回整页。`current_index` 决定哪一行是当前曲。

## 运行时路径

~~~text
view/ 创建空 QueueTab
  -> GUI Task 按 ScrollLead 改 Index 并 request
  -> 看见 READY
  -> Service_GUI_QueueApply(titles, Length, Index, current_index)
  -> 已有行转 head，不够的行由行工厂补造，按 Length 填曲名
  -> Length 之后已构造行 Hidden
  -> current_index 落在本窗则高亮该行
  -> Process() 内 CLICKED 只改行样式并 post 选曲
  -> 下一圈 Service_GUI_ConsumeInput()
~~~

## 配置与依赖

`gui_service_main_queue_config.h` 保存行数上限，属于本 Module 私有配置；
其他 Module 不得包含。Implementation 依赖 LVGL、`view/` 句柄与 `theme/` 共享 style，不依赖 Platform、
Canvas、FreeRTOS 或 APP。
