# GUI Theme Module

`theme/` 是 GUI Service 的私有外观 Module。它持有 Default/Solid 调色板、当前索引，以及
LVGL `color_filter_dsc` 挂接。它不是状态机，不装电量或时间，也不调用 `ui_theme_set()`。

SquareLine 导出的是五个占位 hex；绘制时过滤器把它们换成当前表的 RGB，Opa 仍留在对象上。
`#FFFFFF` 不是占位槽。切外观只换当前表并 invalidate，不扫对象改 style。
SquareLine 常在 create 后 `remove_style_all`，会清掉 theme 刚挂的过滤器，因此 `ui_init()`、
Main 准备和 Queue 造行之后会再绑一遍。每个对象只挂一份共享 MAIN 过滤器；Slider/Bar/Arc
再加 INDICATOR/KNOB，Tab 按钮矩阵再加 ITEMS。不要给每个对象堆十几个 style 槽。

## 私有 Interface

- `service_gui_theme_select(id)` / `map_placeholder(hex)` / `uses_wallpaper()` /
  `uses_glass()`：无 LVGL，供主机测试与 Background 读取标志。
- `service_gui_theme_attach(disp)`：仅由 `gui_service.c` 在 `ui_init()` 之后调用。
  SquareLine 会 `lv_theme_basic_init` 覆盖 display theme，必须把过滤器 theme 重新挂上，
  parent 为 basic。已创建对象再走 `bind_screens`。

公开 `Service_GUI_ThemeApply(id)` 声明在 `gui_service.h`，实现在本目录；仅 GUI Task 调用。

## 运行时路径

~~~text
Service_GUI_Init
  -> ui_init()                     生成代码装上 basic theme
  -> service_gui_theme_attach(disp)  以 basic 为 parent 重新挂过滤器
  -> bind_screens()
  -> Service_GUI_ThemeApply(STARTUP)
       -> 换当前表
       -> Solid：Boot/Lock/Main 关壁纸、铺 Ground；Music Tab 半透明 Wash
       -> Default：Boot/Lock/Main 开壁纸；Tab 底保持透明
  -> service_gui_main_prepare
       -> background：仅 Default 才做全屏模糊与局部裁剪
~~~

Queue 运行时造行走 theme apply_cb；`remove_style_all` 之后仍要 `bind_tree`。行构造必须写死占位 hex，不得再调用
SquareLine themeable API。Boot Arc 动画本轮不改；Solid 下开机背景也铺 Ground。

## 配置与依赖

`gui_service_theme_config.h` 保存占位 hex、Default/Solid RGB 和 Solid Tab Wash Opa。调色板 `.c`
不包含 LVGL；`gui_service_theme_apply.c` 依赖 LVGL 与 `GUI/ui.h`。不得包含
`storage_listbuffer.h`。
