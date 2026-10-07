# Main GUI 运行时 Module

`main/` 是 Main Screen 的私有运行时编排 Module。它向 `Service/gui` 发布
`service_gui_main_prepare()`：在 `view/` 创建界面后，按稳定顺序准备 Main 的循环分页、
Queue、Transport、唱盘与 Background（Default 才做局部毛玻璃）；以及
`service_gui_main_apply_theme()`：外观切换时更新 Music 毛玻璃或薄层。APP 与其他 Service
不得包含或调用本目录中的任何头文件。

它不创建 Screen 或静态对象（属于 `view/`）。Queue 可见行由 `queue/` 在运行时构造；
阅读器与设置页等产品业务仍待各自职责成立后再拆子 Module。

## 子 Module 与资源所有权

~~~text
main/
├─ gui_service_main.c / .h      仅编排初始化顺序与外观更新的私有入口
├─ pager/                       Page 槽位、吸附、循环重排、分页指示器动画
├─ queue/                       按 QueueApply 的 Length 构造 Queue 可见行并滑窗复用
├─ transport/                   Now Playing 三键、进度条假 seek 与 PLAY/PAUSE 符号
├─ vinyl/                       把 Canvas 假唱盘第一帧绑到唱盘 Image，并按 playing 旋转
└─ background/                  壁纸模糊、局部裁剪；Solid 薄层
~~~

- `pager/` 不依赖 Canvas、Platform LCD 或壁纸图像；它只经句柄读取 Main 对象并维护
  三个既有 Page 的物理槽位与逻辑圆点状态。
- `queue/` 不依赖 Canvas 或分页状态；行工厂按 Length 在 `QueueTab` 中构造行。
  可见行由 `Service_GUI_QueueApply()` 按 Length 填入，窗口滑动时转 head，
  并从当前 `scroll_y` 扣整行高度；不包含 `storage_listbuffer.h`。
  Queue 滚动不是 Pager，不要等 `SCROLL_END` 吸附。
- `transport/` 不依赖 Canvas 或 Queue 行；绑三键与进度条松手，并换播放符号 / 写回百分比。
- `vinyl/` 依赖 `canvas/`；合成假唱盘后绑到唱盘 Image。`service_gui_main_vinyl_apply(playing, reset_angle)`
  由 `Service_GUI_VinylApply()` 转发：playing 起转/续转、paused 停在当前角度、reset_angle 归零；
  同一 Image 至多一条 lv_anim。
- `background/` 不维护当前页、吸附阈值、循环映射、圆点动画或 Queue 行；它长期持有页面级
  SDRAM 图像缓冲，并在 MainPageContainer 滚动时更新 MusicModeTabs 的局部背景。仅 Default
  才会填充这些缓冲，Solid 改用半透明 Wash 薄层。
- 子 Module 可以向同一个 `MainPageContainer` 注册不同 LVGL 事件，但不共享私有状态：
  Pager 处理 `LV_EVENT_SCROLL_END`，Background 处理 `LV_EVENT_SCROLL`。

## 编译期依赖

- 各子 Module 的 Implementation 经 `view/gui_service_view.h` 句柄访问对象；
- `background/` 依赖 `canvas/`、`theme/` 和 `Platform/lcd` 的公开 Interface；
- `pager/` 只依赖 LVGL 与 `view/`；
- `queue/` 依赖 LVGL、`view/` 与 `theme/` 共享 style，不得包含 APP 头；
- `transport/` 只依赖 LVGL、`view/` 与 GUI 输入单槽，不得包含 APP 头；
- `vinyl/` 依赖 `canvas/`、LVGL 与 `view/`，不得包含 APP 头；
- 根 `gui_service_main.c` 只依赖五个子 Module 的私有 Interface，不直接访问 UI 对象或
  离屏缓冲。

## 运行时路径

~~~text
GUI Task
  -> Service_GUI_Init()
  -> view/ 创建界面，theme/ 应用 Screen 外观
  -> service_gui_main_prepare(clear_wallpaper)
       -> pager/service_gui_main_pager_prepare()
            -> 解析布局、绑定三页、无动画居中 Music
            -> 注册吸附、循环重排和圆点动画
       -> queue/service_gui_main_queue_prepare()
            -> 打开 QueueTab 滚动
       -> transport/service_gui_main_transport_prepare()
            -> 绑定上一首/播放暂停/下一首 CLICKED，以及进度条 RELEASED
       -> vinyl/service_gui_main_vinyl_prepare()（角度 0、不起转）
            -> Canvas 合成假唱盘写入独立缓冲，绑到唱盘 Image
       -> background/service_gui_main_background_prepare(clear_wallpaper)
            -> 注册滚动同步
            -> Default：生成长期模糊壁纸、裁剪并绑定；Solid：半透明 Wash 薄层
GUI Task 循环
  -> Service_GUI_ConsumeInput()
  -> gui_music_step() / 命令与窗口协议
  -> Service_GUI_Process()
Service_GUI_ThemeApply()
  -> service_gui_main_apply_theme() -> background 按新外观更新
Service_GUI_VinylApply(playing, reset_angle)
  -> vinyl/service_gui_main_vinyl_apply() -> 删旧动画 → 按需归零 → playing 时从当前角度起转
~~~

Pager 必须先完成初始居中；Background 随后读取的 MusicModeTabs 坐标才是实际显示位置。
此顺序由根入口封装，调用者不得绕过。
