# FatFs 下按目录位置快速打开文件与文件内 seek 的代价（#162 调研）

- 票：immorcoding/mp3_project #162「FatFs 下按目录位置快速打开文件与文件内 seek 的代价」，父图 #160（https://github.com/immorcoding/mp3_project/issues/162）
- 日期：2026-10-30（上游页面访问日期相同）
- 对象：本仓库 FatFs（`Middlewares/Third_Party/FatFs`，头注释 R0.12c，`_FATFS 68300`）在 `FATFS/Target/ffconf.h` 配置下的目录查找、单文件打开与 `f_lseek` 代价；上游 elm-chan.org 的修订、补丁与许可惯例。
- 用途：为 0.9.0 相关决定提供事实依据。本文只记录事实、推算与待测项，不选方案。
- 仓库与分支：worktree `E:\Projects\project_mp3\source_code\.claude\worktrees\agent-a4ba6fdf774207d7b`，分支 `research/fatfs-fast-open`，基于 `origin/main`（bcc086e）。`docs/research/` 在 main 上原不存在，本票新建。

证据标记：
- 【确认】：本仓库源码、配置或文档（给出路径与行号），或上游页面原文。
- 【摘要】：经 WebFetch 摘要的上游页面，未逐字核对；关键句需回原页核对。
- 【推断】：由【确认】证据推导，未经实测。
- 【未核对】：需要的原文或补丁无法访问，或未做逐行比对。
- 【需板上测量】：仓库内没有对应实测，不以推断填补。

缩写：`ff.c` = `Middlewares/Third_Party/FatFs/src/ff.c`，`ff.h` 同理，`ffconf.h` = `FATFS/Target/ffconf.h`。行号以本分支 HEAD 为准。

## 0. 结论摘要

1. 【确认】每次 `f_open` 都从目录第 0 项重新扫描。`follow_path` 对路径每段调用 `dir_sdi(dp,0)` 与 `dir_find`（ff.c:2852、2858），`dir_find` 首行再次重置（ff.c:2221）。FatFs 没有“从某偏移继续”的公开入口。
2. 【确认】每个前序文件消耗其 LFN 项加 1 个 SFN 项。每 512 B 扇区含 16 项。`move_window` 每换一个扇区发一次 `disk_read(count=1)`（ff.c:912–933），没有预读。同一扇区内其余 15/16 的项命中单扇区窗口。
3. 【实测加推算】#143 I10 的五个目录位置取点（idx 10/13/21/43/47）最小二乘斜率为 59.0 µs/文件（R² 0.9994），截距约 1.48 ms。外推 32000 首约 1.89 s，与 #143 I10 给出的“约 1.9 s”一致。
4. 【推断】0.06 ms/文件主要由目录扇区的 SD 单扇区读链路决定。每次读包含一次读命令、一次 CMD13 状态同步（经 `HAL_SD_GetCardState`）、任务通知与两次 D-Cache 维护（第 2.4 节）。CPU 比对与遍历估计小于约 10%。单次读的真实延迟仓库内没有测量。
5. 【确认】`bsp_driver_user_diskio.*` 不在 SD 读取路径上。SD 卷由 `sd_diskio.c` 经 `BSP_SD_*`（Service/filesystem/sd）读取；`bsp_driver_user_diskio.*` 是 USER 卷（Flash FTL）的后端契约（FATFS/App/fatfs.c:32–34）。
6. 【确认】单目录项上限为 65,536 项（`MAX_DIR` = 0x200000 字节，ff.c:325）。每文件 3 项（文件名 14–26 码元）时单目录上限为 21,845 首，4 项时为 16,384 首。超限部分不报错：`dir_next` 返回 FR_NO_FILE，`f_readdir` 把它当作目录结束返回 FR_OK（ff.c:1531、4282–4286）。32000 首单目录要求每文件不超过 2 项。
7. 【确认】公开 API 没有“按目录位置打开”入口。扫描时可从公开 `DIR` 结构取得 `obj.sclust`、`dptr`、`blk_ofs`，但 `FILINFO` 不含起始簇（ff.h:197 起）。手工构造 FIL 时 `f_close` 无条件调用 `dec_lock(lockid)`（ff.c:3828），而 `inc_lock` 为 static（ff.c:808），应用层无法注册锁项。
8. 【推断】路线 (a)（记录目录项位置）与位置无关，但需要 FatFs 内部接口或依赖内部约定的手工 FIL；改 ff.c 与 AGENTS.md 的 Vendor 规则冲突。记录开销为每轨 8 B，32000 首约 256 KB SDRAM。
9. 【摘要】路线 (b)（升级上游 R0.13–R0.16）：摘要中未见 FAT 目录查找加速或按位置打开的新 API。主要变化是配置名改为 `FF_` 前缀（R0.13，改 ffconf.h 需先问用户），以及若干修复。
10. 【推断】路线 (c)（扇区缓存）是常数因子，随命中率变化，冷打开不变。全目录缓存需 4.1–6.1 MB（32000 首，每文件 4–6 项）。SD 路径的自维护层是 `Service/filesystem/sd/`，不是 `bsp_driver_user_diskio.*`。路线 (d)（分桶）把单次扫描上界限定在桶容量内并绕开 65,536 项上限，代价是改变 SD 上的目录结构。
11. 【确认】`f_lseek`（`_USE_FASTSEEK 0`）跨簇时每簇调用一次 `get_fat`。FAT32 每扇区 128 项，连续分配时约每 128 簇一次 FAT 读；碎片化时接近每簇一次（表 4-1）。流开启的尾部探测（storage_audio_stream.c:295）已有与文件大小成正比的链遍历。
12. 【确认】CLMT（`_USE_FASTSEEK 1`）缓冲为 2 + 2F 个 DWORD（F 为连续段数，ff.c:4026–4046）。创建需整链遍历；每次打开清零（ff.c:3460）；不占 `_FS_LOCK` 槽；不能扩展文件（ff.c:4051）。
13. 【确认】许可为 ChaN 的单条件 BSD 式许可（ff.h:5–15）。【确认】本地 ff.c 不是逐字的上游 R0.12c：`st_readme.txt:110` 记录 ST 回移植了一处 R0.13 修复。【摘要】上游以对 R0.16 的补丁文件发布修复（patches 页），页面列有若干 CVE，本仓库基线是否受影响【未核对】。

## 1. 范围与来源

- 版本【确认】：`ffconf.h:20` 的 `_FFCONF 68300`；`ff.h:22` 的 `_FATFS 68300`；头注释为 R0.12c（ff.h:2、ffconf.h:4）；`00readme.txt:1` 为 “FatFs Module Source Files R0.12c”。
- 配置【确认】（FATFS/Target/ffconf.h）：`_USE_FASTSEEK 0`（:63）、`_USE_LFN 1`（:112）、`_MAX_LFN 255`（:113）、`_LFN_UNICODE 1`（:129）、`_CODE_PAGE 936`（:84）、`_MAX_SS 512`（:178）、`_FS_TINY 0`（:206）、`_FS_EXFAT 0`（:212）、`_FS_LOCK 2`（:230）、`_FS_REENTRANT 0`（:241）、`_FS_READONLY 0`（:33）、`_USE_EXPAND 0`（:66）、`_USE_FIND 0`（:56）、`_VOLUMES 2`（:157）。上游模板默认 `_USE_FASTSEEK 1`（ffconf_template.h:67）。
- 本地源码与上游的关系【确认】：`st_readme.txt` 第 110 行的 V2.1.0（2018-09-21）条目记录 ST 对 ff.c 的回移植（“back-port a fix from 0.13”，disk_status 调用的信号量保护）。其余差异【未核对】：本票未取上游 R0.12c 原档做 diff。
- 实测来源【确认】：#143 的 I10 节（https://github.com/immorcoding/mp3_project/issues/143），固件 b294558，SD 30436 MB，49 个文件，目录位置取点 idx 10/13/21/43/47。原文称“按目录位置取点”。idx 为曲库下标（DFS 顺序，只含 .mp3），与目录项位置的换算假设目录内只有 .mp3 且无子目录【未核对】。
- 上游页面【摘要】（2026-10-30 访问）：updates、doc/lseek、doc/appnote、patches、archives，以及首页。
- 未做：未上板测量；未修改任何 FatFs、CubeMX 生成或配置文件；未取 R0.12c 原档。

## 2. 问题 1：代价构成（f_open → follow_path → dir_find）

### 2.1 调用链与每次打开的固定部分【确认】

- `f_open`（ff.c:3308）→ `find_volume`（ff.c:3021–3028，已挂载时只调用一次 `disk_status`）→ `follow_path`（ff.c:2813）→ 对路径每段 `dir_sdi(dp,0)`（ff.c:2852）与 `dir_find`（ff.c:2858）→ `chk_lock`（ff.c:3340）→ `inc_lock`（ff.c:3428）→ `fp->cltbl = 0`（ff.c:3460）。
- `validate`（ff.c:3216–3243）在 f_lseek（ff.c:4014）、f_read（ff.c:3527）、f_close（ff.c:3825）等入口调用，内部是 `disk_status`（ff.c:3236）。SD 的 `disk_status` 经 `SD_status` → `SD_CheckStatus` → `BSP_SD_GetCardState`（FATFS/Target/sd_diskio.c:90–100、130–133）；`BSP_SD_GetCardState` 只比较 Platform 状态（Service/filesystem/sd/filesystem_sd_bsp.c:148–153），不发总线命令。`Platform_SD_GetState` 是状态读取（Platform/sd/platform_sd.c:428–430）。
- 每次打开的固定部分：根目录中查找 `Music` 的扫描（根目录项数【未核对】），加上 Music 目录内扫描到目标为止。

### 2.2 目录项数与扇区读【确认】算术；“紧密排列”为【推断】

- 目录项 32 B（ff.c:430 `SZDIRE`）；扇区 512 B（ffconf.h:178）⇒ 每扇区 16 项。
- LFN 每项携带 13 个 UTF-16 码元（ff.c:1670 `LfnOfs[]` 有 13 个偏移）。文件名长 L 码元（含扩展名）时，LFN 项数为 ⌈L/13⌉，另加 1 个 SFN 项。
- 表 2-1：

| 文件名 L（码元） | LFN 项 | 每文件项数 | 每文件扇区读（项数/16） |
|---|---|---|---|
| 1–13 | 1 | 2 | 0.125 |
| 14–26 | 2 | 3 | 0.188 |
| 27–39 | 3 | 4 | 0.250 |
| 40–52 | 4 | 5 | 0.313 |
| 53–65 | 5 | 6 | 0.375 |
| 66–78 | 6 | 7 | 0.438 |
| 255（上限） | 20 | 21 | 1.313 |

- 已删除项（0xE5）同样被遍历（ff.c:2254 只重置 LFN 状态，不跳过读取）【确认】。

### 2.3 逐项操作【确认】

- 每一项先调用 `move_window(fs, dp->sect)`（ff.c:2248）。窗口已是该扇区时不发 I/O（ff.c:920–921）。因此同一扇区内 15/16 的项命中单扇区窗口。
- 换扇区：`dir_next` 递增 `dp->sect`（ff.c:1534），下一循环的 `move_window` 以 `disk_read(fs->drv, fs->win, sector, 1)` 读入（ff.c:925）。每次扫描中每个目录扇区恰好一次 count=1 读，没有预读。
- 目录簇边界（每 csize 个扇区一次）：`dir_next` 调用 `get_fat`（ff.c:1542–1544）读一次 FAT 扇区。FAT 与目录共用 `fs->win`，随后目录扇区再被 `move_window` 读入。这是每簇一次的额外读（开销【推断】较小）。
- LFN 比对：非匹配文件只在其第一个带 LLEF 位的 LFN 项上调用一次 `cmp_lfn`（ff.c:2259–2265）。后续 LFN 项因 `ord = 0xFF` 在 `c == ord` 处短路。`cmp_lfn` 在首个不等字符处返回（ff.c:1677–1705）。每个比对的字符调用两次 `ff_wtoupper`（ff.c:1693；`_CODE_PAGE 936` 时实现见 `Middlewares/Third_Party/FatFs/src/option/cc936.c:10960`）。
- SFN 比对：非匹配文件的 SFN 项不计算 `sum_sfn`（ff.c:2268 的 `!ord` 短路），只做 `mem_cmp` 11 字节（ff.c:2269）。`mem_cmp` 在首个不等字节返回（ff.c:716–725）。
- CPU 估计【推断】（未测）：每项约数十至一百多条指令。假设 HSE = 25 MHz【未核对】，由 PLL1 参数（PLLM=5、PLLN=192、PLLP=2，Core/Src/main.c:168–171）得 SYSCLK 480 MHz。则每项约 0.5 µs 以内，每文件（约 5 项）约 3 µs 以内，占 59 µs 的 5% 左右。

### 2.4 每次目录扇区读的链路【确认】调用链；耗时【需板上测量】

1. FatFs `disk_read` → CubeMX `SD_read`（FATFS/Target/sd_diskio.c:147–163；151 行调用 BSP，count 原样传入）。
2. `BSP_SD_ReadBlocks`（Service/filesystem/sd/filesystem_sd_bsp.c:56–67）→ `filesystem_sd_transfer_read_blocks`（Service/filesystem/sd/filesystem_sd_transfer.c:207–256）。每组最多 64 块（filesystem_config.h:35），经 32 KiB bounce buffer（filesystem_sd_transfer.c:32–34）中转，再 memcpy 到调用者缓冲（:249）。
3. 启动：`Platform_SD_StartReadBlocks`（Platform/sd/platform_sd.c:533–546）→ `SDCard_StartReadBlocks`（Components/sd/sd.c:583）→ 适配器 `sd_stm32_hal_read_blocks_dma`（Adapters/stm32_hal/sd/sd_stm32_hal_adapter.c:356–407）。它先对 bounce buffer 做 clean + invalidate（:379），再调用 `HAL_SD_ReadBlocks_DMA`（:388）。单块为 CMD17，多块为 CMD18（HAL 细节【未核对】）。
4. 完成：数据阶段结束后 IRQ 经 Platform 转发（platform_sd.c:125–160），`xTaskNotifyIndexedFromISR` 唤醒 Storage 任务（filesystem_sd_transfer.c:43–61）。
5. 任务上下文：`Platform_SD_CompleteTransfer`（platform_sd.c:575–590）→ `SDCard_CompleteTransfer`（Components/sd/sd.c:749–781）→ `sdcard_wait_ready`（sd.c:135–150，780 行调用）→ 适配器 `sd_stm32_hal_sync`（sd_stm32_hal_adapter.c:485–535），循环调用 `HAL_SD_GetCardState`（:508）直到 TRANSFER。
6. `HAL_SD_GetCardState`（Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_sd.c:2861–2876）→ `SD_SendStatus`（:3421–3431）→ `SDMMC_CmdSendStatus`（:3431），即 CMD13 总线命令。
7. 成功时 `sd_stm32_hal_finish_dma_read` 对 DMA 缓冲做 invalidate（sd_stm32_hal_adapter.c:67–78）。
8. 回到 transfer 层，memcpy 结束（filesystem_sd_transfer.c:249）。

- 结论【确认】（命令数）：每个目录扇区读 = 1 次数据读命令（512 B）+ 至少 1 次 CMD13（卡已在 TRANSFER 状态时恰好 1 次）+ 一次任务通知与切换 + 2 次 D-Cache 维护 + 一次 512 B memcpy。各段耗时【需板上测量】。
- 超时【确认】：单块 DMA 等待 500 ms（filesystem_config.h:42）；卡同步等待 1000 ms（Components/sd/sd_config.h:17）。
- 注意【确认】：`sd_diskio.c:156` 的 `while(BSP_SD_GetCardState()!= MSD_OK)` 只查询 Platform 软件状态，不是总线命令。

### 2.5 0.06 ms/文件由哪部分主导【推断】

- 实测【确认】（#143 I10）：五个取点 2095 / 2250 / 2678 / 4010 / 4270 µs（idx 10/13/21/43/47）。最小二乘斜率 59.0 µs/文件，截距 1479 µs，R² 0.9994；两两斜率范围 51.7–65.0 µs/文件。外推 32000 首：1.888 s（只算斜率）或 1.890 s（含截距）。
- 代码【确认】：与位置相关的磁盘 I/O 只来自目录扇区（每 16 项一次 count=1 读），另有每簇一次 FAT 读。与位置相关的 CPU 工作是逐项遍历与比对（2.3 节）。
- 相邻参照【确认】数字，【推断】用法：I10 表中 APIC 前置样本比首块样本多一次读取（读取次数 2 对 1），耗时 1720 对 1501 µs，差 219 µs。I10 备注称“多一次 seek 只多约 0.25 ms”。该差值包含一次数据读（而非单扇区），可能偏高。
- 推算【推断】：若每次目录扇区读耗时为 T，则斜率 ≈ T × 每文件扇区数。表 2-2：

| 每文件项数 | 每文件扇区读 | 隐含 T（µs） |
|---|---|---|
| 2 | 0.125 | 472 |
| 3 | 0.188 | 315 |
| 4 | 0.250 | 236 |
| 5 | 0.313 | 189 |
| 6 | 0.375 | 157 |
| 7 | 0.438 | 135 |

- 结论【推断】：59 µs/文件与 T ≈ 0.2 ms 量级相容，对应每文件约 4–5 项（文件名约 27–52 码元）。真实曲库的文件名长度未核对（仓库内无曲名清单），因此 T 与项数都不能唯一确定。主导部分是目录扇区的 SD 读链路；CPU 不是主导（估计 10% 以内，未测）。

### 2.6 整次扫描与阈值位置【推断】

- catalog 的 DFS（storage_catalog.c:142–250）用 `f_readdir` 连续推进，每个目录扇区只读一次。整次扫描的扇区读约为总项数/16，与一次位于末尾的 `f_open` 同阶。docs/shape/storage.md:49（STOR-8）记录“最多 32000 首，可能数秒”，与此一致。
- 单行读取超过 T_data/2（storage_catalog_config.h:23，1,000,000 µs）的首个位置约为第 16,900 首：(1,000,000 − 1,479) / 59.0 ≈ 16,921。依赖 I10 样本的名称长度分布，且假设 idx 与目录位置一致。

### 2.7 目录项上限与静默截断【确认】

- `MAX_DIR` = 0x200000 字节（ff.c:325，注释 “Max size of FAT directory”），即 65,536 项。
- `dir_sdi` 对 ofs ≥ MAX_DIR 返回 FR_INT_ERR（ff.c:1479）。`dir_next` 对 ofs ≥ MAX_DIR 返回 FR_NO_FILE（ff.c:1531）。
- 结束语义：`dir_read` 把 FR_NO_FILE 当作结束（ff.c:2193–2197）→ `f_readdir` 把 FR_NO_FILE 改为 FR_OK（ff.c:4282、4286）→ `get_fileinfo` 在 `!dp->sect` 时把 fname[0] 置 0（ff.c:2444 起）→ `Service_Filesystem_ReadDirectory` 返回空名（Service/filesystem/filesystem_directory.c:86 起）→ `storage_catalog_music_dfs` 把空名当作目录结束（storage_catalog.c:183–192）。因此超过 65,536 项的部分不报错，也不记日志。
- 单目录可容纳的文件数（算术【确认】）：每文件 1 项 → 65,536；2 项 → 32,768；3 项 → 21,845；4 项 → 16,384；5 项 → 13,107；6 项 → 10,922；7 项 → 9,362。
- 对照【确认】：storage_catalog_config.h:13 的最大曲目数为 32000。在单目录中达到 32000 首，要求每文件不超过 2 项，即文件名不超过 13 码元（无 LFN 或短名）。
- exFAT 的对应上限为 `MAX_DIR_EX` = 0x10000000（ff.c:326）；本配置未启用 exFAT（ffconf.h:212）。

### 2.8 锁与槽【确认】

- `_FS_LOCK 2` 下，读打开也占用锁槽（`chk_lock`/`inc_lock` 的 acc = 0 路径，ff.c:3340、3428）。槽满时 `chk_lock` 返回 FR_TOO_MANY_OPEN_FILES（ff.c:789）。同一文件的多次读打开共享一个槽（`inc_lock` 计数，ff.c:833）。
- 子目录的 `f_opendir` 同样占槽（ff.c:4205–4213；根目录不占）。
- Service 层：文件槽 4、目录槽 4（Service/filesystem/filesystem_config.h，filesystem_handle 段），与 FatFs 锁数 2 不同。Storage 文档约定标签读取的 FIL 与音频流 FIL 合计不超过 2（APP/tasks/storage/README.md:18）。
- 单任务访问：Service 的读写接口注释标明“仅 Storage Task 调用”（filesystem_file.c:114、153 等处）。

## 3. 问题 2：可选路线

### 3.0 汇总

| 路线 | 效果类型 | 内存 | 是否改 FatFs 源码 | 是否改 ffconf.h |
|---|---|---|---|---|
| (a) 记录目录项位置后打开 | 打开与位置无关【推断】 | 每轨 8 B（32000 首约 256 KB SDRAM） | 需要（公开 API 不存在），或手工 FIL（依赖内部约定） | 不需要（锁语义需另行处理） |
| (b) 升级上游 R0.13–R0.16 | 摘要中未见位置相关改进 | 视版本 | 需替换 Vendor（上游更新流程） | 需要（R0.13 起为 FF_ 前缀） |
| (c) SD 自维护层加扇区缓存 | 常数因子，随命中率；冷打开不变【推断】 | 512 B × 扇区数；全目录约 4.1–6.1 MB | 不需要 | 不需要 |
| (d) 分桶或限制单目录文件数 | 有界，上界 ∝ 桶容量；绕开 65,536 上限【推断】 | 无（索引除外） | 不需要 | 不需要（但改变 SD 目录结构） |
| (e) 其他（exFAT、f_expand、句柄复用） | 见 3.5 | 见 3.5 | 见 3.5 | exFAT 与 f_expand 需要 |

### 3.1 (a) 记录目录项位置并据此打开

- 扫描时的公开字段【确认】：`DIR.obj.sclust`（目录起始簇，ff.h:179、137）；`dptr`（目录内字节偏移，ff.h:180；`f_readdir` 返回时已前进到下一项，ff.c:4285）；`blk_ofs`（本项 LFN 块的起点，ff.h:186；ff.c:2176、2182 设置）；`sect`（ff.h:182）；`dir`（窗口内指针，ff.h:183，仅在下一次 `move_window` 前有效）。
- 起始簇与文件大小【确认】：`FILINFO` 只有 `fsize`（ff.h:197 起），没有簇号。起始簇位于 SFN 项中，公开 API 没有读取该项的途径。`DIR.dir` 在 `f_readdir` 之后已指向下一项。
- 按位置打开【确认】：`f_open` 只接受路径（ff.c:3308–3310）。FatFs 内部在 `f_open` 中用 `ld_clust` 与 `DIR_FileSize` 构造对象（ff.c:3456–3457），但没有导出入口。
- 手工 FIL【确认】：FIL 的字段是公开的（ff.h:155–172）。但 `f_close` 无条件执行 `dec_lock(fp->obj.lockid)`（ff.c:3828）；`dec_lock` 对 0 返回 FR_INT_ERR（ff.c:848–857）；`inc_lock` 为 static（ff.c:808）。因此手工 FIL 要么无法正常关闭，要么绕过锁计数。
- 锁的对象标识【确认】：Files[] 以 (fs, 目录起始簇 `dp->obj.sclust`, 目录内字节偏移 `dp->dptr`) 识别对象（ff.c:781–783、817–819、826–827）。“目录簇 + 偏移”与 FatFs 的对象标识相同。
- 修改 ff.c 的约束【确认】：AGENTS.md「关键保护」规定 CubeMX 生成文件与 Vendor 只经源工程导出或上游更新，不手改。
- 内存【推断】：每轨 8 B（两个 DWORD：目录内偏移与起始簇，或起始簇与大小）。现有 catalog 的 Offset/Length 已占 8 B/轨（storage_catalog.c:26–31，静态数组见 :46–47，位于 `.storage_catalog` 段）。再加 8 B/轨即约 256 KB（32000 首）。
- 效果【推断】：打开的目录工作量降为常数（读一个目录扇区加固定开销）。建表扫描仍是 O(N)（2.6 节）。

### 3.2 (b) 升级到更新的上游 FatFs

- 【摘要】updates 页：R0.13（2017-05-21）起配置项前缀由 `_` 改为 `FF_`。R0.13c 修复“目录读取在最后一项非空时无限循环”（标注 appeared at R0.12）。R0.14 修复 f_readdir 无法取回长度为 FF_MAX_LFN−1 的长文件名。R0.15 与 R0.15a 的锁与并发修复只在 `FF_FS_REENTRANT` 且 `FF_VOLUMES` ≥ 2 时相关，本仓库 `_FS_REENTRANT 0`（ffconf.h:241）。R0.15b 引入、R0.16 修复 f_readdir 的结束判断错误（无限循环）。
- 【摘要】在摘要所列条目中未见 FAT 目录查找加速、按位置打开或 f_lseek 的新 API。完整条目【未核对】。
- 【摘要】fast seek 仍存在，配置名为 `FF_USE_FASTSEEK`（lseek 页）。
- 【未核对】本仓库 dir_next/dir_read 的链尾分支与 R0.13c 修复条件的关系：本地代码在 `stretch = 0` 时返回 FR_NO_FILE（ff.c:1546–1550），dir_read 在该返回值时退出（ff.c:2193–2197）；未与上游补丁逐行比对。
- 【确认】本地 ffconf.h 仍为 `_` 前缀。升级时需按上游模板重新对照；ST 的 st_readme.txt:208–210 描述了同类的升级做法。
- 上游当前版本为 R0.16（2025-07-22，updates 页）。发布形式见第 5 节。
- 效果【推断】：摘要所列条目中没有位置相关的改进。

### 3.3 (c) 在自维护层加扇区缓存

- 位置【确认】：SD 卷的 disk I/O 由 CubeMX 生成的 `sd_diskio.c` 调用 Service 的 `BSP_SD_*` 强定义（FATFS/Target/README.md；filesystem_sd_bsp.c:56–67）。因此 SD 路径的自维护层是 `Service/filesystem/sd/`。`bsp_driver_user_diskio.*` 属于 USER 卷（FATFS/App/fatfs.c:34），其弱默认值在 bsp_driver_user_diskio.c:14–84，不在 SD 读取路径上。`sd_diskio.c` 只允许在 USER CODE 段内薄转发（FATFS/Target/README.md）。
- 效果【推断】：只对重复访问的目录扇区生效，冷打开不变。若整个目录可缓存，单次打开的 SD 读降为内存拷贝，剩余为 FatFs 的 CPU 遍历，估计为数十至百余毫秒（未测）。
- 容量【确认】算术，用法【推断】：32000 首 × k 项 ÷ 16 个扇区 × 512 B。k = 4 → 8000 扇区，4.1 MB；k = 5 → 10,000 扇区，5.1 MB；k = 6 → 12,000 扇区，6.1 MB。SDRAM 为 32 MiB（docs/shape/sources/hardware-facts.md:9）；块池 1 MiB（APP/tasks/storage/README.md:61）；曲库池 4 MiB（storage_catalog_config.h:12）。SD bounce buffer 32 KiB（filesystem_sd_transfer.c:32–34）不是缓存。
- 一致性【确认】：产品路径不写 SD（filesystem_config.h:40–41 注释）；存在 SD 读写测试分支（APP/tasks/storage/README.md:75）。若缓存需要在写后生效，需要失效规则【推断】。
- FatFs 改动：无。`disk_read` 接口不变（Middlewares/Third_Party/FatFs/src/diskio.h）。

### 3.4 (d) 分桶或限制单目录文件数

- 效果【推断】：每次 f_open 只经过父目录与目标桶，工作量上界与桶容量成正比（父目录项数 + 桶内项数）。桶容量固定时上界为常数。
- 数字示例【推断】（假设 8.3 名的桶目录占 1 项，每首 5 项）：桶 256 首 → 桶内 1280 项，约 80 个扇区；32000 首 → 125 个桶 → 父目录约 125 项，约 8 个扇区；合计约 88 次扇区读，按 T = 0.2 ms 约 18 ms。
- 上限【确认】：每个目录各自受 65,536 项限制，分桶可绕开单目录上限（2.7 节）。
- 已有支持【确认】：catalog 的 DFS 支持子目录（storage_catalog.c:142–250；待扫目录栈深度 128，storage_catalog_config.h:16）。
- 改动【确认】：SD 上的目录结构（用户曲库布局）。FatFs 与 ffconf.h 不需改动。这属于产品层决定。

### 3.5 (e) 其他做法

- e1 exFAT（`_FS_EXFAT 1`）【确认】：`MAX_DIR_EX` = 0x10000000（ff.c:326，约 8.4 M 项）。`dir_find` 的 exFAT 分支先比对名称哈希（ff.c:2227–2233），但仍是线性遍历（ff.c:2228–2240），因此不是位置无关。启用需要 `_USE_LFN ≥ 1`（ffconf.h:212–215），且 FSIZE_t 变为 QWORD（ff.h:72–76）。卡须为 exFAT 格式。本配置未启用（ffconf.h:212）；find_volume 的 exFAT 分支位于 `#if _FS_EXFAT` 下（ff.c:3068–3069）。应用层对超过 4 GiB−1 的偏移按参数错误处理（filesystem_file.c:190）。
- e2 f_expand（`_USE_EXPAND 1`，ffconf.h:66；ff.c:5135–5221，需 `!_FS_READONLY`）【确认】：预分配连续簇，可减少 f_lseek 的 FAT 读（第 4 节）。需要写卡，而产品路径不写 SD。上游 lseek 页摘要也建议在需要连续区域时使用 f_expand（【摘要】）。
- e3 复用打开句柄：受 `_FS_LOCK 2` 限制（2.8 节）。
- e4 只用 f_readdir 顺序遍历、不 f_open【推断】：Storage 需要按路径打开文件读标签（storage_track_text.c:95–100；STOR-16，docs/shape/storage.md:28），因此不能直接替代。

## 4. 问题 3：f_lseek 与 `_USE_FASTSEEK`

### 4.1 无 CLMT（本仓库 `_USE_FASTSEEK 0`）

- 算法【确认】：目标在当前簇或之后时，从 `fp->clust` 起（ff.c:4087–4091）；否则从 `sclust` 起（ff.c:4092–4093）。只读时超过文件末尾则钳制到 objsize（ff.c:4080–4082）。
- 每跨一簇调用一次 `get_fat`（ff.c:4120）。FAT32 下每次调用 `move_window(fatbase + clst/128)`（ff.c:1033）。途经的 FAT 扇区个数即读次数；窗口命中则不读。
- 目标落在扇区中间时，另读一个数据扇区到 `fp->buf`（ff.c:4138–4147）。
- 读次数公式【推断】：连续分配时约 ⌈跨簇数 / 128⌉ + 1；碎片化时约为跨簇数 + 1。FAT16 每扇区 256 项，FAT12 约 341 项。
- 卡的簇大小与 FAT 类型【未核对】：仓库内无记录。表 4-1 按常见簇大小估算（FAT32；连续为下限，碎片化为上限）。簇数 = 文件大小 ÷ 簇大小【推断】。上游 appnote 给出的簇上限为 128 扇区（64 KiB）【摘要】。

| 文件 | 簇大小 | 簇数 | 整链 FAT 读（连续） | 整链 FAT 读（每簇一次） |
|---|---|---|---|---|
| 5 MiB | 4 KiB | 1280 | 10 | 1280 |
| 5 MiB | 8 KiB | 640 | 5 | 640 |
| 5 MiB | 16 KiB | 320 | 3 | 320 |
| 5 MiB | 32 KiB | 160 | 2 | 160 |
| 20 MiB | 4 KiB | 5120 | 40 | 5120 |
| 20 MiB | 8 KiB | 2560 | 20 | 2560 |
| 20 MiB | 16 KiB | 1280 | 10 | 1280 |
| 20 MiB | 32 KiB | 640 | 5 | 640 |

- 时间量级【推断】：以 T = 0.16–0.47 ms（表 2-2）计，20 MiB、4 KiB 簇、碎片化的整链遍历约 0.8–2.4 s；连续分配时约 6–19 ms。需板上测量。
- 本仓库的 seek 使用点【确认】：
  - (i) 标签读取：storage_track_text.c:38 的 seek + read 回调；每次读 4 KiB，最多 4 次（storage_catalog_config.h:19–20）。目标多在文件头部，常位于首簇内（无 FAT 读），取决于簇大小与标签长度【推断】。
  - (ii) 流开启：storage_audio_stream.c:292 读头 10 B（storage_audio_range.h:13）；:295 seek 到 size − 160 读尾部 160 B（storage_audio_range.h:14）。这一次 seek 从头部向后跨越约 size ÷ 簇大小 个簇，即表 4-1 的整链量级。随后 :305 seek 回音频起点，从 sclust 起，跨簇数约为起点 ÷ 簇大小。因此流开启对大文件有与文件大小成正比的 FAT 读，尽管尾部只读 160 B。

### 4.2 CLMT（`_USE_FASTSEEK 1`）

- 启用【确认】：`f_open` 之后由应用设置 `fp->cltbl`（ff.h:167）；f_open 清零（ff.c:3460）。`cltbl[0]` 为缓冲容量（DWORD 数），再以 `CREATE_LINKMAP`（ff.h:334）调用 f_lseek。
- 创建【确认】（ff.c:4025–4049）：遍历整链，每簇一次 `get_fat`（ff.c:4035）；每段连续簇记为一对 (ncl, tcl)。
- 缓冲大小【确认】：2 + 2F 个 DWORD（F 为连续段数）。ulen 初值为 2，每段加 2（ff.c:4032），写回使用量（ff.c:4044），末尾写 0（ff.c:4045–4047）。上游 lseek 页摘要给出“2 × (fragments + 1)”【摘要】，与代码一致。
- 容量不足【确认】：返回 FR_NOT_ENOUGH_CORE（ff.c:4048），`cltbl[0]` 给出所需 DWORD 数（ff.c:4044）。
- 缓冲大小示例【推断】（按段数等于簇数的最坏情形）：

| 文件 | 簇大小 | 最坏段数 | 缓冲（DWORD） | 缓冲（字节） |
|---|---|---|---|---|
| 5 MiB | 4 KiB | 1280 | 2562 | 10,248 |
| 5 MiB | 16 KiB | 320 | 642 | 2,568 |
| 20 MiB | 4 KiB | 5120 | 10,242 | 40,968 |
| 20 MiB | 16 KiB | 1280 | 2562 | 10,248 |

  连续文件只需 4 个 DWORD（16 B）。
- 跳转【确认】：`clmt_clust`（ff.c:1440–1458）线性扫描段表，代价为 O(段数)；f_lseek 不读 FAT（ff.c:4054）；落在扇区中间时至多读一个数据扇区（ff.c:4058–4068）。
- 读写路径【确认】：f_read 跨簇时使用 CLMT（ff.c:3541–3545），不读 FAT；f_write 同样（ff.c:3646–3650）。
- 限制：
  - 每次打开后清零（ff.c:3460），需要重新设置【摘要】（lseek 页“cltbl is nulled on file open”）。
  - 不能扩展文件【确认】（ff.c:4050–4051 钳制到 objsize）；lseek 页摘要称“the file size cannot be expanded by f_write, f_lseek function while the file is in fast seek mode”【摘要】。
  - 文件被修改后表是否仍有效：lseek 页摘要未说明【未核对】。
  - 只读打开【确认】（代码）：CREATE_LINKMAP 分支不检查 FA_WRITE（ff.c:4024–4049），读路径直接使用（ff.c:3541）。代码中没有只读专属限制。lseek 页摘要未列只读限制【摘要】。

### 4.3 与 `_FS_LOCK` 与只读打开的关系【确认】

- CLMT 挂在 FIL 上（ff.h:167），不占 Files[] 锁槽。锁按对象计（ff.c:808–836），`_FS_LOCK 2` 的槽数不因 CLMT 改变。
- CLMT 缓冲由应用提供，FatFs 不分配内存（ffconf.h:262–266 只定义 ff_malloc 宏；本仓库未启用 CLMT）。
- 只读打开（FA_READ）不占用额外的 CLMT 配额，但仍占一个锁槽（2.8 节）。同一文件的两个只读 FIL 各自拥有 CLMT，共享锁计数（ff.c:833）。

### 4.4 与问题 1 的交互【推断】

- 流开启的整链遍历与 CLMT 创建同为一次整链遍历。若使用 CLMT，创建成本在打开时一次付出（表 4-1 的整链量级），之后的跳转没有 FAT 读。
- 标签读取只读文件的小区域，通常不足以摊薄 CLMT 的创建成本。

## 5. 问题 4：许可证与本地修改惯例

### 5.1 许可证

- 【确认】ff.h:5–15、ff.c:5–15（ChaN，2017）：源码与二进制形式的再分发与修改，在保留版权声明与免责条件的前提下允许（单条件；“AS IS”，不承担责任）。
- 【摘要】上游首页（http://elm-chan.org/fsw/ff/）称 FatFs 为 “free software opened for education, research and development”，并称可以 “use, modify and/or redistribute it for any purpose without any restriction under your responsibility”（首页摘要，未逐字核对）。
- 【摘要】appnote 的 “About FatFs License” 节称：再分发含修改或 fork 时，许可可以改为 GPL、BSD 类或与 FatFs 兼容的开源许可。
- 【确认】本仓库根 LICENSE 为 MIT，与 Vendor 文件的 ChaN 许可无关；Vendor 文件保留 ChaN 头。

### 5.2 本仓库的基线

- 【确认】`Middlewares/Third_Party/FatFs/src/00readme.txt:1` 标注 R0.12c；ff.h:2 与 ffconf.h:4 相同。
- 【确认】`st_readme.txt` 按版本记录 ST 对 FatFs 的修改：V2.1.0（2018-09-21）的 `+ ff.c` 条目为回移植 R0.13 的 disk_status 信号量修复（st_readme.txt:110 起）。其余条目主要涉及模板文件。
- 【未核对】本地 ff.c 与上游 R0.12c 原档的完整差异（本票未取原档）。
- 【确认】Vendor 历史：`git log -- Middlewares/Third_Party/FatFs` 最近一次提交为 d15ed4f（2026-08-09）。

### 5.3 上游的修订与补丁方式

- 【摘要】archives 页：41 个修订压缩包（R0.01 至 R0.16），每个修订一个 zip。未列出仅含补丁的包。
- 【摘要】patches 页（“Known Problems and Patches”，最新条目 2026-07-10）：修复以对 R0.16 的补丁发布，文件为 ff16p1.diff、ff16p2.diff，以及 ffunicode.zip。页面称修复是“patches to R0.16, not as a new full revision”。
- 【摘要】安全条目：页面列出 CVE-2026-6682、-6683、-6687（由 patch-2 修复），并称 -6687 与 -6688 在存储卡插座与运行代码可达时可能导致 ACE。页面称跟踪范围为 R0.16 及更早版本，但未说明更早版本是否会修补。本仓库基线是否受影响【未核对】，需回原页逐条核对。
- 【摘要】updates 页：修复按修订分条列出，并标注“appeared at R0.xx”的引入版本。

### 5.4 本仓库的修改惯例

- 【确认】AGENTS.md「关键保护」：CubeMX 生成文件与 Vendor 只经源工程导出或上游更新，不手改；改生成器配置头（`ffconf.h`）前先问用户。
- 【确认】FATFS/Target/README.md：CubeMX 再生成后须复核 USER CODE、链接顺序与自维护源收集；`bsp_driver_user_diskio.*` 为自维护契约。
- 【确认】仓库内没有 FatFs 补丁清单文件；可查的 Vendor 变更只有 git 历史。
- 【确认】ST 的做法可作为参照：st_readme.txt 按版本列出文件级修改，并注明升级时的 ffconf 处理（st_readme.txt:208–210）。

### 5.5 对后续升级与修改的影响【推断】

- 修改 ff.c 后，升级需要合并。目前除 ST 条目外没有本地修改清单。
- 上游补丁以 R0.16 为基（diff 文件），本仓库是 R0.12c（加 ST 回移植）。补丁不能直接套用，需先确定基线。
- R0.13 的 FF_ 前缀改名会改变 ffconf.h 的全部配置名，需用户确认（AGENTS.md）。

## 6. 对后续决定的含义（不做选择）

1. 打开与位置无关的可行类别只有两类：绕开目录线性扫描（需要 FatFs 内部接口或修改 Vendor；或依赖内部约定的手工 FIL），或降低单次扫描的上界与常数（缓存、分桶）。
2. 32000 首单目录与 FatFs 的 65,536 项上限冲突（每文件 3 项时上限 21,845，4 项时 16,384）。超限部分静默消失，没有错误码。若保留 32000 上限，就需要分目录或 exFAT，选择由用户决定。
3. 单次目录扇区读的真实延迟与 CMD13 同步开销未测。0.06 ms/文件的分解，以及所有路线的收益数字，都依赖这两项。
4. 流开启的尾部探测对大文件有与文件大小成正比的 FAT 读（表 4-1）。若要降低，需同时考虑 CLMT 的创建成本与内存（表 4-2），或 f_expand（需写卡）。
5. `_FS_LOCK 2` 与 Storage 的 2 个 FIL 预算相同（APP/tasks/storage/README.md:18）。任何需要额外对象的方案（目录句柄、预开文件）都会在 FR_TOO_MANY_OPEN_FILES 处受限。
6. 升级上游会改 ffconf.h（FF_ 前缀）。选择目标版本时需考虑 R0.15b 的 readdir 结束判断问题（R0.16 已修）与 R0.13c 的目录读取修复条目。
7. 本地 ff.c 不是逐字的上游 R0.12c，补丁页又列有安全条目。任何升级或修改前都需要先确定基线。

## 7. 未决与需板上测量清单

- 【需板上测量】单次目录扇区读（读命令 + DMA + 通知）与 CMD13 同步的耗时。可复用仓库已有的 DWT 周期计数器 Adapter（Adapters/cortex/cycle_counter/cortex_m7_cycle_counter_adapter.c）与标签读取计时（#141，APP/tasks/storage/README.md:19）。
- 【需板上测量】同一目录内 f_open 随位置的分段耗时（复现 I10 斜率），以及 CPU 占比。
- 【未核对】真实曲库的文件名长度分布（每文件项数），以及测试卡的簇大小、FAT 类型与碎片度。
- 【未核对】根目录项数（影响固定开销）。
- 【未核对】上游 R0.13c–R0.16 的完整修复列表（本票取了摘要中的条目），以及本地 ff.c 与 R0.12c 原档的差异。
- 【未核对】patches 页 CVE 条目对本仓库基线的适用性。
- 【未核对】lseek 页与 appnote 的原文（本票使用 WebFetch 摘要）。
- 【未核对】idx 与目录项位置的换算（假设目录内只有 .mp3 且无子目录）。
- 【未找到】仓库内没有单扇区读或 f_open 的实测记录。#143 I10 是唯一的位置相关实测。

## 8. 来源清单

- 本仓库（路径与行号以本分支为准）：
  - FatFs：Middlewares/Third_Party/FatFs/src/ff.c、ff.h、diskio.h、00readme.txt、st_readme.txt、ffconf_template.h；FATFS/Target/ffconf.h、sd_diskio.c、bsp_driver_user_diskio.c、README.md；FATFS/App/fatfs.c；Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_sd.c；Core/Src/main.c。
  - Service：Service/filesystem/filesystem_config.h、filesystem_directory.c、filesystem_file.c、filesystem_types.h、sd/filesystem_sd_bsp.c、sd/filesystem_sd_transfer.c。
  - Platform 与 Components：Platform/sd/platform_sd.c；Components/sd/sd.c、sd_config.h。
  - 适配器：Adapters/stm32_hal/sd/sd_stm32_hal_adapter.c；Adapters/cortex/cycle_counter/。
  - APP：APP/tasks/storage/README.md、catalog/storage_catalog.c、storage_catalog_config.h、storage_track_text.c、stream/storage_audio_stream.c、storage_audio_range.h。
  - 文档：docs/shape/storage.md；docs/shape/sources/hardware-facts.md；AGENTS.md；docs/shape/git.md（GIT-2、GIT-4、GIT-5）。
- 票与测量：https://github.com/immorcoding/mp3_project/issues/162 ；https://github.com/immorcoding/mp3_project/issues/143（I10 节）。
- 上游【摘要】（2026-10-30 访问）：http://elm-chan.org/fsw/ff/updates.html 、http://elm-chan.org/fsw/ff/doc/lseek.html 、http://elm-chan.org/fsw/ff/doc/appnote.html 、http://elm-chan.org/fsw/ff/patches.html 、http://elm-chan.org/fsw/ff/archives.html 、http://elm-chan.org/fsw/ff/ （首页）。
- 未访问：elm-chan.org 的 API 页原文（doc/*.html）；ST 的 UM1721（st_readme.txt 所引用）。
