# Main Background Module

`background/` 是 Main Screen 的私有局部毛玻璃 Module。它让 SquareLine 未暴露的
`MusicModeTabs` 内部 Content container 透明，禁用其内容区横滑；并为该 Tabview 建立一张
长期全屏模糊壁纸和一张可重复覆写的局部裁剪图。

它不管理 Main 的分页槽位、松手吸附、循环顺序、圆点动画、歌曲状态或播放器交互。它不修改
`GUI/` 生成文件；除 Tabview 的 LVGL 内部 Content container 外，不覆盖 SquareLine 导出对象的
背景、边框、阴影、圆角或文字样式。唯一运行时替换的导出 Style 是 `ui_MusicModeTabs` 的
Background image，因为该图像由本 Module 动态裁剪生成。

## 私有 Interface

- `service_gui_main_background_prepare(clear_wallpaper)`：仅由 `main/gui_service_main.c` 调用。
  调用前 Pager 必须已经完成布局和初始回中。该函数创建全屏模糊副本、裁剪首帧背景、绑定图像，
  并向 MainPageContainer 注册 `LV_EVENT_SCROLL`。

## 资源与滚动路径

~~~text
clear_wallpaper
  -> Canvas 共享工作区：执行一次全屏软件模糊
  -> Background 的 SDRAM 全屏副本：长期保留，避免被 Boot 复用 Canvas 后覆盖
  -> Background 的 SDRAM 局部裁剪图：绑定 MusicModeTabs

MainPageContainer 的 LV_EVENT_SCROLL
  -> 读取 MusicModeTabs 当前屏幕坐标
  -> 相对 ui_Main 换算为壁纸坐标
  -> 从长期模糊壁纸重裁剪同一局部输出图
  -> 失效 MusicModeTabs，交由 LVGL 重绘
~~~

滚动回调只复制局部像素，不重复执行软件模糊。裁剪允许越出壁纸边界，并以透明像素填充，
因此 MusicPage 在横滑时仍采样其实际下方位置而不会越界读取。

## 配置与依赖

`gui_service_main_background_config.h` 保存模糊半径和局部裁剪缓冲容量，属于本 Module 私有配置；
其他 Module 不得包含。Implementation 依赖 `canvas/`、`Platform/lcd`、LVGL 与 `GUI/ui.h`。
该 Module 持有的页面级 SDRAM 缓冲不属于共享 Canvas 工作区。
