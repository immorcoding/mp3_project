# GUI View Module

`view/` 是 GUI Service 的私有界面层：手写创建全部 Screen 的对象树与静态样式，并把行为 Module 需要的对象发布为句柄。它取代了原 SquareLine 导出的 `GUI/`（ADR-0016）。APP、其他 Service 不得包含本目录的头文件。

## 职责

- 创建 Boot、BootReveal、Lock、Main 四个 Screen 并加载 Boot；创建前装上 LVGL basic theme 作为默认样式基线。
- 静态样式（尺寸、对齐、Flex、圆角、Padding、Opa、字体）直接写在对象上；**颜色只按调色板角色引用** `theme/` 的共享 style（`service_gui_theme_style_add()`），不写死 RGB。
- 发布 `Service_GUI_ViewTypeDef` 句柄（`service_gui_view_get()`）；Boot 切走后被 LVGL 删除，句柄回到 NULL。
- 屏幕自身的局部交互可以留在本层：Lock 的上滑解锁淡出到 Main、解锁提示呼吸动画。
- 默认壁纸：`gui_service_view_wallpaper.c` 是描述符；`gui_service_view_wallpaper_region.c` 在固件 SDRAM 资源区预留像素（沿用原输入段名，链接脚本不变），由 Service/resource 从资源包装入。模拟器不编译 region 文件，由替身提供像素。

不负责：跨屏时序（boot/）、分页吸附与圆点动画（main/pager）、Queue 行（main/queue）、事件到命令的转换（main/transport、main/queue）、毛玻璃（main/background）、外观切换（theme/、`gui_service.c`）。

## 文件

| 文件 | 内容 |
| --- | --- |
| `gui_service_view.c / .h` | 句柄类型、创建顺序（basic theme → Boot/BootReveal → Main → Lock）、壁纸入口 |
| `gui_service_view_screens.h` | 各 Screen 构建函数，仅本目录内部使用 |
| `gui_service_view_boot.c` | Boot（壁纸根背景、启动环 Arc、LOADING）与 BootReveal（清晰壁纸） |
| `gui_service_view_lock.c` | Lock：时间、日期、电量、解锁提示与 Home 指示条；上滑解锁与呼吸提示 |
| `gui_service_view_main.c` | Main 外壳：状态栏 5%、横向分页视口 85%（Settings 0% / Music 100% / Books 200%）、圆点 10% |
| `gui_service_view_music.c` | Music 标签视图（20 px 标签栏）：Now Playing 唱盘与控制区、Queue 竖向 Flex 容器、Library 空页；Tabview 内部 Content 透明且不可滚动 |
| `gui_service_view_wallpaper*.{c,h}` | 默认壁纸尺寸、描述符与固件像素区 |

视觉规范与页面层级以 [docs/gui_ui_design.md](../../../docs/gui_ui_design.md) 为准；界面每推进一步先更新该文档。

## 修改流程

1. 先在 `docs/gui_ui_design.md` 记下要改的层级、相对位置或样式。
2. 改本目录代码；颜色用角色，不加新的 RGB 字面量；新增需要被行为 Module 访问的对象时加到句柄结构体。
3. 跑 `./Tools/gui_simulator/run-scenarios.ps1`。哈希变化时打开 `build/gui_simulator/shots/<场景>/` 的截图确认，确属有意再 `-Update` 并在提交说明写明原因；必要时新增场景覆盖新界面。
4. 编译固件并上板确认（`hw:pending`）。
