# Main Pager Module

`pager/` 是 Main Screen 的私有循环分页 Module。它在 `ui_init()` 后读取 SquareLine
公开的三张 Page 与三个分页指示器对象，维护“左 / 中 / 右”物理槽位、松手吸附、首尾循环
重排和底部圆点动画。

它不创建、复制或销毁 Page，不管理壁纸或 Canvas 缓冲，不覆盖 SquareLine 定义的颜色、圆角、
背景、布局或文字样式。SquareLine 是圆点静态视觉的唯一来源；Pager 只从 `DotSettings` 与
`DotMusic` 读取初始宽度、透明度，并在逻辑页变化时动画这些同类数值。

## 私有 Interface

- `service_gui_main_pager_prepare()`：仅由 `main/gui_service_main.c` 在 `ui_init()` 后调用。
  它解析 Main 布局、绑定初始槽位与圆点、无动画定位 MusicPage 到中间槽位，并注册
  `LV_EVENT_SCROLL_END`。

无其他 Module 可以访问 Pager 的物理槽位、滚动状态或动画状态。

## 循环机制

SquareLine 初始导出的 `Settings`、`Music`、`Books` 三张既有 Page 依次放于视口的
`0% / 100% / 200%`。准备完成后先滚动到 `100%`，故真机初始显示 Music。

每次手势结束，Pager 按当前物理槽位计算横向偏移；超过 50% Viewport 宽度才动画吸附到相邻
槽位，同一手势最多前进或后退一页。活动页到达左端或右端后，Pager 轮换三个对象指针、重新
写入 `0% / 100% / 200%`，并立即无动画回到中间。对象身份不变、页面不复制，因此视觉上连续
循环而资源占用固定。

Background Module 独立监听同一对象的 `LV_EVENT_SCROLL`，负责局部毛玻璃坐标更新；Pager
不调用它，也不依赖其图像状态。

## 配置与依赖

`gui_service_main_pager_config.h` 保存翻页阈值和圆点动画时长，属于本 Module 私有配置；
其他 Module 不得包含。Implementation 依赖 LVGL 与 `GUI/ui.h`，不依赖 Platform、Canvas 或
FreeRTOS。
