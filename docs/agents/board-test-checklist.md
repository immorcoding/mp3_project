# 板级验收清单模板

每次上板复制本模板为一条 issue 评论（标题 `## 板级验收 <n>/<m>：<主题>（#<票>）`），填完贴出；规则见 WF-3。样本由 [make_card.py](../../Tools/board_test_card/README.md) 生成。

## 烧录与日志

- 产物：固件 `cmake --preset firmware-debug` 后 `cmake --build --preset firmware-debug`，得 `build/firmware-debug/mp3_project.hex`；资源包 `python Tools/package_maker/main.py`，得 `build/external-resources/resource_pack.bin`（包长与 PackageVersion 见同目录 `resource_pack_manifest.json`）；样本卡 `python Tools/board_test_card/make_card.py <输出目录> --copy-to <盘符>:\Music --set <子集>`。在集成分支末端的独立 worktree 里构建。
- 固件：DFU 只写内部 Flash，不动 NOR。
- 资源包：STM32CubeProgrammer 用 ST-LINK 连接，**External loaders** 勾选 `W25Q256_STM32H743ZG`（已安装，无需辅助固件）；地址 `0x90401000`，勾 **Verify programming**，不用 **Full chip erase**、不用 **Run after programming**；写后读 `0x90401028`（8 B）核对 PackageVersion。参数与排查见 [External Loader](../../Tools/external_loader/README.md)。
- 串口日志：板子 USB CDC 口（本机常为 COM3），`./Tools/board_test_card/capture-serial.ps1 -Port COM3 -OutFile <日志路径>` 逐行加时间戳写文件，复位或重新枚举后自动重连；日志放会话临时目录，不进仓库。日志文件允许边写边读（如 `tail -F`）。

## 怎么填

1. 烧录后按启动日志 `BOOT: FW <提交号>[-dirty] <构建类型>` 填页首“固件提交”与“构建”（日志为 `FW unknown` 说明版本串生成有问题，在票里报）；中途重烧，在每个受影响行的“固件提交”列写新的提交号。
2. 先做 A 组（出声）再做其余；每行结果只能是下表四个词之一，细节写备注。
3. “证据来源”列写实际用的来源：串口日志 / DWT / 示波器 / 逻辑分析仪 / 听感 / 屏幕 / map。标 `无固件出口：仅听感/外部仪器` 的行，没有仪器就写 未测，不得凭印象写 通过。
4. 超出阈值的项只记录，不自行调参；在票里问。
5. 汇总后，把 部分 / 未测 的项列入“未测项清单”。

| 结果 | 含义 | 计入通过？ |
|---|---|---|
| 通过 | 方法做完，满足阈值，有证据来源 | 是 |
| 部分 | 只做了一部分（缺样本、缺量化、口述未逐项确认） | 否，保留 `hw:pending` |
| 未测 | 没做，或没有证据来源 | 否，保留 `hw:pending` |
| 记录 | 仅记录数值，无阈值 | 不适用 |

完成标准：所有行都有结果词；票只在无 部分 / 未测 行时移除 `hw:pending`。

## 页首（必填）

- 固件提交：`＿＿`　构建：☐ Debug ☐ Release　日期：＿＿　测试人：＿＿
- 板卡 / DAC：＿＿　SD 卡（型号、容量）：＿＿（按手上的卡验收，时序数字只代表这张卡）
- 必备样本集（由 `make_card.py` 生成，按其打印的顺序复制；缺项在对应行写 部分 / 未测）：
  - ☐ 32 kHz、☐ 44.1 kHz、☐ 48 kHz，MPEG-1 L3 立体声 320 kbps，各 ≥ 3 分钟
  - ☐ 约 1 MB ID3v2 标签文件　☐ 短于 1 s 文件　☐ MPEG-2 22.05 kHz 单声道（应被跳过）
  - ☐ `bad-zero.mp3`（200000 B 全零）　☐ `bad-empty.mp3`（0 B）
  - ☐ 曲目显示名文件名样本（`--set names`，9 个，复制进 `Music/`，建议格式化后单独复制；用于 0.7.0 T7、T8）
  - ☐ 跳转样本（`--set seek`，四首刻度音；用于清单 2 S 组）　☐ 大目录（`--set bigdir`，12000 首含末尾子目录；用于清单 1 O 组、清单 3 R6.6）　☐ 长跑样本（`--set soak`，三首 320 kbps；用于清单 1 C 组）
  - 配置：A 全集；B 首个文件是 `bad-zero.mp3`（`--set first-bad`）；C 整库不可播（`--set all-unsupported`）；D 空库
  - 另需至少 5 首真实歌曲和 1 张可拔插的卡用于交互项
  - 一张卡放综合（`all`、`seek`、真实歌曲）、异常（整库不可播）、大目录三个文件夹，只复制一次，另有长跑样本单独放 `Soak` 文件夹（库里只有三首才是三首循环），上板前把要用的改名成 `Music`，没有 `Music` 就是空库；卡上留约 1 GiB。这个做法没在板上试过，第一次上板时确认
- 实际样本库（文件数、码率、采样率）：＿＿

## 清单 1：播放后端

### A. 出声与基本操作

| # | 项 | 方法 | 固件提交 | 证据来源 | 结果 | 备注 |
|---|---|---|---|---|---|---|
| A1 | 开机不自动播放 | 上电观察 | | 听感 / 屏幕 | | |
| A2 | 32 / 44.1 / 48 kHz 各一首出声 | 样本播放 | | 听感 | 32k＿ 44.1k＿ 48k＿ | |
| A3 | 暂停 → 恢复；恢复重放约 24 ms 可接受 | 听感 | | 听感 | | |
| A4 | 切歌（上一首 / 下一首 / 点行） | 听感 | | 听感 | | |
| A5 | 切到其他页面音乐继续 | 观察 | | 听感 / 屏幕 | | |
| A6 | 播完自动下一首；播完间隙 ＿＿ ms | 观察 + 录音 | | 听感 / 外部仪器（`无固件出口：仅听感/外部仪器`） | | |
| A7 | 咔哒声：暂停 / 恢复 / 切歌 / 曲间 | 听感 + 录音 | | 听感（`无固件出口：仅听感/外部仪器`） | | |
| A8 | MPEG-2 22.05 kHz 文件被跳过 | 样本播放 | | 屏幕 | | |
| A9 | 1 MB ID3 标签文件能播、跳过耗时 ＿＿ s | 样本播放 | | 听感 / 屏幕 | | |
| A10 | 短于 1 s 文件不卡死 | 样本播放 | | 屏幕 | | |

### B. 时序与资源

| # | 项 | 阈值 | 固件提交 | 证据来源 | 实测 | 结果 |
|---|---|---|---|---|---|---|
| B1 | 单帧解码 CPU 时间（320 kbps 立体声） | ≤ 4 ms（诊断） | | DWT（需加测点，固件无出口） | | |
| B2 | 单次唤醒最长执行时间（不含 Stop/Start） | ≤ 6 ms | | DWT（需加测点） | | |
| B3 | 半区释放 → 播放任务开始运行 | ≤ 1 ms | | 逻辑分析仪 | | |
| B4 | Start 阻塞 + 一次补满半区（E8） | ≤ 12 ms | | DWT / 逻辑分析仪 | | |
| B5 | 播放任务 CPU 占比 | ≤ 20% | | 串口日志（Monitor 批次的各任务 CPU 占比） | | |
| B6 | 播放任务栈高水位（栈 20480 B） | 有余量 | | 串口日志（`Playback Task` 行，单位 B） | | |
| B7 | 堆最低余量 | 记录 | | 串口日志（`Heap` 行） | | 记录 |
| B8 | 音频读取等 SD 锁最长（非故障，含块池刚空时） | ≤ 1 s，超过就报给用户 | | 持锁统计汇总 | | |
| B9 | 插卡后扫描曲库耗时 | 记录 | | 串口日志（扫描结束 INFO：结果、条数、耗时） | | 记录 |
| B10 | 坏文件单次跳过耗时 | 记录 | | 无固件出口：仅听感/外部仪器 | | 记录 |
| B11 | GUI 最长一帧（播放 / 不播放） | 记录 | | 串口日志（Monitor 的 GUI 最长帧行） | | 记录 |

### C. 稳态（两段长跑，都在 Debug 上）

连续播放时长要计时记录；未跑满的行写 部分 并写实际时长。块池约 26 s 的余量会掩盖等锁，第二段必须切歌，欠载为 0 不代表不受影响。

| # | 项 | 方法 | 阈值 | 固件提交 | 证据来源 | 实测 | 结果 |
|---|---|---|---|---|---|---|---|
| C1 | 纯播放 2 h | 32、44.1、48 kHz 三首循环（`--set soak`，末曲环回首曲），唱盘页无人值守；开头 10 分钟手动滑动 Queue、切主题 | 欠载 0；T_out 误触发 0；日志丢失 0；访问文件的任务栈最低剩余 ≥ 256 B | | Monitor 批次 + `Switch` 行 + `Stop reason` 行 | | |
| C2 | 播放 + Flash 压力 30 分钟 | 开 Flash 压力钩子，压到回收水位以下（本板 `low` = 462），按秒表切歌和翻 Queue | 欠载 0；切歌到出声 ≤ 100 ms；音频读取等 SD 锁 ≤ 1 s | | 同 C1 + 持锁统计 | | |
| C3 | 播放时回收且欠载为 0 | C2 期间确认有真擦除 | 真擦除次数 > 0；欠载 0 | | 钩子的空闲块数和擦除次数 + Monitor 批次 | | |

### D. 故障注入与硬件事实

| # | 项 | 阈值 / 要求 | 固件提交 | 证据来源 | 实测 | 结果 |
|---|---|---|---|---|---|---|
| D1 | T_out 故障注入（停 I2S 时钟或 DMA） | T_out + 一次唤醒延迟内触发 | | 示波器 / 逻辑分析仪 | | |
| D2 | DMA Abort 耗时；5 ms 超时可否复现；DeInit/Init 后能否恢复 | 记录；重试可用 | | 示波器 / 串口日志 | | |
| D3 | `Platform_Audio_Stop` 返回后不再回调 | 0 次回调 | | 无固件出口：仅听感/外部仪器（无回调计数） | | |
| D4 | XSMT 斜坡时长；PCM5102A standby → 就绪 | 记录 | | 示波器 / 逻辑分析仪 | | 记录 |
| D5 | 实际 Fs 与 BCK、DAC 锁定 | 偏差 ≤ ±0.4% | | 逻辑分析仪 | | |
| D6 | ES0392：SPE 使能后是否需等约 1 µs；关 SPE 前 TXP/EOT | 记录 | | 勘误原文 + 示波器 | | 记录 |
| D7 | R = 64 KiB 误判 | 0 首 | | 串口日志 / 听感 | ＿ 首 | |

### E. 拔卡 / 插卡

| # | 项 | 阈值 | 固件提交 | 证据来源 | 结果 |
|---|---|---|---|---|---|
| E1 | 播放中拔卡 | 立即停止，CARD_REMOVED | | 屏幕 / 听感 | |
| E2 | 拔卡后再插卡 | 不续播 | | 屏幕 / 听感 | |
| E3 | 播放中轻触卡槽 | 流保持，不误判拔卡 | | 屏幕 / 听感 | |

### F. 本次改动专项（`hw:pending`，每次按票填写）

| # | 项 | 固件提交 | 证据来源 | 结果 |
|---|---|---|---|---|
| F1 | I2S2 TX DMA NVIC 优先级 9 工作正常 | | 听感 / 串口日志 | |
| F2 | 链接 map：`.playback_pcm`（AXI SRAM）与 `.storage_audio_pool`（SDRAM）放置 | | map | |
| F3 | 配置 B：首个文件是坏文件仍跳到下一首 | | 屏幕 / 听感 | |
| F4 | 配置 C：整库不可播，最终 ALL_BAD、不无限轮转 | | 屏幕 | |

### O. 打开耗时（0.9.0，#172 O1–O8）

样本：大目录文件夹（`make_card.py <输出目录> --copy-to <盘符>:\Music --set bigdir`，曲库 12000 首，其中 11998 首同在一个文件夹、最后 2 首在排在末尾的子目录 `zz-tail-subdir/`，用法与容量见[样本卡 README](../../Tools/board_test_card/README.md#大目录卡--set-bigdir)）。5 个探针位置：第 1、3000、6000、9000、12000 首（≥ 3 分钟 CBR；文件名以 `00001-`、`03000-`、`06000-`、`09000-`、`12000-` 开头）。阈值为 #170 的推断值，板测回填后更新 #172 与 STOR 规则中的初值。O1–O4 只测两端探针：第 1 首在 Queue 首行，第 12000 首从第 1 首按 PREV 绕到；中间探针在 Queue 里滑不过去，打开耗时随位置线性增长，两端已能界定（#181）。

| # | 项 | 方法 | 固件提交 | 证据来源 | 阈值 / 预期 | 实测 | 结果 |
|---|---|---|---|---|---|---|---|
| O1 | 曲名打开 | 大目录，在第 1、12000 首切歌 | | 曲名 INFO 的 `open=` | 单次 ≤ 约 30 ms | 1＿ 12000＿ | |
| O2 | 窗口加载 | 在列表开头连续滚动音乐队列 | | 窗口汇总行（若出现）与自启动最长值 | 一窗 ≤ 约 0.5 s，无超阈汇总行 | | |
| O3 | 开流 | 在第 1、12000 首开播 | | `Stream` INFO 的 `open=`、`lseek=` | 打开 ≤ 约 30 ms | 1＿ 12000＿ | |
| O4 | 跳转重开流 | 在第 1、12000 首上跳转 | | `Stream` INFO（off≠0）+ `Seek` INFO 的 `lat` | 同 O3；`lat` ≤ 100 ms | | |
| O5 | 定位符核对 | O1–O4 全程 | | `Locator miss` WARN 与计数器 | 记录（预期 0；退回正确性由 FULL 保证） | | 记录 |
| O6 | 扫描 12000 首 | 插入大目录 | | 扫描结束 INFO（B9 记录项） | 记录 | | 记录 |
| O7 | 内存 | 核对 `.map` | | map：曲库、定位符数组与路径池的 SDRAM 占用 | 记录；定位符约 188 KiB | | 记录 |
| O8 | 常规曲库不退化 | 现有板测卡（约 50 首）切歌、滚动、播放 | | 现有日志 | 与 0.8.2 行为一致 | | |

## 清单 2：音乐页播放交互

样本：至少 5 首正常文件 + 1 个坏文件；一张可拔插的卡。

### A. 命令与回显

| # | 项 | 方法 / 预期 | 固件提交 | 证据来源 | 结果 |
|---|---|---|---|---|---|
| A1 | 连按下一首 5 次 | 前进 5 首；不闪烁 | | 屏幕 | |
| A2 | 连按上一首 | 同上 | | 屏幕 | |
| A3 | 连点 3 个不同行 | 即时高亮；停在最后一行；无闪烁 | | 屏幕 | |
| A4 | 暂停中按上一首 / 下一首 | 换曲，保持暂停 | | 屏幕 / 听感 | |
| A5 | 停止时按播放 | 开播 | | 屏幕 / 听感 | |
| A6 | 播放键图标跟随真实三态 | 拔卡、出错后图标不骗人 | | 屏幕 | |
| A7 | Queue 高亮为当前曲；滚动位置不被打断 | 滚动后切歌 | | 屏幕 | |
| A8 | 点行同时滚动列表 | 高亮闪一下：无 / 有（可接受？） | | 屏幕 | 记录 |

### B. 进度、时间与唱盘

| # | 项 | 固件提交 | 证据来源 | 结果 |
|---|---|---|---|---|
| B1 | 时间 `m:ss/m:ss`；总时长未知 `0:42/--:--`，进度条停 0 | | 屏幕 | |
| B2 | 暂停时时间与进度条冻结 | | 屏幕 | |
| B3 | 切歌后时间回 0，总时长先 `--:--` | | 屏幕 | |
| B4 | 拖动进度条真跳转：滑块跟手，松手后播放跳到该位置，时间与进度条从落点继续走（0.9.0 起不再回真实位置） | | 屏幕 + `Seek` INFO（tgt/land） | |
| B5 | 唱盘只在播放时转，暂停停住，恢复续转 | | 屏幕 | |
| B6 | 开机后第一次播放，唱盘是否回跳（约 6°） | | 屏幕 | 记录 |
| B7 | 切歌、同曲重选、停止归零时唱盘角度归零；跳转不归零。依据 `TrackStartCount` 判断：计数增加才应归零，跳转时计数不变、唱盘角度不跳变 | | 屏幕 + 串口日志（`TrackStartCount`） | |

### C. 状态行（真屏可读、不截断）

无法在真机触发的原因写 未测并说明，不要留空。

| # | 停止原因 | 触发方法 | 真屏短语 | 固件提交 | 证据来源 | 结果 |
|---|---|---|---|---|---|---|
| C1 | CARD_REMOVED | 播放中拔卡 | `No SD card` | | 屏幕 | |
| C2 | NOT_READY | 无卡时按播放 | `SD card not ready` | | 屏幕 | |
| C3 | NO_TRACK | 空库 | `No tracks` | | 屏幕 | |
| C4 | CATALOG_CHANGED | 曲库变化后点旧窗口的行 | `Library changed` | | 屏幕 | |
| C5 | READ_ERROR | 播放中卡接触不良 | `Read error` | | 屏幕 | |
| C6 | NO_DATA | T_data 到期 | `Read timeout` | | 屏幕 | |
| C7 | AUDIO_ERROR | 可重试的音频错误 | 看屏填写 | | 屏幕 | |
| C8 | AUDIO_FAULT | STOP_FAILED 锁存 | 看屏填写 | | 屏幕 | |
| C9 | ALL_BAD | 整库不可播 | `No playable files` | | 屏幕 | |
| C10 | NONE | 开机未播过 | `0:00/--:--`，无错误提示 | | 屏幕 | |

### D. 提示时长

| # | 项 | 预期 | 固件提交 | 证据来源 | 实测 | 结果 |
|---|---|---|---|---|---|---|
| D1 | `Skipped 1 file` 显示时长 | 约 2 s | | 屏幕 + 秒表 | | |
| D2 | 连续跳过累加 `Skipped k files`，每次重新计时 | | | 屏幕 | | |
| D3 | 状态行闪烁时长 | 约 300 ms | | 无固件出口：仅听感/外部仪器（目视难量） | | |
| D4 | 每次按播放进入停止都闪一下 | | | 屏幕 | | |
| D5 | 停止原因一直显示到开始播放或换曲；离开页面再回来仍在 | | | 屏幕 | | |
| D6 | 闪烁观感可接受 | | | 屏幕 | | 记录 |

### E. 异常场景

| # | 项 | 预期 | 固件提交 | 证据来源 | 结果 |
|---|---|---|---|---|---|
| E1 | 拔卡 | Queue 清空、`No SD card`、唱盘归零 | | 屏幕 | |
| E2 | 命令队列满时点行 | 高亮撤回；日志有 `queue full` | | 串口日志 | |
| E3 | 三键与进度条任何时候都不置灰 | | | 屏幕 | |

### S. 跳转（0.9.0，#172 S1–S8；样本 `--set seek`）

`Seek` INFO 字段：`tgt`（目标）、`land`（落点）、`map`（映射方式）、`lat`（时延）、`clamp`（越尾夹取）。`--set seek` 的四首：CBR 320 无头、CBR 320 带 Info、VBR 带 TOC、VBR 无头；内容是刻度音（静音底，每 10 s 一声短哔，整分钟双哔）。

| # | 项 | 方法 | 固件提交 | 证据来源 | 阈值 / 预期 | 实测 | 结果 |
|---|---|---|---|---|---|---|---|
| S1 | 落点粗验 | `--set seek` 四首，各跳到刻度前约 2 s | | 屏幕时间 + 听感 + `Seek` INFO（tgt/land/map） | 屏幕走到刻度时听到哔声，偏差 ±0.5 s 内；VBR 无头只记录 | | |
| S2 | 播放中跳转时延 | 播放中前后各跳 5 次 | | `Seek` INFO 的 `lat` | `lat` ≤ 100 ms | | |
| S3 | 暂停中跳转 | 暂停后拖到 50% 松手，再按播放 | | 屏幕 + `Seek` INFO（预填完成时记） | 保持暂停、唱盘静止、显示落点；按播放从落点出声；`lat` 记录 | | |
| S4 | 越尾 | 四首各拖到 100%：播放中一次、暂停中一次 | | 屏幕 + `Seek` INFO（`clamp`）+ `Open idx=N` | 播放中放完并前进；暂停中停住，恢复后放完再前进 | | |
| S5 | 可闻瑕疵 | 播放中跳转 10 次，以清单 1 A7 切歌咔哒声为基准 | | 无固件出口：仅听感/外部仪器 | 不比切歌更明显为通过；更明显记录并另开票 | | |
| S6 | 跳转后连续收听 | 跳转后连续听 30 s | | 无固件出口：仅听感/外部仪器 | 无断续 | | |
| S7 | 连续松手与竞争 | 快速连拖 3 次；松手后立即按 NEXT；队列满不可造则记“未测” | | 屏幕 + `Seek` INFO 条数 + `Open idx=N` | 最终落在最后一次目标；NEXT 后换曲、无 `Seek` INFO | | |
| S8 | 总时长未知 | 切歌后首帧前、停止态下按住拖动 | | 屏幕 | 进度条不跟手、不置灰、无命令 | | |

## 清单 3：0.10.0 架构（[#202](https://github.com/immorcoding/mp3_project/issues/202)）

依据见 [0.10.0 板级验收](https://github.com/immorcoding/mp3_project/issues/198)、[迁移切片与分支顺序](https://github.com/immorcoding/mp3_project/issues/201)。

- **节奏**：并发票合入集成分支前先上板跑该票的专项行。前六次不算最终证据，只有第 7 次在集成分支末端同一提交上跑的才算（开测试钩子的行在同一提交上加开关构建）。
- **构建**：全部回归用 Debug；Release 只跑时序核心（切歌到出声、开流、跳转、持锁与等锁、栈、欠载、CPU），只记录，用同一组数字对照。
- **要报给用户的情况**（从第 2 次上板起就看，换不换回请求队列由用户决定）：拆分后访问文件的任务，调大栈后最低剩余仍不足 256 B，或 AXI SRAM 新段放不下这些栈；非故障情况下音频读取等 SD 锁超过 1 s，或欠载确实是等锁造成的；死锁或数据损坏的根因在锁方案本身。
- **人手碰不到的瞬间**（切歌后约 140 ms 内拔卡、30 ms 消抖内快速拔插、插卡初始化中途再拔）：手动做 10 次，靠日志判断是否落进窗口。命中至少一次算通过；一次都没命中记“部分”。
- **通过门槛**：必测档全部“通过”才建议合并。“部分”和“未测”由用户逐条接受，保留 `hw:pending`。走一遍档发现问题时开票，不自动阻塞合并。验收通过后，ARC-11、ARC-12、STOR-1、STOR-6、STOR-7、STOR-8、STOR-9、STOR-12 从 provisional 升回 settled。
- **超预算**只记录，在票里问，不自行调参。曲名和 Queue 清空没有旧基线，这两个数字最没底，超了不代表一定有问题。
- **Monitor 的 CPU 行有两种格式**（#218）：同一批里有 12 个任务（开机后几秒内多一个 Flash 启动任务）且占比很分散时，一位小数的完整格式放不下 127 字符，`monitor_cpu_format()` 整行退化成**整数百分比**（`gui=10`、`idle=86`，没有小数点）；平常是一位小数（`gui=10.0`）。读 R1.1、R7.4 的 CPU 空闲时按当时那一行的格式读，整数格式下的空闲占比误差小于 1 个百分点，不要当成数据异常。

### 回归范围

| 档 | 内容 | 清单行 |
|---|---|---|
| **必测** | 切歌、连按、拔插卡、重扫、跳转、坏文件跳过、播放中滑动 Queue、状态行、打开耗时 | 清单 1 的 A4–A6、A8–A10、E、F3、F4、O；清单 2 的 A、B4、B7、C、E、S；0.8.0 的 I8、I9、I11；清单 3 全部 |
| **走一遍** | 开机不自动播放、暂停恢复、咔哒声、时间与进度、唱盘、提示时长 | 清单 1 的 A1–A3、A7；清单 2 的 B1–B3、B5、D |
| **不测** | 解码时序、音频硬件事实、资源包与字体、字形和标签解析（和架构无关，或由主机测试和模拟器覆盖） | 清单 1 的 B1–B4、D；0.7.0、0.8.1、0.8.2 的字体与字形行；0.8.0 的 I1–I6 |

旧的待上板项：**带上**稳态与欠载、扫描和跳过耗时、靠后窗口 O2。**不带**（继续留在「板上状态」）字体回退 F8、另一外观 V7、真实中文曲库 I7、NOR 133 MHz、Flash 掉电恢复、音频硬件事实。

### 每次上板带上的回归行

| 上板 | 票 | 回归行 |
|---|---|---|
| 1 | 证据出口 + GUI 睡眠 | 清单 1 A2、A4；清单 2 A1、B4、B5、S2 |
| 2 | 文件接口三张 | 清单 1 A4、E；清单 2 C1、C2、E1；O3、O8 |
| 3 | 卷任务 | 清单 1 E；清单 2 C1、C2、E1 |
| 4 | 音频读取 | 清单 1 A4–A6、A8–A10、E、F3、F4、O3、O4；清单 2 C、S |
| 5 | 前台一次切 | 清单 2 A、B7、E；O1、O2、O5、O7、O8；0.8.0 I8、I9、I11 |
| 6 | 后台任务 | O6、B9；清单 2 C3、C4 |
| 7 | 完整验收 | 同一提交跑全部必测和走一遍档，加下面的 R7 行 |

### 专项行

标“钩子”的行在同一提交上加开关构建。参考值来自 spike（Debug），不是产品固件的实测。

**第 1 次：证据出口 + GUI 睡眠（烧两版，先只带证据，再加睡眠）**

| # | 项与方法 | 固件提交 | 证据来源 | 阈值 | 实测 | 结果 |
|---|---|---|---|---|---|---|
| R1.1 | CPU 空闲：唱盘、非唱盘、停播，两版对比 | | Monitor 批次 | 记录（参考：唱盘约 13%，停播非唱盘约 86%） | | |
| R1.2 | GUI 最长帧，两版对比 | | Monitor 的 GUI 最长帧行 | 记录（0.8.2 为 96 ms） | | |
| R1.3 | 欠载、日志丢失 | | Monitor 批次 | 0；日志丢了要注明，受影响的行不算通过 | | |
| R1.4 | 切歌到出声 | | `Switch idx= lat=` | 无负载 ≤ 50 ms | | |
| R1.5 | 扫描耗时 | | 扫描结束 INFO | 记录 | | |

**第 2 次：文件接口三张**

| # | 项与方法 | 固件提交 | 证据来源 | 阈值 | 实测 | 结果 |
|---|---|---|---|---|---|---|
| R2.1 | SD 读 32 KiB 持锁、表锁持有（不含大目录按路径打开） | | 持锁统计 | 各 ≤ 20 ms（参考 6.85 ms、10.1 ms） | | |
| R2.2 | 迟到 DMA（钩子）：单块超时 1 ms，紧接着再读并校验 | | 校验结果 + Abort 日志 | 内容一致；到上界还没停时卷保持 ERROR | | |
| R2.3 | `_USE_LFN 2` 后访问文件的任务的栈 | | Monitor 栈行 | 最低剩余 ≥ 256 B | | |
| R2.4 | GUI、播放调用文件接口即停机（临时改动构建，不提交） | | 串口日志 / 调试器 | 停机 | | |

R2.1 用钩子关闭的 Debug 构建量（探测会给被探测的那次读加约 10–15 ms 且持 SD 锁，钩子构建的持锁数字不是 R2.1 的证据）。R2.2 用钩子打开的 Debug 构建：`cmake --preset firmware-debug -DMP3_HOOK_SD_LATE_DMA=1` 后构建，串口日志出现 `W (…) HOOK: late_dma ON period=16 timeout=1ms`（**标签后面有冒号**，搜 `HOOK: late_dma ON`；没有这一行就是没开）。这一行由第一次 `InitSD()` 投，位置在初始化 SD 的时刻——第 2 次上板（旧顺序：Storage Task 先挂 Flash 再初始化 SD）实测是开机后约 9.8 s，`FLASH: Filesystem mounted.` 之后、`SD: Card ready` 之前；**第 3 次上板起（#211）由卷任务启动 SD，不再等 Flash 挂载，约在开机后 0.2 至 0.6 s、`SD: Card ready` 之后、`SD: Filesystem mounted.` 之前**；都不在 `BOOT: FW` 旁边，也不会被日志核心丢掉。烧录后正常播放，每 16 次读请求自动探测一次（连续播放约每十几秒一次），每次探测在日志里是两行：`W (…) FSABORT: stopped us=<微秒> R blk=0 n=64`（WARN，受害读超时后的中止，`us` 是中止耗时；第 2 次上板实测 42–43 µs）和 `I (…) SDHOOK: late_dma n=<序号> blk=<目标块> victim=timeout verify=ok`。**通过**：`victim=timeout verify=ok` 连续出现 ≥ 20 次，没有 `MISMATCH`、`read_failed`、`stuck`，播放不断音、不报读错误；`us` 记录最大值。`victim=done` 不算证据（那次没造出迟到 DMA）；`verify=MISMATCH` 是迟到 DMA 污染了紧接着的读，不通过；出现 `FSABORT stuck` 时卷保持 ERROR，检查是否真的保持（读文件失败、不自动恢复），再拔插卡看能否恢复，这条分支在健康卡上构造不出来，没出现就记“未测”。中途拔卡会得到 `FSABORT no_media`，可顺手记一次。

第 2 次上板的拔卡回归（清单 1 E、清单 2 E1）多看这几件事：读到一半拔卡有两条路径。HAL 先给出错误事件时，日志里没有 `FSABORT:` 行，`Stop reason=CARD_REMOVED` 约在拔卡后几十毫秒；等不到事件时，等待在约 500 ms（`FILESYSTEM_FATFS_BSP_DMA_TIMEOUT_MS`，BSP 不再用 FatFs 传来的 0xFFFFFFFF）后超时，日志出现 `FSABORT: no_media us=…`。两条路径都必须满足：`Card removed.` 只出现一次（不管拔卡落在读之前、之中、之后），随后重新插卡出现 `Card inserted` 和 `Filesystem mounted.`，不出现 `Filesystem mount failed` 或 `Mount failed`（文件槽没有释放时挂载会返回 BUSY）。出现 `FSABORT stuck` 时卷保持 ERROR，日志里是 `Hotplug refresh failed`，要拔卡再插卡才恢复；这条分支在健康卡上构造不出来，没出现就记“未测”。

**第 3 次：卷任务**

| # | 项与方法 | 固件提交 | 证据来源 | 阈值 | 实测 | 结果 |
|---|---|---|---|---|---|---|
| R3.1 | 读取中拔卡：不再吞掉拔出 | | `Stop reason=CARD_REMOVED` + 卷路径日志 | 通过；收尾时刻记录（参考 +34–35 ms） | | |
| R3.2 | 只收到 INSERTED 的换卡路径 | | 卷路径日志 | 卸载、发卷消失、挂载、发卷就绪 | | |
| R3.3 | 消抖内快速拔插、插卡初始化中途再拔（各 10 次）；暂停状态下快速抽插（5 次，记录实际行为） | | 卷路径日志 | 至少命中一次；状态正确 | | |
| R3.4 | 假抖动（钩子，加一次手动轻触卡槽） | | 卷路径日志 + 听感 | 只开闸，流和窗口不受影响 | | |
| R3.5 | `MountFlash` 期间插拔卡 | | 卷路径日志 | SD 不受影响 | | |
| R3.6 | 卷任务和 Flash 启动任务的栈 | | Monitor 栈行；Flash 启动任务自打一行 | ≥ 256 B | | |

第 3 次上板的构建与读法（#211；卷路径日志的完整格式和每个字段的含义见 [卷任务 README](../../APP/tasks/volume/README.md)“卷路径日志”）：

- **构建**：R3.1、R3.2、R3.3、R3.5、R3.6 和回归行用钩子关闭的 Debug 构建（`cmake --preset firmware-debug -DMP3_HOOK_FAKE_BOUNCE=0`，缓存变量会留在构建目录里，之前开过要显式关）。R3.4 用钩子打开的 Debug 构建：`cmake --preset firmware-debug -DMP3_HOOK_FAKE_BOUNCE=1` 后构建，串口日志出现 `W (…) HOOK: fake_bounce ON period=5000ms`（**标签后面有冒号**，搜 `HOOK: fake_bounce ON`；没有这一行就是没开）。钩子构建每 5 s 有一个 30 ms 的闸，R3.1 的收尾时刻要在钩子关闭的构建上量。
- **开机时间线**（参考，以串口 tick 为准）：开机后 0.1 s 内出现 `VOLUME: flash boot start`；SD 约 0.2 至 0.6 s 出现 `SD: Card ready`、`SD: Filesystem mounted.`、`VOLUME: sd boot path=mount …`；Flash 的 `MountFlash` 正常 3.3 s，之后出现 `VOLUME: flash ready path=mount … ms=<n>`；Flash 被压到只剩两个空闲块之后实测是 8.3 s，上次板测的 Flash 挂载大约 9 s，所以窗口很可能比 3 s 长得多，**以 `flash ready` 行的 tick 为准**。
- **判读用的三类行**（标签 `VOLUME`，正文）：边沿 `sd edge src=<irq|hook>`；对账 `sd settle path=<idle|gate_only|mount|unmount|remount> refresh=<ok|fail> card=<pri> mount=<ok|fail|-> seq=<前>-><后> edges=<n> ref=<ms>ms end=<ms>ms gate=<open|kept>`（`end` 是从第一个边沿到开闸的**收尾时刻**，`remount` 的 `seq` 是 `a->a+2`）；Flash `flash boot start` / `flash ready … ms=<n>` / `flash boot failed …`。

| # | 要做什么（计时从动作起算） | 通过时日志里看到 |
|---|---|---|
| R3.1 | 播放中拔卡，做 10 次，每次拔卡后等 3 s 再插回，等出现 `Scan done` 后再点播放（约 10 s 一轮）。至少一次落在读取中（日志有 `Audio: Audio read interrupted: …`） | `sd edge` → `sd settle path=unmount … seq=a->a+1 … end=<ms>ms` → `SD: Card removed.` → 播放侧 `Stop reason=CARD_REMOVED`；屏幕 “No SD card”、Queue 清空、游标重置；`end` 记录（参考 34 至 35 ms） |
| R3.2 | 播放中把卡快速抽出再按回（约 20 ms 内，一半抽出的手法），做到见到一次为止，最多 10 次 | `path=remount card=pri … seq=a->a+2`，随后 `Card inserted`、`Filesystem mounted.`、曲库重新扫描；播放侧 `CARD_REMOVED`（旧流作废） |
| R3.3 | ① 消抖内快速拔插：抽出再按回，10 次。② 插卡初始化中途再拔：卡推到位后**不到 0.15 s** 就拔出（插卡初始化约 186 ms），10 次。③ **暂停状态下**快速抽插：先暂停播放，再把卡抽出按回（约 20 ms 内），5 次，恢复播放后看实际行为（有没有出声、有没有读错误提示、日志是 `path=gate_only` 还是 `remount`），记录，不判通过与否（#195 “只开闸”的取舍：设备仍 READY 时走 `gate_only`，卡如果掉过电，要到下一次读才报错，需要再拔一次） | ① `edges=` 大于 1，或 `path=gate_only`/`remount`；② `gate=kept` 或 `sd edge during settle`。至少命中一次；最终状态都正确：卡在位有 `Filesystem mounted.` 且能播放，卡不在位 `Card removed.` 之后没有新的 `mount`。一次都没命中记“部分” |
| R3.4 | 钩子构建，播放 3 分钟（180 s，36 次人造边沿），其间手动轻触卡槽 3 次 | 连续 ≥ 20 次 `path=gate_only`，`seq=N->N` 不变；没有 `Stop reason`、没有 `CARD_REMOVED`；听感不断音，Queue 不闪；Monitor `Health` 的 `underrun` 增量 0；手动轻触的 `src=irq` 同样多数是 `gate_only` |
| R3.5 | 复位后看到 `VOLUME: flash boot start` 就开始计时：**第 2 s 拔卡、第 3 s 插回；第 5 s 拔卡、第 6 s 插回**（在 `flash ready` 之前做完才算数，`flash ready` 晚于第 6 s 时都在窗口里；`flash ready` 早于第 2 s 时改为复位后立刻拔卡、插卡） | 这些操作的 `sd settle` 行在 `flash boot start` 与 `flash ready` 之间出现，`end` 与平时同量级（约 35 ms，不被拖到秒级），状态正确；记录 `flash ready` 的 `ms=`（R6.5） |
| R3.6 | 做完上面所有插拔之后再读：Monitor 栈行 `Volume Task: total=4096 B, min free=…`；Flash 启动任务在 `flash ready` 之后自己打一行 `VOLUME: Flash Boot Task: total=4096 B, min free=… B, peak=…%` | 两者 `min free` 都 ≥ 256 B；更新 Monitor 栈行要等下一批（5 s 一批，插拔之后至少等 10 s） |

回归行：清单 1 E（E1 播放中拔卡立即停止，E2 拔后再插卡不续播，E3 播放中轻触卡槽流保持）；清单 2 C1（播放中拔卡 `No SD card`）、C2（无卡时按播放 `SD card not ready`）、E1（拔卡 Queue 清空、`No SD card`、唱盘归零）。这次回归比以前多看一件事：**在途读取被拔卡打断时，C1/E1 应显示 “No SD card” 并且游标重置**（原来是读取错误、游标保留，这是用户确认过的第三处可见变化），日志里伴随 `Audio read interrupted`，没有 `Audio stream read failed.`。


**第 4 次：音频读取**

| # | 项与方法 | 固件提交 | 证据来源 | 阈值 | 实测 | 结果 |
|---|---|---|---|---|---|---|
| R4.1 | 切歌到出声：无负载；Flash 写入中（钩子，块池余量会掩盖等锁，必须切歌） | | `Switch idx= lat=` | ≤ 50 ms；≤ 100 ms | | |
| R4.2 | 音频读取等 SD 锁最长，含块池刚空时 | | 持锁统计 | ≤ 1 s，超过就报告 | | |
| R4.3 | 开流 `open`、`lseek`，大文件跳转到末尾单独记 | | `Stream` INFO | `open` ≤ 30 ms；`lseek` 记录（没有实测过） | | |
| R4.4 | 定位符失效（钩子）：大目录开第 12000 首（在末尾子目录，要先找遍 11998 个目录项） | | 屏幕 + `Locator miss` | 能出声，不显示 “Read timeout” | | |
| R4.5 | 补块期（切歌后约 140 ms 内）拔卡，10 次 | | 屏幕 + `Stop` 日志 | “No SD card”，游标重置；至少命中一次 | | |
| R4.6 | 已读完、还在放内存数据时拔卡 | | 屏幕 + `Stop` 日志 | 立即停止 | | |
| R4.7 | 抖动后卡还在 | | 听感 + 日志 | 流继续，不断音不报错 | | |
| R4.8 | 扫描中点播放 | | 屏幕 | 立刻 “SD card not ready”，扫完能再点播放 | | |
| R4.9 | 拔卡时大目录按路径打开 | | 持锁统计 | 记录，确认很快失败（目前是推断） | | |
| R4.10 | 写 `CARD_REMOVED` 的延迟 | | `CARD_REMOVED` 时延行 | 记录 | | |
| R4.11 | 音频读取任务的栈 | | Monitor 栈行 | ≥ 256 B | | |
| R4.12 | 文件槽：音频流和 Storage Task 的读取同时打开文件（中间状态，这次上板时前台还没切；Storage Task 已随 #220 删除，此后由 R6.10 接着核对） | | `SERVICE_BUSY` 日志 | 不出现 | | |

第 4 次上板的构建与读法（#215；日志格式见 [音频读取任务 README](../../APP/tasks/audio_reader/README.md)“日志与消息池”，串口里每行前缀是 `<时间> <I|W|E> (<tick>) <标签>: `，标签后面有冒号）：

- **三个构建**（缓存变量会留在构建目录里，开过的钩子要显式关）：
  - **A 无钩子**：`cmake --preset firmware-debug -DMP3_HOOK_FLASH_PRESSURE=0 -DMP3_HOOK_LOCATOR_MISS=0 -DMP3_HOOK_FAKE_BOUNCE=0 -DMP3_HOOK_SD_LATE_DMA=0` 后构建。用于 R4.1（无负载）、R4.2、R4.3、R4.5 至 R4.8、R4.10 至 R4.12 和回归行。
  - **B Flash 压力钩子**：`-DMP3_HOOK_FLASH_PRESSURE=1`（其余同上）。串口出现 `W (…) HOOK: flash_pressure ON …`；约 4 分钟内 `FLASHHOOK: run free=…` 的 `free` 降到 462 以下且 `erases` > 0 才算压到位。用于 R4.1 的“Flash 写入中”。这一版里 Storage 和钩子都在回收（见音频读取任务 README“与 Flash 回收”），负载比产品重。
  - **C 定位符失效钩子**：`-DMP3_HOOK_LOCATOR_MISS=1`（其余同 A）。串口出现 `W (…) HOOK: locator_miss ON`，每次开流多一行 `STORAGE: Locator miss pos=…`。用于 R4.4、R4.9，卡用大目录（`--set bigdir`）。
- **R4.1（秒表）**：A 版，开机后点播放，从第 10 s 起每 5 s 点一次“下一首”，共 10 次（约 1 分钟）；B 版，等 `free` 压到位后，从秒表 0:10 起每 10 s 点一次“下一首”，共 10 次（约 100 s；`FLASHHOOK` 每 10 s 一行，切歌时钩子在写，块池余量会掩盖等锁，所以必须切歌）。读每次切歌的 `PLAYBACK: Switch idx=N lat=<µs>`（不带 `auto`）：A 版 ≤ 50000，B 版 ≤ 100000。
- **R4.2**：读最后一批 `FSLOCK: sd n=… hold=…us:<操作> wait=<µs>:<操作>` 的 `wait=`（开机以来的最大值，不清零；A 版和 B 版各自重启后读）。**测的时候屏幕停在唱盘页、不要滚 Queue**，这样 Storage 不读文件，最大等待就是音频读取任务的；每次切歌块池都是刚空的，所以 R4.1 的 10 次切歌就覆盖了“块池刚空”。阈值 ≤ 1000000 µs，超过就报告。
- **R4.3**：`I (…) STORAGE: Stream pos=<位置> off=<字节> open=<µs> lseek=<µs>`。开流：在第 1、3000、12000 首各点一次播放，`open` ≤ 30000。大文件跳转到末尾：播放一首约 7 MB 的长曲（`--set soak`），把进度条拖到 100% 一次，读那行 `off≠0` 的 `lseek=`，只记录。
- **R4.4（C 版，大目录卡）**：等 `Scan done` 出现（约 2.3 s）。秒表 0:00 在 Queue 里滑到最后一首（第 12000 首，在末尾子目录）点它，**0:03 前**应出声（按路径打开约 1.4 至 2.05 s）；日志有 `Locator miss pos=11999` 和 `Stream pos=11999 … open=<约 2000000>`；屏幕全程不出现 “Read timeout”。
- **R4.5（A 版）**：点“下一首”，**0.1 至 0.3 s 内**拔卡（先练手，凭手感），重复 10 次；每次拔卡后等 3 s 再插回，等 `Scan done` 再点播放。命中窗口的标志：新曲的 `Stream pos=` 行之后约 150 ms 内就有 `Audio: Audio read interrupted: volume unavailable, waiting for volume event.` 或 `Audio: CARD_REMOVED lat=…`，屏幕 “No SD card”、游标重置。至少命中一次算通过，一次都没命中记“部分”。读取被拔卡打断时，卷任务和读取任务同级、卡检测中断不能立刻让卷任务抢占，读取任务看到“卷不可用而卷序号还没变”，会先在等待点等最多 100 ms（`AUDIO_READER_VOLUME_GRACE_MS`，初值）的卷事件，所以这条路径下日志里只有 `Audio: CARD_REMOVED lat=…`、没有 `Audio read interrupted`，`lat` 仍是边沿到写 `CARD_REMOVED`，不会因为宽限而变成 100 ms。**如果仍看到 `Audio: Audio stream read failed.` 和屏幕 “Read error”**，说明宽限用完（卷任务在边沿后约 100 ms 内没发出卷事件，例如触点抖动拖长了消抖，或数据脚比检测脚先断、卡其实没被识别为拔出）：把次数、`VOLUME: sd edge` 与该行的 tick 差记在备注里，不要改常量。
- **R4.6（A 版）**：用一首能在 1 MiB 内读完的短曲（`--set seek` 的刻度音，或自备 ≤ 20 s 的 320 kbps 文件）。秒表 0:00 点播放，**0:03 拔卡**（文件早已读完、播放还在放内存里的数据）：立即停（屏幕 “No SD card”，`Stop reason=CARD_REMOVED`，日志有 `Audio: CARD_REMOVED lat=`）。做 3 次。
- **R4.7（A 版）**：播放中用手指轻触卡槽，间隔 3 s，共 5 次。通过：`VOLUME: sd settle path=gate_only … seq=N->N`，不出现 `Stop reason`、`Audio stream read failed.`，听感不断音。也可以另编假抖动钩子的构建（`-DMP3_HOOK_FAKE_BOUNCE=1`）连听 180 s，同 R3.4 的读法。
- **R4.8（A 版，大目录卡）**：秒表 0:00 把卡插进去，**0:01 点播放**（扫描约 2.3 s，还没完）：立刻（< 0.2 s）显示 “SD card not ready”，不出现 “Read timeout”；等到 `Scan done` 之后再点一次播放，能出声。
- **R4.9（C 版，大目录卡）**：秒表 0:00 点最后一首（按路径打开约 1.4 至 2.05 s，期间持 SD 锁），**0:00.5 拔卡**。读 `FSLOCK: sd … hold=<µs>:Open`（开机以来的最大持有，开机后第一次先重启，免得被别的最大值盖住）：确认远小于 2000000，即拔卡后很快失败；同时 `VOLUME: sd settle path=unmount` 的 `ref=` 记录。屏幕 “SD card not ready”：文件还没打开就没了卡，开流应答 `NOT_READY`（`Stop reason=NOT_READY`）；“No SD card” 只用于已打开的流被拔卡打断（第 4 次上板实测）。
- **R4.10**：把 R3.1、R4.5、R4.6 里所有 `Audio: CARD_REMOVED lat=<ms>ms seq=<前>-><后> stream=<n>` 的 `lat` 列出来，记录最小、最大（参考：卷任务卸载完成在边沿后 +34 至 35 ms，`lat` 是边沿到写 `CARD_REMOVED`）。`lat=-` 表示读不到边沿时刻。
- **R4.11**：全部做完后读 Monitor 栈行 `Audio Read Task: total=4096 B, min free=<B> B, peak=<…>%`（5 s 一批，插拔之后至少等 10 s），≥ 256 B。
- **R4.12**：全程（上面所有项）搜日志里的 `SERVICE_BUSY`，**不应出现**。另做一段专门的并发：秒表 0:00 起 60 s，一边播放一边每 5 s 点一次“下一首”、一边在 Queue 里连续滑动（让 Storage 读窗口和曲名）；做完再搜一次 `SERVICE_BUSY`。出现了就记下 `op=`、`pos=`、`n=` 并报告（SD 只有 4 个文件槽，音频流 1 个、Storage 1 个，按代码核对不该用尽）。
- **回归行**（A 版，在 R4.x 之间顺带做，不单独占时间）：清单 1 A4（切歌、上一首、点行）、A5（切到别的页面音乐继续）、A6（播完自动下一首，用 `--set soak` 或短曲）、A8（MPEG-2 22.05 kHz 被跳过）、A9（1 MB ID3 标签文件能播）、A10（短于 1 s 文件不卡死）、E1 至 E3（拔卡立即停止、拔后再插不续播、轻触不误判）、F3（首文件是坏文件仍跳到下一首，用 `--set first-bad`）、F4（整库不可播最终 `No playable files`，不无限轮转，用 `--set all-unsupported`）、O3（在第 1、12000 首开播，`Stream` 的 `open` ≤ 30 ms）、O4（在第 1、12000 首跳转，`Seek` INFO 的 `lat` ≤ 100 ms）；清单 2 C（C1 播放中拔卡 “No SD card”，**游标重置**；C2 无卡时按播放 “SD card not ready”，**另测插卡后曲库没好时点播放也是它**；C3 空库 “No tracks”；C6 的 5 s 超时不用特意触发；C9 整库不可播 “No playable files”）、S（S1 至 S8 的跳转，`--set seek`；S4 越尾和 S2 的 `lat` 重点看）。**本次回归多看一件事**：C1、E1 在途读取被拔卡打断时屏幕应是 “No SD card” 且游标重置，日志有 `Audio read interrupted`，没有 `Audio stream read failed.`（用户确认过的第三处可见变化）。

**第 5 次：前台一次切**

| # | 项与方法 | 固件提交 | 证据来源 | 阈值 | 实测 | 结果 |
|---|---|---|---|---|---|---|
| R5.1 | 连滑、连按时的显示延迟：曲名、新窗口 | | 显示延迟汇总 | 曲名 ≤ 200 ms；新窗口 ≤ 0.5 s | | |
| R5.2 | 拔卡后 Queue 清空 | | 显示延迟汇总 | ≤ 200 ms | | |
| R5.3 | `ui_cell` 持锁、GUI 拿不到锁的比例（真实负载） | | `UICELL` 行 | ≤ 5 ms；≤ 1% | | |
| R5.4 | 水位与让路（翻页注入钩子）：切歌后一帧内（GUI 看到新曲的那一圈，≤ 16 ms，不是 2 ms）自动翻页。让路改 0 再跑一遍对比 | | 持锁统计 + 钩子的补满耗时 | 前台等 SD 锁 ≤ 50 ms（推算 14–27 ms）；补满记录（推算约 140 ms） | | |
| R5.5 | 前台回退按路径打开时切歌 | | 屏幕 | 不显示 “Read timeout” | | |
| R5.6 | 各应用 step 的最长耗时 | | 前台 step 耗时日志 | 记录；核对 50 ms 阈值和 1 ms CPU 初值 | | |
| R5.7 | 前台任务的栈 | | Monitor 栈行 | ≥ 256 B | | |
| R5.8 | `.apps_sdram` 实际用量 | | map | 记录 | | |

第 5 次上板的构建与读法（#218；日志格式见 [前台任务 README](../../APP/tasks/foreground/README.md)、[音乐应用 README](../../APP/apps/music/README.md)“证据出口”和 [ui_cell README](../../APP/ui_cell/README.md)“持锁统计”，串口里每行前缀是 `<时间> <I|W|E> (<tick>) <标签>: `，标签后面有冒号）。**这次切的是前台任务、音乐前台流程和 GUI 客户，主机测试是单线程的，证明不了真实交错，下面每一行都靠这次上板**：

- **四个构建**（缓存变量会留在构建目录里，开过的钩子要显式关；全部五个钩子都写出来，免得上一次的留着）：
  - **A 无钩子（Debug）**：`cmake --preset firmware-debug -DMP3_HOOK_PAGE_TURN=0 -DMP3_HOOK_LOCATOR_MISS=0 -DMP3_HOOK_FLASH_PRESSURE=0 -DMP3_HOOK_FAKE_BOUNCE=0 -DMP3_HOOK_SD_LATE_DMA=0` 后 `cmake --build --preset firmware-debug`。用于 R5.1 至 R5.3、R5.6 至 R5.8 和全部回归行（I11 的“窗口加载中拔卡”用 C 版更好命中，见下）。
  - **B 翻页注入钩子（Debug）**：同 A，只把 `-DMP3_HOOK_PAGE_TURN=1`。开机日志出现 `W (…) HOOK: page_turn ON watermark=4 timeout=5000ms`（搜 `HOOK: page_turn ON`，没有就是没开；这一行在前台任务的第一步里投，开机后很早就出现）。用于 R5.4。
  - **C 定位符失效钩子（Debug）**：同 A，只把 `-DMP3_HOOK_LOCATOR_MISS=1`。日志出现 `W (…) HOOK: locator_miss ON`；之后音频读取任务**和前台任务**每次开文件前都把定位符清零，走“退回按路径打开”，每次一行 `STORAGE: Locator miss pos=…`。卡用大目录（`--set bigdir`）：按路径打开最后一首约 1.4 至 2.05 s 且持 SD 锁。用于 R5.5、R5.6（慢 step 会出 WARN，这里是预期）、I11，R5.7 也在这版上读一次（按路径打开栈最深）。
  - **D Release**：`cmake --preset firmware-release` 后 `cmake --build --preset firmware-release`（不带任何钩子，Release 本来就不能开钩子）。只用于对照：R5.6、R5.7 的常开日志照常有；**`UICELL:` 和 `DISPLAY:` 两种行 Release 里不应出现**（持锁统计和显示延迟汇总的开关，Debug 开、Release 关，没出现就说明开关生效）；顺手用眼睛确认连滑 Queue 和连按下一首的观感。
- **样本卡**：常规库（`--set all`，约 50 首也行，I8、I9、O8、R5.1 的曲名）、大目录（`--set bigdir`，R5.1 的新窗口、O1、O2、R5.5、I11）、含坏文件的库（`--set first-bad`，I9 的跳过）。一张卡放多个文件夹时把要用的改名成 `Music`（做法见页首，没在板上试过）。
- **串口日志要开着**（`capture-serial.ps1`），每一行的“实测”都从日志读。下面写的秒表时间是动作的**开始时刻**，读数要在动作结束后再等 10 s（汇总行每 10 s 才出一行，且只有本节拍有新样本时才出）。

| # | 要做什么（秒表） | 通过时日志里看到 |
|---|---|---|
| R5.1 | **A 版，常规库**。复位，等 `Scan done`（常规库很快）。0:00 点播放；0:05 切到 Queue 页，**0:05 至 0:35 连续上下滑动 Queue 30 s**（每秒快滑一个来回，让新窗口一直在换）；0:40 切回唱盘页，**0:40 至 1:10 每秒点一次“下一首”**（30 次）；1:20 读。再换**大目录库**重做一遍 0:05 至 0:35 的滑动（滑到中段和末尾）。 | 每 10 s 一行 `DISPLAY: title n=<样本数> max=<该 10 s 最大 ms>ms top=<开机以来最大 ms>ms win n=… max=…ms top=…ms clear n=… max=…ms top=…ms`。取 `title` 和 `win` 两段的 `top` 当实测：曲名 `top` ≤ 200，新窗口 `top` ≤ 500。**延迟按需求序号计，每个需求只在第一次显示其结果时计一次**：曲库发布后前台对同一需求的重答不计入，所以扫描时间不会混进曲名延迟；复位后第一个需求的第一次应答（往往是代次 0 的 `--`）会计入，它很快。样本数 `n` 是 0 说明那一类没测到，要重做。记两张卡（常规、大目录）各自的数 |
| R5.2 | **A 版，常规库**，Queue 页上有窗口时：**0:00 拔卡**（卡槽带锁，一次推出即可，做不到 30 ms 内抽插这次也不要求），屏幕应立即出 “No SD card”、Queue 清空；**0:15 插回**，等 `Scan done` 和 Queue 重新出内容后，**0:30 再拔**。共 5 次。 | 每次拔卡后等 10 s，读 `DISPLAY: … clear n=<次数> max=<ms>ms top=<ms>ms`。`clear` 的 `top` ≤ 200 ms；`n` 累加到 5 说明 5 次都量到了。屏幕上看得到 Queue 立即清空，不是等几百毫秒才空 |
| R5.3 | R5.1 的负载做完之后，**不重启**，读最后一行 `UICELL: n=<拿锁次数> hold=<持锁最长 µs>us gui=<GUI 尝试次数> busy=<没拿到的次数> pct=<占比 %>`。 | `hold` ≤ 5000（µs，即 5 ms）；`pct` ≤ 1.0。`busy` 与 `gui` 是开机以来的累计，所以要读 R5.1 全部做完后的那一行 |
| R5.4 | **B 版，常规库**（大目录也行，窗口更长）。复位，确认有 `HOOK: page_turn ON`。0:00 点播放；**0:10 起每 10 s 点一次“下一首”，共 10 次**（到约 1:50）。每次切歌，GUI 看到新曲的那一圈（最慢一帧 16 ms）自动写一次翻页需求，前台马上去读下一窗 12 行标签，同时音频读取任务在补块（补到水位约 140 ms）。做完再读。可选对照：临时把 `AUDIO_READER_YIELD_TICKS` 改成 0 重新构建（不提交，也不算钩子），同样做一遍，只记录对比。 | ① 10 行 `HOOK: page_turn refilled after=<ms>ms ready=<块> free=<块>`（推算 `after` 约 140，记录最大值）；没有 `HOOK: page_turn timeout`；② 最后一批 `FSLOCK: sd n=… hold=…us:<操作> wait=<µs>:<操作>` 的 `wait=` ≤ 50000（µs；它是**所有调用者**的最长等待，这时只有前台和音频读取任务在读，所以它是前台等 SD 锁的上界；推算 14000 至 27000）；③ 没有 `FOREGROUND: Foreground step slow`；④ Monitor `Health` 的 `underrun` 增量 0，听感不断音 |
| R5.5 | **C 版，大目录库**。复位，等 `Scan done`（约 2.3 s）和 `HOOK: locator_miss ON`。0:00 切到 Queue 页，把滚动条拖到最底（最后一窗，里面有末尾子目录里的两首）；**0:03 点最后一首**（开流按路径打开约 1.4 至 2.05 s，同时前台还在按路径读这一窗的标签，两边都要 SD 锁）；用秒表量从点下到出声。做 3 次（每次点另一首最后窗口里的曲目）。 | 屏幕全程**不出现 “Read timeout”**；日志有 `Locator miss pos=…`（音频读取任务和前台各会有）和 `Stream pos=… open=<约 2000000>`；出声时间记录（T_data 是 5 s，这是上限，不是阈值） |
| R5.6 | 全部构建、全部负载做完后搜 `FOREGROUND:`。A 版读最后一行 `FOREGROUND: Foreground step max music=<µs>us`（最长值有刷新才出，至多每 10 s 一行，没有新刷新不出）和是否出现 `FOREGROUND: Foreground step slow app=music us=<µs> limit=50000`。C 版同样读，**那里出 WARN 是预期**（每次按路径打开约 2 s，一步里只有这一次调用）。 | 记录 A 版 `music=` 的最大值，核对两个初值：阈值 50 ms（A 版不应出 WARN，一次文件调用含打开 ≤ 约 30 ms 加读取）和“一步约 1 ms CPU”（用 Release 对照：D 版的 `music=` 应与 A 版同量级，主要是文件调用的等待而不是 CPU）。超了不自行调参，在票里问 |
| R5.7 | 全部做完之后，读 Monitor 栈行 `Foreground Task: total=6144 B, min free=<B> B, peak=<…>%`（5 s 一批，插拔之后至少等 10 s）。A 版（R5.1 之后）和 C 版（R5.5 之后，按路径打开栈最深）都读，取较小的。 | `min free` ≥ 256 B，否则在票里报告（调大栈或 AXI 新段放不下要由用户决定，见本节“要报给用户的情况”） |
| R5.8 | 不用上板：`build/firmware-debug/mp3_project.map`（以及 `build/firmware-release/…`）里搜 `.apps_sdram` 和 `.app_axi_ram`，读它们的大小。 | 记录：`.apps_sdram` 预期 `0x9d20`（40224 B），`.app_axi_ram` 预期 `0x9380`（37760 B），起点 `0x2404b600`；核对链接脚本没有改。DTCM 用量也记一下（Debug 预期约 109408 B、83.47 %，Release 约 108936 B） |

**回归行**（A 版，在 R5.x 之间顺带做；每行写在对应清单的行里，下面只写这次要多看的东西）：

- **清单 2 A（A1 至 A8）和 B7、E（E1 至 E3）**：在 R5.1 的“连按下一首”那段顺带做。多看：切歌后曲名区先是**空白**再出新名，**不出现上一首的名字**（A1 连按 5 次后只显示最后一首的名字）；点行（A3）即时高亮、停在最后一行。B7（切歌、同曲重选、停止归零；跳转不归零）：切歌和停止后唱盘角度归零，同曲重选也归零，拖进度条后唱盘不跳变。E1 拔卡：Queue 立即清空、`No SD card`、唱盘归零（与 R5.2 同一动作，一起做）。E2 命令队列满点行：不容易在板上触发，没有就写未测。E3 三键和进度条任何时候都不置灰。
- **O1、O2、O5、O7、O8（A 版，大目录库做 O1、O2、O5、O7；常规库做 O8）**：O1：0:00 在第 1 首切歌，0:15 在第 12000 首切歌（Queue 拖到最底点最后一首，注意它在末尾子目录里），读两次的 `STORAGE: Tag idx=N src=… reads=R open=<µs> us=<µs>` 的 `open=`，单次 ≤ 约 30000。O2：0:00 起在列表开头连续滚动 30 s，读窗口汇总行 `STORAGE: Tag win rows=… max=… omax=… top=…`（只有越过阈值才出，没有这一行就是没越过）和自启动最长单行 `top=`：一窗 ≤ 约 0.5 s（500000 µs）。**这一行现在记在前台任务的读标签里，不再是 Storage 任务**，格式不变。O5：O1 到 O4 全程搜 `Locator miss`，A 版预期 0 行。O7：map 里 `.storage_catalog` 的 SDRAM 占用（定位符约 188 KiB），和 R5.8 一起读。O8：常规库（约 50 首）切歌、滚动、播放，与 0.8.2 行为一致（曲名先空白再出名字，Queue 滑动不卡，窗口一次出现整窗）。
- **0.8.0 I8（只有一张卡，按一张卡做）**：A 版，播放中在 Queue 上连续上下滑动 30 s（0:00 点播放，0:05 至 0:35 滑动），读最后的 `Tag win …`（若出现）与 `top=`，听感无断音，Monitor `Health` 的 `underrun` 增量 0。原判据“单行最长耗时 < T_data/2（2.5 s）”现在没有固件里的阈值（单行日志阈值是 1 s），所以 `top` 记录下来，≥ 1 s 就在票里报告；用户只有一张卡，**不要求三张卡**，一张卡做完写 通过并注明“一张卡”。
- **0.8.0 I9（切歌时曲名应答）**：A 版，常规库做单按“下一首”和连按 5 次，含坏文件的库（`--set first-bad`）做坏文件跳过。单按和连按（0:00 起 2 s 内连点 5 次）：曲名先空白再显示最后一首，中间的曲名不闪现；坏文件跳过：显示被跳到的下一首的名字，与 0.7.0 T3、T4 一致。耗时记录：每次的 `STORAGE: Tag idx=N … us=` 和 `STORAGE: Stream pos=N …` 的位置序列（原来的 `Open idx=N` 日志已不存在）。
- **0.8.0 I11（拔卡与重扫）**：**C 版，大目录库**（按路径读标签，一窗约 24 s，好命中）。0:00 把 Queue 拖到最底（前台开始按路径读最后一窗的标签）；**0:04 拔卡**（窗口加载中）；0:10 插回，等 `Scan done`（约 2.3 s）；0:20 点一首播放。通过：拔卡后 Queue 立即清空、屏幕 “No SD card”、没有卡住不动（这是旧槽版本里用 `complete_unavailable` 防的情形）；重扫后窗口重新出现，曲名与 Queue 高亮对齐。A 版在滑动中拔卡也做一次。

**人手碰不到的瞬间**（30 ms 消抖内快速拔插、读取中拔卡）这次不再单独要求，沿用第 3、4 次的结果；第 5 次的拔卡动作只验证显示侧（R5.2、I11）。

**要报给用户的情况**（从本节起新增）：R5.7 的 `min free` 不足 256 B；前台 step 在 A 版里出现 `Foreground step slow`（说明一步里的文件调用等得太久，可能是水位以下被音频读取挡住，R5.4 的 `wait=` 会同时偏大）；R5.1 的曲名或新窗口明显超阈值；GUI 拿不到 `ui_cell` 锁的比例 `pct` 超过 1 %；日志里出现 `MUSIC: SERVICE_BUSY pos=<位置> n=<次数>`（前台遇到文件槽用尽，R5、R6.10 都不应出现，出现了记下 `pos`、`n`；前台会一直重试，不会回退成文件主名）。这些都只记录，不自行调参。

R5 的这些行都是常开日志加 Debug 日志，不需要仪器；没做的行写 未测，不得凭印象写 通过。**待上板（`hw:pending`）**：本票的全部专项行与回归行，合并到集成分支之前跑完。

**第 6 次：后台任务**

| # | 项与方法 | 固件提交 | 证据来源 | 阈值 | 实测 | 结果 |
|---|---|---|---|---|---|---|
| R6.1 | 插卡到可以播放（12000 首），非唱盘页和停播唱盘页 | | 扫描结束 INFO | 非唱盘页 ≤ 3 s；唱盘页记录 | | |
| R6.2 | 播放中真擦除（钩子，先压到水位以下，本板 `low` = 462） | | Monitor 批次 + 钩子的空闲块数和擦除次数 | 欠载 0；确认有真擦除 | | |
| R6.3 | 窄表锁并发（钩子）：SD 打开与 Flash 关闭或删除同时；Flash 打开等大目录路径打开占着的表锁 | | 持锁统计 | Flash 操作占表锁 ≤ 20 ms（参考 10.1 ms）；大目录按路径打开占表锁记录（参考 2047 ms），Flash 等多久记录 | | |
| R6.4 | 16 MiB 文件满额删除；SD 上删大文件；空闲块接近耗尽时写 32 KiB | | 持锁统计 | 记录（Flash 持锁推算 1.5 s，只挡 Flash 卷） | | |
| R6.5 | `MountFlash` 8.3 s 的原因 | | 持锁统计 + 日志 | 记录 | | |
| R6.6 | 扫描时翻 Queue、读 TXT 的等锁，含大目录末尾子目录按路径打开（预计持锁约 2 s） | | 持锁统计 | 记录 | | |
| R6.7 | 扫描中拔卡，重新插卡 | | 扫描结束 INFO | 不发布旧卡结果；重插后正常发布 | | |
| R6.8 | 后台任务的栈，扫描和回收都跑过再读 | | Monitor 栈行 | ≥ 256 B | | |
| R6.9 | 曲库短锁最长持有 | | 持锁统计 | 记录（预期微秒级） | | |
| R6.10 | 音频流、前台、后台同时打开文件 | | `SERVICE_BUSY` 日志 | 不出现 | | |
| R6.11 | **开机扫描时长**（#220 评估，用户 2026-11-07 选方案 A：Flash 启动任务 2 → 1，与后台任务同级）：常规库、大目录各一次 | | 扫描结束 INFO 的 `us=`；`VOLUME: flash ready` 行的 tick；开机时 `FSLOCK: sd` 的最长持有 | 记录；推算值：大目录约 3.2–4.3 s，常规库约 0.1 s，Flash 就绪约 10 s（大目录）；明显超过推算在票里报告 | | |

第 6 次上板的构建与读法（#220；扫描和回收的日志格式见 [后台任务 README](../../APP/tasks/background/README.md)、[曲库 README](../../APP/apps/music/catalog/README.md)“扫描单步与错误分类”，串口里每行前缀是 `<时间> <I|W|E> (<tick>) <标签>: `，标签后面有冒号）。**这次切的是扫描和回收（Storage Task 删除），并把 Flash 启动任务降到 1；主机测试是单线程的，证明不了真实的任务轮转与持锁交错，下面每一行都靠这次上板。**

- **构建**（缓存变量会留在构建目录里，开过的钩子要显式关；五个钩子开关和压力钩子的两个调参（`FREE_FLOOR`、`FILE_MIB`）**每次都全部写出来**，免得上一次的留着；每一版都是 `cmake --preset firmware-debug` 加下面的七个 `-D`，再 `cmake --build --preset firmware-debug`）：

  七个参数：`MP3_HOOK_PAGE_TURN`、`MP3_HOOK_LOCATOR_MISS`、`MP3_HOOK_FLASH_PRESSURE`、`MP3_HOOK_FAKE_BOUNCE`、`MP3_HOOK_SD_LATE_DMA`（钩子开关，0 关 1 开）；`MP3_HOOK_FLASH_PRESSURE_FREE_FLOOR`（压力钩子的空闲块下限，默认 0；300 把空闲块压在回收水位 462 以下、耗尽以上，擦除全来自回收作业）；`MP3_HOOK_FLASH_PRESSURE_FILE_MIB`（压力文件大小，1 至 16 MiB，默认 4）。两个调参只在 `MP3_HOOK_FLASH_PRESSURE=1` 时有意义，**关着钩子也要写出默认值 0 和 4**。非默认值只允许 Debug。

  - **A 无钩子（Debug）**：`cmake --preset firmware-debug -DMP3_HOOK_PAGE_TURN=0 -DMP3_HOOK_LOCATOR_MISS=0 -DMP3_HOOK_FLASH_PRESSURE=0 -DMP3_HOOK_FAKE_BOUNCE=0 -DMP3_HOOK_SD_LATE_DMA=0 -DMP3_HOOK_FLASH_PRESSURE_FREE_FLOOR=0 -DMP3_HOOK_FLASH_PRESSURE_FILE_MIB=4` 后 `cmake --build --preset firmware-debug`。用于 R6.1、R6.5（D 版压到耗尽后重烧 A 版）、R6.6、R6.7、R6.8、R6.9、R6.11 和全部回归行。
  - **B 限速压力钩子（Debug，下限 300）**：`cmake --preset firmware-debug -DMP3_HOOK_PAGE_TURN=0 -DMP3_HOOK_LOCATOR_MISS=0 -DMP3_HOOK_FLASH_PRESSURE=1 -DMP3_HOOK_FAKE_BOUNCE=0 -DMP3_HOOK_SD_LATE_DMA=0 -DMP3_HOOK_FLASH_PRESSURE_FREE_FLOOR=300 -DMP3_HOOK_FLASH_PRESSURE_FILE_MIB=4`。开机日志出现 `W (…) HOOK: flash_pressure ON chunk=32768 file=4194304 pace=100ms floor=300 reclaim=0 log=10000ms`（搜 `HOOK: flash_pressure ON`；**`floor=300` 和 `file=4194304` 是对的，`reclaim=0` 也是对的**：回收由后台任务自己的回收作业按产品节拍做，钩子不再另调；看到别的值说明构建目录里留着旧的缓存变量，重新 configure）。空闲块降到 300 左右后钩子停写、只等回收作业把空闲块抬回来，所以**这一版里全部真擦除都来自回收作业，节拍是产品的**（每 100 ms 一次、一次一块，`erases` 约每 100 ms 涨 1）。压到位的判据：`FLASHHOOK: run free=…` 的 `free` 降到 462 以下并停在 300 上下、`erases` 在涨（全新格式化的卷从约 6000 压下来要 3.5 至 5 分钟，压低过的卷很快）。**下限让写入变慢：低于下限就停写，每写 32 KiB（10 个块）要等回收作业擦回 10 个块，约 1 s，所以一个 4 MiB 文件约 2 分多钟才写满，关闭、删除、新建的节拍也是这个**（`FLASHHOOK` 的 `files=` 涨 1 就是一轮关闭、删除、新建）。用于 R6.2、R6.3 的“SD 打开与 Flash 关闭或删除同时”、R6.8、R6.10。
  - **C 限速压力钩子加定位符失效钩子（Debug，下限 300）**：`cmake --preset firmware-debug -DMP3_HOOK_PAGE_TURN=0 -DMP3_HOOK_LOCATOR_MISS=1 -DMP3_HOOK_FLASH_PRESSURE=1 -DMP3_HOOK_FAKE_BOUNCE=0 -DMP3_HOOK_SD_LATE_DMA=0 -DMP3_HOOK_FLASH_PRESSURE_FREE_FLOOR=300 -DMP3_HOOK_FLASH_PRESSURE_FILE_MIB=4`。两个 `HOOK:` 启动行都要出现（`HOOK: flash_pressure ON … floor=300`、`HOOK: locator_miss ON`）。用大目录卡。用于 R6.3 的“Flash 打开等大目录路径打开占着的表锁”：每次点末尾曲目都按路径打开约 1.2 至 2 s，同时占 SD 卷锁和表锁，而压力钩子每个文件周期（下限 300 时约 2 分多钟）新建一次文件要拿表锁。想让新建更密，可以把 `MP3_HOOK_FLASH_PRESSURE_FILE_MIB` 改成 1（每个文件约 30 s 一轮），那就是另一版，在“实测”里写明。
  - **D 压到底复现（Debug，无下限，16 MiB 压力文件）**：`cmake --preset firmware-debug -DMP3_HOOK_PAGE_TURN=0 -DMP3_HOOK_LOCATOR_MISS=0 -DMP3_HOOK_FLASH_PRESSURE=1 -DMP3_HOOK_FAKE_BOUNCE=0 -DMP3_HOOK_SD_LATE_DMA=0 -DMP3_HOOK_FLASH_PRESSURE_FREE_FLOOR=0 -DMP3_HOOK_FLASH_PRESSURE_FILE_MIB=16`。开机日志出现 `HOOK: flash_pressure ON chunk=32768 file=16777216 pace=100ms floor=0 reclaim=0 log=10000ms`（**`floor=0` 和 `file=16777216` 要对**）。钩子一路写到空闲块耗尽，用来复现第 6 次上板的停下并读出失败原因（R6.4，也用于 R6.5）；16 MiB 的 Flash 卷逻辑容量约 18.9 MiB 放得下。
  - **E Release 对照**：`cmake --preset firmware-release` 后 `cmake --build --preset firmware-release`（不带任何钩子，Release 本来就不能开，也不要在 Release 里传两个调参的非默认值）。只用于对照 R6.11 的开机扫描时长（只记录，用同一组数字对照，不判通过与否）。
- **FTL 失败诊断行怎么读**（第 6 次上板追加；压力钩子停下或回收作业失败时才出，字段含义的完整表见 [后台任务 README](../../APP/tasks/background/README.md)“Flash 失败诊断”）。文件接口把 FTL 的任何失败都压成 `status=1`，原来分不出原因；现在行里带 Platform 记下的 FTL 最近一次失败：
  - 钩子停下行（ERROR，一行）：`E (…) FLASHHOOK: stopped at=<步骤> status=<n> ftl=<结果> op=<请求> raw=<原始> n=<次数> free=<空闲块>[ valid=… stale=…] pre=<空闲>/<有效>/<失效> req=<字节> file=<字节>[ rej=…][ hw=…/…]`。`pre=` 是**失败那次写开始之前**的块数（失败后 FTL 不就绪，有效、失效读作 0 并从行里省略），`req=` 是那次写请求的字节数，`file=` 是当前压力文件已写入的字节数（不含这次）；只有写步失败才有这三段。
  - 回收失败行（ERROR，一行）：`E (…) FLASH: Reclaim failed status=<n> (stopped until Flash seq changes) ftl=<结果> op=<请求> raw=<原始> n=<次数> free=<空闲块>…`。
  - 怎么判：`ftl=NO_SPACE` 空闲块耗尽且没有可回收块（卷仍就绪，`valid`/`stale` 非零）；`ftl=CORRUPT` 提交关联、数据或擦除回读验证不符，FTL 进入故障；`ftl=IO_ERROR` 原始访问失败或被中止（看 `raw=error|timeout` 和 `hw=`），FTL 进入故障；`ftl=NOT_READY` 请求撞上了已故障或未打开的卷（`op=` 是**先前**失败的那次请求，`rej=` 也是这个意思）；**`ftl=none` 说明错误不是 FTL 来的**（去 FatFs 或 Filesystem Service 找）；`diag=n/a` 是 FTL 还没绑定。`at=` 与 `op=` 不同（例如 `at=write` 而 `op=reclaim`）说明当前请求只是撞上了先前回收留下的故障。`n=` 是开机以来的失败次数，1 就是第一次。
  - **抄下的方式**：这一行（和紧挨着的前后各 2 行，含 `FLASHHOOK: run` 的最近一行）整行一字不差抄进“实测”并告诉用户，**不要自己判断是不是 bug，也不要调参重试**。
- **样本卡**：常规库（`--set all`，24 首以上都行）、大目录（`--set bigdir`，12000 首，末尾有子目录）、空库（没有 `Music` 文件夹，清单 2 C3）。一张卡放多个文件夹，把要用的改名成 `Music`（做法见页首）。用户只有一张卡，卡槽弹出带锁，**做不到 30 ms 内抽插，这次不要求**；12000 首的大目录在 Queue 里**滑不到中段和末尾**，但在第 1 首点“上一首”能跳到第 12000 首（要进末尾子目录的曲目就用这个办法）。
- **串口日志要开着**（`capture-serial.ps1`），每一行的“实测”都从日志读。下面写的秒表时间是动作的**开始时刻**；汇总行每 10 s 才出一行（`FSLOCK:`、`CATLOCK:`、`FLASHHOOK:`），动作结束后至少再等 10 s 才读。

| # | 要做什么（秒表） | 通过时日志里看到 |
|---|---|---|
| R6.1 | **A 版，大目录卡**。① **非唱盘页**：复位，等 `Scan done` 出现后切到 Queue 页（或任一非唱盘页）；**0:00 拔卡，0:05 插回**，读那一轮的 `I (…) Catalog: Scan done res=ok n=12000 us=<µs>`。做 3 次。② **停播的唱盘页**：切到唱盘页（不要点播放），**0:00 拔卡，0:05 插回**，同样读 3 次。（插卡到可以播放 ≈ `Scan done` 的 `us=` 加约 0.2 至 0.6 s 的挂载；卡槽带锁，一次推出再按回即可。） | 非唱盘页每次 `us` ≤ 3000000；唱盘页只记录（预计更慢，GUI 在唱盘页几乎不睡，后台只分得约 7 至 13 % 的 CPU）。参考：第 5 次上板热插重扫 1.49–1.59 s，非唱盘页预期相近。`res=ok n=12000` |
| R6.2 | **B 版（下限 300），常规库卡**（`--set soak` 三首 320 kbps 更好，播放不会很快放完）。复位，等压到位：`FLASHHOOK: run free=… erases=…` 的 `free` 降到 462 以下并停在 300 上下、`erases` 开始涨（见上“B 限速压力钩子”）。**0:00 点播放**，让它放 **5 min**（到 5:00），其间不要滑 Queue；5:00 读最近的 `FLASHHOOK:` 行和 Monitor 的 `Health` 行。再做一段：**5:00 起每 10 s 点一次“下一首”共 10 次**（到 6:40），读 `Switch idx= lat=`。 | 播放 5 min 内 `erases` 持续在涨（**全部来自回收作业**：空闲块降到 300 钩子就停写，写入没有自己的前台回收；回收作业每 100 ms 一次、每次真擦除约 40 至 50 ms，`erases` 约每 100 ms 涨 1，5 min 约 3000，约数，只看“持续在涨”和“约每 100 ms 一块”）就算“确认有真擦除”；`Health underrun=0(+0)`；播放不断音、没有 `Stop reason`；`lat` ≤ 100000（µs，与 R4.1 的 B 版同阈值）；日志里没有 `FLASH: Reclaim failed`、没有 `FLASHHOOK: stopped`（出现了整行抄下，读法见上“FTL 失败诊断行怎么读”）。记录 `erases` 起止值 |
| R6.3 | ① **B 版（下限 300），常规库**：复位，等压到位（`free` 停在 300 上下）。**0:00 点播放；0:10 起每 5 s 点一次“下一首”共 36 次**（到 3:10，SD 上每次切歌都按定位符打开，同时压力钩子在这 3 分钟里至少有一轮关闭、删除、新建一个 4 MiB 文件——下限 300 时一轮约 2 分多钟，三种操作各有机会和 SD 打开同时）。做完读 `FLASHHOOK: run … files=`：这 3 分钟里 `files` 至少涨 1，没涨说明没有碰上一轮，再做一遍。② **C 版（下限 300），大目录卡**：复位，等 `Scan done` 和两个 `HOOK:` 行。**0:00 在第 1 首点“上一首”跳到第 12000 首**（要点 30 次，每次都从第 1 首再点“上一首”，**间隔 10 s**，到 5:00；每次都是按路径打开，约 1.2 至 2 s，占着 SD 卷锁和表锁；压力钩子在这 5 分钟里新建两三次文件，`files` 至少涨 2）。两段做完各等 10 s 再读最后一批 `FSLOCK:` 三行。 | ① 读 `FSLOCK: table n=… hold=<µs>:<操作> wait=<µs>:<操作>`：没有路径打开时，表锁最长持有应 ≤ 20000（µs，参考 10100 至 18000，操作是 `Open` 或 `OpenLoc` 之类，Flash 的 `Open` 在内）；`FSLOCK: sd … wait=` 记录（不应有秒级）；`FSLOCK: flash … hold=<µs>:<操作>` 记录。② 表锁 `hold` 记录大目录按路径打开占表锁的最长值（参考 2047000，操作 `Open`），`wait=` 是别的调用（含 Flash 新建文件）等表锁的最长时间，**记录**，并看 `FLASHHOOK:` 的 `wmax`/`dmax` 有没有因此变成秒级 |
| R6.4 | **D 版（压到底复现，无下限，16 MiB 压力文件）**。复位，等 `HOOK: flash_pressure ON … floor=0 … file=16777216`（这两个值要对）和 `FLASHHOOK: start`。钩子一路写（无下限，空闲块一路降到耗尽；每 100 ms 写 32 KiB，约 55 s 起写满第一个 16 MiB 文件，空闲块低了更慢）、关闭、删除，**一直开着约 8 min，或到出现 `FLASHHOOK: stopped`**，期间不要操作。读最近的 `FLASHHOOK: run …` 与 `FSLOCK: flash`。“空闲块接近耗尽时写 32 KiB”看 `FLASHHOOK:` 的 `free=` 降到 2 至 6 之后的 `wmax=`。**第 6 次上板这个压法（当时 4 MiB 文件）在约 69 s、空闲块降到 2 至 3 时停在 `stopped at=write status=1`，之后重启又停在 `stopped at=open status=1`**，没有 `Reclaim failed`。**这次若又出现 `FLASHHOOK: stopped …`（或 `FLASH: Reclaim failed …`），整行一字不差抄进“实测”并告诉用户**（读法见上“FTL 失败诊断行怎么读”），这不算 R6.4 失败，是复现要的证据；停下后卷在重启前不会恢复，要继续看 R6.4 的 `dmax` 就复位再来。 **SD 上删大文件没有触发手段**（没有产品功能或钩子在 SD 上删文件），这一项写 未测，注明缺手段。 | `FSLOCK: flash n=… hold=<µs>:Remove`（16 MiB 删除推算约 2.2 s，138 ms/MiB）与 `FLASHHOOK:` 的 `dmax=<ms>` 记录（若停下得早、没删成 16 MiB 文件，写 未测，注明停下行）；空闲块耗尽时 `wmax` 约 1.5 至 3 s（#191 实测写 64 KiB 最长 3.04 s）；这段时间 `FSLOCK: sd … wait=` 不应受影响（Flash 持锁只挡 Flash 卷），播放（若同时在放）不断音；只记录 |
| R6.5 | `MountFlash` 8.3 s 的原因：**先用 D 版把空闲块压到耗尽**（`FLASHHOOK: run free=` ≤ 10，约 4 至 6 min；钩子已停下也算，空闲块已经只剩 2 至 3），**不动 NOR**，重烧 **A 版**（DFU 只写内部 Flash）。复位，读 `VOLUME: flash boot start` 与 `VOLUME: flash ready path=mount … ms=<n>` 的 tick，再读最后一批 `FSLOCK: flash n=… hold=<µs>:Mount`。另外**新格式化的卷**也量一次（若有）：同样复位读。 | 记录：`ms` 与 `Mount` 的 `hold`、压到耗尽之前与之后各一次。对比第 3 次上板的 3.3 s（卷较空）和 8.3 s（空闲块只剩两个）：若空闲块多时 `ms` 约 3.3 s、耗尽后约 8.3 s，原因就是 FTL 启动扫描要校验的块数随空闲块减少而增加（推断，记录证据）。**注意这一版 Flash 启动任务优先级是 1（#220），与后台任务同级轮转，`ms` 是挂载加被其他任务占去的时间：插着大目录卡开机时会被扫描分走一部分 CPU 而拉长约 1.5–2 s，所以 R6.5 用常规库卡或没有卡开机，免得混进扫描**；同时读开机后第一批 Monitor 的 CPU 行里 `flas=` 的占比，记录 |
| R6.6 | **A 版，大目录卡**。复位，等 `Scan done`。**0:00 拔卡，0:03 插回（开始扫描）；0:03 起 2 s 内**：切到 Queue 页，上下快滑 Queue，点“播放”一次（应立刻 “SD card not ready”，R4.8）。扫完再等 10 s，读最后一批 `FSLOCK: sd n=… hold=<µs>:<操作> wait=<µs>:<操作>` 与 `FSLOCK: table …`。末尾子目录按路径打开是扫描里最长的一次持锁（`OpenDir`，12000 项的目录要线性找到那个子目录）。 | 记录：`hold` 的最大值应是 `OpenDir`，预计约 1.2 至 2 s（参考 12000 首按路径打开最后一首 1.43–2.05 s）；`wait=` 的最大值与操作；扫描中点播放不出现 “Read timeout”。**扫描期间曲库代次为 0，前台读不了标签，Queue 窗口要等发布后才有内容**，所以这里不会有前台读标签的等锁；扫描那几秒 Monitor CPU 行的 `idle=` 读 0 是预期的（后台任务不让出，见后台任务 README），顺带记一下；等锁的主要是音频读取任务（点播放时 NOT_READY 立刻应答，不碰文件）和卷任务的对账。只记录 |
| R6.7 | **A 版，大目录卡**。**0:00 插卡，0:00.6 拔卡**（大目录扫描约 1.6 s，0.6 s 在中途）；**0:03 插回**，等 `Scan done`。做 3 次。常规库太快（约 0.1 s），做不到中途拔，用大目录。 | 第一轮日志 `Catalog: Scan done res=aborted n=0 us=…`（被丢弃，**不发布**，没有 `res=ok`、没有 ERROR），重插后 `Scan done res=ok n=12000`；屏幕拔卡后 “No SD card”、Queue 清空，重插扫完后窗口正常（不出现旧卡的曲目）。**拔卡时若落在“卡还在、读不了”**（极少）会出一条 ERROR `Catalog scan aborted but the card is still there; reinsert the card.`，记下次数和上下文，不是预期路径 |
| R6.8 | **A 版和 B 版**各在做完上面各行之后（扫描和回收都跑过：A 版开机后回收作业每 100 ms 调一次，Flash 空闲块高于水位时约 14 µs 就返回；B 版有真擦除）读 Monitor 栈行 `Background Task: total=6144 B, min free=<B> B, peak=<…>%`（5 s 一批，扫描之后至少等 10 s）。 | `min free` ≥ 256 B，否则在票里报告（后台任务栈放 `.app_axi_ram`，不改链接脚本，放不下由用户决定）；两版取较小的 |
| R6.9 | 做完 R6.1 至 R6.7 后读最后一行 `CATLOCK: n=<拿取次数> hold=<最长持有 µs>us`（A 版；每 10 s 一行，开机以来的最大值）。 | 记录，预期微秒级（远小于 100 µs） |
| R6.10 | **B 版（下限 300）**，常规库卡，等压到位，**0:00 点播放，0:10 起每 5 s 点一次“下一首”共 30 次，同时在 Queue 里连续滑动（让前台读窗口和曲名）约 2.5 min（到 2:40）**，压力钩子在这段时间里至少有一轮在 Flash 卷上关闭、删除、新建文件（下限 300 时一轮约 2 分多钟；做完 `FLASHHOOK: run … files=` 至少涨 1，没涨再做一遍）。做完搜日志里的 `SERVICE_BUSY`（音频读取 `Audio: SERVICE_BUSY op=…`、前台 `MUSIC: SERVICE_BUSY pos=…`、钩子 `FLASHHOOK` 的新建 BUSY 重试）。 | **不出现**（SD 上最多同时 2 个文件：音频流 1、前台 1；扫描只开目录；钩子只开 Flash 卷的 1 个文件，Flash 有 2 个文件槽）。出现了记下 `op=`、`pos=`、`n=` 并报告 |
| R6.11 | **开机扫描时长**。**A 版**两次：① **常规库卡**，② **大目录卡**。每种卡**复位 3 次**（按复位键，卡一直在槽里；不是热插），每次读：`VOLUME: flash boot start` 的 tick、`I (…) Catalog: Scan done res=ok n=<n> us=<µs>`（`us` 是扫描开始到结束的总时间，含被抢占和轮转）与它的 tick、`VOLUME: flash ready path=mount … ms=<n>` 的 tick，以及开机后第一批（约 10 s 时）`FSLOCK: sd n=… hold=<µs>:<操作> wait=<µs>:<操作>`。大目录卡再用 **E 版（Release）** 复位 3 次，同样读，只记录。 | 记录三次的值和最大值；同时读开机后第一批 Monitor 的 CPU 行里 `flas=`（Flash 启动任务）和 `back=`（后台任务）的占比，看开机前这两个任务怎么分 CPU；**`idle=` 在开机扫描那一批读 0 是预期的**（扫描进行中后台任务不让出，IDLE 跑不到，不是故障），再读 `Heap` 行的历史最小余量（`Create Task` 自删后的堆要等 IDLE 回收，可能比稳态低约 0.9 KiB），只记录。**推算**（调度模型，按第 4、5 次上板粗校，不是实测）：大目录 `us` 约 3200000 至 4300000，常规库约 100000；`flash ready` 的 tick 大目录约 10 s（常规库几乎不变，约 8 s）。**对照第 5 次上板（Flash 启动任务优先级 2）：大目录 9.0–9.1 s，常规库 0.39–0.87 s，Flash 就绪约 8 s。** **顺带看**：开机时 `FSLOCK: sd` 的最长持有是否**不再出现 800 ms 级的 `ReadDir`**（第 5 次是 820 ms，持锁的扫描被 Flash 任务抢占；现在应降到毫秒级，只剩 `OpenDir` 找末尾子目录那一次约 1.2 至 2 s）。**超过推算明显**（大目录 `us` > 5 s、常规库 > 0.3 s、`flash ready` > 12 s）在票里报告，不自行调参；实测不理想就退到“扫描期间临时升到 2、只升开机第一次扫描”，那要再问用户 |

**回归行**（A 版，在 R6.x 之间顺带做；每行写在对应清单的行里，下面只写这次要多看的东西）：

- **O6（扫描 12000 首）和 B9（插卡后扫描耗时）**：就是 R6.1 的读数（插入大目录读 `Scan done … us=`，B9 另在常规库卡上读一次），结果词都是 记录。多看：`res=ok`，没有 ERROR。
- **清单 2 C3（空库 `No tracks`）**：用没有 `Music` 文件夹的卡（配置 D 空库）插入或复位：屏幕 `No tracks`，日志 `Music Directory Open Failed.`（WARN，按空库发布，沿用 STOR-7）和 `Scan done res=ok n=0`；没有 ERROR。
- **清单 2 C4（`Library changed`，曲库变化后点旧窗口的行）**：**按代码预期很难在板上造出来**。卡一拔，前台就发代次 0 的空窗，旧窗口立刻没了（R5.2），“点旧窗口的行”只剩点行命令与重扫发布同时到达的窄竞态。试一次：大目录卡 **0:00 插卡，0:00 起 1.5 s 内（扫描未发布）连点 Queue 的行 5 次**，再 **扫完后立刻点**；记录屏幕提示。**不要求出现**，一次都没造出就写 未测并注明原因。
- **清单 1 C3（播放时回收且欠载为 0）**同 R6.2 的数据；**回归说明**：播放中回收不再有“流是否打开”的条件（STOR-12 现行文字），所以 R6.2 之前播放中的真擦除没有上过板，这是第一次。

**要报给用户的情况**（从本节起新增）：R6.8 的 `min free` 不足 256 B；R6.11 明显超过推算（见上）；播放中真擦除时欠载不是 0，或 `FSLOCK: sd … wait=` 在播放中因回收出现秒级（回收只拿 Flash 卷锁，不应出现）；日志里出现 `FLASH: Reclaim failed status=…` 或 `FLASHHOOK: stopped …`（回收失败一次就停，之后到 Flash 卷序号变化为止不再回收；**整行连同 `ftl=`、`op=` 等诊断一字不差抄下**，加上下文）；R6.10 出现 `SERVICE_BUSY`；`FLASHHOOK` 的 `wmax`/`dmax` 或 `Reclaim` 的 `wait=` 比第 4 次上板的 1.03 s 更长（同一任务里钩子的写会让回收晚到，预期）。这些都只记录，不自行调参。

R6 的这些行都是常开日志加 Debug 日志，不需要仪器；没做的行写 未测，不得凭印象写 通过。**待上板（`hw:pending`）**：本票的全部专项行与回归行，合并到集成分支之前跑完。

**第 7 次：完整验收（同一提交）**

| # | 项与方法 | 固件提交 | 证据来源 | 阈值 | 实测 | 结果 |
|---|---|---|---|---|---|---|
| R7.1 | 长跑 1：见清单 1 C1 | | 见 C1 | 见 C1 | | |
| R7.2 | 长跑 2：见清单 1 C2、C3 | | 见 C2、C3 | 见 C2、C3 | | |
| R7.3 | Release 时序核心，开持锁统计开关 | | 同上 | 只记录，用同一组数字对照 | | |
| R7.4 | 堆最低余量、CPU 空闲 | | Monitor | 记录（参考：堆 15,424 B） | | |

## 汇总

- 超出阈值的项：
- 需要新建的票 / 需要调整的参数：
- 结论：☐ 通过　☐ 有条件通过　☐ 不通过（仅 通过 行有效；部分 / 未测 保留 `hw:pending`）

### 未测项清单

- 每个 部分 / 未测 行一条，写缺什么（样本、仪器、时长）。
