# GUI · 现行界面设计

[GUI 规则](gui.md)规定界面与主题所有权。本参考记录当前视觉与交互，新增或修改前按 GUI-3 同步；特效与缓冲改动读[效果生命周期](gui.effects.md)，尚未实施的已确认意图读[后续设计](gui.planned.md)。静态数值以 [view](../../Service/gui/view/README.md) 为准，除明确约束外不逐像素维护第二份代码。

## 当前范围

LVGL 8.3.11，240 × 320 RGB565 竖屏；手写 view 与 [PC 模拟器](../../Tools/gui_simulator/README.md)共用界面。Queue 已接窗口与假切歌，Now Playing 三键和假进度已接线；尚无真实解码、ID3、时间推进、Library 浏览、背光/设置持久化、电子书或中文字库。当前时间、电量为假数据，Books/Settings/Library 是空白占位。

## 视觉角色

| 角色 | 用途 |
| --- | --- |
| Accent | 当前行、进度、选中 Tab、电池填充 |
| Ink | 文字、图标、弱轮廓；次级文字用同角色低 Opa |
| Muted | 非活动轨道，不用于小字号正文 |
| Wash | 按钮与 Queue 行薄填充、轨道细边 |
| Ground | Screen 纯色底 |

RGB 真值由 `theme/` 唯一维护；透明度、尺寸、圆角、Padding、字体属于对象。Default 是深蓝靛紫壁纸与青色强调、Music 局部毛玻璃；Solid 是近黑底与品红强调，关闭壁纸和模糊，只保留半透明 Wash。启动外观以 `SERVICE_GUI_THEME_STARTUP` 为准（当前 Solid）。运行时 Queue 行也引用角色，共享 style 的切换遵循 GUI-2。

Default 系统壁纸是 `Indigo Mist Soft Dark`，240 × 320，低对比蓝紫雾状光团，无文字、卡片或其他业务内容。量化以 RGB565 真机观感为准；当前保留少量 Alpha，转不透明版须经打包链并重新验证。两色渐变会产生色带，运行时渐变抖动也未达到目标；保持 `LV_DITHER_GRADIENT = 0`。壁纸不烘焙卡片，普通卡片用半透明底、弱边及轻阴影，不普遍引入模糊。

页面边距约 12 px、卡片间距约 8 px；卡片圆角约 12 px、小控件约 8 px。使用细线、圆形播放键、短进度条，强调色留给活动状态。英文短标签预留截断/省略空间，未来中文不能挤破布局。

## Lock

Lock 是独立待机视觉 Screen，不是认证边界。根对象的向上手势进入 Main；时间为上半部焦点，短日期（如 `SUN, AUG 24`）位于其下，电量组再下方。电量组水平居中，仅包围 Bar 与百分比，透明且不滚动；Bar 值与假百分比一致，不显示实时充电或告警。日期约 14–18 px、时间约 42–52 px、电量约 12–14 px，优先调整相对留白而非固定坐标。

电池使用 LVGL Bar 的圆角轮廓与容量填充，不加正极小突起。底部是 `Swipe up to unlock` 与短 Home indicator，同处透明 `LockUnlockGroup`；不承担事件，不做文字阴影/发光或位移闪烁。每次 SCREEN_LOADED 重启低频透明度呼吸。普通布局 Container 不需为手势额外加 Gesture Bubble；此前触摸问题来自采样率。Lock 尚无 Mini Player，保留中下部空间；不复用 Main 状态栏。

## Main 与分页

```text
Main（唯一普通根 Screen）
├─ StatusBarContainer（固定顶部）
├─ MainPageContainer（透明横滑视口）
│  ├─ SettingsPageContainer（占位）
│  ├─ MusicPageContainer
│  │  └─ MusicModeTabs [Playing] [Queue] [Library]
│  │     ├─ NowPlayingTab（唱片、时间、进度、三键）
│  │     ├─ QueueTab（窗口行）
│  │     └─ LibraryTab（占位）
│  └─ BooksPageContainer（占位）
└─ DotPanelContainer（固定底部）
```

根 Screen 不滚动、Padding 为零；三张 Page 共用唯一状态栏、圆点和系统壁纸，不创建页面级 Screen 或状态栏。MainPageContainer 本身就是视口，不套另一层 Viewport；它保留 Clickable 才能成为触摸命中对象。主体约占屏高 85%，位于固定顶部信息与底部圆点之间。

Pager 始终只持有三张页面实例，逻辑顺序 Settings / Music / Books，初始 Music 居中。手势结束按当前物理槽位的偏移判定，达到半个视口才切相邻一页，否则回原位；一次手势最多一页。到边槽后轮换既有 Page 到前/中/后槽，并同任务无动画回中，下一帧才显示稳定结果。程序吸附/回中产生的 SCROLL_END 由私有状态隔离，不能再次当手势。关闭 Momentum/Elastic；速度翻页需另行设计验证。

SCROLL 仅更新 Music 毛玻璃裁剪，翻页在 SCROLL_END 判定。Slider 使用原生命中/拖动，三键使用原生点击；已有真机行为不需额外全局手势屏蔽层。

圆点非活动是小圆，活动是横向胶囊；Pager 从 view 读取宽度与 Opa 基线，只动画这两项，不覆盖颜色/圆角/布局。确认切页才同步收缩/展开，未达阈值不切换；快速切页取消旧动画并从当前绘制值续接，吸附与圆点过渡同步。

## Music 模式与播放控制

MusicModeTabs 填满内容区，初始 Playing；顶部标签靠点击切换，选中态只有 Accent 文字与底部细线，其他是低 Opa Ink，按钮栏透明、不用整块高亮。内部 Content container 透明且关闭 Scrollable；Queue/Library 的纵向滚动由各 Tabpage 承担，横滑交给 Main。MusicPage 和各 Tab 背景透明，只有具有卡片语义的对象保留染色/圆角。

Now Playing 底部控制区依次放时间、进度条、上一首/播放暂停/下一首；Queue/Library 不复制控制条。时间仍是 `1:00/3:14` 占位。Slider 用 Muted 轨道、Wash 细边、Accent 进度；Knob 平时透明、按下显示，尺寸通过 Knob Padding 调整。圆形按钮用 Wash 低 Alpha、按下增亮，Ink 符号 Label 居中；用现有 Montserrat 的 PREV/PLAY/PAUSE/NEXT，不新增图片图标。按钮和控制区不是毛玻璃。

`main/transport` 绑定 CLICKED/RELEASED，GUI Task 的 `music/` 消费输入；拖动中 Apply 不覆盖 Slider，松手提交 0..100 假 seek 且不改 playing。切歌/拔卡归零，playing 不按墙钟走表。播放图标按假状态替换，真实时间和解码对接仍待实现。

唱盘 Image 位于控制区上方，当前 144 × 144；Resource ID 4 从 Pack 加载至 SDRAM，`main/vinyl/` 用 Canvas 复制底图、叠青色中心假封面，只显示第一帧。无 PNG 编入内部 Flash、无 ID3、无旋转、无事件；view 尺寸与 `SERVICE_GUI_MUSIC_VINYL_DIAMETER` 必须一致。底图格式见[资源加载](resources.loading.md)。

## Queue 窗口与行

Queue 展示播放列表的一段窗口，不接整表指针。view 只造空 QueueTab；`main/queue` 行工厂按 READY 的 Length 按需构造，最多 12 行，与 STORAGE_LISTBUFFER_MAX_ENTRIES 一致。Length 缩短则隐藏已有行，零长无可见行。回收 panel 时轮换 head 改字，不将 storage_listbuffer 改成环形数组。

滚动过程中 GUI Task 按 QueueScrollLead 更新 Index/request；Index=0 不留上一窗，其余留一行便于回滑。换窗从 scroll_y 扣整行高度，保留手指余下像素；不吸回整页，也不等 SCROLL_END 才请求。QueueTab 负责纵向 Flex 与滚动，不能打开 Tabview 内部 Content 的滚动。

行是 Wash 浅薄片（不是深色实心卡），含左侧固定比例 InfoContainer（曲名/歌手）与右侧状态符号。曲名锁单行：当前行 Accent + circular，其余 Ink + dot；歌手是低 Opa Ink。当前行左 Accent 边可见、右 LV_SYMBOL_AUDIO 可见，其他行仅将两者 Opa 置零。Border Width/Side 和右侧符号占位保持不变，避免文字漂移。按下提高底 Opa 并加 Ink Outline，不改 Border Width；Panel Clickable，子对象非 Clickable，无 Checkable。

CLICKED 先更新本窗样式再 post 播放列表下标；GUI Task 更新 cursor 后滑窗 Apply，避免闪回旧行。没有文件打开或解码。READY 的 Buffer[i] 原样传给 QueueApply，对应 Index+i；当前仍是曲库路径，歌手空串，GUI 不裁 Music/、后缀或拆文件名。未来元数据由窗口 load 路径提供。当前行须与有效播放游标且 Catalog 代次相符，否则无当前行。Service/gui 不包含 storage_listbuffer.h。Queue 不放封面，封面属于 Now Playing。

## 视觉验收

三页循环且固定 Shell 不动；Music 仅标签点击切模式、内容横滑交外层；Slider/三键默认和按下状态正确；唱盘首帧与假封面可见；占位页不呈现未实现功能。按 GUI-4 比较模拟器截图与既有基线，PC 等价不替代板级触摸/性能验收。
