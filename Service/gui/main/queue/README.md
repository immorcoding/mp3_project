# Main Queue Module

`queue/` 是 Main Screen 的私有 Queue 行 Module。SquareLine 只保留一份 `SongPanel1`
范本；本 Module 把 `GUI/screens/ui_Main.c` 里该范本的构造序列摘进 for 循环，按
`Service_GUI_QueueApply()` 的 `Length` 写入 `QueueTab`。导出的那一行隐藏，不参与
Flex 排布。

本 Module 不包含 `storage_listbuffer.h`，也不向 APP 公开 Interface。曲名由 GUI Task
把窗口 `Buffer` 原样传入；当前仍是路径。标题/歌手等 `load` 调解析器后，窗口载荷改成
曲名与歌手，见 `docs/catalog_architecture.md` 第 5 节。`Length` 为 0 则没有可见行。
SquareLine 重新导出后若范本构造变了，对照更新本目录的 create 函数，不得手改 `GUI/`。

当前/非当前只改 Border Opa、曲名色、Long mode 和右侧符号 Opa。不改 Border Width，
以免文字漂移。右侧符号运行时为 `LV_SYMBOL_AUDIO`，非当前行用透明度隐藏并占位，不 HIDDEN。Queue 行不放封面。游标未落地
时第 0 行当当前曲。歌手 Label 写空串，直到窗口载荷带上解析器给出的歌手。

## 私有 Interface

- `service_gui_main_queue_prepare()`：仅由 `main/gui_service_main.c` 在 `ui_init()` 后、
  Background 准备前调用。只隐藏范本并打开 `QueueTab` 滚动。
- `service_gui_main_queue_apply()`：仅由 `Service_GUI_QueueApply()` 转发。按 `Length`
  补造或 Hidden 已有行。

## 运行时路径

~~~text
ui_init() 导出 SongPanel1 范本并隐藏
  -> GUI Task 看见 READY
  -> Service_GUI_QueueApply(titles, Length)
  -> for i in 0 .. Length-1：按需构造一行并填曲名
  -> Length 之后已构造行 Hidden
  -> 游标未落地：第 0 行当当前曲
~~~

## 配置与依赖

`gui_service_main_queue_config.h` 保存行数上限，属于本 Module 私有配置；
其他 Module 不得包含。Implementation 依赖 LVGL 与 `GUI/ui.h`，不依赖 Platform、
Canvas、FreeRTOS 或 APP。
