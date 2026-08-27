# Main GUI 运行时视觉 Module

本 Module 只负责 Main Screen 的运行时视觉补充：将 SquareLine 未暴露的 Tabview 内部
Content container 的默认白底置为透明，并将清晰壁纸、全屏模糊工作帧与目标控件的真实布局
区域合成为一张长期 Main 背景图。

它不创建、删除或修改 `GUI/` 的生成文件，也不维护播放状态、歌曲数据、触摸手势或任何
硬件资源。对象名称和层级由 SquareLine 导出；本 Module 只在 `ui_init()` 后读取其公开对象
指针。为显示合成结果，它会将 `ui_Main` 的运行时 Background image 绑定为自己持有的
合成帧；除此以外，只修改 SquareLine 无法访问的内部 Content container 的运行时 LVGL
Style。SquareLine 导出对象的背景、边框、阴影、圆角和文字样式始终由 SquareLine 决定。

## 编译期依赖

- `GUI/ui.h`：读取 SquareLine 导出的 Main、Tabview、Tabpage 和播放器 Button 对象；
- `canvas/` 私有 Interface：生成全屏模糊帧并执行局部区域合成；
- `Platform/lcd`：取得当前显示分辨率与 SDRAM 缓冲对齐要求；
- `lvgl.h`：布局、Tabview 内部对象与背景 Style Interface。

`gui_service_main_config.h` 保存局部毛玻璃的全屏壁纸模糊半径。调整该值会影响
`MusicModeTabs` 和三颗控制按钮的共同模糊观感与初始化耗时，但不会改变 Canvas 区域坐标。

## 运行时路径

```text
GUI Task
  -> Service_GUI_Init()
  -> ui_init()
  -> service_gui_main_prepare_background()
       -> 读取 Main / Music 控件真实布局
       -> Canvas 生成全屏模糊工作帧
       -> Canvas 合成 MusicModeTabs + 三颗圆形按钮
       -> 绑定 Main 根 Background image
```

合成背景只在初始化、未来的壁纸切换或相关布局变化后重建；`Service_GUI_Process()` 与
MainPager 滑动过程中不执行软件模糊。Settings、Books 将来各自提供区域列表与背景生命周期，
不得复用 Music 的静态区域描述。
