# GUI Theme Module

`theme/` 是 GUI Service 的私有外观 Module。它持有 Default/Solid 调色板、当前索引，以及一组
按角色共享的 LVGL 颜色 style。它不是状态机，不装电量或时间。

## 模型

- **角色**：`Accent`（进度、当前行、Tab 选中、电池填充）、`Ink`（主文字、浅轮廓、图标）、
  `Muted`（非活动轨道）、`Wash`（按钮、Queue 行等薄填充）、`Ground`（无壁纸外观的页底）。
- **共享 style**：每个（颜色属性 × 角色）一个只含一项颜色的 `lv_style_t`，属性为 text、bg、
  border、outline、arc，共 5 × 5 个。`view/` 与 Queue 行用 `service_gui_theme_style_add()`
  按角色挂到对象的 part/state 上；Opa、宽度等其他属性仍写在对象上。
- **切外观**：只刷新这些 style 的颜色并 `lv_obj_report_style_change(NULL)`，不扫对象改 style；
  同时设置各 Screen 根对象：有壁纸外观显示壁纸、打透明 Ground，Solid 反之。Music Tab 的毛玻璃
  或薄层由 `main/background` 负责。

## 私有 Interface

- `gui_service_theme.h`（不含 LVGL，主机测试覆盖）：`service_gui_theme_select(id)`、
  `service_gui_theme_get_current()`、`service_gui_theme_role_color(role)`、
  `service_gui_theme_uses_wallpaper()`、`service_gui_theme_uses_glass()`。
- `gui_service_theme_style.h`（LVGL）：
  - `service_gui_theme_style_init()`：以当前外观初始化共享 style；必须先于 `view/` 创建对象。
  - `service_gui_theme_style_add(obj, prop, role, selector)`：给对象挂角色颜色。
  - `service_gui_theme_style_replace(obj, prop, role, selector)`：运行时换角色（例如 Queue 当前行曲名 Accent ↔ Ink）。
  - `service_gui_theme_style_apply(view)`：刷新颜色并设置 Screen 壁纸/Ground；Boot 已删除时跳过。

公开 `Service_GUI_ThemeApply(id)` 声明在 `gui_service.h`，由 `gui_service.c` 编排：
`select` → `style_apply` → `service_gui_main_apply_theme()`。

## 运行时路径

~~~text
Service_GUI_Init
  -> service_gui_theme_style_init()      当前外观（STARTUP = Solid）
  -> service_gui_view_create()           对象按角色引用共享 style
  -> service_gui_theme_style_apply(view) Screen 壁纸/Ground
  -> service_gui_main_prepare            background 按外观设置毛玻璃或薄层
Service_GUI_ThemeApply(id)
  -> service_gui_theme_select(id)
  -> service_gui_theme_style_apply(view)
  -> service_gui_main_apply_theme()      首次切到 Default 时按需生成模糊壁纸
~~~

## 配置与依赖

`gui_service_theme_config.h` 保存 Default/Solid 各角色 RGB 和 Solid Tab Wash Opa。
`gui_service_theme.c` 不包含 LVGL；`gui_service_theme_style.c` 依赖 LVGL 与 `view/` 句柄类型。
不得包含 `storage_listbuffer.h`。新增角色或颜色属性时同步枚举、调色板表与主机测试。
