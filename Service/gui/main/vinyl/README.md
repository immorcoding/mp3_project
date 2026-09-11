# Now Playing 唱盘

`vinyl/` 是 Main Screen 的私有 Now Playing 唱盘 Module。它在 `ui_init()` 之后把 Canvas 合成的唱盘第一帧绑到 `MusicPlayerVinylImage`。

不旋转。不包含 `storage_playback_cursor.h`。不在 SquareLine 里添加事件。底图来自 Resource Pack ID 4 的 SDRAM 槽；Canvas 复制后再叠假封面。不把唱盘 C 数组编进内部 Flash。

## 私有 Interface

- `service_gui_main_vinyl_prepare()`：仅由 `service_gui_main_prepare()` 在 Transport 之后、Background 之前调用。
