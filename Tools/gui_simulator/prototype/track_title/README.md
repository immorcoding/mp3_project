# PROTOTYPE：唱盘页曲名呈现

一次性决策原型，不进入产品固件，也不作为产品 Screen 的第二事实源合并到 `main`；候选代码只保存在本临时分支，定稿后把决定交给后续 spec。

## 已确定

- 曲名字号 **12 px**。
- 过长曲名使用**单行循环滚动**。
- 无卡／空库显示 `--`，原因由时间／状态行表达。
- 坏文件跳过显示下一首候选曲名，并保留 `Skipped k file(s)` 提示。
- 切歌取得新曲目身份后立即显示新曲名，时间为 `0:00/--:--`，进度为 0。

## 第三轮：重排间距与视觉层级

用户未批准第二轮布局，具体反馈是“上下间距要重排、视觉样式仍不满意”。本轮因此重排整段布局，而不是只移动曲名。三种方案都保持原生 **144 px** 唱盘、12 px 曲名和透明文字底色：

- **G · Center**：上置居中；Playing 标签 → 曲名 → 唱盘 → 原时间行形成均匀间隔。
- **H · Below split**：唱盘下方左曲名、右时间／状态，下方只留进度与控制；分栏对齐进度条边宽。
- **I · Above split**：唱盘上方左曲名、右时间／状态，唱盘下方只留进度与控制。

| 方案 | 短曲名 | 超长曲名 | 中英混排 |
| --- | --- | --- | --- |
| G | [短名](shots/round3/G_short.png) | [长名](shots/round3/G_long.png) | [混排](shots/round3/G_mixed.png) |
| H | [短名](shots/round3/H_short.png) | [长名](shots/round3/H_long.png) | [混排](shots/round3/H_mixed.png) |
| I | [短名](shots/round3/I_short.png) | [长名](shots/round3/I_long.png) | [混排](shots/round3/I_mixed.png) |

H 分栏状态：[无卡](shots/round3/H_no_card.png) · [空库](shots/round3/H_empty.png) · [坏文件跳过](shots/round3/H_bad_skip.png) · [切歌过渡](shots/round3/H_next_track.png)

## 运行

```powershell
./Tools/gui_simulator/prototype/track_title/run.ps1
```

约 6 秒后自动进入唱盘页，无需解锁。操作：

- `←` / `→` 或底部 `<` / `>`：切换 G／H／I。
- `↑` / `↓`：切换状态；点击底部中间文字也可顺序切换。
- 数字键 `1..7`：直达短名、长名、中英混排、无卡、空库、坏文件跳过、切歌过渡。
- `V`：下一方案；`N`：下一状态（供脚本使用）；`Esc`：退出。

底部悬浮条与控制台日志显示当前方案／状态。悬浮条是原型工具，不属于候选界面。

## 截图复现

`demo-round3.args` 使用确定性虚拟时钟截取第三轮 13 张图：

```powershell
$exe = './build/gui_simulator_track_title_prototype/gui_simulator.exe'
$out = './build/gui_simulator_track_title_prototype/shots-round3'
New-Item -ItemType Directory -Force -Path $out
$arguments = @('--prototype-track-title', '--hidden', '--out-dir', $out)
Get-Content './Tools/gui_simulator/prototype/track_title/demo-round3.args' -Encoding UTF8 |
    Where-Object { $_ -and -not $_.StartsWith('#') } |
    ForEach-Object { $arguments += ($_ -split '\s+') }
& $exe @arguments
```

截图是 `240×320` BMP；`shots/round3/` 保存转换为 2 倍 PNG 后的审阅副本。

## 历史方案

- **第一轮**（`76ee112`，`demo-round1.args`）：14–18 px、多行和缩小唱盘；因字号过大、排版不协调、侵占唱盘空间被否决。
- **第二轮**（`30d12db`，`demo-round2.args`）：保留 144 px，D 上置／E 下置／F 下沿悬浮，10/12 px；用户确定 12 px，但上下间距与视觉样式仍需调整。[D](shots/round2/D_long_12.png) · [E](shots/round2/E_long_12.png) · [F](shots/round2/F_long_12.png)

历史 `demo-round1.args` / `demo-round2.args` 分别只与对应提交配套；当前代码用于第三轮。

## 字体

当前 `lv_font_track_title_12.c` 是 Noto Sans CJK SC Regular 2.004 的演示子集，含 ASCII 与本原型用到的汉字、全角括号和省略号；4 bpp、未压缩。它只用于比较排版，不预设完整字集、字体存放或加载方案。历史字号字体仍在对应提交中。

- 源字体：<https://github.com/notofonts/noto-cjk/blob/main/Sans/OTF/SimplifiedChinese/NotoSansCJKsc-Regular.otf>
- SHA-256：`2c76254f6fc379fddfce0a7e84fb5385bb135d3e399294f6eeb6680d0365b74b`
- 许可：SIL Open Font License 1.1，见 `OFL.txt`。
- 重新生成：`python generate_fonts.py <NotoSansCJKsc-Regular.otf>`（需要 Node/npm，固定 `lv_font_conv@1.5.3`）。
