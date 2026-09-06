# GUI 原型设计

> 相关 ADR：[ADR-0007：GUI 运行时所有权与 SquareLine 生成边界](adr/0007-gui-runtime-and-squareline-boundary.md)、[ADR-0015：卷分工与资源安装](adr/0015-volume-roles-and-resource-install.md)

## 1. 目的与当前范围

本文定义便携式媒体播放器第一版 GUI 的视觉语言、页面层级和交互边界，作为 SquareLine Studio 原型的实施依据。目标是先验证 240 x 320 竖屏上的布局、切换和触摸体验；页面中的歌曲、专辑、书籍、时间、电量和亮度均可使用固定假数据。

当前基线为 LVGL 8.3.11、SquareLine Studio 和 240 x 320 RGB565 LCD。

本阶段 SquareLine 原型**不包含**解码。Queue 已接线 `storage_listbuffer` 第一窗；Now Playing、Library、状态栏等仍可用固定假数据。产品侧已确认：首版曲库只扫 SD `Music/`，Queue 只绑定可见行；壁纸/模型的设备更新经 Settings 发起、FTL 暂存后再写 Resource Pack（安装未实现）。实现这些数据路径前不必改 SquareLine 导出。详见 [ADR-0015](adr/0015-volume-roles-and-resource-install.md)、[catalog_architecture.md](catalog_architecture.md)。

本阶段原型**仍不包含**：

- SD 卡扫描、文件排序、媒体库建立的 GUI 接线；
- MP3 解码、真实播放队列、ID3 标签或专辑封面读取（等解析组件；`load` 填窗时解析，见 [catalog_architecture.md](catalog_architecture.md) 第 5 节）；
- 真实背光 PWM、亮度调节和设置持久化；
- 书签持久化、真实电子书解析和排版；
- 中文字库、多语言切换和密码锁。

这些能力接入后应替换假数据和空回调，不应推翻本页面层级与交互规则。

## 2. SquareLine 生成边界

`GUI/` 是 SquareLine Studio 的导出目录，SquareLine 工程与其模拟器是当前 UI 原型的唯一事实来源。为确保编辑器状态、模拟器预览和固件导出结果保持一致，助手**不得直接修改**该目录中的生成代码或生成配置，包括但不限于：

- `GUI/ui.c`、`GUI/ui.h`、`GUI/ui_events.h` 与 `GUI/ui_helpers.*`；
- `GUI/screens/` 下的所有文件；
- `GUI/CMakeLists.txt`、`GUI/filelist.txt`、`GUI/project.info` 与生成的资源清单。

后续 GUI 工作流程固定为：助手给出 SquareLine 的组件、相对布局、视觉层级、样式和事件配置步骤；用户在 SquareLine 编辑器中完成操作并先在模拟器验证；确认后由用户导出 `GUI/`，再进行固件编译和硬件验证。除非用户明确撤销此约束，助手不得以“临时原型”“修复导出结果”或其他理由手工改写任何 SquareLine 生成文件。`scripts/check-generated-write.ps1` 会拒绝 `GUI/` 与 `SquareLineProject/` 相对 `HEAD` 的手改；维护者重新导出后，在本机 PowerShell 当前会话执行 `$env:ALLOW_GENERATED_UPDATE = '1'` 再 `git commit`，不要写入用户或系统环境变量。

需要真实硬件或业务行为时，应先在 SquareLine 中保留可视控件与空事件；后续功能接入的代码边界另行评审，不能反向破坏 SquareLine 对 `GUI/` 的所有权。

### 2.1 设计文档同步

GUI 设计每推进一步，都必须先同步更新本文档，再开始下一步 SquareLine 操作。这里的“推进一步”包括新增或删除页面、确定组件层级或坐标、调整视觉规范、改变手势/事件归属、确定动画和状态切换，以及收缩或扩展原型范围。

文档应记录已经确认的结果，而不是事后笼统回顾。若某项仅为待验证想法，必须明确标记为“待验证”，不能写成既定规范。模拟器或硬件验证推翻既有设计时，同样必须在继续修改 UI 前同步更正本文档。

### 2.2 布局决策边界

除非用户明确要求精确坐标或尺寸，助手只给出组件层级、相对位置、对齐关系、留白与字号范围等视觉约束，不将布局数值定死。用户以 SquareLine 模拟器中的实际视觉效果为准，自主微调坐标、宽高和间距；文档记录的是已确认的关系与范围，而不是替代编辑器进行像素级排版。

## 3. 页面层级

```text
Lock Screen
    └─ 向上解锁
       └─ Main Screen
          ├─ StatusBarContainer（固定，不参与页面横滑）
          ├─ MainPageContainer（透明内容区域、循环分页视口）
          │  ├─ SettingsPageContainer
          │  ├─ MusicPageContainer
          │  │  ├─ MusicModeTabs
          │  │  │  ├─ NowPlayingTab
          │  │  │  ├─ QueueTab
          │  │  │  └─ LibraryTab
          │  │  └─ MusicPlayerControlContainer
          │  │     ├─ MusicPlayingSlider
          │  │     ├─ MusicPreviousButton
          │  │     ├─ MusicPlayPauseButton
          │  │     └─ MusicNextButton
          │  └─ BooksPageContainer（当前仅为空白占位页）
```

`Main` 是解锁后唯一的普通根 Screen。`StatusBarContainer` 与 `MainPageContainer` 是它的直接子对象；后者本身就是透明、横向可滚动的循环分页视口。StatusBar 固定在 Main 层，只有 MainPageContainer 内的三张内容页会左右移动。`Music`、`Bookshelf` 和 `Settings` 不再是独立加载的 LVGL Screen，而是其中的同级 Page。`Lock Screen` 是待机视觉页，首版不提供安全认证。`MainPagerDots`、Album Detail、Mini Player、Reader 和各 Settings 子页尚未创建；开始这些设计前必须先更新本文档。

## 4. 全局视觉规范

### 4.1 色彩与背景

首版只使用一组稳定主题，不在运行时随机改变配色。

| 用途 | 建议颜色 | 说明 |
| --- | --- | --- |
| 页面背景顶部 | `#173B63` | 深蓝，使用纵向线性渐变起点。 |
| 页面背景中段 | `#1B2B58` | 保留为渐变过渡色。 |
| 页面背景底部 | `#2B2050` | 靛紫，形成低调层次。 |
| 卡片底色 | `#13223D` | 比背景更深，避免纯黑。 |
| 主文字与浅色轮廓（`White1`） | `#F1F6FF` | 冷白，保证小尺寸文字、状态栏轮廓和图标可读。 |
| 次级文字 | `#F1F6FF` + 对象级低透明度 | 首版不额外占用主题色；通过字号、字重和对象透明度建立次级层级。 |
| 强调色（`Blue1`） | `#00B0DE` | 播放、进度、激活状态和电池容量填充。 |
| 非活动轨道（`Gray1`） | `#404040` | 加载环的静态轨道等低对比、非交互元素；不作为小字号正文文字色。 |

背景的两色纵向线性渐变在当前 SquareLine 模拟器和真机中均已验证会产生明显色带，因此不作为最终视觉方案。目标固件保持 `LV_DITHER_GRADIENT = 0`；运行时渐变抖动已经在真机测试，色带虽可变成颗粒，但不能形成干净的雾状层次，故不采用。

首版正式采用**系统级静态壁纸图**：壁纸目标规格为 `240 x 320` 的全屏资源，先在图像工具中以深蓝到靛紫底色叠加三处大范围、低对比的蓝紫模糊光团，再导入 SquareLine。当前选中的 `Indigo Mist Soft Dark` 含少量半透明像素，因此导出为带 Alpha 的资源；其余技术细节和后续不透明化条件见第 10 节。Image Dither 是否启用及其强度以 RGB565 真机观感为准。壁纸图只承担背景雾感，不烘焙固定信息卡；卡片仍由 SquareLine 组件叠加，以便页面内容和布局独立调整。

普通应用内容页的大型卡片首版仍只模拟为半透明深色底、弱描边与上/左侧更亮的细边，不使用运行时背景模糊。`MusicModeTabs` 是已确认的例外：它使用第 10.5.2 节定义的运行时局部毛玻璃。这样把软件模糊限制在单一主要视觉区域，既保留玻璃质感，也避免将整页所有卡片都变为高成本动态模糊。`Boot` 的全屏壁纸模糊是第 10.3 节定义的独立启动视觉效果，不属于普通卡片样式。

壁纸是系统级外观：Lock Screen 与 Main Screen 使用同一个当前选中壁纸；MusicPage、BooksPage、SettingsPage 通过透明背景露出 Main 的壁纸。首版不做随机壁纸、动态壁纸或每帧变化的渐变。

默认壁纸随 SquareLine 导出为只读 GUI 资源，并跟随固件存放在内部 Flash；首版不从 SD 卡或外部 SPI Flash 读取它。后续只有用户新增或替换的壁纸，才在文件系统与外部存储方案完成后评审 SD 卡或 SPI Flash 的资源加载方式。

### 4.2 统一组件风格

- 页面边距：`12 px`；同级卡片间距：`8 px`。
- 卡片圆角：`12 px`；小控件圆角：`8 px`。
- 卡片优先使用深色底、弱描边和极轻阴影；不大量使用高亮描边。
- 图标和控件以细线、圆形播放按钮、短进度条为主；强调色只用于当前状态和可操作主按钮。
- 当前原型使用英文短标签。文本必须预留截断或省略空间，避免将来替换中文后遮挡。

### 4.3 顶部状态栏

普通应用界面由 Main 持有唯一一个约 `22 px` 高的 `StatusBar`，显示简短时间、日期提示和电量图标。它固定在顶部、不随 MainPageContainer 内容横滑，也不承载关键操作。MainPageContainer 的内容区域从 StatusBar 下方开始；每张内容页不得各自再创建一套状态栏。

阅读页的状态栏属于阅读工具栏的一部分：沉浸阅读时隐藏，用户点击屏幕中央后与上下工具栏一起显示。

## 5. 全局导航与手势所有权

| 页面 | 左右滑动 | 垂直滑动 | 点击 |
| --- | --- | --- | --- |
| MusicPage / BooksPage / SettingsPage | MainPageContainer 循环切换内容页 | 页面内部列表或书架可滚动 | 控件、卡片、标签。 |
| Music 的 Now Playing / Queue / Library | 不使用左右滑动切换模式 | Queue、Library 内容可滚动 | 点击顶部标签切换模式。 |
| Album Detail | 保留 MainPageContainer 左右切换 | 曲目列表滚动 | 返回、曲目、Mini Player。 |
| Reader | 翻页 | 不作为常规滚动 | 中央切换阅读工具栏。 |
| Lock Screen | 不使用 | 向上解锁 | 歌曲迷你控制。 |

Music 内部不采用横滑切换 `Now Playing / Queue / Library`，避免和 MainPageContainer 的全局左右滑动竞争。阅读器的横滑是核心功能，因此仅在 `Reader` 中禁用 MainPageContainer 的横向切换。

## 6. 音乐页面

### 6.1 MusicPage

MusicPage 每次作为 MainPageContainer 内容页显示时默认展示 `Now Playing`。页面顶部、状态栏下方放置可点击标签：

```text
[ Now Playing ] [ Queue ] [ Library ]
```

标签只改变页面内容区，不移动 MainPageContainer。页面下方始终保留完整播放器区域，包含当前曲名、进度条和上一首/播放暂停/下一首控制。

推荐初始坐标分配：

```text
状态栏                 y = 0   ~ 22
模式标签               y = 28  ~ 52
模式内容区             y = 60  ~ 202
完整播放器区           y = 210 ~ 312
```

`Now Playing` 内容区显示较大的方形假封面、曲名和歌手。`Queue` 显示当前及后续曲目的简化列表。Queue 只绑定 `storage_listbuffer` 单槽窗口：SquareLine 只保留一份行范本，运行时按 READY 后的 `Length` 复制行（上限 `STORAGE_LISTBUFFER_MAX_ENTRIES`，现为 8）。完整播放列表由 Storage 持有，不把整表指针交给 GUI。行样式与对象层级见第 10.5.3 节。`Library` 显示两列专辑封面网格；首版每张专辑都使用固定标题、歌手和占位封面。产品曲库首版只来自 SD `Music/`，不展示空的 Flash 分区。

### 6.2 专辑详情

从 `Library` 点击专辑后进入 `Album Detail`。它不是新的 MainPageContainer 内容页，因此顶端不再保留三标签；原标签位置转换为返回和专辑标题。

进入动画：`Library` 标签向左移动，左侧淡入返回箭头；`Now Playing` 与 `Queue` 淡出；专辑标题从右侧滑入，曲目列表随后淡入并轻微上移。总时长约 `160 ms`，使用 ease-out。返回时执行反向动画。

已确定的 240 x 320 布局为：

```text
状态栏                 x = 0,  y = 0,   w = 240, h = 22
标题栏                 x = 0,  y = 22,  w = 240, h = 42
专辑摘要               x = 12, y = 68,  w = 216, h = 52
  ├─ 封面              x = 12, y = 70,  w = 48,  h = 48
  └─ 标题/歌手/曲数    x = 72, y = 72
曲目列表               x = 12, y = 128, w = 216, h = 124
Mini Player            x = 8,  y = 260, w = 224, h = 52
```

曲目行高度为 `40 px`，内容仅保留序号、曲名和时长；首版不添加收藏按钮。专辑详情底部不保留完整进度条，改为 `Mini Player`：小封面、当前曲名和播放/暂停按钮。点击 Mini Player 返回 `Now Playing`。

## 7. 阅读器

### 7.1 书架

Bookshelf 是 MainPageContainer 的 BooksPage。使用两列书籍卡片展示封面、书名和阅读进度；首版的书籍和封面均为假数据。点击书籍进入 `Reader`。

### 7.2 沉浸阅读

阅读页默认隐藏所有阅读控制，左右滑动用于翻页。首版不做复杂卷页，而使用成本可控的假翻页：新页从右侧滑入、旧页从左侧离开，并带一条窄阴影，时长约 `120` 到 `160 ms`。

点击屏幕中央切换阅读工具栏显示状态：

- 顶部：返回、书签、书签列表；
- 底部：字号小/中/大、假亮度滑块、阅读进度；
- 工具栏以覆盖层显示，不能挤压正文或触发重新分页；
- 翻页后立即隐藏工具栏；若仅显示工具栏而无操作，约 `3 s` 后自动隐藏。

假亮度滑块只改变滑块视觉位置；首版不接背光控制。后续真实亮度必须通过 PWM 和 Platform 的公开显示能力实现，而不是在 GUI 内用半透明遮罩模拟。

## 8. 设置页面

Settings 使用 2 x 2 方形卡片，而不是长列表：

```text
┌─────────────┬─────────────┐
│ Playback    │ Display     │
├─────────────┼─────────────┤
│ Typography  │ System      │
└─────────────┴─────────────┘
```

- `Playback`：预留音量、播放顺序；暂不做 EQ。
- `Display`：假亮度、自动锁屏时间，并进入 `Wallpaper` 选择页。
- `Typography`：系统 UI 字号小/中/大，并预留未来系统字体选择。
- `System`：日期时间、设备信息、电量、存储与固件版本。

阅读字体大小属于 `Reader` 底部工具栏，不与系统 UI 字号混用。设置卡片进入子页后采用与书籍详情相同的“返回 + 标题”结构。

### 8.1 Wallpaper

`Settings → Display → Wallpaper` 是系统壁纸的选择页。它展示多张静态壁纸预览卡；点击预览后切换该页内的选中态，选中项以强调色细边与勾选标记表示。首版只验证视觉选择和页面跳转，不接 Flash 持久化、真实资源切换或动态壁纸；候选壁纸数量、缩略图布局和默认壁纸以首张 `Indigo Mist` 导入后的实际观感为准，尚待下一步确认。

## 9. 锁屏

锁屏首版是待机视觉页，不是安全边界：

- 主视觉为大号时间和日期；
- 正在播放时显示简短 Mini Player，支持播放/暂停；
- 底部显示 `Swipe up to unlock`；
- 不实现 PIN、密码、错误次数或权限保护。

是否加入安全锁和实际唤醒/背光策略留待硬件交互阶段单独决定。

## 10. SquareLine 实施顺序

1. 建立全局背景、状态栏、卡片样式、颜色和字体层级；
2. 建立 Main 固定 Shell、三张 MainPageContainer 内容页与顶部状态栏；
3. 完成 Music 的三标签、播放器控制骨架、Library 和 Album Detail；
4. 完成 Bookshelf、Reader 工具栏和假翻页；
5. 完成 Settings 的 2 x 2 卡片与四张假子页；
6. 完成 Lock Screen 及其假播放状态；
7. 最后统一检查触摸热区、文本截断、动画时长和 RGB565 渐变效果。

### 10.1 第一步：页面骨架（已被 Main 架构替代）

本节记录早期的四张独立 Screen 骨架：`Lock`、`Music`、`Books`、`Settings`。当时 `ui_init()` 按该顺序初始化并默认加载 `ui_Lock`，四张 Screen 均关闭 Scrollable、四边 Padding 为 `0`。

该结构无法让顶部信息栏在根页面横滑时保持固定，现已被第 10.5 节的 Main 架构正式替代。迁移时已从当前 SquareLine 导出中移除独立的 `Music`、`Books`、`Settings` Screen；后续正式 UI 只能在 `MainPageContainer` 内的 Page 上扩展。

### 10.2 第二步：Lock 的主视觉

本步骤已完成锁屏主视觉和基础解锁事件：`Lock` 的 `LV_EVENT_GESTURE` 检测到向上手势后切换至 `Main`。锁屏仍不提供播放控制、密码或其他业务事件，也不复用 Main 的顶部状态栏；日期、时间和电量构成独立的居中信息层。

系统默认壁纸已经替换为 `Indigo Mist Soft Dark`：文件为 `wallpaper_indigo_mist_soft_dark.png`，规格 `240 x 320 px`、竖屏 PNG、sRGB；保留原有蓝紫烟雾构图，只将右下近黑区改为连续的深靛蓝柔边过渡。图片不得包含文字、图标、状态栏、卡片、边框或任何固定业务内容。

当前 SquareLine 导出将此资源绑定到 `Boot`、`BootReveal`、`Lock` 与 `Main` 四张根 Screen；它们共用同一个导出资源 `ui_img_wallpaper_indigo_mist_soft_dark_png`，不会为每页重复编译一份图像数据。MusicPage、BooksPage、SettingsPage 均保持透明。旧壁纸资源可留在 SquareLine Assets 中作为回退候选，但只要它不在生成的 `filelist.txt` / CMake 源清单中，就不参与固件编译。

这张候选 PNG 实测存在少量半透明像素（共 `1116 / 76800`，Alpha 最低为 `219`），所以当前 SquareLine 导出格式为 `LV_IMG_CF_TRUE_COLOR_ALPHA`，而非原先计划的无 Alpha `CF_TRUE_COLOR`；这会让资源像素数据约为 `230,400 B`，并让底层屏幕背景在半透明处参与混合。后续真机应专门确认右下暗部是否仍显得突兀：若存在问题，再生成一个以深靛蓝底色预合成、Alpha 全为 `255` 的不透明副本，并由用户重新导入/导出；严禁手改生成的图片 `.c` 文件。

`Indigo Mist Soft Dark` 的视觉源已获用户初步认可；壁纸的最终真机验收与后续 UI 文本可读性评估，待锁屏内容叠加后统一进行。

已稳定复现资源构建失败：`GUI/images/ui_img_wallpaper_indigo_mist_png.c` 使用了 LVGL v9 的 `lv_image_dsc_t`、`LV_COLOR_FORMAT_RGB565` 与 `LV_IMAGE_HEADER_MAGIC`，而项目其余 SquareLine 导出文件及中间件均为 LVGL v8.3.11，导致 Debug 构建在该资源文件处失败。用户确认该图片是通过 SquareLine Studio 的 Asset Panel 导入原 PNG、设置 `Image Dither` 的 `Filter Strength = 5`、再使用左上角 `Export UI Files` 导出；不存在手工写入资源 C 文件或使用外部转换器的步骤。`​Filter Strength` 只影响图像抖动，不影响 LVGL API 主版本。

为隔离 Image Dither，用户已将该资源的 `Filter Strength` 临时设为 `0`、重新 Export UI Files，助手使用同一条 `cmake --build --preset Debug` 构建环复验；资源仍导出为相同的 v9 描述符并在相同行失败。因此排除抖动功能、原 PNG、RGB565 与 Alpha。随后用户以 **SquareLine Studio 1.6.1** 导出同一工程，生成的图片资源可正常通过现有 LVGL v8.3.11 工程编译，完成版本回归验证。最终结论：**SquareLine Studio 1.6.2 在此 LVGL v8.3.11 项目的图片 C 源导出存在兼容性回归；1.6.1 是当前已验证可用的 GUI 导出工具版本。** 后续本项目固定使用 1.6.1 导出 `GUI/`，不手改生成文件；只有在新版本经同样的“含 PNG 资源的导出 + 固件编译”验证通过后，才允许升级。

初始布局采用相对约束，而非固定像素坐标：大时间位于屏幕上半部并作为全页视觉焦点；日期直接位于时间下方；电量提示与日期共同构成次级信息，位于日期下方并保留一段较短留白。时间和日期直接作为 `Lock` 的子对象；电池图标与百分比使用一个仅包围二者的小型 `BatteryGroup` 透明 Container，保证整组始终水平居中。该 Container 不承载业务事件，也不需要滚动。此前“Container 会吃掉手势”的判断已被实测推翻：锁屏滑动不灵敏的根因是触摸采样率过高，调整后已恢复正常，因此普通布局 Container 不需要为规避手势额外开启 Gesture Bubble。屏幕下半部预留给后续的 Mini Player 与 `Swipe up to unlock`，因此首版不把电量放到底部。

日期固定使用三字母星期与月份缩写，例如当前导出的 `SUN, AUG 24`，避免 `THURSDAY, NOVEMBER 28` 一类文本在 240 px 屏幕上溢出。日期使用约 `14–18 px` 的 `White1`（`#F1F6FF`）；它与大时间通过字号、字重和后续按需设置的对象透明度形成层级，而不再保留独立的次级文字主题色。大时间使用当前 SquareLine 可选的最大或接近最大的英文字体，建议约 `42–52 px`，主文字色同为 `#F1F6FF`。

电量提示由一个简洁的 `LockBatteryBar` 与 `82%` 短文本组成，二者置于 `BatteryGroup` 中，以 Row 布局作为同一行视觉组水平居中，位于日期下方；`LockBatteryBar` 使用 SquareLine 的 LVGL Bar 控件绘制细轮廓与容量填充，而非导入 SVG 或使用 LVGL 字符图标。首版不使用电池正极突起：在当前屏幕尺寸上，额外的窄 Panel 会削弱图标的简洁性，保留圆角轮廓和容量填充即可。

`BatteryGroup` 背景、边框与阴影均透明，Padding 仅保留电量条和文字之间的窄间距。电量条轮廓与百分比使用 `White1`（`#F1F6FF`），填充使用强调色 `Blue1`（`#00B0DE`），文字字号约 `12–14 px`；固定假数据时，Bar 的填充值必须与显示百分比一致，例如 `84%` 对应值 `84`。首版不表现实时电量、充电状态或低电量告警。以 SquareLine 模拟器的视觉平衡为最终依据，优先调整信息层的相对间距与留白，不因本文档强行固定坐标。若导出后固件缺少对应字体，再单独评审并由用户手动调整 `lv_conf.h`，助手不修改生成目录。

锁屏底部解锁提示采用 iPhone 风格的“文字 + Home indicator”：`LockUnlockHint` 显示 `Swipe up to unlock`，其下方为短圆角横条 `LockHomeIndicator`；二者水平居中并靠近屏幕底部安全区。首版不使用箭头或可见容器；向上手势由 `Lock` 根对象处理，底部提示 Group 不承担事件。二者与上部的时间/日期/电量信息保持足够大留白，Mini Player 仍留待后续单独设计与接入。

`LockUnlockHint` 使用主文字色的低亮度版本，不创建文字阴影、发光副本或预渲染模糊资源。240 x 320 的 RGB565 屏幕以文字本身的轻微透明度变化维持简洁与可读性，避免伪阴影产生脏边或使层级过多。

提示文字和 Home indicator 放入透明的 `LockUnlockGroup`，仅用于统一设置透明度动画，不绘制背景、边框或阴影，也不承担事件。该 Group 以低频呼吸提示手势：从低可见度平滑变亮，再平滑变暗，往返周期约 `1.5–2.0 s`，无限循环；避免位移、闪烁或高频动画。文字与横条随 Group 同步呼吸，以保持 iPhone 式的单一底部手势锚点。

呼吸动画由 `Lock` Screen 的 `Screen loaded` 事件以零延迟执行 `Play Animation` 启动，目标为 `LockUnlockGroup`。这是当前“每次进入锁屏都从头开始呼吸”的设计选择，包含默认首屏首次加载与后续从其他页面重新进入 Lock。当前导出还保留了 `LockUnlockGroup` 自身的同名 `Screen loaded` 事件；它与根 Screen 的启动逻辑重复，下一次用户在 SquareLine 编辑时应删除 Group 上的该事件，不得再新增同类重复事件。

SquareLine 的 `Initial actions` 同样可以启动初始动画，但它更适合希望动画跨 Screen 持续运行、而不是每次进入 Screen 都重启的情况；当前不得与 `Screen loaded → Play Animation` 同时启动同一个无限动画，以免重复创建或累积。若后续真机验证发现默认首屏不派发 `Screen loaded`，再以 `Initial actions` 作为单独的替代方案，而不是与本事件并存。

### 10.3 开机动画视觉与实现边界

本节记录开机视觉从候选到实现的过程。**以 10.3.1 为当前有效设计**；其后的候选
方案和资源模型保留为历史决策背景或后续功能规划，不能反向覆盖当前导出的 UI。
所有 Screen、样式、事件和资源仍只允许用户在 SquareLine 中创建和导出；不得手改
`GUI/` 生成代码。

#### 10.3.1 当前有效设计与运行时调用链

当前开机页采用已选定的 **Orbital loader**，最终由 `Boot`、`BootReveal` 两张
SquareLine Screen 组成。此前为排查切屏问题曾临时删除 `BootReveal`；用户现已确认恢复它，
所以下一次 SquareLine 导出前的“`Boot` 直接切到 `Lock`”仅是过渡状态，不能作为最终实现：

    Boot
      └─ BootOrbitGroup（透明布局 Group）
          ├─ BootOrbitRing（普通 LVGL Arc：MAIN 为完整暗色轨道）
          │   ├─ INDICATOR 为内缩的青色活动弧
          │   └─ KNOB 透明
          └─ BootOrbitLabel（“LOADING”）

    BootReveal
      └─ 同一张清晰壁纸的根 Background image

`BootOrbitRing` 是普通 Arc，不是 Spinner。运行时保持 MAIN 完整轨道不变，以一条
`1400 ms` 的线性相位动画同时计算 INDICATOR 两端：活动弧在 `30°` 到 `210°` 间
缓入缓出地伸缩；伸长时前端加速前进，缩短时后端追赶前端，因此两个端点均不会倒退。
周期末的活动弧位置和长度与下一周期起点一致，不产生跳变。当前统一起始偏移为
`255°`，用于使动态轨迹相对静态预览逆时针偏移。该效果由 Service 运行时实现，SquareLine
只负责静态预览。当前导出的对象名为
`ui_BootOrbitRing`、`ui_BootOrbitGroup` 和 `ui_BootOrbitLabel`，后续若用户在 SquareLine 重命名，必须先导出，再同步适配实现
与本文档。

开机链路固定为：

```text
Boot --（SquareLine：SCREEN_LOADED → Change Screen）--> BootReveal
BootReveal --（SquareLine：SCREEN_LOADED → Call function）--> GUI Service
GUI Service --（lv_async_call 延后一轮 LVGL 调度）--> Lock
```

`Boot → BootReveal` 保留 SquareLine 的普通切屏事件；当前 SquareLine 导出的视觉基准为 `4000 ms` 停留后
以 `400 ms` `FADE_OUT` 切入。各 Screen 根背景色 Alpha 均为 `0`，仅由根 Background
image 显示壁纸，避免 Screen Fade 中混入默认黑色或白色底。

`BootReveal` 的 `SCREEN_LOADED` **不得**直接再添加 `Change Screen → Lock`。在 LVGL v8
中，前一个 Screen Fade 的 `SCREEN_LOADED` 回调仍处于前一次切屏的收尾过程；此时嵌套调用
下一次 `lv_scr_load_anim()` 会强制完成或取消旧动画，表现为 Lock 突然出现，且后一次的
延迟参数看似失效。

取而代之，`BootReveal` 仅配置 `SCREEN_LOADED → Call function`，函数名为
`Service_GUI_Boot_RequestLock`。该函数已由 GUI Service 实现，作用仅是请求一次异步交接：
它使用 `lv_async_call()` 让当前 LVGL 事件和前一段切屏完整返回后，再由 GUI Task 发起
`BootReveal → Lock` 的 Fade。当前私有配置为 Reveal 保持 `200 ms`、随后 `600 ms`
`FADE_OUT`；这两个参数位于 `Service/gui/boot/gui_service_boot_config.h`，不再作为
SquareLine 中的第二个直接切屏事件存在。后续如按真机观感调整，必须先同步本文档。

这种回调是运行时生命周期衔接，SquareLine 模拟器没有 GUI Service 的实际实现时可以停在
`BootReveal`；静态布局和前一段 `Boot → BootReveal` 仍可在模拟器检查，完整链路以导出后
的真机 Release 构建为准。任何情况下均不手改 `GUI/` 生成文件。

`ui_init()` 内部会加载 `Boot`，所以这次首次 `SCREEN_LOADED` 已发生在 GUI Service
获得控制权之前。为避免依赖一个已经错过的事件，`gui_service.c` 在 `ui_init()` 后：

1. 先调用私有 `main/gui_service_main` Module，以 SDRAM Canvas 工作区生成当前默认壁纸的
   模糊副本，并立即复制为 Main 长期持有的完整模糊壁纸；
2. 再调用私有 `boot/gui_service_boot` Module，以同一 Canvas 工作区生成 Boot 专用模糊背景
   并直接绑定到 `Boot` 根对象；此时 Boot 是共享工作区的最后使用者；
3. 显式调用私有 `service_gui_boot_start()`。

后者现在会创建一组无限 LVGL 相位动画，并在每次回调中同时设置 INDICATOR 的
start/end angle，实现无前端回退的伸缩。动画以 `ui_BootOrbitRing` 为对象；当 Screen
销毁该对象时，LVGL 自动删除其动画。`BootReveal → Lock` 的异步交接由同一 `boot/`
Module 实现：只允许 GUI Task 中的 LVGL `lv_async_call()` 回调调用切屏 API，
不得以 FreeRTOS Timer 或 ISR 直接操作 LVGL。该 Module 不修改 SquareLine 生成文件，
也不得在 ISR 中调用。

可复用 Canvas 离屏处理已迁入 `Service/gui/canvas/`。它提供“源图片 + 调用方指定半径
→ 模糊描述符”的通用能力；Canvas 工作对象挂在 display top layer，不再作为 `Boot` 的
子对象，因此 Boot 作为临时 Screen 销毁后，后续壁纸切换或 Settings 局部毛玻璃仍可安全
复用同一 SDRAM 工作区。Canvas 输出只在下一次模糊调用前有效：Main 会立即复制为自己的
长期全屏背景，而 Boot 在启动阶段直接绑定这份共享输出，因而 Boot 显示期间不得再发起
Canvas 模糊。MainPageContainer 的局部毛玻璃滚动只裁剪 Main 的长期副本，不会改写共享工作区。
现有大数组保留 `service_gui_effect_canvas_buffer` 名称，表示它是通用视觉效果的 Canvas
缓冲，而不是某张启动页壁纸的专属存储。

#### 10.3.2 历史候选与后续资源模型

以下内容是早期方案选择和资源规划的记录，不是当前实现指令；当前 UI、时序和
运行时调用链只以 10.3.1 为准。

1. **Orbital ignition（已选定）**：近黑靛蓝底上由活动圆弧和完整轨道组成抽象“轨道标记”；它不依赖未确定的品牌文字或 Logo，具有更强识别度。当前实际对象、相位动画和切屏时序已以第 10.3.1 节为准落地。
2. **Minimal wordmark**：近黑靛蓝底上仅出现产品字标或单字母标记，短暂停留后淡出，再进入 Lock。层级最克制，但需要先确定项目的显示名称或 Logo。
3. **Music signal**：近黑底上以三到五根细竖条完成一次简短的“由静到动再归零”的节奏动画，随后切入 Lock。产品属性最直观，但会更偏播放器/科技感。

这些历史候选共同确认了独立 `Boot` Screen、一次性启动序列和短淡入切换的方向。当前实现不再使用该阶段提出的 `Initial actions` 或 `1.0–1.5 s` 时序；实际启动事件、停留时间和 Fade 参数只以第 10.3.1 节及导出的 SquareLine 代码为准。产品正常流程不得重新进入 Boot，Lock 的底部解锁呼吸提示只应在切换完成后启动。

参考评估：高质感移动端 Splash 常将单个抽象标记置于深色留白中心，避免在启动阶段堆叠播放器内容、均衡器或 Loading 文案。此前的 LVGL v9 SquareLine `Smart_Gadget` 示例也采用同一节奏结构：Logo 与两行文本按 `100 / 200 / 300 ms` 错峰上移淡入，并在约 `1.4 s` 后用短 Fade 切入主 Screen。当前 `Orbital ignition` 借用其“错峰显现 + 快速淡入切屏”的结构，不复制其白底、Logo 或文字视觉；若未来重新设计启动视觉，应以实际硬件时序和资源占用重新评审，而不是直接恢复本节的历史候选。

下列分镜是 `Orbital ignition` 的资源与背光演进目标；其中运行时 Canvas 模糊已经完成首轮验证，真实背光渐亮和可换壁纸缓存仍待后续实现：

```text
Boot（预模糊、偏暗的同款壁纸）
  └─ 背光由低亮度升至目标亮度（后续硬件钩子，当前留空）
     └─ 轨道标记依次显现并短暂停留
        └─ Boot → BootReveal：约 120–180 ms，预模糊壁纸快速过渡为清晰壁纸
           └─ BootReveal → Lock：约 160–220 ms，Lock 信息层淡入
```

壁纸可由用户更换，故不得为每张候选壁纸都随固件静态保存一张模糊副本，也不得仅以深色遮罩冒充模糊。最终方案为**运行时生成、按壁纸缓存**：解码后的当前壁纸先保留为清晰 RGB565 帧，再通过图像处理生成对应的模糊 RGB565 帧；`Boot` 显示模糊帧，`BootReveal` 显示清晰帧，两个短 Fade 共同模拟“背景由模糊变清晰、随后 Lock 出现”。

H743 的 DMA2D 可协助像素格式转换、拷贝、填充和 Alpha 混合，但没有卷积/高斯模糊功能。LVGL v8.3 的 Canvas 提供 `lv_canvas_blur_hor()` 与 `lv_canvas_blur_ver()`；当前首轮实现已使用位于 SDRAM 的 Canvas，依次执行横向、纵向模糊，而非手写模糊算法。SquareLine Studio 不提供可直接拖拽的 Canvas 控件，Canvas 对象由 GUI Service 在运行时首次创建并挂到 display top layer，不修改生成目录中的任何文件。其大像素缓冲由 GUI Service 在 SDRAM 静态持有，并定义为可复用的“视觉效果工作区”，不以 `Boot` 命名或限定用途；GUI Task 一次只允许一个离屏效果任务占用该工作区。该路径仍是 CPU 软件处理，DMA2D 仅可用于输入/输出格式处理、拷贝、遮罩和过渡混合，不承担模糊卷积本身。若 Canvas 的视觉效果或性能实测不满足要求，再回退至滑动窗口多次 Box Blur 方案。

可换壁纸的后续处理策略不应在每次开机重复计算：当用户首次导入或切换某张壁纸时，在 SDRAM 中生成其模糊帧，并按壁纸内容版本写入外部 SPI Flash 或文件系统缓存；后续启动优先直接读取匹配缓存。缓存缺失或校验不匹配时，设备在低背光、Boot 尚未显示有效内容的阶段运行一次生成任务，再异步补写缓存。这样仍完整支持任意可换壁纸，同时避免为每张壁纸占用内部 Flash，也避免每次开机重复消耗 CPU。后续正式缓存格式暂定为全不透明 `240 x 320 RGB565`，清晰帧与模糊帧各约 `150 KiB`；生成阶段需要位于 SDRAM 的工作缓冲，后续结合实际算法与缓存策略单独评审峰值容量和 D-Cache 一致性。

可换壁纸运行时模糊与缓存的待实现数据流为：

```text
壁纸源（内部 Flash / SPI Flash / 文件系统）
  → 解码或预转换为 SDRAM 中的 Clear RGB565 帧
  → 检查 {内容校验值、尺寸、格式、算法版本、半径} 对应的 Blur Cache
  ├─ 命中：读取 Blur RGB565 帧至 SDRAM
  └─ 未命中：GUI Service 创建离屏 Canvas，执行水平/垂直 Canvas Blur，输出 Blur RGB565 帧
                → 写入外部缓存
  → 将 Blur / Clear 描述符分别绑定至 Boot / BootReveal、Lock 的根背景
```

第一轮 Canvas 验证直接复用当前 SquareLine 导出的 `LV_IMG_CF_TRUE_COLOR_ALPHA` 默认壁纸：按源描述符的 `data_size` 复制到同格式 Canvas，再调用 `lv_canvas_blur_hor(canvas, NULL, radius)` 与 `lv_canvas_blur_ver(canvas, NULL, radius)`；该 Canvas 缓冲约 `225 KiB`。这是为了以最少的格式转换证明视觉链路，Canvas 的实现可处理 Alpha。正式可换壁纸缓存再统一为已预合成、全不透明 RGB565：将清晰壁纸复制到 RGB565 Canvas 后执行同一组调用，必要时以较小半径重复一到两轮，使视觉更接近高斯模糊。Canvas 只解决“怎样算出模糊帧”，并不替代按壁纸内容缓存结果的机制。

正式 RGB565 路径至少保留 Clear 与 Blur Canvas 两个 `240 x 320 RGB565` SDRAM 缓冲，合计约 `300 KiB`；若 Canvas 实现或后续手写 Box Blur 回退方案需要乒乓缓冲，再增加第三个缓冲，总计约 `450 KiB`。首轮 Alpha 资源验证仅额外持有一块约 `225 KiB` 的通用视觉效果 Canvas 工作缓冲，清晰源继续位于内部 Flash。Canvas 对象本身可由 LVGL 小堆动态创建，但当前 LVGL 堆仅 `48 KiB`、FreeRTOS 堆仅 `32 KiB`，不得用 `lv_mem_alloc()`、`pvPortMalloc()` 或未审计的 `malloc()` 分配此大像素缓冲。所有 Canvas 缓冲禁止放入 DTCM。由 DMA 写入的壁纸源在 CPU 读取前需要失效对应 D-Cache 区间；CPU 生成的 Blur 帧在被 DMA2D 或外设 DMA 读取前需要清理对应 D-Cache 区间。若最终由 LVGL 软件渲染直接读取该帧，则同一 CPU 缓存域内不额外做无意义的清理。当前原型使用 `service_gui_effect_canvas_buffer` 与 `canvas/gui_service_canvas` Module；正式缓存的长期资源 Interface 仍须在实现前审校。

### 10.4 Settings 局部毛玻璃资源模型（设计已验证，功能待实现）

Settings 的目标效果是：卡片外的壁纸保持清晰；每张圆角卡片区域显示与其屏幕坐标连续对应的模糊壁纸，卡片再叠加半透明色层、边框和正文。该效果不能用同一块会被覆写的 Canvas 缓冲分别绑定给多张卡片。

已用可删除的 `.scratch/settings_backdrop_prototype` 状态原型验证 `Boot → Lock → Settings → 更换壁纸` 的资源所有权，结论如下：

1. GUI Service 长期持有一块通用全屏离屏效果工作区。它只在生成全屏模糊壁纸、重组背景等短任务期间独占，GUI Task 同一时刻只能运行一个此类任务。
2. Settings 显示时额外长期持有一张完整的“局部毛玻璃合成背景”：先复制清晰壁纸，再只把所有卡片的圆角区域替换为同位置的模糊壁纸。Settings 根背景绑定此合成帧，卡片自身仅绘制透明染色、边框和内容。
3. 不为每张卡片建立长期 Canvas 或整屏副本；卡片数量只增加一次合成中的裁剪次数，不线性增加常驻像素缓冲。
4. 壁纸、卡片位置、尺寸或圆角变化时重新生成 Settings 合成背景；页面静止显示、文字变化或卡片内容更新时不重复模糊计算。

当前 Alpha 原型中，通用工作区和 Settings 合成背景各为 `240 x 320 x 3 B = 230400 B`，并存峰值约 `450 KiB`；统一为不透明 RGB565 后约 `300 KiB`。该模型适合 H743 的计算能力与现有 32 MiB SDRAM，但最终模糊半径、圆角裁剪算法、生成耗时和 SPI 全屏刷新成本必须在真机测量后定稿。

`Boot` / `BootReveal` 与 `Lock`、`Main` 一样，将壁纸作为各自 Screen 根对象的 Background image，而不是额外放置全屏 Image。当前 GUI Service 已在不修改生成代码的前提下，以运行时样式把模糊帧绑定给 `Boot`；`BootReveal` 与 `Lock` 仍使用 SquareLine 导出的同一清晰壁纸。根背景图更符合“系统壁纸”语义，减少一个全屏对象，也使 Screen Fade 直接参与背景过渡。可换壁纸的统一绑定、缓存键、失效规则与 Settings 合成背景的长期资源 Interface 均属于后续实现任务，必须先经用户审校。

已完成的 SquareLine 原型结构为：`Boot` 与 `BootReveal` 位于导出顺序的首部，且 `Boot` 位于 `BootReveal` 与 `Lock` 之前；三张 Screen 都关闭 Scrollable、四边 Padding 为 `0`。`Boot` 与 `BootReveal` 的根背景均使用当前系统壁纸，根背景色 Alpha 为 `0`。运行时由 GUI Service 覆盖 `Boot` 的根背景图为模糊帧，`BootReveal` 保持导出的清晰帧。后续设计仍不得为模糊效果手改或另行导出 SquareLine 生成的静态图片。

真实背光渐亮不属于 SquareLine 原型：后续由 GUI Service 调用 Platform 暴露的 LCD/PWM 亮度接口实现，Boot 仅预留时序位置，当前不创建空的生成代码回调。`BootReveal` 是仅用于视觉过渡的短生命周期 Screen，不提供输入或业务控件；原型结束后，若能将清晰壁纸与 Lock 的信息层拆分到同一受控层级，可再评审是否删除该中间 Screen。

### 10.5 Main 固定 Shell 与 Music 页面骨架（当前实现）

锁屏与开机视觉已完成首轮原型后，当前导出已建立解锁后的 `Main` 固定 Shell 及 MusicPage 的首轮视觉骨架。外层 MainPager 已替换为 MainPageContainer 的普通 Container 循环分页结构；本步骤不创建真实播放逻辑、歌曲数据或 Music 业务回调。Lock 到 Main 的基础解锁跳转已由 Lock Screen 的向上手势实现。

`Main` 是普通应用界面的唯一根 Screen，继续使用当前系统壁纸、关闭 Scrollable、四边 Padding 为 `0`。外层分页已从 SquareLine 的 Tabview 改为普通 Container；MainPageContainer 本身承担视口和滚动职责，不额外创建嵌套 Viewport。任何情况下均不得手改 `GUI/` 生成代码。

```text
Main
├─ StatusBarContainer      固定顶部信息栏，屏幕高度的 5%
├─ MainPageContainer       透明横滑视口，屏幕高度的 85%
│  ├─ SettingsPageContainer  当前仅为空白占位页，初始位于前一槽位
│  ├─ MusicPageContainer
│  │  ├─ MusicModeTabs      [ Now Playing ] [ Queue ] [ Library ]
│  │  │  ├─ NowPlayingTab
│  │  │  ├─ QueueTab
│  │  │  └─ LibraryTab
│  │  └─ MusicPlayerControlContainer
│  │     ├─ MusicPlayingSlider
│  │     ├─ MusicPreviousButton
│  │     ├─ MusicPlayPauseButton
│  │     └─ MusicNextButton
│  └─ BooksPageContainer     当前仅为空白占位页，初始位于后一槽位
└─ DotPanelContainer        固定分页指示区，屏幕高度的 10%
   ├─ DotSettings
   ├─ DotMusic
   └─ DotBooks
```

壁纸只绑定到 `Main` 根对象。`MainPageContainer` 与三张 Page Container 的背景、边框、阴影均保持透明，使滑动时始终露出同一张固定系统壁纸；不得为 MusicPage、BooksPage、SettingsPage 分别再设置壁纸。`MusicModeTabs` 的内部 Content container 是唯一无法由 SquareLine 直接公开的对象：`Service/gui/main` 在运行时将其背景、背景图、边框、轮廓和阴影置为透明，以消除 LVGL Simplified Theme 默认白底。除该内部对象与 `ui_MusicModeTabs` 的运行时裁剪背景源外，Service 不得覆盖任何 SquareLine 导出对象的视觉 Style。

`StatusBar` 为透明的横向 Container，位于 Main 最顶端，当前高度采用屏幕高度的较小比例。左侧为短时间文本；中间为简短日期提示；右侧为电池轮廓与百分比。它不绘制独立卡片底色、不承载点击事件，也不与 Lock Screen 复用对象：Lock 的大时间、电量信息仍是独立的居中信息层。首版所有数值均为固定假数据。

Main 的纵向固定分区为：顶部 `StatusBarContainer` 占屏幕高度 `5%`，中部 `MainPageContainer` 占 `85%`，底部 `DotPanelContainer` 占 `10%`。`MainPageContainer` 使用 SquareLine 的普通 `Container` 承担横滑内容区，不使用 Tabview 或隐藏的 Tab 按钮栏。它是 Main 中唯一允许横向滚动的普通视口：透明、无边框、无阴影、关闭 Scrollbar，不启用 Flex 或 Grid 布局；左、右、下 Padding 为 `0 px`，顶部保留 `3 px` 的轻微留白，使内容页与 StatusBar 视觉分离。其可视范围裁剪子对象，子 Page 不得溢出可视范围绘制。三张 Page 均填满 Viewport 的有效内容宽高。

三张 Page Container 自身保持不可滚动，但都必须开启 **Horizontal Scroll Chain**：LVGL 命中一个不可滚动的子 Page 后，只有该 Flag 才会继续向父级寻找可横滑的 `MainPageContainer`。`MainPageContainer` 自身保持关闭 Scroll Chain，防止全局分页继续传递给 `Main`。同时，`MainPageContainer` 必须保留 **Clickable** 与 **Scrollable** Flag：在 LVGL v8 中，指针命中测试只会把 Clickable 对象作为活动对象；若关闭 Clickable，即使对象开启 Scrollable，触摸也不会选中该视口，横向滚动无法开始。该 Flag 仅表示可接收指针命中，不会把视口变成视觉上的按钮。不得手改 `GUI/` 生成代码绕过这一结构。

#### 10.5.1 MainPageContainer 循环分页（当前 SquareLine 骨架已导出）

Viewport 内始终只保留同一组三张 Page 实例，不复制首尾页。初始槽位从左到右为：`SettingsPage` 位于 `0%`、`MusicPage` 位于 `100%`、`BooksPage` 位于 `200%`；每张 Page 的宽高均填满 Viewport。GUI Service 在对象布局完成后无动画滚动到一个 Viewport 宽度，因此真机初始可见的是位于中间槽位的 MusicPage。SquareLine 模拟器默认从滚动起点显示 SettingsPage 属于预期限制；完整初始定位以导出后的 GUI Service 为准。

横向手势结束后，不通过 Tabview 索引切换，而是计算当前水平滚动位置相对当前物理槽位的偏移 `delta`：

1. `delta` 未达到翻页阈值时，动画回到当前物理槽位；
2. `delta` 超过正阈值时，完成向后一页的短滚动；超过负阈值时，完成向前一页的短滚动；
3. 完成切页后，Service 只重新排列三张既有 Page 到“前一页 / 当前页 / 后一页”的 `0% / 100% / 200%` 槽位，并立即无动画回到中间槽位；用户视觉上连续循环，且不会看到页面重排。

首轮阈值当前为一个 Viewport 宽度的 `50%`，同一手势最多切换一页。Service 记录当前物理槽位，在 `LV_EVENT_SCROLL_END` 中按阈值决定“原页或相邻页”，并动画滚动至对应的 `0%`、`100%` 或 `200%` 槽位。若活动页到达左端或右端，Service 随即在同一 GUI 任务中循环轮换三个既有 Page 指针、重新写入 `0% / 100% / 200%` 位置，并无动画回到中间槽位；屏幕只在下一次刷新时呈现重排后的稳定画面，因此用户视觉上连续循环。当前仍不更新圆点。Viewport 关闭 Scroll Momentum 和 Scroll Elastic，使短距离快速拖动不会被惯性推进到下一页；日后若实测手感需要“短而快的甩动翻页”，必须将速度判定、阈值和验证结果一并补充到本文档后再启用。Service 必须以私有状态防止程序化吸附和回中产生的 `LV_EVENT_SCROLL_END` 被再次判定为新手势。

`LV_EVENT_SCROLL` 只承担 MusicModeTabs 局部毛玻璃的实时坐标更新；翻页判定只在 `LV_EVENT_SCROLL_END` 执行。当前 `MusicPlayingSlider` 依赖 LVGL 原生命中与拖动行为，真机验证中不会触发 MainPageContainer 翻页，因此不额外创建手势仲裁回调或临时修改外层 Scrollable Flag。若未来出现可复现的 Slider 与全局分页竞争，再以实测问题为依据单独诊断。播放器按钮维持 LVGL 原生点击语义，不为它们创建额外的全局手势屏蔽层。

`MusicModeTabs` 位于 MusicPage 顶部，使用 MusicPage 内嵌的 Tabview 实现，当前宽度为页面的 `90%`、高度为有效页面高度的 `66%`，默认 Tab 按钮栏高度以实际观感为准。它采用轻量选中态：`STYLE (BUTTONS MAIN)` 保持透明；`STYLE (BUTTONS ITEMS)` 的 `DEFAULT` 状态为 `White1` 低透明文字、无背景与无边框；`CHECKED` 状态为不透明 `Blue1` 文字，并仅在底边显示一条细 `Blue1` 指示线。不得使用整块高亮填充背景，以免在 240 px 宽屏上与播放器主体争夺视觉重心。`Now Playing` 为初始选中页，`Queue` 与 `Library` 为非选中页。模式切换只允许点击顶部标签；`Service/gui/main` 会禁用该 Tabview 内部 Content container 的 Scrollable Flag，避免内层横滑与 MainPageContainer 的全局横滑竞争。后续 Queue、Library 的竖向列表滚动应由各自 Tabpage 承担，不得重新开启该内部 Content container 的滚动。

当前 `NowPlayingTab` 与 `LibraryTab` 仍为空白内容区。`QueueTab` 只保留 SquareLine 单行范本；`Service/gui/main/queue` 把该范本构造摘进 for 循环，GUI Task 在 `storage_listbuffer` READY 后经 `Service_GUI_QueueApply()` 按 `Length` 填行。`MusicPlayerControlContainer` 是 `MusicPage` 的直接子对象，位于 MusicModeTabs 下方，当前宽度为页面的 `90%`、高度为 `28%`，上、下 Padding 均为 `2 px`；它承载一条假进度条与上一首/播放暂停/下一首三个静态控件。`MusicPlayingSlider` 宽度为播放器区的 `90%`、高度 `7%`，相对顶部下移 `5%`。当前尚未创建 `MusicPlayerCard`、假曲名或专辑封面。

`DotPanelContainer` 是 `Main` 的固定底部子对象，使用居中的 Flex Row 布局，列间距为 `5 px`，自身不接受点击或滚动。它包含按页面物理顺序创建的 `DotSettings`、`DotMusic` 与 `DotBooks`。非当前页圆点为 `5 x 5 px`、圆角 `3 px`、`White1` 且背景透明度 `180`；当前页指示器为 `14 x 5 px` 的水平胶囊、同一 `White1` 且背景透明度 `220`。初始当前页是 Music，因此初态由 `DotMusic` 显示胶囊。

分页控制器确认切页后，旧当前页指示器动画收缩为圆点（宽度 `14 → 5`、透明度 `220 → 180`），新当前页圆点同时伸展为胶囊（宽度 `5 → 14`、透明度 `180 → 220`）；高度始终为 `5 px`。该动画与页面吸附同步，首轮时长为 `160 ms`、使用 ease-out；手势未达到翻页阈值而回到原页时，不触发圆点状态切换动画。SquareLine 仍是圆点的唯一静态样式来源：`Service/gui/main` 初始化时从 `DotSettings` 和 `DotMusic` 读取非活动与活动的实际宽度、透明度基线，切页时只对 `DotSettings`、`DotMusic`、`DotBooks` 执行这两项运行时动画，不覆盖其颜色、圆角或布局。快速连续切页会取消同一圆点的旧动画，并从当前已绘制状态继续过渡。

分页控制、局部毛玻璃和 Main 初始化在当前原型阶段继续保留于 `Service/gui/main/` 的同一 Module：它们共用 SquareLine 对象绑定、物理槽位映射与滚动事件时序，暂不为了目录形式拆成多个浅 Module。待循环分页与圆点动画均完成并通过真机验证后，再审视是否按职责拆出 `main/pager/`（吸附、循环、圆点）与 `main/background/`（壁纸与局部毛玻璃）；届时 Interface 必须隐藏 LVGL 回调顺序和对象映射细节，确保拆分能够提升 Locality 与 Leverage，而非只移动文件。

BooksPage 和 SettingsPage 已直接共用 Main 的单一 StatusBar；它们后续复用 MusicPage 的卡片视觉语言时，仍不得复制新的状态栏对象或单独设置系统壁纸。

#### 10.5.2 Music 局部毛玻璃（已实现，待真机验收）

`MusicModeTabs` 需要真实局部毛玻璃：目标区域内显示从系统壁纸**当前屏幕坐标**裁剪出的模糊像素，区域外壁纸保持清晰。播放器的圆形控制按钮不使用毛玻璃：在 240 px 宽屏上的可见收益不足以抵消额外缓冲与裁剪复杂度，仍使用 SquareLine 的半透明染色、弱描边和图标。该效果不能直接赋给 SquareLine 组件的背景色，也不为每个组件建立独立 Canvas。

运行时实现由 `Service/gui/canvas/` 与 `Service/gui/main/` 共同负责，不修改 `GUI/`：

1. 在 `Main` 的布局已计算后，从当前清晰壁纸生成一次全屏 Blur 工作帧。复用现有 `service_gui_effect_canvas_buffer` 与隐藏 Canvas 工作对象；随后立即复制到 `main/` 自己长期持有的全屏模糊壁纸，以免 Boot 或其他离屏效果复用 Canvas 工作区后覆盖数据。
2. `main/gui_service_main` 在 SDRAM 中长期持有一张与 `MusicModeTabs` 同尺寸的裁剪图。它从长期全屏 Blur 壁纸按 Tabview 相对 `ui_Main` 的实际坐标提取连续像素；`canvas/gui_service_canvas_compositor` 提供通用矩形裁剪 Interface。裁剪图尺寸与目标对象相同，LVGL 会在对象区域内绘制它。
3. 将裁剪图在运行时绑定为 `ui_MusicModeTabs` 的 Background image，并保持 `ui_Main` 的清晰 SquareLine 壁纸不变。`main/` 监听 MainPageContainer 的 `LV_EVENT_SCROLL`；每次横滑都读取 MusicModeTabs 的实际坐标，并从长期 Blur 壁纸重新裁剪同一张输出图。这样玻璃区域始终采样其当前下方背景，而不是带着初始位置的静态模糊贴图移动。隐藏 Canvas 始终不参与可见层级。

`MusicModeTabs` 的整个可见区域（标签栏与当前 Tabpage 内容区）都是一块连续的圆角玻璃区域，不只处理标题栏。当前 `Service_GUI_Init()` 在 `ui_init()` 后调用 `service_gui_main_prepare_background()`：它先对 `ui_Main` 调用 `lv_obj_update_layout()`，再读取 `ui_MusicModeTabs` 的实际屏幕坐标；随后以 `ui_Main` 左上角为裁剪图原点换算为壁纸内坐标。MainPageContainer 横滑期间重复同一坐标换算，越出壁纸边界的输出像素写为透明，保证对象半离屏时仍不读越界。这样不把 SquareLine 中的相对尺寸、位置或对象名称复制成 Service 内的固定像素常量。裁剪图与 Tabview 尺寸相同，LVGL 对该对象执行圆角 Background image 绘制，因此圆角外不会留下方形模糊块。

`MusicModeTabs` 的 LVGL 内部 Content container 不由 SquareLine 直接暴露，`main/` Module 仅将该内部对象的背景、背景图、边框、轮廓和阴影设为透明，以消除 Simplified Theme 产生的白色内容底。`ui_MusicModeTabs` 的运行时 Background image 是唯一允许 Service 替换的 SquareLine 导出对象 Style，因为它承载本 Module 生成的裁剪模糊图；`ui_Main` 的 Background image 仍由 SquareLine 的清晰系统壁纸负责。除该背景源外，所有 SquareLine 导出对象（包括 MusicModeTabs 本体、Tabpage、Button）的 Border、Shadow、Radius、背景和文字样式必须只在 SquareLine 中配置，Service 不得覆盖。

为避免 Tabpage 的主题默认白底重新出现，用户必须在 SquareLine 中为 `MusicPage`、`NowPlayingTab`、`QueueTab`、`LibraryTab`、`BooksPage` 和 `SettingsPage` 的 `STYLE (MAIN)` 设置透明背景；若该对象不承担独立卡片视觉，还应将 Border、Outline 和 Shadow 设为零。`MusicModeTabs` 本体和三个播放器 Button 不在此清单内：它们应保留各自设计需要的半透明染色、弱描边、圆角或阴影，Service 会原样保留。此项配置改动后必须由用户重新导出 `GUI/`，不得手改生成代码。

当前 SquareLine 已导出如下静态控制骨架：

```text
MusicPage
├─ MusicModeTabs
│  └─ NowPlayingTab             当前为空白内容区
└─ MusicPlayerControlContainer  透明、水平布局、不滚动
   ├─ MusicPlayingSlider         Gray1 轨道、Blue1 进度
   ├─ MusicPreviousButton       圆形半透明控制按钮
   │  └─ MusicPreviousIcon      上一首符号 Label
   ├─ MusicPlayPauseButton      圆形半透明控制按钮，视觉略强
   │  └─ MusicPlayPauseIcon     初始播放符号 Label
   └─ MusicNextButton           圆形半透明控制按钮
      └─ MusicNextIcon          下一首符号 Label
```

`MusicPlayingSlider` 位于控制按钮上方。当前导出中，主轨道为低 Alpha `Gray1`，Indicator 使用 `Blue1`；`DEFAULT` 状态的 Knob 透明，`PRESSED` 状态才显示 `Blue1` Knob，并通过 `STYLE (KNOB) → Paddings` 增大。LVGL v8 的 Slider Knob 默认边长等于 Slider 较短边，因此若将来改为常显 Knob，仍应通过 Padding 调整其大小，而非修改 Slider 本体的宽高。三个 Button 当前使用 `WhiteMask1` 的低 Alpha 填充，按下态提高 Alpha；不设置独立边框，图标使用 `White1`。当前不在 SquareLine 中添加播放事件。每个图标均由 Button 的独立 Label 子对象承载并居中对齐，以便后续 Service 将 `MusicPlayPauseIcon` 的播放符号替换为暂停符号；不得用 ImageButton 或导入图标图片资源。项目已启用的 `lv_font_montserrat_16` 包含 LVGL 的 `PREV`、`PLAY`、`PAUSE` 与 `NEXT` 符号；用户优先从 SquareLine 的符号选择器使用它们，不新增图标图片资源。三个按钮与外层 `MusicPlayerControlContainer` 均不是毛玻璃区域。

当前导出的壁纸为 `LV_IMG_CF_TRUE_COLOR_ALPHA`，一张全屏帧约 `230400 B`。Music 运行时会同时持有共享 Canvas 工作区、一张 Main 长期全屏 Blur 壁纸和一张最大 `240 x 192` 的 MusicModeTabs 裁剪背景，三者峰值上限约 `585 KiB`；实际 MusicModeTabs 比该上限更小，但静态缓冲按安全上限预留。壁纸切换时才重新执行全屏模糊；静态显示、文字更新和每次 `Service_GUI_Process()` 都不得重复模糊。MainPageContainer 横滑的 `LV_EVENT_SCROLL` 只更新局部裁剪图；SquareLine 重新导出导致对象尺寸或位置改变后，会在下一次初始化按新布局重新建立首帧裁剪。若 SquareLine 将 MusicModeTabs 高度扩展到屏幕的 60% 以上，必须先审校并提高 `main/gui_service_main_config.h` 中的上限。未来切换到 RGB565 缓存资源后，三帧合计约 `390 KiB`。

多个 Main Page 的玻璃区域不能共用一张固定局部裁剪图而不加管理：当前只在 Music 页面实施；BooksPage、SettingsPage 后续需要各自的裁剪图与更新时机。MainPageContainer 的滑动回调只会更新 MusicModeTabs，且只做从长期模糊壁纸到局部输出缓冲的像素复制，不将软件模糊放入动画路径。当前可复用的私有 Canvas Interface 接收完整模糊图、对象区域和调用方持有的输出缓冲；未来页面可复用裁剪算法，但仍必须各自持有页面级背景与决定重建时机。新增页面级玻璃区域前必须先经用户审校。

#### 10.5.3 Queue 行模板（运行时按 Length 复制）

SquareLine 1.6.1 没有 List 控件。Queue 不用 `lv_list`。SquareLine 只保留**一份**行范本（当前导出名为 `SongPanel1` 及其子对象）；不要在编辑器里 Duplicate 成 8 行。`Service/gui/main/queue` 把 `GUI/screens/ui_Main.c` 里该范本的构造序列摘进 for 循环。可见行数跟 READY 后的 `Length` 走，**至多** `SERVICE_GUI_MAIN_QUEUE_MAX_ROWS`（须与 `STORAGE_LISTBUFFER_MAX_ENTRIES` 同为 8），不是按整表无限 `create`，也不预先造满 8 个空行。已构造行在后续 `Length` 变短时 Hidden。窗口沿播放列表移动时，在这些 panel 上转 head 回收改字，不把 `storage_listbuffer` 改成环形数组。导出的范本行隐藏。不得手改 `GUI/`。竖向滚动由 `QueueTab` 承担；不得打开 Tabview 内部 Content 的滚动。GUI Task 在滑动过程中根据 `Service_GUI_QueueScrollLead()` 改 `Index` 再 `request`：Index=0 不留上一窗，其余留一行给回滑。换窗从当前 `scroll_y` 扣整行高度，保留手指剩下的像素，不吸回整页，也不像 MainPager 那样等 `SCROLL_END` 吸附。SquareLine 重新导出后若范本构造变了，对照更新 create 函数。

范本层级：

```text
QueueTab                         透明；Flex Column；纵向 Scrollable
 └─ SongPanel1                   范本；WhiteMask1 底 Opa 40，圆角 8，无 Shadow
      ├─ SongInfoContainer1      左侧文字组，固定宽度，勿用 Content 撑开
      │    ├─ SongName1          曲名
      │    └─ SongCreator1       歌手，White1 约 Opa 160
      └─ SongStatus1             右侧符号 Label；非当前行 Opa 0 占位，不 HIDDEN
```

| | 窗口内正在播放的那一行 | 其它行 |
| --- | --- | --- |
| 曲名过长 | **Scroll circular**；宽 `pct(100)`，高度锁成一行 | **Dot**；同一行高（LVGL 8 的 DOT 看高度溢出） |
| 左边条 | Border 仅 Left、4 px、`Blue1` Opa 跟范本 | 同一 Width/Side，**Border Opa 0**（不改 Width，避免文字漂移） |
| 右侧符号 | 音乐标 `LV_SYMBOL_AUDIO`，`Blue1`，可见。范本仍可占位 `S`，运行时替换 | 同一 Label，Opa 0 占位，不从布局移除 |
| 曲名颜色 | `Blue1` | `White1` |

点按（所有行）：`PRESSED` 只提高底 Opa（约 70～80）并加 **Outline** 1 px `White1`（Pad 0）。不要在 PRESSED 或非当前态里改 Border Width，以免文字漂移。非当前行只把左边条 Border Opa 打到 0。Panel 开 Clickable，不开 Checkable。

READY 时 GUI Task 按 `Length` 把 `Buffer[i]` 原样交给 `Service_GUI_QueueApply()`（播放列表 `Index + i`）。当前 `Buffer` 仍是曲库路径；标题/歌手由以后的 `load` 调解析器写入窗口，见 [catalog_architecture.md](catalog_architecture.md) 第 5 节。未落地前歌手 Label 为空串。GUI 不裁 `Music/`、不裁 `.mp3`、不按文件名切开。`Length == 0` 则没有可见行。正在播放行由「播放列表游标」判定，游标与 Catalog 同代次，见 [catalog_architecture.md](catalog_architecture.md)；游标未落地前，列表下标 0 若在本窗则当当前曲。复制行只改文字、Border Opa、符号 Opa、曲名色和 Long mode；不改 Border Width。右侧音乐标是运行时写入的 `LV_SYMBOL_AUDIO`，不要把 SquareLine 里的占位 `S` 抄回固件。字形、字体、圆角、底色以 SquareLine 范本构造为准。`Service/gui` 不得包含 `storage_listbuffer.h`。Queue 行不放封面；封面属于 Now Playing。

`QueueTab` 在 SquareLine 模拟器里仍可能看到 Tabview 默认白底；真机由第 10.5.2 节的 Content 透明与毛玻璃处理。行底用浅白薄片，不要再做成 `#13223D` 实心卡。

## 11. 原型验收标准

- 用户可在三张 MainPageContainer 内容页间稳定循环左右切换，且 StatusBar 保持固定；
- Music 内部标签只能通过点击顶部标签切换；在其内容区左右滑动时，手势只交给 MainPageContainer；
- MusicPlayingSlider 与三个静态控制按钮的默认、按下视觉状态正确；
- BooksPage、SettingsPage 当前保持空白占位，不误表现为已实现的阅读器、专辑详情或设置子页；
- 后续新增 Album Detail、Mini Player、Reader、MainPagerDots 或 Settings 子页前，先同步本文档再在 SquareLine 中实现；
- 首版无需真实歌曲、书籍、文件系统或背光硬件即可完整演示交互流程。
