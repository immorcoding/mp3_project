# Main GUI 运行时 Module

`main/` 是 Main Screen 的私有运行时编排 Module。它只向 `Service/gui` 发布
`service_gui_main_prepare()`：在 SquareLine 的 `ui_init()` 完成后，按稳定顺序准备
Main 的循环分页与局部毛玻璃。APP、其他 Service 和 `GUI/` 生成代码不得包含或调用
本目录中的任何头文件。

它不创建、删除或修改 SquareLine 导出的文件。Queue 可见行由 `queue/` 在运行时从
范本复制；阅读器与设置页等产品业务仍待各自职责成立后再拆子 Module。

## 子 Module 与资源所有权

~~~text
main/
├─ gui_service_main.c / .h      仅编排初始化顺序的私有入口
├─ pager/                       Page 槽位、吸附、循环重排、分页指示器动画
├─ queue/                       按 QueueApply 的 Length 用范本构造 Queue 可见行
└─ background/                  壁纸模糊、局部裁剪、Tabview 内部 Content 兼容
~~~

- `pager/` 不依赖 Canvas、Platform LCD 或壁纸图像；它只读取 SquareLine 公开对象并维护
  三个既有 Page 的物理槽位与逻辑圆点状态。
- `queue/` 不依赖 Canvas 或分页状态；它把 `SongPanel1` 范本构造摘进 for 循环。
  可见行由 `Service_GUI_QueueApply()` 按 Length 填入，不包含 `storage_listbuffer.h`。
- `background/` 不维护当前页、吸附阈值、循环映射、圆点动画或 Queue 行；它长期持有页面级
  SDRAM 图像缓冲，并在 MainPageContainer 滚动时更新 MusicModeTabs 的局部背景。
- 子 Module 可以向同一个 `MainPageContainer` 注册不同 LVGL 事件，但不共享私有状态：
  Pager 处理 `LV_EVENT_SCROLL_END`，Background 处理 `LV_EVENT_SCROLL`。

## 编译期依赖

- `GUI/ui.h` 只由具体子 Module 的 Implementation 包含，用于读取 SquareLine 导出的对象；
- `background/` 依赖 `canvas/` 和 `Platform/lcd` 的公开 Interface；
- `pager/` 只依赖 LVGL 与 `GUI/ui.h`；
- `queue/` 只依赖 LVGL 与 `GUI/ui.h`，不得包含 APP 头；
- 根 `gui_service_main.c` 只依赖三个子 Module 的私有 Interface，不直接访问 UI 对象或
  离屏缓冲。

## 运行时路径

~~~text
GUI Task
  -> Service_GUI_Init()
  -> ui_init()
  -> service_gui_main_prepare(clear_wallpaper)
       -> pager/service_gui_main_pager_prepare()
            -> 解析布局、绑定三页、无动画居中 Music
            -> 注册吸附、循环重排和圆点动画
       -> queue/service_gui_main_queue_prepare()
            -> 隐藏 SongPanel1 范本，打开 QueueTab 滚动
       -> background/service_gui_main_background_prepare(clear_wallpaper)
            -> 使 Tabview 内部 Content 透明并禁用其横滑
            -> Canvas 生成全屏模糊工作帧
            -> 复制为 Main 长期模糊壁纸
            -> 裁剪 MusicModeTabs 首帧并注册滚动同步
GUI Task 循环
  -> storage_listbuffer_request() / 看见 READY
  -> Service_GUI_QueueApply(titles, Length)
  -> 写回 IDLE
  -> Service_GUI_Process()
~~~

Pager 必须先完成初始居中；Background 随后读取的 MusicModeTabs 坐标才是实际显示位置。
此顺序由根入口封装，调用者不得绕过。
