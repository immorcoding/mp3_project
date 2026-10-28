# Now Playing 曲名与歌手

`title/` 是 Main Screen 的私有曲名与歌手 Module。`view/` 在唱盘与进度条之间上下创建曲名 Label 与歌手 Label（都宽 `SERVICE_GUI_MUSIC_TITLE_WIDTH` 192 px、高为字体行高、文本居中、theme TEXT × INK、文字底透明；曲名 Opa 不透明、滚动速度 `SERVICE_GUI_MUSIC_TITLE_SPEED` 24 px/s，歌手 Opa `SERVICE_GUI_MUSIC_ARTIST_OPA` 180、`LV_LABEL_LONG_DOT`；位置按 GUI-27 由 view 推出），本 Module 只写两者的文本与曲名的长文本模式（GUI-26）。

调用方给什么就画什么：曲名是曲目显示名、空串（留空）或 `--`；歌手是调用方给出的文本（含 `Unknown Artist` 占位）、空串或 `NULL`（留空）。这些语义与何时调用由 APP 音乐分区的曲名客户决定，本 Module 不包含 APP 或 Storage 头。

## 行为

- prepare 时曲名写入初始 `--`、歌手为空，与“无当前曲”一致。
- 每次 Apply 同一次写两列；`text` 为 NULL 时两列都不改。歌手文本与 Label 现有文本相同则不重写，超宽由 `LV_LABEL_LONG_DOT` 截断加省略号。
- 曲名：文本与 Label 现有文本相同则不动（避免重启滚动）；否则用 Label 当前字体与字距量出像素宽度，宽于可见窗口设 `LV_LABEL_LONG_SCROLL_CIRCULAR`，否则 `LV_LABEL_LONG_CLIP`（静止、居中），再写文本。先设模式再写文本：切模式会删旧滚动动画并复位偏移，写文本后 LVGL 按新模式重建动画。
- 曲名不换行、不截断、不加省略号；两行 Label 高都等于字体行高，位置按完整字形固定、不随文本移动（GUI-27）。
- 循环滚动是常驻 LVGL 动画：长名显示期间动画链表不空，会移动其他动画（唱盘、状态行闪烁、翻页回弹）的采样相位（GUI-4）。
- 两行字体由 view 按 `font/` 的结果设置：资源包 FONT 解码成功为 Noto Sans CJK SC 12 px，否则 `lv_font_montserrat_12`；宽度按 Label 当前字体量，因此两种字体下滚动判定都正确。缺字画占位框，本 Module 不感知（GUI-24）。

## 私有 Interface

- `service_gui_main_title_prepare()`：仅由 `service_gui_main_prepare()` 在 Transport 之后、Vinyl 之前调用。
- `service_gui_main_title_apply(text, artist)`：仅由 `Service_GUI_TitleApply()` 转发。

## 编译期依赖

LVGL、`view/gui_service_view.h` 句柄与 `gui_service_config.h`。
