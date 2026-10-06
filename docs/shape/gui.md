# GUI

界面所有权、交互边界与视觉效果生命周期。

Next id: GUI-16

## Pillars

- 对象树只有一个事实源，行为通过句柄协作。
- 视觉连续性与资源生命周期共同决定交互方案。
- PC 像素等价不替代板级触摸与性能验收。

## Open questions

- Library、Album Detail/Mini Player、Books/Reader、Settings 子页何时进入实施 spec？现有占位不代表已实现；阅读横滑与 Main 手势、阅读字号与系统字号须分开决定。
- 可换壁纸、按内容版本的 Blur 缓存及 Settings 合成背景何时实施？先确认缓存键、失效、峰值内存和真机耗时，不把当前 Alpha 原型视为 RGB565 持久缓存。
- 唱盘旋转、ID3 封面、真实时间和音频进度何时接入？先确认 playing/paused/CLEAR 行为；当前假状态不能充当解码进度。
- 安全锁、背光与唤醒何时设计？Lock 当前仅视觉锁屏，真实亮度需 Platform LCD/PWM 能力。
- 何时升级 LVGL 9 并评估 XML 编辑器？随 Vendor 升级再决定，ADR-0016 当前未采用。

## ownership

对象、设计变更与行为模块的职责。

### Rules

- **GUI-1** · provisional · 全部 Screen 在 `Service/gui/view/` 手写，不用图形化工具导出代码；boot/、main/*、theme/ 只经 `Service_GUI_ViewTypeDef` 句柄访问对象。_Why:_ 生成器与运行时补丁使同一界面出现多份事实源。_Source:_ [ADR-0016](../adr/0016-hand-written-gui-view-and-shared-theme-styles.md)
- **GUI-3** · provisional · 新增或改动界面前先更新本 area 的受影响设计约束；未决设计先列 Open questions 或 Proposed，再修改手写界面。_Why:_ 设计意图须先于实现明确，避免事后补记与重复长文。_Source:_ [ADR-0016](../adr/0016-hand-written-gui-view-and-shared-theme-styles.md)、[spec #15](https://github.com/immorcoding/mp3_project/issues/15)

### Rejected

- SquareLine 导出 GUI/ 作为事实源：主题、分页和事件仍需补丁；ADR-0007 已被 ADR-0016 取代。

## appearance

颜色来源、外观与系统背景。

### Rules

- **GUI-2** · provisional · 颜色只来自 theme/ 按颜色属性×调色板角色持有的共享 lv_style_t；外观切换更新共享颜色并 report_style_change，不用 color filter 或遍历对象改色；Screen 背景透明度与壁纸由 gui_service.c 编排。_Why:_ 角色共享使换色独立于对象树和 remove_style_all。_Source:_ [ADR-0016](../adr/0016-hand-written-gui-view-and-shared-theme-styles.md)
- **GUI-5** · provisional · Accent 表示活动、Ink 表示文字图标、Muted 表示低对比轨道、Wash 表示薄填充、Ground 表示页底；Opa 属于对象；Default 使用无业务内容的系统壁纸与 Music 玻璃，Solid 关闭壁纸和模糊。_Why:_ 语义角色与效果开关共同保持两套外观一致，壁纸不绑定页面布局。_Source:_ [主题实现](../../Service/gui/README.md)

### References

- [Default 基线](../../Tools/gui_simulator/scenarios/theme_default.expected)、[外观切换基线](../../Tools/gui_simulator/scenarios/theme_toggle.expected)：已批准画面的哈希，截图由同名场景重现后审阅。

### Rejected

- 两色渐变与运行时渐变抖动：真机色带或颗粒不满足雾状背景；保留静态壁纸，LV_DITHER_GRADIENT 为 0。

## navigation

普通页面结构与横向手势。

### Rules

- **GUI-6** · provisional · Main 是唯一普通根 Screen，固定状态栏和圆点共用；Settings/Music/Books 是三张内容页，MainPageContainer 自身是可命中的透明横滑视口。_Why:_ 页面切换不应移动系统信息或复制 Screen/Viewport。_Source:_ [GUI 模块](../../Service/gui/README.md)
- **GUI-7** · provisional · Pager 仅复用三页，初始 Music 居中；SCROLL_END 达半视口才切相邻一页，再轮换槽位无动画回中并隔离程序滚动重入；无惯性/弹性，圆点只在确认切页后按 view 基线过渡。_Why:_ 限定手势判定与物理重排避免重复翻页、跳帧和样式漂移。_Source:_ [分页基线](../../Tools/gui_simulator/scenarios/pager.expected)
- **GUI-8** · provisional · Music 模式仅顶部标签点击切换，内部 Content 透明且不滚动；横滑交 Main，纵滑由 Queue/Library 页承担，Slider/按钮保留原生命中；标签选中仅 Accent 文字与细底线。_Why:_ 内外横滑不能竞争，额外手势屏蔽层没有现有实测依据。_Source:_ [解锁与主页基线](../../Tools/gui_simulator/scenarios/unlock_main.expected)

## queue

窗口交接与播放列表行的稳定呈现。

### Rules

- **GUI-9** · provisional · Queue 只接 READY 窗口原文，行数按 Length 且不超过窗口容量；换窗回收 panel 并保留余像素，非首窗留回滑行；点击先改本窗再 post 下标，cursor 更新后 Apply；当前行须同 Catalog 代次，边宽/状态占位固定、仅换 Opa/角色/Long mode，封面属于 Now Playing。_Why:_ 防止整表泄漏、旧游标闪回、换窗跳动与文字漂移；GUI 不解析路径或依赖 storage_listbuffer.h。_Source:_ [Queue 基线](../../Tools/gui_simulator/scenarios/queue.expected)、[GUI 任务](../../APP/tasks/gui/README.md)

## transport

播放控制与唱盘呈现。

### Rules

- **GUI-10** · provisional · 时间、进度与三键仅在 Now Playing；transport 绑定 CLICKED/RELEASED，Task 消费输入；拖动不被 Apply 覆盖，松手假 seek 不改 playing，切歌/拔卡归零，假 playing 不按墙钟走表。_Why:_ 尚无真实解码时维持明确的输入/状态边界。_Source:_ [播放控制基线](../../Tools/gui_simulator/scenarios/music_transport.expected)
- **GUI-14** · provisional · 唱盘底图从 Pack 载入 SDRAM，由 vinyl 合成封面后绑定无事件 Image，图像尺寸与 view 一致；图标复用 LVGL 符号，图片不另编入内部 Flash。_Why:_ 区分资源加载、像素合成和输入，避免重复静态资源。_Source:_ [GUI 模块](../../Service/gui/README.md)、[资源 Service](../../Service/resource/README.md)

## effects

离屏缓冲所有权、启动次序与局部玻璃。

### Rules

- **GUI-11** · provisional · Canvas 隐藏对象独立于 Boot，静态大缓冲位于 SDRAM且单任务独占；输出仅下次处理前有效，长期消费者复制，Boot 直接引用期间禁止覆写；CPU/DMA 按实际读写方向交接 Cache。_Why:_ 短命 Screen、共享工作区与长期背景有不同寿命，大帧不能占小堆/DTCM。_Source:_ [GUI 模块](../../Service/gui/README.md)
- **GUI-12** · provisional · Default 初始化先准备 Main 布局并保存长期 Blur，再生成 Boot 共享输出，最后显式启动并重对 tick；BootReveal 的 SCREEN_LOADED 仅排 lv_async_call 后切 Lock，启动与切屏操作在 GUI Task；LCD 最终完成 ISR 内的 lv_disp_flush_ready 例外沿用模块契约。_Why:_ 先保存可避免覆写，异步交接避免嵌套切屏打断旧动画，显式启动不依赖已错过事件。_Source:_ [Boot 基线](../../Tools/gui_simulator/scenarios/boot_lock.expected)、[GUI 模块](../../Service/gui/README.md)
- **GUI-13** · provisional · Default 的整块 MusicModeTabs 按实际屏幕坐标裁剪长期 Blur，越界透明且保留圆角；横滑只裁剪，壁纸改变才全屏模糊；背景模块仅改图与 Opa，按钮不做玻璃，新页面自行持有输出。_Why:_ 保持背景连续、避免动画路径模糊与共享图被覆写。_Source:_ [Default 基线](../../Tools/gui_simulator/scenarios/theme_default.expected)

## lock

待机视觉层级与解锁提示。

### Rules

- **GUI-15** · provisional · Lock 根处理上滑；大时间、短日期与值一致的电量组形成上部层级，底部文字/Home indicator 同组低频透明度呼吸且不承接事件，不加阴影或额外 Gesture Bubble。_Why:_ 小屏保持单一手势锚点；既有触摸问题来自采样率，非布局容器。_Source:_ [Boot/Lock 基线](../../Tools/gui_simulator/scenarios/boot_lock.expected)

## regression

视觉变化的验收依据。

### Rules

- **GUI-4** · provisional · 模拟器帧哈希变化时先看截图、确认是有意的，才用 run-scenarios.ps1 -Update 重写基线，并在提交说明写明原因。_Why:_ 自动像素证据依赖基线不被随手覆盖。_Source:_ [ADR-0016](../adr/0016-hand-written-gui-view-and-shared-theme-styles.md)

### References

- [场景运行器](../../Tools/gui_simulator/README.md)：重现已批准的七场景/39帧并产出截图。
