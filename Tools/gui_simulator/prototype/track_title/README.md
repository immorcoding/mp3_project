# PROTOTYPE：唱盘页曲名呈现

一次性决策原型，不进入产品固件，也不作为产品 Screen 的第二事实源合并到 `main`；候选代码只保存在本临时分支，定稿后把决定交给后续 spec。

## 已确定

- 曲名字号 **12 px**。
- 过长曲名使用**单行循环滚动**。
- 无卡／空库显示 `--`，原因由时间／状态行表达。
- 坏文件跳过显示下一首候选曲名，并保留 `Skipped k file(s)` 提示。
- 切歌取得新曲目身份后立即显示新曲名，时间为 `0:00/--:--`，进度为 0。

## 当前：第四轮 J（按用户指定顺序）

用户以 H 为基础提出“进度条上方曲目，下方时间”。J 因此改为：

**144 px 唱盘 → 居中曲名独占一行 → 进度条 → 居中时间／状态 → 播放按钮**

曲名不再与时间左右分栏。唱盘保持 144 px 并小幅上移；进度略上移，播放按钮仅下移 4 px，时间／状态仍复用同一个 Label。

| 短曲名 | 超长曲名 | 中英混排 |
| --- | --- | --- |
| [J 短名](shots/round4/J_short.png) | [J 长名](shots/round4/J_long.png) | [J 混排](shots/round4/J_mixed.png) |

状态截图：[无卡](shots/round4/J_no_card.png) · [空库](shots/round4/J_empty.png) · [坏文件跳过](shots/round4/J_bad_skip.png) · [切歌过渡](shots/round4/J_next_track.png)

### 第三轮候选仍可在模拟器内切换

- **G · Center**：上置居中，时间保留原位。[长名](shots/round3/G_long.png)
- **H · Below split**：下置左曲名、右时间／状态。[长名](shots/round3/H_long.png)
- **I · Above split**：上置左曲名、右时间／状态。[长名](shots/round3/I_long.png)

## 运行

```powershell
./Tools/gui_simulator/prototype/track_title/run.ps1
```

约 6 秒后自动进入唱盘页，无需解锁。操作：

- `←` / `→` 或底部 `<` / `>`：切换 G／H／I／J。
- `↑` / `↓`：切换状态；点击底部中间文字也可顺序切换。
- 数字键 `1..7`：直达短名、长名、中英混排、无卡、空库、坏文件跳过、切歌过渡。
- `V`：下一方案；`N`：下一状态（供脚本使用）；`Esc`：退出。

底部悬浮条与控制台日志显示当前方案／状态。悬浮条是原型工具，不属于候选界面。

## 截图复现

`demo-round4.args` 使用确定性虚拟时钟截取 J 的 7 张图：

```powershell
$exe = './build/gui_simulator_track_title_prototype/gui_simulator.exe'
$out = './build/gui_simulator_track_title_prototype/shots-round4'
New-Item -ItemType Directory -Force -Path $out
$arguments = @('--prototype-track-title', '--hidden', '--out-dir', $out)
Get-Content './Tools/gui_simulator/prototype/track_title/demo-round4.args' -Encoding UTF8 |
    Where-Object { $_ -and -not $_.StartsWith('#') } |
    ForEach-Object { $arguments += ($_ -split '\s+') }
& $exe @arguments
```

截图是 `240×320` BMP；`shots/round4/` 保存转换为 2 倍 PNG 后的审阅副本。

## 历史方案

- **第一轮**（`76ee112`，`demo-round1.args`）：14–18 px、多行和缩小唱盘；因字号过大、排版不协调、侵占唱盘空间被否决。
- **第二轮**（`30d12db`，`demo-round2.args`）：保留 144 px，D 上置／E 下置／F 下沿悬浮，10/12 px；用户确定 12 px，但上下间距与视觉样式仍需调整。
- **第三轮**（`a61653e`，`demo-round3.args`）：G／H／I 重排纵向间距与分栏；用户选择以 H 为基础继续调整为 J。

历史 `demo-round1.args` / `demo-round2.args` / `demo-round3.args` 分别只与对应提交配套；当前代码另包含第四轮 J。

## 字体

当前 `lv_font_track_title_12.c` 是 Noto Sans CJK SC Regular 2.004 的演示子集，含 ASCII 与本原型用到的汉字、全角括号和省略号；4 bpp、未压缩。它只用于比较排版，不预设完整字集、字体存放或加载方案。历史字号字体仍在对应提交中。

- 源字体：<https://github.com/notofonts/noto-cjk/blob/main/Sans/OTF/SimplifiedChinese/NotoSansCJKsc-Regular.otf>
- SHA-256：`2c76254f6fc379fddfce0a7e84fb5385bb135d3e399294f6eeb6680d0365b74b`
- 许可：SIL Open Font License 1.1，见 `OFL.txt`。
- 重新生成：`python generate_fonts.py <NotoSansCJKsc-Regular.otf>`（需要 Node/npm，固定 `lv_font_conv@1.5.3`）。
