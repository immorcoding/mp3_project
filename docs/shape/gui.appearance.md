# GUI · appearance

颜色来源、外观与系统背景，离屏缓冲所有权、启动次序与局部玻璃。

[返回 GUI](gui.md)

### Rules

- **GUI-2** · provisional · 颜色只来自 theme/ 按颜色属性×调色板角色持有的共享 lv_style_t；外观切换更新共享颜色并 report_style_change，不用 color filter 或遍历对象改色；Screen 背景透明度与壁纸由 gui_service.c 编排。_Why:_ 角色共享使换色独立于对象树和 remove_style_all。_Source:_ [ADR-0016](../adr/0016-hand-written-gui-view-and-shared-theme-styles.md)
- **GUI-5** · provisional · Accent 表示活动、Ink 表示文字图标、Muted 表示低对比轨道、Wash 表示薄填充、Ground 表示页底；Opa 属于对象；Default 使用无业务内容的系统壁纸与 Music 玻璃，Solid 关闭壁纸和模糊。_Why:_ 语义角色与效果开关共同保持两套外观一致，壁纸不绑定页面布局。_Source:_ [主题实现](../../Service/gui/README.md)
- **GUI-11** · provisional · Canvas 隐藏对象独立于 Boot，静态大缓冲位于 SDRAM且单任务独占；输出仅下次处理前有效，长期消费者复制，Boot 直接引用期间禁止覆写；CPU/DMA 按实际读写方向交接 Cache。_Why:_ 短命 Screen、共享工作区与长期背景有不同寿命，大帧不能占小堆/DTCM。_Source:_ [GUI 模块](../../Service/gui/README.md)
- **GUI-12** · provisional · Default 初始化先准备 Main 布局并保存长期 Blur，再生成 Boot 共享输出，最后显式启动并重对 tick；BootReveal 的 SCREEN_LOADED 仅排 lv_async_call 后切 Lock，启动与切屏操作在 GUI Task；LCD 最终完成 ISR 内的 lv_disp_flush_ready 例外沿用模块契约。_Why:_ 先保存可避免覆写，异步交接避免嵌套切屏打断旧动画，显式启动不依赖已错过事件。_Source:_ [Boot 基线](../../Tools/gui_simulator/scenarios/boot_lock.expected)、[GUI 模块](../../Service/gui/README.md)
- **GUI-13** · provisional · Default 的整块 MusicModeTabs 按实际屏幕坐标裁剪长期 Blur，越界透明且保留圆角；横滑只裁剪，壁纸改变才全屏模糊；背景模块仅改图与 Opa，按钮不做玻璃，新页面自行持有输出。_Why:_ 保持背景连续、避免动画路径模糊与共享图被覆写。_Source:_ [Default 基线](../../Tools/gui_simulator/scenarios/theme_default.expected)

### References

- [Default 基线](../../Tools/gui_simulator/scenarios/theme_default.expected)、[外观切换基线](../../Tools/gui_simulator/scenarios/theme_toggle.expected)：已批准画面的哈希，截图由同名场景重现后审阅。

### Rejected

- 两色渐变与运行时渐变抖动：真机色带或颗粒不满足雾状背景；保留静态壁纸，LV_DITHER_GRADIENT 为 0。
