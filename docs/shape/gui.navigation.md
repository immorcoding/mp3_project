# GUI · navigation

普通页面结构与横向手势。

[返回 GUI](gui.md)

### Rules

- **GUI-6** · provisional · Main 是唯一普通根 Screen，固定状态栏和圆点共用；Settings/Music/Books 是三张内容页，MainPageContainer 自身是可命中的透明横滑视口。_Why:_ 页面切换不应移动系统信息或复制 Screen/Viewport。_Source:_ [GUI 模块](../../Service/gui/README.md)
- **GUI-7** · provisional · Pager 仅复用三页，初始 Music 居中；SCROLL_END 达半视口才切相邻一页，再轮换槽位无动画回中并隔离程序滚动重入；无惯性/弹性，圆点只在确认切页后按 view 基线过渡。_Why:_ 限定手势判定与物理重排避免重复翻页、跳帧和样式漂移。_Source:_ [分页基线](../../Tools/gui_simulator/scenarios/pager.expected)
- **GUI-8** · provisional · Music 模式仅顶部标签点击切换，内部 Content 透明且不滚动；横滑交 Main，纵滑由 Queue/Library 页承担，Slider/按钮保留原生命中；标签选中仅 Accent 文字与细底线。_Why:_ 内外横滑不能竞争，额外手势屏蔽层没有现有实测依据。_Source:_ [解锁与主页基线](../../Tools/gui_simulator/scenarios/unlock_main.expected)
