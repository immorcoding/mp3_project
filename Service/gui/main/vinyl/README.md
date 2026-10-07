# Now Playing 唱盘

`vinyl/` 是 Main Screen 的私有 Now Playing 唱盘 Module。它在界面创建后把 Canvas 合成的唱盘第一帧绑到唱盘 Image（`view/` 句柄 `music.vinyl_image`），并按 GUI Task 给出的 playing 让唱盘绕圆心顺时针匀速旋转。

不包含 `storage_playback_cursor.h`，不读游标、不按墙钟走表。唱盘 Image 不带事件。底图来自 Resource Pack ID 4 的 SDRAM 槽；Canvas 复制后再叠假封面。不把唱盘 C 数组编进内部 Flash。

## 旋转

- 角度只存在 lv_img 对象里（`lv_img_get_angle()`），动画以该 Image 为 var、`exec_cb` 固定，所以同一 Image 任一时刻至多一条 lv_anim；`apply` 先 `lv_anim_del` 再决定是否起转，反复 play/pause 不叠加。
- playing：从当前角度到当前角度 + 3600（0.1°），线性、`SERVICE_GUI_MAIN_VINYL_REVOLUTION_MS`（6 s）一轮、无限重复，跨轮角度连续。
- paused：删动画即停在当前角度；恢复从该角度续转。
- `reset_angle`：切歌或清空时先把角度归零，再按 playing 决定是否起转。
- 角度 0 时 LVGL 不走变换路径，第一帧像素与不旋转时相同；Main Screen 不销毁，Image 句柄长期有效，不需要析构。旋转为软件变换（`LV_DRAW_COMPLEX`），真机帧率待上板。

## 私有 Interface

- `service_gui_main_vinyl_prepare()`：仅由 `service_gui_main_prepare()` 在 Transport 之后、Background 之前调用；绑第一帧、pivot 置圆心、角度 0、不起转。
- `service_gui_main_vinyl_apply(playing, reset_angle)`：仅由 `Service_GUI_VinylApply()` 转发。
- `gui_service_main_vinyl_config.h`：一圈毫秒数与 LVGL 一圈角度单位。
