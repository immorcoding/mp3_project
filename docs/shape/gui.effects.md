# GUI · effects

离屏缓冲所有权、启动次序与局部玻璃。

[返回 GUI](gui.md)

### Rules

- **GUI-11** · provisional · Canvas 隐藏对象独立于 Boot，静态大缓冲位于 SDRAM且单任务独占；输出仅下次处理前有效，长期消费者复制，Boot 直接引用期间禁止覆写；CPU/DMA 按实际读写方向交接 Cache。_Why:_ 短命 Screen、共享工作区与长期背景有不同寿命，大帧不能占小堆/DTCM。_Source:_ [GUI 模块](../../Service/gui/README.md)
- **GUI-12** · provisional · Default 初始化先准备 Main 布局并保存长期 Blur，再生成 Boot 共享输出，最后显式启动并重对 tick；BootReveal 的 SCREEN_LOADED 仅排 lv_async_call 后切 Lock，启动与切屏操作在 GUI Task；LCD 最终完成 ISR 内的 lv_disp_flush_ready 例外沿用模块契约。_Why:_ 先保存可避免覆写，异步交接避免嵌套切屏打断旧动画，显式启动不依赖已错过事件。_Source:_ [Boot 基线](../../Tools/gui_simulator/scenarios/boot_lock.expected)、[GUI 模块](../../Service/gui/README.md)
- **GUI-13** · provisional · Default 的整块 MusicModeTabs 按实际屏幕坐标裁剪长期 Blur，越界透明且保留圆角；横滑只裁剪，壁纸改变才全屏模糊；背景模块仅改图与 Opa，按钮不做玻璃，新页面自行持有输出。_Why:_ 保持背景连续、避免动画路径模糊与共享图被覆写。_Source:_ [Default 基线](../../Tools/gui_simulator/scenarios/theme_default.expected)
