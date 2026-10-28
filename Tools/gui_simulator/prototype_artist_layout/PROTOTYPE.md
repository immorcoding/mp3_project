# PROTOTYPE · #152 唱盘页歌手行版式（throwaway，不合入）

本目录和 `Tools/gui_simulator/prototype_artist_layout.c` 只在 `prototype/vinyl-artist-layout` 分支上，用来回答 #146 B 节的问题：唱盘页的歌手行放在哪里、长什么样。用户选定版式后，实施票（#153）按选定的取值在 `view/` 和 `main/title` 里正式实现。本分支不开 PR，不改基线，也不合入。

## 原型做了什么

- 没有改 `Service_GUI_TitleApply`。原型在每圈 `gui_music_step()` 之后查看曲名 Label 的文本：文本变了，就用名称槽替身最后一次应答里的 `Artist` 写歌手 Label。曲名为空白或 `--` 时，歌手写空串。这样近似于“曲名和歌手在同一次 Apply 里写入”。
- 歌手 Label 和曲名用同一个字体：`service_gui_font_title()`，即资源包 Noto 12 px（line_height 16），Label 高 18。两者都居中，都不响应点击。滚动速度同曲名，24 px/s。
- 假标签放在 `fakes/storage_sim.c` 的 `storage_sim_tags`：

| 位置 | 曲名 | 歌手 | 状态 |
|---|---|---|---|
| 0 | Clair de Lune | Claude Debussy | 有歌手 |
| 1 | Gymnopedie No.1 | （无） | 无歌手 |
| 2 | 晴天 | 周杰伦 | 中文 |
| 3 | Nuvole Bianche | Ludovico Einaudi / Yiruma | 多歌手 |
| 4 | Comptine d'un autre ete | Yann Tiersen / The Royal Philharmonic Orchestra / London Symphony Chorus | 超长歌手 |
| — | `--`（解锁后、开播前，没有当前曲） | 空 | 曲名 `--` |

## 运行

```powershell
cmake -S Tools/gui_simulator -B build/gui_simulator -G Ninja -DCMAKE_C_COMPILER=gcc
cmake --build build/gui_simulator
./build/gui_simulator/gui_simulator.exe --artist-layout 1   # 0 = 现状，1 = A，2 = B，3 = C
```

截图用的脚本（`T` 键切换到 Solid，在 6500 ms 按下）：

```text
--artist-layout N [--key 6500:t] --drag 6000:120,290:120,80:250 --shot 6800:6_dash
--tap 7000:120,263 --shot 7600:1_artist --tap 7700:180,263 --shot 8300:2_no_artist
--tap 8400:180,263 --shot 9000:3_cjk --tap 9100:180,263 --shot 9700:4_multi
--tap 9800:180,263 --shot 10400:5_long --shot 12400:5_long_later
```

## 版式取值

Y 是相对 Playing 标签页内容区的坐标，括号里是 LCD 绝对坐标（内容区原点在 LCD y = 39）。现状的取值是：唱盘 Y 4（LCD 43..186），曲名 Y 160（LCD 199），进度条 LCD 216..220，时间 LCD 224..234，三键 LCD 242..285。

| | A：曲名下方·留空 | B：曲名上方·省略号 | C：曲名下方·收起 |
|---|---|---|---|
| 唱盘 Y | 0（上移 4） | 0（上移 4） | 4（不动） |
| 曲名 Y（有歌手） | 145（LCD 184），上移 15 | 160（LCD 199），不动 | 150（LCD 189），上移 10 |
| 歌手 Y | 160（LCD 199），占原曲名位，在曲名下方 | 145（LCD 184），在唱盘和曲名之间 | 165（LCD 204），在曲名下方 |
| 歌手宽度 | 160 | 180 | 160 |
| 调色板角色 | Ink | Ink | Accent |
| Opa | 180（同 Queue 歌手） | 140 | 200 |
| Long mode | 超宽时 `SCROLL_CIRCULAR`，否则 `CLIP` 居中（同曲名） | `LONG_DOT`（截断，末尾加 `...`） | 超宽时 `SCROLL_CIRCULAR`，否则 `CLIP` 居中 |
| 无歌手 | 留空：歌手行空着，曲名停在 145 | 留空：歌手行空着，曲名停在 160，画面等于现状（唱盘上移 4） | 收起：曲名移到 157（LCD 196），在唱盘和进度条之间居中 |
| 曲名 `--` | 曲名 `--` 在 145，歌手空 | 曲名 `--` 在 160，歌手空 | 曲名 `--` 在 157，歌手空 |
| 进度条 / 时间 | 不动 | 不动 | 下移 6（进度条 LCD 222..226，时间 230..240） |
| 三键 | 不动 | 不动 | 不动 |

三种版式共同的地方：切歌时曲名先变空白，空白期间曲名的 Y 不变；名字（或 `--`）到了，才按有没有歌手决定曲名放在哪个 Y。这样每次切歌曲名最多挪一次。

## 截图

- 对照图：`sheet_A.png`、`sheet_B.png`、`sheet_C.png`，以及现状 `sheet_current.png`。每张两行，上面是 Default，下面是 Solid；列依次是有歌手、无歌手、中文、多歌手、超长、超长 +2 s、曲名 `--`。
- 单张截图（2 倍放大）：`A/`、`B/`、`C/` 下的 `<状态>_<default|solid>.png`。

## 观察（仅供参考，由用户选定）

- A：曲名离唱盘下沿只有 2 px，两行文字贴着进度条，比较挤；超长歌手名会滚动。
- B：曲名的位置和现状完全一样；歌手用省略号截断，屏幕上没有第二条滚动动画；Opa 140 在 Solid 外观下偏淡。
- C：Accent 的颜色区分最明显。但 Solid 外观的 Accent 是蓝色，压在蓝色壁纸上对比度偏低；进度条和时间下移 6，与三键的间距变小。
