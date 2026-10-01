# Main Background Module

`background/` 是 Main Screen 的私有局部毛玻璃 Module。Default 外观下，它为 `MusicModeTabs`
建立一张长期全屏模糊壁纸和一张可重复覆写的局部裁剪图；Solid 外观下改用半透明 Wash 薄层。
Tabview 内部 Content 的透明与禁滚已由 `view/` 在创建时完成。

它不管理 Main 的分页槽位、松手吸附、循环顺序、圆点动画、歌曲状态或播放器交互。它不覆盖 `view/`
创建对象的边框、阴影、圆角或文字样式；只替换 `MusicModeTabs` 的 Background image 与背景 Opa，
因为该图像由本 Module 动态裁剪生成。

## 私有 Interface

- `service_gui_main_background_prepare(clear_wallpaper)`：仅由 `main/gui_service_main.c` 调用。
  调用前 Pager 必须已经完成布局和初始回中。记录壁纸、注册滚动同步，再调用 apply。
- `service_gui_main_background_apply()`：经 `service_gui_main_apply_theme()` 在外观切换时调用。
  Default：首次需要时生成全屏模糊副本，裁剪当前帧并绑定；Solid：清除背景图、设半透明 Wash。

## 资源与滚动路径

~~~text
clear_wallpaper
  -> Canvas 共享工作区：执行一次全屏软件模糊
  -> Background 的 SDRAM 全屏副本：长期保留，避免被 Boot 复用 Canvas 后覆盖
  -> Background 的 SDRAM 局部裁剪图：绑定 MusicModeTabs

MainPageContainer 的 LV_EVENT_SCROLL
  -> 读取 MusicModeTabs 当前屏幕坐标
  -> 相对 Main Screen 换算为壁纸坐标
  -> 从长期模糊壁纸重裁剪同一局部输出图
  -> 失效 MusicModeTabs，交由 LVGL 重绘
~~~

滚动回调只复制局部像素，不重复执行软件模糊。裁剪允许越出壁纸边界，并以透明像素填充，
因此 MusicPage 在横滑时仍采样其实际下方位置而不会越界读取。Solid 外观下滚动回调直接返回。

## 配置与依赖

`gui_service_main_background_config.h` 保存模糊半径和局部裁剪缓冲容量，属于本 Module 私有配置；
其他 Module 不得包含。Implementation 依赖 `canvas/`、`theme/`、`Platform/lcd`、LVGL 与 `view/` 句柄。
该 Module 持有的页面级 SDRAM 缓冲不属于共享 Canvas 工作区。
