# Now Playing 曲名

`title/` 是 Main Screen 的私有曲名 Module。`view/` 在唱盘与进度条之间创建曲名 Label（y=160、宽 `SERVICE_GUI_MUSIC_TITLE_WIDTH` 160 px、居中、theme TEXT × INK、文字底透明、滚动速度 `SERVICE_GUI_MUSIC_TITLE_SPEED` 24 px/s），本 Module 只写它的文本与长文本模式（GUI-21）。

调用方给什么就画什么：曲目显示名、空串（留空）或 `--`。名字 / 空白 / `--` 的语义与何时调用由 APP 音乐分区的曲名客户决定，本 Module 不包含 APP 或 Storage 头。

## 行为

- prepare 时写入初始 `--`，与“无当前曲”一致。
- 每次 Apply：文本与 Label 现有文本相同则不动（避免重启滚动）；否则用 Label 当前字体与字距量出像素宽度，宽于可见窗口设 `LV_LABEL_LONG_SCROLL_CIRCULAR`，否则 `LV_LABEL_LONG_CLIP`（静止、居中），再写文本。先设模式再写文本：切模式会删旧滚动动画并复位偏移，写文本后 LVGL 按新模式重建动画。
- 不换行、不截断、不加省略号；单行高度为字体行高 + 2 px。
- 循环滚动是常驻 LVGL 动画：长名显示期间动画链表不空，会移动其他动画（唱盘、状态行闪烁、翻页回弹）的采样相位（GUI-4）。
- 字体暂为 `lv_font_montserrat_12`（view 设置）；CJK 字体与回退在后续票接入。

## 私有 Interface

- `service_gui_main_title_prepare()`：仅由 `service_gui_main_prepare()` 在 Transport 之后、Vinyl 之前调用。
- `service_gui_main_title_apply(text)`：仅由 `Service_GUI_TitleApply()` 转发。

## 编译期依赖

LVGL、`view/gui_service_view.h` 句柄与 `gui_service_config.h`。
