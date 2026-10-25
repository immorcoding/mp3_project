# PROTOTYPE：唱盘页曲名呈现

一次性决策原型，不进入产品固件，也不作为产品 Screen 的第二事实源合并到 `main`；候选代码只保存在本临时分支，定稿后把决定交给后续 spec。

## 已定与待定

用户在第一轮已决定：

- 过长曲名使用**单行循环滚动**。
- 无卡／空库显示 `--`，原因继续由现有时间／状态行表达。
- 坏文件跳过显示下一首候选曲名，并保留现有 `Skipped k file(s)` 提示。
- 切歌取得新曲目身份后立即显示新曲名，时间为 `0:00/--:--`，进度为 0。

布局与字号尚未定。第一轮 A/B/C 的 14–18 px、多行和缩小唱盘方案已被用户否决：字号过大、排版不协调、侵占唱盘空间。

## 第二轮：原尺寸唱盘 + 小字号

三种方案都保持原生 **144 px** 唱盘，曲名都为单行；按 `P` 在同一方案内比较 **12 px / 10 px**。

- **D · Above**：曲名上置；唱盘仅比原布局下移 2 px。
- **E · Below**：曲名下置；唱盘保持 144 px 并上移 10 px，时间／进度／按钮不动。
- **F · Overlay**：唱盘尺寸与位置完全不变，曲名悬浮于唱盘下沿；短名底板随文字收宽，长名限宽滚动。

### 12 px 截图

| 方案 | 短曲名 | 超长曲名 | 中英混排 |
| --- | --- | --- | --- |
| D | [短名](shots/round2/D_short_12.png) | [长名](shots/round2/D_long_12.png) | [混排](shots/round2/D_mixed_12.png) |
| E | [短名](shots/round2/E_short_12.png) | [长名](shots/round2/E_long_12.png) | [混排](shots/round2/E_mixed_12.png) |
| F | [短名](shots/round2/F_short_12.png) | [长名](shots/round2/F_long_12.png) | [混排](shots/round2/F_mixed_12.png) |

### 10 px 与状态截图

- 10 px 长曲名：[D](shots/round2/D_long_10.png) · [E](shots/round2/E_long_10.png) · [F](shots/round2/F_long_10.png)
- E / 12 px 状态：[无卡](shots/round2/E_no_card_12.png) · [空库](shots/round2/E_empty_12.png) · [坏文件跳过](shots/round2/E_bad_skip_12.png) · [切歌过渡](shots/round2/E_next_track_12.png)

## 运行

```powershell
./Tools/gui_simulator/prototype/track_title/run.ps1
```

约 6 秒后自动进入唱盘页，无需解锁。操作：

- `←` / `→` 或底部 `<` / `>`：切换 D／E／F。
- `↑` / `↓`：切换状态；点击底部中间文字也可顺序切换。
- `P`：在 12 px 与 10 px 之间切换。
- 数字键 `1..7`：直达短名、长名、中英混排、无卡、空库、坏文件跳过、切歌过渡。
- `V`：下一方案；`N`：下一状态（供脚本使用）；`Esc`：退出。

底部悬浮条与控制台日志显示当前完整方案／字号／状态。悬浮条是原型工具，不属于候选界面。

## 截图复现

`demo-round2.args` 使用确定性虚拟时钟截取第二轮 16 张图：

```powershell
$exe = './build/gui_simulator_track_title_prototype/gui_simulator.exe'
$out = './build/gui_simulator_track_title_prototype/shots-round2'
New-Item -ItemType Directory -Force -Path $out
$arguments = @('--prototype-track-title', '--hidden', '--out-dir', $out)
Get-Content './Tools/gui_simulator/prototype/track_title/demo-round2.args' -Encoding UTF8 |
    Where-Object { $_ -and -not $_.StartsWith('#') } |
    ForEach-Object { $arguments += ($_ -split '\s+') }
& $exe @arguments
```

截图是 `240×320` BMP；`shots/round2/` 保存转换为 2 倍 PNG 后的审阅副本。

## 第一轮归档

第一轮代码与字体见提交 `76ee112`；`demo-round1.args` 只与该提交配套。截图保留用于说明被否决的方案：

| 方案 | 短曲名 | 超长曲名 | 中英混排 |
| --- | --- | --- | --- |
| A · 上置 16 px | [短名](shots/A_short.png) | [长名](shots/A_long.png) | [混排](shots/A_mixed.png) |
| B · 下置 18 px | [短名](shots/B_short.png) | [长名](shots/B_long.png) | [混排](shots/B_mixed.png) |
| C · 右侧 14 px | [短名](shots/C_short.png) | [长名](shots/C_long.png) | [混排](shots/C_mixed.png) |

## 字体

当前 `lv_font_track_title_10/12.c` 是 Noto Sans CJK SC Regular 2.004 的演示子集，含 ASCII 与本原型用到的汉字、全角括号和省略号；4 bpp、未压缩。它们只用于比较字号与排版，不预设完整字集、字体存放或加载方案。第一轮的 14/16/18 px 字体仍在历史提交中。

- 源字体：<https://github.com/notofonts/noto-cjk/blob/main/Sans/OTF/SimplifiedChinese/NotoSansCJKsc-Regular.otf>
- SHA-256：`2c76254f6fc379fddfce0a7e84fb5385bb135d3e399294f6eeb6680d0365b74b`
- 许可：SIL Open Font License 1.1，见 `OFL.txt`。
- 重新生成：`python generate_fonts.py <NotoSansCJKsc-Regular.otf>`（需要 Node/npm，固定 `lv_font_conv@1.5.3`）。
