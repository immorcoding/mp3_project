# Main GUI 运行时视觉 Module

本 Module 只负责 Main Screen 的运行时视觉补充：将 SquareLine 未暴露的 Tabview 内部
Content container 的默认白底置为透明，并根据目标控件的真实布局从全屏模糊工作帧中裁剪出
一张长期 MusicModeTabs 局部背景图。

它不创建、删除或修改 `GUI/` 的生成文件，也不维护播放状态、歌曲数据、触摸手势或任何
硬件资源。对象名称和层级由 SquareLine 导出；本 Module 只在 `ui_init()` 后读取其公开对象
指针。为显示局部毛玻璃，它会将 `ui_MusicModeTabs` 的运行时 Background image 绑定为自己
持有的裁剪帧；该帧会随对象与 MainPager 一起移动。除此以外，只修改 SquareLine 无法访问的内部 Content container 的运行时 LVGL
Style。除 `ui_MusicModeTabs` 的运行时 Background image 外，SquareLine 导出对象的背景、
边框、阴影、圆角和文字样式始终由 SquareLine 决定。

## 编译期依赖

- `GUI/ui.h`：读取 SquareLine 导出的 Main、Tabview、Tabpage 和播放器 Button 对象；
- `canvas/` 私有 Interface：生成全屏模糊帧并执行局部区域合成；
- `Platform/lcd`：取得当前显示分辨率与 SDRAM 缓冲对齐要求；
- `lvgl.h`：布局、Tabview 内部对象与背景 Style Interface。

`gui_service_main_config.h` 保存全屏壁纸模糊半径以及 MusicModeTabs 局部背景的容量上限。
调整模糊半径会影响初始化耗时与毛玻璃观感；若在 SquareLine 中扩大 Tabview 尺寸导致超过
容量上限，初始化会安全返回 `SERVICE_INVALID_PARAM`，必须经审校后同步调整该配置。

## 运行时路径

```text
GUI Task
  -> Service_GUI_Init()
  -> ui_init()
  -> service_gui_main_prepare_background()
       -> 读取 Main / Music 控件真实布局
       -> Canvas 生成全屏模糊工作帧
       -> Canvas 裁剪 MusicModeTabs 区域
       -> 绑定 MusicModeTabs 的 Background image
```

裁剪背景只在初始化、未来的壁纸切换或相关布局变化后重建；`Service_GUI_Process()` 与
MainPager 滑动过程中不执行软件模糊。裁剪图作为 MusicModeTabs 自身 Background image 会随
对象自动移动。Settings、Books 将来各自提供背景与生命周期，不能复用 Music 的裁剪图。
