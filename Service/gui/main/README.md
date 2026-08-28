# Main GUI 运行时视觉 Module

本 Module 只负责 Main Screen 的运行时视觉补充：将 SquareLine 未暴露的 Tabview 内部
Content container 的默认白底置为透明，并根据目标控件的真实布局从长期保存的全屏模糊壁纸
中裁剪出 MusicModeTabs 局部背景图。

它不创建、删除或修改 `GUI/` 的生成文件，也不维护播放状态、歌曲数据、原始触摸采样或任何
硬件资源。对象名称和层级由 SquareLine 导出；本 Module 只在 `ui_init()` 后读取其公开对象
指针。为显示局部毛玻璃，它会将 `ui_MusicModeTabs` 的运行时 Background image 绑定为自己
持有的裁剪帧；MainPager 横滑时，Module 从当前屏幕坐标对应的模糊壁纸区域重裁剪该帧，
使玻璃内容持续对应其下方背景，而非带着一张静态贴图移动。除此以外，只修改 SquareLine
无法访问的内部 Content container 的运行时 LVGL Style。它还会禁用 `MusicModeTabs` 内部
Content container 的手势滚动，使顶部 Tab Button 独占模式切换、`MainPager` 独占全局左右
翻页手势。除 `ui_MusicModeTabs` 的运行时
Background image 外，SquareLine 导出对象的背景、边框、阴影、圆角和文字样式始终由
SquareLine 决定。

## 编译期依赖

- `GUI/ui.h`：读取 SquareLine 导出的 Main、Tabview、Tabpage 和播放器 Button 对象；
- `canvas/` 私有 Interface：生成全屏模糊帧并执行带透明越界填充的局部裁剪；
- `Platform/lcd`：取得当前显示分辨率与 SDRAM 缓冲对齐要求；
- `lvgl.h`：布局、Tabview 内部对象与背景 Style Interface。

`gui_service_main_config.h` 保存全屏壁纸模糊半径以及 MusicModeTabs 局部背景的容量上限。
调整模糊半径会影响初始化耗时与毛玻璃观感；若在 SquareLine 中扩大 Tabview 尺寸导致超过
容量上限，初始化或后续滚动裁剪会安全返回 `SERVICE_INVALID_PARAM`，必须经审校后同步
调整该配置。

## 运行时路径

```text
GUI Task
  -> Service_GUI_Init()
  -> ui_init()
  -> service_gui_main_prepare_background()
       -> 读取 Main / Music 控件真实布局
       -> Canvas 生成全屏模糊工作帧
       -> 复制为 Main 长期全屏模糊壁纸
       -> Canvas 裁剪 MusicModeTabs 初始区域
       -> 绑定 MusicModeTabs 的 Background image
       -> 禁用 MusicModeTabs 内部 Content 的手势滚动
       -> 为 MainPager 内部 Content 注册 LV_EVENT_SCROLL

MainPager 内部 Content 滚动
  -> 读取 MusicModeTabs 当前屏幕坐标
  -> 从长期全屏模糊壁纸裁剪带透明越界填充的局部背景
  -> 失效 MusicModeTabs，交由 LVGL 重绘
```

`MusicModeTabs` 的内部 Content 不接受左右手势滚动，三个模式页仅由 SquareLine 导出的顶部
Tab Button 选择。该设置不影响后续 Queue、Library 在各自 Tabpage 内实现独立的竖向列表滚动。

全屏软件模糊只在初始化、未来的壁纸切换或相关布局变化后执行。`Service_GUI_Process()`
不做该处理；MainPager 滑动期间只复制当前局部区域，不再模糊整张图片。局部裁剪允许部分
越出壁纸边界，越界像素为透明，故横滑至屏幕边缘时仍能保持背景坐标正确。Settings、Books
将来各自提供背景与生命周期，不能复用 Music 的裁剪图或滚动事件。
