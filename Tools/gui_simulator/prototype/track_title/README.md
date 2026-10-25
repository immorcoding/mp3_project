# PROTOTYPE：唱盘页曲名呈现

一次性决策原型，不进入产品固件，也不作为产品 Screen 的第二事实源合并到 `main`；候选代码只保存在本临时分支，定稿后把决定交给后续 spec。问题是：唱盘页的曲目显示名放在哪里、用多大字号、过长如何呈现，以及无卡／空库／坏文件跳过／切歌过渡时显示什么。

三种方案在真实 Now Playing 页上切换：

- **A · Top scroll 16**：曲名上置，16 px，过长时单行循环滚动。
- **B · Bottom 2-line 18**：曲名下置，18 px，最多两行，超出以 `…` 收尾。
- **C · Side 3-line 14**：唱盘居左、曲名居右，14 px，最多三行，超出以 `…` 收尾。

## 方案截图

| 方案 | 短曲名 | 超长曲名 | 中英混排 |
| --- | --- | --- | --- |
| A | [短名](shots/A_short.png) | [长名](shots/A_long.png) | [混排](shots/A_mixed.png) |
| B | [短名](shots/B_short.png) | [长名](shots/B_long.png) | [混排](shots/B_mixed.png) |
| C | [短名](shots/C_short.png) | [长名](shots/C_long.png) | [混排](shots/C_mixed.png) |

状态演示采用一个具体提案供用户反应，并非已批准决定：无卡与空库显示 `--`，原因继续由现有时间／状态行表达；坏文件跳过时立即显示下一首候选曲名并保留现有 `Skipped 1 file` 提示；切歌开启期间立即显示新曲名，时间显示 `0:00/--:--`，不保留旧曲名。

## 运行

```powershell
./Tools/gui_simulator/prototype/track_title/run.ps1
```

约 6 秒后自动进入唱盘页，无需解锁。操作：

- `←` / `→` 或底部 `<` / `>`：切换 A／B／C。
- `↑` / `↓`：切换状态；点击底部中间文字也可顺序切换。
- `V`：下一方案；`N`：下一状态（供脚本使用）。
- `Esc`：退出。

底部悬浮条与控制台日志会显示当前完整方案／状态。悬浮条是原型工具，不属于候选界面。

## 截图

`demo.args` 使用确定性虚拟时钟截取三种方案的短曲名／长曲名／中英混排，以及 C 方案的无卡、空库、坏文件跳过和切歌过渡：

```powershell
$exe = './build/gui_simulator_track_title_prototype/gui_simulator.exe'
$out = './build/gui_simulator_track_title_prototype/shots'
New-Item -ItemType Directory -Force -Path $out
$arguments = @('--prototype-track-title', '--hidden', '--out-dir', $out)
Get-Content './Tools/gui_simulator/prototype/track_title/demo.args' -Encoding UTF8 |
    Where-Object { $_ -and -not $_.StartsWith('#') } |
    ForEach-Object { $arguments += ($_ -split '\s+') }
& $exe @arguments
```

截图是 `240×320` BMP。提交中的 `shots/` 保存转换为 PNG 后的审阅副本。

## 字体

`lv_font_track_title_14/16/18.c` 是 Noto Sans CJK SC Regular 2.004 的演示子集，含 ASCII 与本原型用到的汉字、全角括号和省略号；4 bpp、未压缩。它们只用于比较字号与排版，不预设完整字集、字体存放或加载方案。

- 源字体：<https://github.com/notofonts/noto-cjk/blob/main/Sans/OTF/SimplifiedChinese/NotoSansCJKsc-Regular.otf>
- SHA-256：`2c76254f6fc379fddfce0a7e84fb5385bb135d3e399294f6eeb6680d0365b74b`
- 许可：SIL Open Font License 1.1，见 `OFL.txt`。
- 重新生成：`python generate_fonts.py <NotoSansCJKsc-Regular.otf>`（需要 Node/npm，固定 `lv_font_conv@1.5.3`）。
