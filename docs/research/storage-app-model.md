# 存储与应用模型：外部项目事实报告

- 调研日期：2026-11-02。只读调研，未修改任何代码仓库，未提交，未创建 issue。
- 由 Haiku subagent 从一手源码写成；主会话抽查过 Flipper 存储 API 与单应用限制、Rockbox 插件入口与缓冲线程、PebbleOS 执行模型等关键结论，其余标“推断”“未核实”的条目未复查。下表提交号即读取时的版本。
- 每条结论后附永久链接。标注“（推断）”的条目由调用关系推出、未实测；标注“未核实”的条目见第 3 节。

## 0. 取定的提交号与线索路径差异

| 项目 | 仓库 / 分支 | 提交号 |
|---|---|---|
| Rockbox | Rockbox/rockbox / master | 7fc6775be5e8435b9c29a40cb6058d7caa9ef7fc |
| Flipper Zero | flipperdevices/flipperzero-firmware / dev | 9981d9d4aa5ec7b14bb46cf15763dd293a7dec6a |
| PebbleOS | coredevices/PebbleOS / main | 3b9284a698ec3a1ff518be537d8859f0ed6bba33 |
| ESP-ADF（release/v2.x，线索路径所在） | espressif/esp-adf / release/v2.x | 74e6be93f007faa411dab985a2b7613725506790 |
| ESP-ADF（master，ADF v3.0） | espressif/esp-adf / master | a8a14c8fa6cb8aefa5299f4c06fe9679278097cc |
| ESP-IDF | espressif/esp-idf / master | 0e6b480f858c69862ffb6001d96e9eef74599a6e |
| Zephyr | zephyrproject-rtos/zephyr / main | d1b8eec76bfd14058854245d09aa1e413e355c4a |
| Linux | torvalds/linux / master | 6628e7d4c6b25c8d18f1345e3a20d21829aae7e4 |

线索路径与当前树的差异（事实）：
- ESP-ADF master 不含 `components/audio_pipeline/` 与 `components/audio_stream/fatfs_stream.c`，这两个路径只存在于 release/v2.x。master 为 ADF v3.0，README 声明与 v2.x 不兼容，核心管线改由外部的 ESP-GMF 承担（https://github.com/espressif/esp-adf/blob/a8a14c8fa6cb8aefa5299f4c06fe9679278097cc/README.md#L7 ，#L24-L25）。下文 ESP-ADF 分 v2.x 与 master 两节。
- PebbleOS 的 `src/fw/` 已在 `748b81e3f38879d3b67be8877ac6aac50ab0925a`（treewide: move src/fw to a top-level fw directory，2026-10-01）中迁为顶层 `fw/`，线索路径因此不存在。
- Rockbox、Flipper、Zephyr、ESP-IDF 的线索路径均存在。

## 1. 对照总表

| 项目 | 应用 / 插件能否阻塞 UI；前台数 | 存储接口形态 | 锁粒度 | 音频与文件的共存 | 元数据由谁填 |
|---|---|---|---|---|---|
| Rockbox | 插件在主线程内同步执行，插件阻塞即主线程阻塞；非 TSR 插件同时只能有 1 个 | 直接调用文件 API（同步），无 RPC；缓冲线程直接 read | 全局 MRSW 读写锁 + 每个流一个 mutex + ATA 设备 mutex | 音频缓冲区内的环形缓冲，缓冲线程（优先级 15），水位；插件取音频缓冲时可能硬停播放 | ID3 由缓冲线程解析；封面由音频线程同步加载；标签库与目录缓存由后台线程（优先级 20）构建 |
| Flipper Zero | 应用各有一个 FuriThread，GUI 是独立的 gui_srv 线程；同时仅 1 个前台应用；输入队列满时 GUI 线程会阻塞（推断） | 单存储线程的 RPC：请求进入深度 8 的消息队列，调用方在 FuriEventFlag 上等待 | 无卷或文件锁；存储线程串行执行；同一路径已打开则拒绝 | 固件内未找到音频流通路；无预读缓冲 | 无曲库或元数据库；小数据用 saved_struct 整体覆盖写 |
| PebbleOS | App 与 KernelMain 分属不同内核线程；同时仅 1 个 App 与 1 个 Worker；切换 App 时先清理旧 App | 系统服务直接调用 pbl_fs；第三方 app 的持久化经 PBL_SYSCALL 同步执行 | PFS 单一全局互斥量；persist 服务单一互斥量 | 扬声器 PCM 为调用方推入的 RAM 环形缓冲；未见从文件流式读取 | 键值数据库（blob_db、settings_file）；曲库与封面未核实 |
| ESP-ADF v2.x | 无 UI 线程概念；每个 audio element 一个任务（默认优先级 5） | 直接 POSIX open/read（fatfs_stream） | ESP-IDF FatFs 每卷一把互斥量；ringbuf 内为互斥量加两个信号量 | element 之间为 ringbuf（默认 8 KiB），阻塞读写带超时 | 管线内无元数据组件 |
| ESP-ADF master（v3） | 播放服务使用 ESP-GMF 的 OAL 线程；前台数未核实 | 服务内部直接调用文件 API；事件中心非阻塞投递 | 服务级互斥量（config_manager 的互斥量；player 每流 feed_lock） | 管线在外部 ESP-GMF 中（本仓库未核实） | playlist 表式数据库由服务写入（填充方未核实） |
| ESP-IDF FatFs（ESP-ADF 所用文件层） | 不适用 | 库函数直接调用 | `FF_FS_REENTRANT 1`，每卷互斥量，超时 `FF_FS_TIMEOUT` | 不适用 | 不适用 |
| Zephyr（简要） | 不适用 | VFS 直接调用后端 | 挂载表全局互斥量；LittleFS 每个 FS 一把互斥量；FAT 后端无锁 | 不适用 | 不适用 |
| Linux（简要） | 不适用 | 不适用 | 不适用 | I/O 优先级概念（RT / BE / IDLE） | 不适用 |

每个单元格的依据见第 2 节对应条目的链接。

## 2. 各项目

### 2.1 Rockbox（深入）

**Q1 应用与线程模型**
- 插件由主线程的根菜单调起，`int ret = plugin_load(path, param);`：https://github.com/Rockbox/rockbox/blob/7fc6775be5e8435b9c29a40cb6058d7caa9ef7fc/apps/root_menu.c#L797 ；根菜单由 `main()` 调用：https://github.com/Rockbox/rockbox/blob/7fc6775be5e8435b9c29a40cb6058d7caa9ef7fc/apps/main.c#L247
- 插件入口同步执行，`int rc = p_hdr->entry_point(parameter);`：https://github.com/Rockbox/rockbox/blob/7fc6775be5e8435b9c29a40cb6058d7caa9ef7fc/apps/plugin.c#L1039 。因此插件与 UI 同线程，插件内阻塞 I/O 即阻塞该线程（推断）。
- 插件 API 暴露 `create_thread` 与 `thread_set_priority`：https://github.com/Rockbox/rockbox/blob/7fc6775be5e8435b9c29a40cb6058d7caa9ef7fc/apps/plugin.h#L590-L600 。插件是否实际使用：未核实。
- 同时前台：已有非 TSR 插件时，`plugin_load` 拒绝加载新插件（"not allowing another plugin to load"）：https://github.com/Rockbox/rockbox/blob/7fc6775be5e8435b9c29a40cb6058d7caa9ef7fc/apps/plugin.c#L923-L937 ；TSR 插件可在退出后驻留：https://github.com/Rockbox/rockbox/blob/7fc6775be5e8435b9c29a40cb6058d7caa9ef7fc/apps/plugin.c#L1050-L1058
- 线程优先级：音频线程为 `MIN(PRIORITY_BUFFERING, PRIORITY_USER_INTERFACE)`（即 15）：https://github.com/Rockbox/rockbox/blob/7fc6775be5e8435b9c29a40cb6058d7caa9ef7fc/apps/audio_thread.c#L168-L170 ；codec 线程为 PRIORITY_PLAYBACK（即 16）：https://github.com/Rockbox/rockbox/blob/7fc6775be5e8435b9c29a40cb6058d7caa9ef7fc/apps/codec_thread.c#L666-L669 ；数值表（数值越小优先级越高）：https://github.com/Rockbox/rockbox/blob/7fc6775be5e8435b9c29a40cb6058d7caa9ef7fc/firmware/kernel/include/thread.h#L49-L60

**Q2 存储接口**
- 无 RPC。文件 API 直接读：`rbfs_read`，https://github.com/Rockbox/rockbox/blob/7fc6775be5e8435b9c29a40cb6058d7caa9ef7fc/firmware/common/file.c#L937-L960 ；插件 API 暴露 open/read：https://github.com/Rockbox/rockbox/blob/7fc6775be5e8435b9c29a40cb6058d7caa9ef7fc/apps/plugin.h#L498-L501
- 缓冲线程直接调用 `read(h->fd, ...)`：https://github.com/Rockbox/rockbox/blob/7fc6775be5e8435b9c29a40cb6058d7caa9ef7fc/apps/buffering.c#L691
- 数据类型枚举在共享模块中（ID3、CODEC、PACKET_AUDIO、ATOMIC_AUDIO、CUESHEET、BITMAP、RAW_ATOMIC）：https://github.com/Rockbox/rockbox/blob/7fc6775be5e8435b9c29a40cb6058d7caa9ef7fc/apps/buffering.h#L30-L39 ；`bufopen` 按类型分支：ID3 见 https://github.com/Rockbox/rockbox/blob/7fc6775be5e8435b9c29a40cb6058d7caa9ef7fc/apps/buffering.c#L919-L947 ，BITMAP 加载见 #L1041-L1051 ，CUESHEET 见 #L1054-L1056
- 插件函数表 `rockbox_api` 中检索不到 bufopen 或 bufread（grep 结果）。
- 新增应用：文件 API 已通用，无需新的存储接口；新增缓冲数据类型需要改动共享的 `buffering.c`（推断，依据上述分支结构）。

**Q3 并发控制**
- 全局 MRSW 锁 `file_internal_mrsw`：https://github.com/Rockbox/rockbox/blob/7fc6775be5e8435b9c29a40cb6058d7caa9ef7fc/firmware/common/file_internal.c#L51 。读路径取 READER（`rbfs_read`，https://github.com/Rockbox/rockbox/blob/7fc6775be5e8435b9c29a40cb6058d7caa9ef7fc/firmware/common/file.c#L939 ），目录与卷修改取 WRITER（`open_internal_locked`，https://github.com/Rockbox/rockbox/blob/7fc6775be5e8435b9c29a40cb6058d7caa9ef7fc/firmware/common/file.c#L514-L519 ）。
- 每个打开的流一个 mutex：https://github.com/Rockbox/rockbox/blob/7fc6775be5e8435b9c29a40cb6058d7caa9ef7fc/firmware/include/file_internal.h#L183-L190 ；READER 模式下才取流锁：#L193-L201 ；读写封装宏：https://github.com/Rockbox/rockbox/blob/7fc6775be5e8435b9c29a40cb6058d7caa9ef7fc/firmware/common/file.c#L69-L86
- ATA 驱动：每次扇区读写包在 `ata_mutex` 内：https://github.com/Rockbox/rockbox/blob/7fc6775be5e8435b9c29a40cb6058d7caa9ef7fc/firmware/drivers/ata.c#L589-L600 、#L613-L615 。DMA 完成在扇区传输中以 `ata_dma_finish()` 等待（https://github.com/Rockbox/rockbox/blob/7fc6775be5e8435b9c29a40cb6058d7caa9ef7fc/firmware/drivers/ata.c#L482 ）；具体等待方式由平台实现，未核实。调用扇区读写的线程即等待者。
- FAT 为 Rockbox 自研（版权头为 Rockbox 作者，源码中无 FatFs 标识）：https://github.com/Rockbox/rockbox/blob/7fc6775be5e8435b9c29a40cb6058d7caa9ef7fc/firmware/common/fat.c#L1-L12
- dircache 线程在运行期间持有 WRITER 锁：`dircache_lock()` 即 `file_internal_lock_WRITER()`（https://github.com/Rockbox/rockbox/blob/7fc6775be5e8435b9c29a40cb6058d7caa9ef7fc/firmware/common/dircache.c#L95 ），线程入口处取锁（#L2015）；仅在显式释放点暂时释放（重分配处见 #L1938-L1952）。扫描时读取目录项在持锁状态下进行（https://github.com/Rockbox/rockbox/blob/7fc6775be5e8435b9c29a40cb6058d7caa9ef7fc/firmware/common/dircache.c#L1260-L1272 ：栈空间条件成立时才在读前释放锁）。因此该期间的 READER 请求（包括缓冲线程的 read）会被阻塞（推断）。
- 优先级继承：内核注释声明基础 PIP（https://github.com/Rockbox/rockbox/blob/7fc6775be5e8435b9c29a40cb6058d7caa9ef7fc/firmware/kernel/thread.c#L144-L167 ）；MRSW 锁使用 blocker splay 结构并调用 `priority_disinherit`（https://github.com/Rockbox/rockbox/blob/7fc6775be5e8435b9c29a40cb6058d7caa9ef7fc/firmware/kernel/mrsw_lock.c#L40-L71 ）。读路径上的完整语义未核实。
- buflib 的可移动分配约束：https://github.com/Rockbox/rockbox/blob/7fc6775be5e8435b9c29a40cb6058d7caa9ef7fc/firmware/core_alloc.c#L83-L85 （"Don't pass them to functions that call yield() like disc input/output."）。

**Q4 实时流与其他访问共存**
- 预读缓冲为一个环形缓冲（"filebuf"），位于音频缓冲内，由 buflib 分配：https://github.com/Rockbox/rockbox/blob/7fc6775be5e8435b9c29a40cb6058d7caa9ef7fc/apps/playback.c#L1074-L1091 ；音频缓冲保留 256 KiB 下限：#L865
- 缓冲线程以 PRIORITY_BUFFERING（15）运行：https://github.com/Rockbox/rockbox/blob/7fc6775be5e8435b9c29a40cb6058d7caa9ef7fc/apps/buffering.c#L1672-L1691 ；每次 read 最多 32 KiB：#L70 ；每个块之间检查请求队列，有请求即让出：#L712-L715
- 消费端：`prep_bufdata` 以 `sleep(0)` 循环等待，每 100 次轮询发送一次 `Q_START_FILL`：https://github.com/Rockbox/rockbox/blob/7fc6775be5e8435b9c29a40cb6058d7caa9ef7fc/apps/buffering.c#L1342-L1368 。`Q_START_FILL` 处理中先 `shrink_buffer()` 并回复调用方，再读盘：https://github.com/Rockbox/rockbox/blob/7fc6775be5e8435b9c29a40cb6058d7caa9ef7fc/apps/buffering.c#L1600-L1604
- 水位：`bytes = bitrate × (1000/8) × seconds`，seconds 含启动时间与 buffer_margin：https://github.com/Rockbox/rockbox/blob/7fc6775be5e8435b9c29a40cb6058d7caa9ef7fc/apps/playback.c#L1120-L1125 、#L1131-L1143 ；`buf_set_watermark`：#L1163 。高水位为缓冲区的 3/4：https://github.com/Rockbox/rockbox/blob/7fc6775be5e8435b9c29a40cb6058d7caa9ef7fc/apps/buffering.c#L1735 ；生效水位取两者较小值：#L252
- 线程优先级（数值越小越高）：BUFFERING 15、USER_INTERFACE 16（主线程；存储线程同为 16，https://github.com/Rockbox/rockbox/blob/7fc6775be5e8435b9c29a40cb6058d7caa9ef7fc/firmware/storage.c#L281-L285 ）、PLAYBACK 16、SYSTEM 18、BACKGROUND 20。
- 共存点：所有读请求经过同一个 MRSW 读锁与流锁（Q3）；扇区 I/O 经 `ata_mutex` 串行（Q3）；dircache 的写锁会阻塞读请求（Q3，推断）。
- 填充结束且未被队列事件打断时调用 `storage_sleep()` 让存储设备休眠（`fill_buffer` 的返回路径）：https://github.com/Rockbox/rockbox/blob/7fc6775be5e8435b9c29a40cb6058d7caa9ef7fc/apps/buffering.c#L815-L840

**Q5 元数据与缓存**
- ID3：`TYPE_ID3` 句柄在缓冲线程中解析（`get_metadata_ex`）：https://github.com/Rockbox/rockbox/blob/7fc6775be5e8435b9c29a40cb6058d7caa9ef7fc/apps/buffering.c#L653-L661
- 封面：`audio_load_albumart` 在音频线程的装载流程中调用（https://github.com/Rockbox/rockbox/blob/7fc6775be5e8435b9c29a40cb6058d7caa9ef7fc/apps/playback.c#L2303 ），该流程由 `Q_AUDIO_FINISH_LOAD_TRACK` 在音频线程中处理（#L3658）；bufopen(TYPE_BITMAP) 同步加载图片（https://github.com/Rockbox/rockbox/blob/7fc6775be5e8435b9c29a40cb6058d7caa9ef7fc/apps/buffering.c#L1041-L1051 ）。文件夹封面缓存见提交 `951e23951734b958d43f3e726b201fa2c52c2c01`（Cache folder album art，2023-09-20）：https://github.com/Rockbox/rockbox/commit/951e23951734b958d43f3e726b201fa2c52c2c01
- 目录缓存 dircache：后台线程（PRIORITY_BACKGROUND）：https://github.com/Rockbox/rockbox/blob/7fc6775be5e8435b9c29a40cb6058d7caa9ef7fc/firmware/common/dircache.c#L2066-L2068 ；内存由 core_alloc 分配：#L478-L481 ；同步构建时"使用整个音频缓冲的可用空间"：https://github.com/Rockbox/rockbox/blob/7fc6775be5e8435b9c29a40cb6058d7caa9ef7fc/firmware/common/dircache.c#L1924-L1927 ；持久化写入 `DIRCACHE_FILE`（O_TRUNC）：https://github.com/Rockbox/rockbox/blob/7fc6775be5e8435b9c29a40cb6058d7caa9ef7fc/firmware/common/dircache.c#L3166-L3170
- 标签库 tagcache：后台线程：https://github.com/Rockbox/rockbox/blob/7fc6775be5e8435b9c29a40cb6058d7caa9ef7fc/apps/tagcache.c#L5477-L5480 ；目录扫描调用 `add_tagcache`（#L4981），其中 `get_metadata_afmt` 读取文件标签（https://github.com/Rockbox/rockbox/blob/7fc6775be5e8435b9c29a40cb6058d7caa9ef7fc/apps/tagcache.c#L2261-L2342 ）；从数据库回填 mp3entry 的是 `tagcache_fill_tags`（#L2133-L2146）；Q_REBUILD 删除临时库后重建（#L5306-L5309）。
- 构建时"尽量抢占缓冲"：先释放 dircache 缓冲（https://github.com/Rockbox/rockbox/blob/7fc6775be5e8435b9c29a40cb6058d7caa9ef7fc/apps/tagcache.c#L3436-L3441 ），插件构建时再取音频缓冲（#L3461-L3468 ），都不可用则推迟到下次启动提交（#L3470）。

**Q6 热插拔**
- 存储线程处理插拔：卸载后广播 `SYS_FS_CHANGED`，插入时再挂载：https://github.com/Rockbox/rockbox/blob/7fc6775be5e8435b9c29a40cb6058d7caa9ef7fc/firmware/storage.c#L207-L226
- `disk_unmount` 对卷执行 `volume_onunmount_internal` 与 `fat_unmount`：https://github.com/Rockbox/rockbox/blob/7fc6775be5e8435b9c29a40cb6058d7caa9ef7fc/firmware/common/disk.c#L524-L548 ；前者调用 `fileobj_mgr_unmount`：https://github.com/Rockbox/rockbox/blob/7fc6775be5e8435b9c29a40cb6058d7caa9ef7fc/firmware/include/dircache_redirect.h#L240-L247
- `fileobj_mgr_unmount` 对该卷上所有打开的流执行卸载回调（写者强制关闭、读者关闭），并置 `FD_NONEXIST`：https://github.com/Rockbox/rockbox/blob/7fc6775be5e8435b9c29a40cb6058d7caa9ef7fc/firmware/common/fileobj_mgr.c#L412-L436 。之后对这些描述符的调用返回 ENXIO：https://github.com/Rockbox/rockbox/blob/7fc6775be5e8435b9c29a40cb6058d7caa9ef7fc/firmware/common/file.c#L65
- 进行中的缓冲：`read()` 返回 0 或错误时，把文件大小截断为已缓冲长度：https://github.com/Rockbox/rockbox/blob/7fc6775be5e8435b9c29a40cb6058d7caa9ef7fc/apps/buffering.c#L693-L704 ；CODEC 类型则中断填充：#L695-L698
- 以上为代码路径，未实测。

**Q7 内存预算**
- 所有 core 分配经 buflib（可移动、可收缩）：https://github.com/Rockbox/rockbox/blob/7fc6775be5e8435b9c29a40cb6058d7caa9ef7fc/firmware/core_alloc.c#L110 ；`buflib_alloc_maximum` 以"尽量收缩"的提示循环调用各收缩回调：https://github.com/Rockbox/rockbox/blob/7fc6775be5e8435b9c29a40cb6058d7caa9ef7fc/firmware/buflib_mempool.c#L859-L870 ；收缩回调在 `buflib_compact_and_shrink` 中调用：#L435-L479
- 播放的收缩回调：剩余不足 256 KiB 且提示为双向（即 alloc_maximum 的情形）时置 `give_up`：https://github.com/Rockbox/rockbox/blob/7fc6775be5e8435b9c29a40cb6058d7caa9ef7fc/apps/playback.c#L963-L992 。`give_up` 分支：硬停播放，释放音频缓冲，直接返回，不再投递 `Q_AUDIO_PLAY` 续播（https://github.com/Rockbox/rockbox/blob/7fc6775be5e8435b9c29a40cb6058d7caa9ef7fc/apps/playback.c#L1026-L1045 ；续播代码在 #L1060-L1063，位于该分支之后）。缓冲被标记为 TRASHED，下次播放前 `audio_reset_buffer()` 重建：https://github.com/Rockbox/rockbox/blob/7fc6775be5e8435b9c29a40cb6058d7caa9ef7fc/apps/playback.c#L2475-L2478
- 插件取音频缓冲：`plugin_get_audio_buffer` 调用 `core_alloc_maximum`，注释写明"Playback gets stopped"：https://github.com/Rockbox/rockbox/blob/7fc6775be5e8435b9c29a40cb6058d7caa9ef7fc/apps/plugin.c#L1169-L1184 ；插件退出时释放：https://github.com/Rockbox/rockbox/blob/7fc6775be5e8435b9c29a40cb6058d7caa9ef7fc/apps/plugin.c#L1055-L1058
- 插件本体加载到静态数组 `pluginbuf[PLUGIN_BUFFER_SIZE]`：https://github.com/Rockbox/rockbox/blob/7fc6775be5e8435b9c29a40cb6058d7caa9ef7fc/apps/plugin.c#L77 、#L958
- 音频缓冲、dircache、tagcache 共享同一 core 池（分配点：`apps/playback.c#L1083` ，`firmware/common/dircache.c#L478-L481` ，`apps/tagcache.c#L789` ，`apps/plugin.c#L1177` ）。

**Q8 写入与持久化**
- settings：`settings_save` 注销并重新注册空闲回调，不立即写：https://github.com/Rockbox/rockbox/blob/7fc6775be5e8435b9c29a40cb6058d7caa9ef7fc/apps/settings.c#L772-L778 ；回调由存储线程调用：https://github.com/Rockbox/rockbox/blob/7fc6775be5e8435b9c29a40cb6058d7caa9ef7fc/firmware/storage.c#L181
- 写入方式：写入临时文件（`open(..., O_CREAT|O_TRUNC|O_WRONLY)`），https://github.com/Rockbox/rockbox/blob/7fc6775be5e8435b9c29a40cb6058d7caa9ef7fc/apps/settings.c#L618-L625 、#L712-L733 ；下次加载时 rename 为正式文件：#L129（注释）、https://github.com/Rockbox/rockbox/blob/7fc6775be5e8435b9c29a40cb6058d7caa9ef7fc/apps/settings.c#L135-L147
- 运行状态（runtime）按 `SYSTEM_STATUS_UPDATE_TICKS` 节流写入：https://github.com/Rockbox/rockbox/blob/7fc6775be5e8435b9c29a40cb6058d7caa9ef7fc/apps/settings.c#L698-L708
- 多次 `settings_save` 只注册同一回调，因此在下一次空闲时合并写入（由注册语义推出，注册函数实现未核实）。

**Q9 演化（提交与说明）**
- 2014-08-30 `7d1a47cf13726c95ac46027156cc12dd9da5b855`（Rewrite filesystem code (WIP)）：说明写明旧代码 "Thread safety: There is none right now in the trunk code"，并引入 fileobj_mgr、让文件与目录共享核心：https://github.com/Rockbox/rockbox/commit/7d1a47cf13726c95ac46027156cc12dd9da5b855
- 2014-08-30 `5b08f1a5b99136ef052b6f430b62bc618cd44946`（Remove I/O priority. It is harmful when used with the new file code.）：移除 HAVE_IO_PRIORITY，说明称 dircache 已不再降低其线程的 I/O 优先级：https://github.com/Rockbox/rockbox/commit/5b08f1a5b99136ef052b6f430b62bc618cd44946
- 2011-02-14 `6938255b6b99f353652149057b5fe94fd0bb8f5d`（Buffering: tin cup. Update threading structure and handle rebuffer more reliably on buffer thread using a single message send.）：重缓冲请求改为经单条消息送达缓冲线程：https://github.com/Rockbox/rockbox/commit/6938255b6b99f353652149057b5fe94fd0bb8f5d
- 2020-12-25 `ca09f91f647f6677fea7804930d7d1b2c876f76e`（Fix deadlocks when trying to buffer large album art.）：大封面导致播放端把"装不下"当作"缓冲区满"而死等，新增 `ERR_BITMAP_TOO_LARGE`（https://github.com/Rockbox/rockbox/blob/7fc6775be5e8435b9c29a40cb6058d7caa9ef7fc/apps/buffering.c#L1010-L1013 ）：https://github.com/Rockbox/rockbox/commit/ca09f91f647f6677fea7804930d7d1b2c876f76e
- 2022-04-20 `9e93796407ecb8a347f9799c0a03e80939004bd7` 删除 bufgettail/bufcuttail（理由：仅有一个调用者）；2025-05-20 `2bd88936f78903f1f84d4f0480d92af1327df7ba` 回退（FS#13626，非音频的 ID3v1/APE 尾标）；2025-05-26 `78903e9d388d04f0af047eeebc790874326d0268` 重新删除（剥离标签的功能移至 codec）。链接：https://github.com/Rockbox/rockbox/commit/9e93796407ecb8a347f9799c0a03e80939004bd7 ，https://github.com/Rockbox/rockbox/commit/2bd88936f78903f1f84d4f0480d92af1327df7ba ，https://github.com/Rockbox/rockbox/commit/78903e9d388d04f0af047eeebc790874326d0268

### 2.2 Flipper Zero（深入）

**Q1 应用与线程模型**
- 每个应用一个 FuriThread：`loader_start_app_thread` 设置 insomnia 与状态回调后启动线程：https://github.com/flipperdevices/flipperzero-firmware/blob/9981d9d4aa5ec7b14bb46cf15763dd293a7dec6a/applications/services/loader/loader.c#L392-L415
- 同时仅一个前台应用：`loader_do_is_locked` 判断 `loader->app.thread != NULL`，已有则返回 `LoaderStatusErrorAppStarted`：https://github.com/flipperdevices/flipperzero-firmware/blob/9981d9d4aa5ec7b14bb46cf15763dd293a7dec6a/applications/services/loader/loader.c#L600-L626
- GUI 为独立服务线程 `gui_srv`（服务清单：栈 2 KiB，order 70）：https://github.com/flipperdevices/flipperzero-firmware/blob/9981d9d4aa5ec7b14bb46cf15763dd293a7dec6a/applications/services/gui/gui.c#L550-L576 ，https://github.com/flipperdevices/flipperzero-firmware/blob/9981d9d4aa5ec7b14bb46cf15763dd293a7dec6a/applications/services/gui/application.fam#L3-L12 。重绘时取 `gui_lock`：https://github.com/flipperdevices/flipperzero-firmware/blob/9981d9d4aa5ec7b14bb46cf15763dd293a7dec6a/applications/services/gui/gui.c#L243-L247 、#L341-L349
- 应用的事件循环运行在应用线程中：`view_dispatcher_run` 调用 `furi_event_loop_run`：https://github.com/flipperdevices/flipperzero-firmware/blob/9981d9d4aa5ec7b14bb46cf15763dd293a7dec6a/applications/services/gui/view_dispatcher.c#L110-L123
- 输入路径：GUI 线程 `gui_input` 调用 `view_port_input`（https://github.com/flipperdevices/flipperzero-firmware/blob/9981d9d4aa5ec7b14bb46cf15763dd293a7dec6a/applications/services/gui/gui.c#L564 、#L315 ），再由 `view_dispatcher_input_callback` 以 `FuriWaitForever` 投递到深度 16 的输入队列（https://github.com/flipperdevices/flipperzero-firmware/blob/9981d9d4aa5ec7b14bb46cf15763dd293a7dec6a/applications/services/gui/view_dispatcher.c#L250-L254 ，队列深度 #L5 ）。应用线程阻塞且队列已满时，GUI 线程会在该 put 上阻塞（推断，未实测）。
- 应用阻塞的 I/O 只占用应用线程；不直接占用 gui_srv 线程或 gui 互斥量（推断）。

**Q2 存储接口**
- RPC：每个 API 调用构造 `StorageMessage`（含 `FuriApiLock`），以 `furi_message_queue_put(..., FuriWaitForever)` 投递后在锁上等待：https://github.com/flipperdevices/flipperzero-firmware/blob/9981d9d4aa5ec7b14bb46cf15763dd293a7dec6a/applications/services/storage/storage_external_api.c#L23-L27 ；读示例：#L145-L163 、#L186-L202 ；命令枚举：https://github.com/flipperdevices/flipperzero-firmware/blob/9981d9d4aa5ec7b14bb46cf15763dd293a7dec6a/applications/services/storage/storage_message.h#L127-L154 ，消息结构 #L156-L161
- 锁原语：`FuriApiLock` 即 `FuriEventFlag`，每次调用分配一个：https://github.com/flipperdevices/flipperzero-firmware/blob/9981d9d4aa5ec7b14bb46cf15763dd293a7dec6a/lib/toolbox/api_lock.h#L28-L43
- 服务线程：`storage_srv` 为单线程，消息队列深度 8：https://github.com/flipperdevices/flipperzero-firmware/blob/9981d9d4aa5ec7b14bb46cf15763dd293a7dec6a/applications/services/storage/storage.c#L36 ，主循环 #L100-L115 ；派发在 https://github.com/flipperdevices/flipperzero-firmware/blob/9981d9d4aa5ec7b14bb46cf15763dd293a7dec6a/applications/services/storage/storage_processing.c#L572-L746 ，完成时 `api_lock_unlock`（#L746）。
- 新增应用：应用只调用公共 API；新增文件操作需在命令枚举与派发中加分支（推断，依据上述两处结构）。

**Q3 并发控制**
- 无卷级或文件级锁。所有 FS 操作由存储线程串行执行；同一路径已打开则返回 `FSE_ALREADY_OPEN`：https://github.com/flipperdevices/flipperzero-firmware/blob/9981d9d4aa5ec7b14bb46cf15763dd293a7dec6a/applications/services/storage/storage_processing.c#L104-L117 ；判断函数：https://github.com/flipperdevices/flipperzero-firmware/blob/9981d9d4aa5ec7b14bb46cf15763dd293a7dec6a/applications/services/storage/storage_glue.c#L99-L111
- FatFs 非重入：`_FS_REENTRANT 0`（https://github.com/flipperdevices/flipperzero-firmware/blob/9981d9d4aa5ec7b14bb46cf15763dd293a7dec6a/targets/f7/fatfs/ffconf.h#L243-L244 ），`_FS_LOCK 0`（#L232）。仓库内仅 `storage_ext.c` 调用 `f_*` 函数（grep 结果）。
- 等待 DMA 的线程：SD 读扇区经 `furi_hal_sd_read_blocks` → `sd_device_read`（获取 SPI 总线并轮询卡忙）：https://github.com/flipperdevices/flipperzero-firmware/blob/9981d9d4aa5ec7b14bb46cf15763dd293a7dec6a/targets/f7/furi_hal/furi_hal_sd.c#L840-L860 、#L974-L985 。DMA 传输由 `furi_hal_spi_bus_trx_dma` 完成（https://github.com/flipperdevices/flipperzero-firmware/blob/9981d9d4aa5ec7b14bb46cf15763dd293a7dec6a/targets/f7/furi_hal/furi_hal_spi.c#L194-L212 ），在 `spi_dma_completed` 信号量上带超时等待（#L258-L272 ）。调用者即存储线程（推断）。
- 扇区缓存：FatFs 层 8 个扇区（https://github.com/flipperdevices/flipperzero-firmware/blob/9981d9d4aa5ec7b14bb46cf15763dd293a7dec6a/targets/f7/fatfs/sector_cache.c#L10 ），从 memmgr 池分配（https://github.com/flipperdevices/flipperzero-firmware/blob/9981d9d4aa5ec7b14bb46cf15763dd293a7dec6a/targets/f7/fatfs/sector_cache.c#L20-L23 ）。

**Q4 实时流与其他访问共存**
- 固件中未找到音频流式通路（FAP 未核实）。存储 API 为同步 RPC，无预读缓冲（由 API 形态推出）。
- 所有应用的文件操作在存储服务的同一个 FIFO 队列上排队（推断，依据 Q2 的队列结构）。

**Q5 元数据与缓存**
- 固件中未见全局元数据库或曲库；应用读取目录的方式未核实。
- 小数据结构用 `saved_struct` 整体写入（见 Q8）。

**Q6 热插拔**
- 卸载在存储线程执行：`sd_unmount_card` 将状态置为 NotReady 并调用 `f_mount(0, ...)`；源码注释为 "TODO FL-3522: do i need to close the files?"：https://github.com/flipperdevices/flipperzero-firmware/blob/9981d9d4aa5ec7b14bb46cf15763dd293a7dec6a/applications/services/storage/storages/storage_ext.c#L157-L169
- 检测：`storage_ext_tick_internal` 轮询 `furi_hal_sd_is_present`：https://github.com/flipperdevices/flipperzero-firmware/blob/9981d9d4aa5ec7b14bb46cf15763dd293a7dec6a/applications/services/storage/storages/storage_ext.c#L317-L345 ；`storage_tick` 只在 1000 ms 内没有消息时执行（https://github.com/flipperdevices/flipperzero-firmware/blob/9981d9d4aa5ec7b14bb46cf15763dd293a7dec6a/applications/services/storage/storage.c#L100-L115 ，周期定义 #L9 ）。持续有消息时 tick 不执行（推断）。
- 卸载与挂载事件经 pubsub 发布：https://github.com/flipperdevices/flipperzero-firmware/blob/9981d9d4aa5ec7b14bb46cf15763dd293a7dec6a/applications/services/storage/storage.c#L60-L96
- 残留的文件对象在 FatFs 中的行为与 `storage->files` 列表的清理：未核实。

**Q7 内存预算**
- 应用栈大小由服务清单指定：存储 3 KiB（https://github.com/flipperdevices/flipperzero-firmware/blob/9981d9d4aa5ec7b14bb46cf15763dd293a7dec6a/applications/services/storage/application.fam#L9 ），GUI 2 KiB（https://github.com/flipperdevices/flipperzero-firmware/blob/9981d9d4aa5ec7b14bb46cf15763dd293a7dec6a/applications/services/gui/application.fam#L11 ）。
- 应用退出：`loader_do_app_closed` 等待线程结束、释放参数、退出 insomnia、释放 FAP，并打印剩余堆：https://github.com/flipperdevices/flipperzero-firmware/blob/9981d9d4aa5ec7b14bb46cf15763dd293a7dec6a/applications/services/loader/loader.c#L751-L777
- 未见跨应用共享的流式缓冲；应用堆的隔离程度未核实。

**Q8 写入与持久化**
- `saved_struct_save` 以 `FSOM_CREATE_ALWAYS` 直接覆盖写（头加数据），同步返回：https://github.com/flipperdevices/flipperzero-firmware/blob/9981d9d4aa5ec7b14bb46cf15763dd293a7dec6a/lib/toolbox/saved_struct.c#L16-L68 （打开 #L33，写入 #L54）。
- Dolphin 统计：`dirty` 标记，30 秒 flush 定时器，设置变更时立即保存：https://github.com/flipperdevices/flipperzero-firmware/blob/9981d9d4aa5ec7b14bb46cf15763dd293a7dec6a/applications/services/dolphin/dolphin.c#L16 、#L118-L123 、#L230 、#L259-L260 ；保存函数：https://github.com/flipperdevices/flipperzero-firmware/blob/9981d9d4aa5ec7b14bb46cf15763dd293a7dec6a/applications/services/dolphin/helpers/dolphin_state.c#L28-L46
- 桌面设置：同步调用 `saved_struct_save`：https://github.com/flipperdevices/flipperzero-firmware/blob/9981d9d4aa5ec7b14bb46cf15763dd293a7dec6a/applications/services/desktop/desktop_settings.c#L66-L79

**Q9 演化（提交与说明）**
- 2024-08-04 `4d985ba8f843a2a67be9579dcbe39de73f975b4a`（Storage: remove LFS (#3577)，首行 "Storage: drop internal storage"）：https://github.com/flipperdevices/flipperzero-firmware/commit/4d985ba8f843a2a67be9579dcbe39de73f975b4a
- 2024-06-10 `6d8b050eda8d1e782f553880da66f4135f01a685`（[FL-3833] Furi: event loop (#3675)）：引入事件循环，GUI 的派发改用 `furi_event_loop`：https://github.com/flipperdevices/flipperzero-firmware/commit/6d8b050eda8d1e782f553880da66f4135f01a685
- 2024-09-07 `8c2223df5dfc9c5b1119adee634e53610ca504d1`（Threading, Timers improvements (#3865)）：区分 FuriThread 与 FuriThreadId 的使用：https://github.com/flipperdevices/flipperzero-firmware/commit/8c2223df5dfc9c5b1119adee634e53610ca504d1
- 2023-02-08 `e3d473bf429bd254b53550ebcbbb0d389b49240c`（[FL-2435] SD over SPI improvements (#2204)）：说明含 "SD-card: use DMA for big blocks of SPI data" 与 "Storage: faster api lock primitive"：https://github.com/flipperdevices/flipperzero-firmware/commit/e3d473bf429bd254b53550ebcbbb0d389b49240c

### 2.3 PebbleOS（中等深度）

**Q1 应用与线程模型**
- 执行模型（官方文档）：KernelMain 运行输入、UI 与多数服务；App 与 Worker 各为内核线程；"Only one app and one worker run at a time"：https://github.com/coredevices/PebbleOS/blob/3b9284a698ec3a1ff518be537d8859f0ed6bba33/docs/architecture/index.md#L64-L84 （任务表 #L66-L76，单前台约束 #L79-L80，第三方 app 降权 #L82-L84）
- App 线程创建时为特权态（`.privileged = true`）：https://github.com/coredevices/PebbleOS/blob/3b9284a698ec3a1ff518be537d8859f0ed6bba33/fw/process_management/app_manager.c#L388-L405 ；标记为 `is_unprivileged` 的 app 在 `prv_app_task_main` 中进入非特权态（文档称第三方 app 与 worker 以非特权态运行）：https://github.com/coredevices/PebbleOS/blob/3b9284a698ec3a1ff518be537d8859f0ed6bba33/fw/process_management/app_manager.c#L136-L152
- 同时单前台：`s_app_task_context` 为单例（https://github.com/coredevices/PebbleOS/blob/3b9284a698ec3a1ff518be537d8859f0ed6bba33/fw/process_management/app_manager.c#L96 ）；启动新 app 时 `prv_app_switch` 先确保旧 app 可安全结束，再清理，然后启动新 app：https://github.com/coredevices/PebbleOS/blob/3b9284a698ec3a1ff518be537d8859f0ed6bba33/fw/process_management/app_manager.c#L585-L632 ；入口 `app_manager_launch_new_app`：#L707-L752
- Worker：单例 `s_worker_task_context`（https://github.com/coredevices/PebbleOS/blob/3b9284a698ec3a1ff518be537d8859f0ed6bba33/fw/process_management/worker_manager.c#L41 ），优先级 `PBL_PRIO_IDLE + 1`，创建于 #L224-L238
- KernelBackground：优先级 `PBL_PRIO_IDLE + 1`（https://github.com/coredevices/PebbleOS/blob/3b9284a698ec3a1ff518be537d8859f0ed6bba33/fw/services/system_task/service.c#L27 ），创建见 #L160-L173 ；有待执行回调时临时提升到 `PBL_PRIO_IDLE + 3`（与 KernelMain 相同）：#L326-L331
- 任务表：https://github.com/coredevices/PebbleOS/blob/3b9284a698ec3a1ff518be537d8859f0ed6bba33/fw/kernel/pebble_tasks.h#L14-L32 ；内核默认优先级数量为 5：https://github.com/coredevices/PebbleOS/blob/3b9284a698ec3a1ff518be537d8859f0ed6bba33/kernel/Kconfig#L38-L40
- 内核为单级优先级继承（https://github.com/coredevices/PebbleOS/blob/3b9284a698ec3a1ff518be537d8859f0ed6bba33/docs/kernel/internals.md#L46-L49 ）；文档称内核"替换了 FreeRTOS shim"（#L3）。

**Q2 存储接口**
- 系统服务直接调用 pbl_fs，例如 persist 服务的文件复制：https://github.com/coredevices/PebbleOS/blob/3b9284a698ec3a1ff518be537d8859f0ed6bba33/fw/services/persist/service.c#L109-L153 。文件 API 为 `pbl_fs_*`，句柄同一时间只由一个线程使用（https://github.com/coredevices/PebbleOS/blob/3b9284a698ec3a1ff518be537d8859f0ed6bba33/include/pbl/fs/fs.h#L28-L31 ）。
- 第三方 app 的持久化写入经 `PBL_SYSCALL`：`persist_write_data` 对当前进程对应的 settings 文件执行 `settings_file_set`（https://github.com/coredevices/PebbleOS/blob/3b9284a698ec3a1ff518be537d8859f0ed6bba33/fw/applib/persist.c#L107-L113 ；获取存储句柄的辅助函数 #L18-L24）。系统调用的 SVC 机制见 https://github.com/coredevices/PebbleOS/blob/3b9284a698ec3a1ff518be537d8859f0ed6bba33/docs/kernel/internals.md#L81-L86 （SVC 2 仅从 syscall stub 或 `pbl_user_call()` 返回处接受）。
- `fw/syscall/` 中未见 pbl_fs 相关的 PBL_SYSCALL（grep 结果）；SDK 对 app 暴露的存储入口是 persist（SDK 文档未全部核实）。
- 新增 app 接口：需新增 `PBL_SYSCALL` 项（推断，依据 persist 的模式）。

**Q3 并发控制**
- PFS 为单一全局互斥量 `s_pfs_mutex`：https://github.com/coredevices/PebbleOS/blob/3b9284a698ec3a1ff518be537d8859f0ed6bba33/subsys/fs/pfs/pfs.c#L33 ；read 等操作在锁内执行：#L1061-L1117 ；open 的缓存查找与 E_BUSY：#L1601-L1612 、#L1752-L1782
- flash 访问为同步调用：https://github.com/coredevices/PebbleOS/blob/3b9284a698ec3a1ff518be537d8859f0ed6bba33/subsys/fs/pfs/pfs.c#L224-L229 （读）、#L254-L268 （写与擦除）。
- 创建文件前的页准备：每次只在锁内探测一页，页间释放锁，每 4 页睡眠 2 ms：https://github.com/coredevices/PebbleOS/blob/3b9284a698ec3a1ff518be537d8859f0ed6bba33/subsys/fs/pfs/pfs.c#L742-L768 ；`pfs_open` 中先释放锁再调用它：#L1667-L1669
- 同一文件只能打开一次，且最多 8 个文件同时打开：https://github.com/coredevices/PebbleOS/blob/3b9284a698ec3a1ff518be537d8859f0ed6bba33/include/pbl/fs/pfs.h#L18 ；E_BUSY 实现见 pfs.c#L1601-L1612
- persist 服务：单一互斥量 `s_mutex`：https://github.com/coredevices/PebbleOS/blob/3b9284a698ec3a1ff518be537d8859f0ed6bba33/fw/services/persist/service.c#L40-L57 ；注释说明可改为每文件锁：#L201-L204 ；store 文件打开后保持打开：#L205-L217
- DMA 完成由哪个线程等待：未核实（flash 驱动未下载）。

**Q4 实时流与其他访问共存**
- 扬声器：DMA 回调路径为 ISR → 系统任务 → `prv_audio_trans_cb`，持 `s_lock` 补充 PCM：https://github.com/coredevices/PebbleOS/blob/3b9284a698ec3a1ff518be537d8859f0ed6bba33/fw/services/speaker/speaker_service.c#L309-L331
- 调用方经 `speaker_service_stream_write_owned` 把 PCM 写入 RAM 环形缓冲：https://github.com/coredevices/PebbleOS/blob/3b9284a698ec3a1ff518be537d8859f0ed6bba33/fw/services/speaker/speaker_service.c#L806-L824 ，环形缓冲的写入：https://github.com/coredevices/PebbleOS/blob/3b9284a698ec3a1ff518be537d8859f0ed6bba33/fw/services/speaker/pcm_stream.c#L20-L35 ，缓冲由 `pbl_malloc` 分配（#L12）。
- 驱动预读约 64 ms，停止前补 80 ms 静音：https://github.com/coredevices/PebbleOS/blob/3b9284a698ec3a1ff518be537d8859f0ed6bba33/fw/services/speaker/speaker_service.c#L37-L40
- 合成器 `track_fill` 从内存中的采样或波形生成：https://github.com/coredevices/PebbleOS/blob/3b9284a698ec3a1ff518be537d8859f0ed6bba33/fw/services/speaker/track_player.c#L124-L140
- 本次读取的源码中未见从 PFS 文件流式读取音频的路径（未核实全部服务）。

**Q5 元数据与缓存**
- blob_db 为按数据库分派的键值接口：https://github.com/coredevices/PebbleOS/blob/3b9284a698ec3a1ff518be537d8859f0ed6bba33/fw/services/blob_db/api.c#L243-L262 （插入成功后发出事件）。settings blob 的同步回调有 `s_sync_callback_pending` 合并标记：https://github.com/coredevices/PebbleOS/blob/3b9284a698ec3a1ff518be537d8859f0ed6bba33/fw/services/blob_db/settings_blob_db.c#L29-L34 、#L305
- settings 的本地存储为 settings_file（PFS 上的键值文件）：https://github.com/coredevices/PebbleOS/blob/3b9284a698ec3a1ff518be537d8859f0ed6bba33/fw/services/settings/settings_file.c#L148-L151
- 媒体元数据、曲库、封面：未核实。

**Q6 热插拔**
- 不适用于可移除介质：PFS 位于外部 NOR flash（https://github.com/coredevices/PebbleOS/blob/3b9284a698ec3a1ff518be537d8859f0ed6bba33/include/pbl/fs/pfs.h#L13 ）；打开的文件被删除是致命错误（#L22）。本次未见插拔处理代码（依据头文件注释推断）。

**Q7 内存预算**
- app 与 worker 各有独立的 RAM 段，app 段大小按 SDK 类型区分：https://github.com/coredevices/PebbleOS/blob/3b9284a698ec3a1ff518be537d8859f0ed6bba33/fw/process_management/app_manager.c#L208-L235 ；段内切出栈与堆：#L305-L323 ；堆初始化：#L354-L364
- worker 使用链接器符号 `__WORKER_RAM__`：https://github.com/coredevices/PebbleOS/blob/3b9284a698ec3a1ff518be537d8859f0ed6bba33/fw/process_management/worker_manager.c#L45-L46 ，段划分 #L170-L187
- 内核堆与进程堆分离：https://github.com/coredevices/PebbleOS/blob/3b9284a698ec3a1ff518be537d8859f0ed6bba33/docs/kernel/memory.md#L34-L36 （内核堆），#L41-L44 （app 与 worker 的堆随进程消失）
- 应用切换时新 app 重新初始化其堆（#L362）；旧 app 的堆随 RAM 段被重用（推断）。扬声器 PCM 环形缓冲位于内核堆（Q4）。

**Q8 写入与持久化**
- settings_file：`settings_file_set` 同步写：https://github.com/coredevices/PebbleOS/blob/3b9284a698ec3a1ff518be537d8859f0ed6bba33/fw/services/settings/settings_file.c#L600-L601 ；重写或压缩时写新文件再替换，注释称 1 MiB 增长可能需要数秒的擦写时间：https://github.com/coredevices/PebbleOS/blob/3b9284a698ec3a1ff518be537d8859f0ed6bba33/fw/services/settings/settings_file.c#L236-L256 （注释位于 #L251）
- app 持久化：同步写入（见 Q2），在本次读取的源码中未见延迟或批量写入机制。

**Q9 演化（提交与说明）**
- 2026-09-05 `17e5626c5cc63b6dbb9f88e44cd8d783788e2a32`（kernel: flatten kernel/native into kernel/）：https://github.com/coredevices/PebbleOS/commit/17e5626c5cc63b6dbb9f88e44cd8d783788e2a32
- 2026-10-01 `748b81e3f38879d3b67be8877ac6aac50ab0925a`（treewide: move src/fw to a top-level fw directory）：https://github.com/coredevices/PebbleOS/commit/748b81e3f38879d3b67be8877ac6aac50ab0925a
- 2026-10-19 `1abcae95267d3ea58b47f114ff946237d7a98481`（kernel: move PBL_SYSCALL and the user-memory checks into the kernel）：https://github.com/coredevices/PebbleOS/commit/1abcae95267d3ea58b47f114ff946237d7a98481 ；2026-10-19 `55b2acca291daa2c0480b8919b7d9ce048060631`（kernel: give threads their user memory and syscall stack）：https://github.com/coredevices/PebbleOS/commit/55b2acca291daa2c0480b8919b7d9ce048060631
- 2026-10-22 `3b69e4b894d299c98ace176f3a5e40cdb786e905`（process_management: read what the runtime needs before dropping privileges）：https://github.com/coredevices/PebbleOS/commit/3b69e4b894d299c98ace176f3a5e40cdb786e905
- 2026-11-02 `e9cf0b59afb9a962febfeb7b25fa483843980bf6`（fs: make PFS private to the file system subsystem）：说明中 PFS "becomes an implementation detail of the subsystem's PFS backend"：https://github.com/coredevices/PebbleOS/commit/e9cf0b59afb9a962febfeb7b25fa483843980bf6
- 2026-11-02 `ac6d57d7566f99c92dcd8e847fd1ee59e82264e7`（kernel: add heap and malloc APIs）：https://github.com/coredevices/PebbleOS/commit/ac6d57d7566f99c92dcd8e847fd1ee59e82264e7

### 2.4 ESP-ADF 与 ESP-IDF（中等深度）

**ESP-ADF release/v2.x（线索路径所在）**
- Q1：每个 audio element 一个任务：https://github.com/espressif/esp-adf/blob/74e6be93f007faa411dab985a2b7613725506790/components/audio_pipeline/audio_element.c#L460-L462 ；默认任务优先级 5、栈 2 KiB、ringbuf 8 KiB：https://github.com/espressif/esp-adf/blob/74e6be93f007faa411dab985a2b7613725506790/components/audio_pipeline/include/audio_element.h#L177-L181 ；未配置时取默认值：https://github.com/espressif/esp-adf/blob/74e6be93f007faa411dab985a2b7613725506790/components/audio_pipeline/audio_element.c#L985-L988 。应用为用户代码；v2.x 核心中无 UI 线程（本仓库未核实 UI 框架）。
- Q2：`fatfs_stream` 在 element 任务中直接 `open(path, O_RDONLY)`：https://github.com/espressif/esp-adf/blob/74e6be93f007faa411dab985a2b7613725506790/components/audio_stream/fatfs_stream.c#L107-L135 ，`read()`：#L174-L186 。无存储线程，无 RPC。
- Q3：ringbuf 为一把互斥量加两个二值信号量：https://github.com/espressif/esp-adf/blob/74e6be93f007faa411dab985a2b7613725506790/components/audio_pipeline/ringbuf.c#L58-L82 ；`rb_read` 在 `can_read` 上带超时等待：#L161-L216 。FatFs 的锁见下方 ESP-IDF 条目。
- Q4：element 之间由 ringbuf 连接，读写带超时：https://github.com/espressif/esp-adf/blob/74e6be93f007faa411dab985a2b7613725506790/components/audio_pipeline/audio_element.c#L392 、#L431 ；输出超过期望水位时置 `BUFFER_REACH_LEVEL_BIT`：#L432-L433
- Q5：v2.x 管线中未见元数据组件。Q9：master 与 v2.x 不兼容（见第 0 节 README 链接）。

**ESP-ADF master（ADF v3.0）**
- Q1：播放服务的 GMF 任务经 `esp_gmf_oal_thread` 创建：https://github.com/espressif/esp-adf/blob/a8a14c8fa6cb8aefa5299f4c06fe9679278097cc/components/esp_player_service/src/core/esp_player_service.c#L19 、#L69-L72 ；延迟任务 `ps_defer_task` 等待信号量：#L93-L100 。前台数量未核实。
- Q2：服务内部直接使用文件 API。playlist 的 VFS 后端调用 `write`、`rename`、`fsync`：https://github.com/espressif/esp-adf/blob/a8a14c8fa6cb8aefa5299f4c06fe9679278097cc/components/esp_playlist/src/epl_db_storage_vfs.c#L155-L166 、#L198 、#L226-L230 ；配置后端调用 `fopen`、`fwrite`、`unlink`、`rename`：https://github.com/espressif/esp-adf/blob/a8a14c8fa6cb8aefa5299f4c06fe9679278097cc/components/esp_config_manager/src/esp_config_storage_fs.c#L67-L100
- Q3：配置管理器持互斥量执行保存：https://github.com/espressif/esp-adf/blob/a8a14c8fa6cb8aefa5299f4c06fe9679278097cc/components/esp_config_manager/src/esp_config_manager.c#L322-L330 ；事件中心非阻塞投递，队列满则丢弃并记日志：https://github.com/espressif/esp-adf/blob/a8a14c8fa6cb8aefa5299f4c06fe9679278097cc/components/adf_event_hub/adf_event_hub.c#L606-L608
- Q5：playlist 为表式数据库（三个表文件，表头与键缓存）：https://github.com/espressif/esp-adf/blob/a8a14c8fa6cb8aefa5299f4c06fe9679278097cc/components/esp_playlist/src/epl_table_db.c#L142-L165 ；临时文件以 `~` 为前缀，最多 3 个同时打开：#L51-L52 ；保存 #L472-L500 ；关闭临时文件前 fsync：#L1904-L1915 。
- Q8：配置保存为同步写，主、备两槽都写：https://github.com/espressif/esp-adf/blob/a8a14c8fa6cb8aefa5299f4c06fe9679278097cc/components/esp_config_manager/src/esp_config_manager.c#L167-L176 ；FS 后端先写 `tmp`，再 `unlink(path)`，然后 `rename(tmp, path)`：https://github.com/espressif/esp-adf/blob/a8a14c8fa6cb8aefa5299f4c06fe9679278097cc/components/esp_config_manager/src/esp_config_storage_fs.c#L81-L100 ，本段未见 fsync。
- Q9：README 声明 master 为 ADF v3.0，与 v2.x 不兼容，核心管线由 ESP-GMF 承担：https://github.com/espressif/esp-adf/blob/a8a14c8fa6cb8aefa5299f4c06fe9679278097cc/README.md#L7 、#L24-L25

**ESP-IDF（FatFs，ESP-ADF 所用文件层）**
- `FF_FS_REENTRANT 1`，超时 `FF_FS_TIMEOUT`：https://github.com/espressif/esp-idf/blob/0e6b480f858c69862ffb6001d96e9eef74599a6e/components/fatfs/src/ffconf.h#L343-L345 ；`FF_FS_LOCK` 来自 Kconfig：#L331
- 每卷一把互斥量：`ff_mutex_create` 为每卷调用 `xSemaphoreCreateMutex()`：https://github.com/espressif/esp-idf/blob/0e6b480f858c69862ffb6001d96e9eef74599a6e/components/fatfs/port/freertos/ffsystem.c#L100-L121 ；`ff_mutex_take` 以 `FF_FS_TIMEOUT` 等待：#L176-L196

### 2.5 Zephyr（简要）
- 挂载表全局互斥量，保护挂载列表及相关操作：https://github.com/zephyrproject-rtos/zephyr/blob/d1b8eec76bfd14058854245d09aa1e413e355c4a/subsys/fs/fs.c#L26-L27 、#L81-L117 ；`fs_read` 不加锁，直接调用后端：https://github.com/zephyrproject-rtos/zephyr/blob/d1b8eec76bfd14058854245d09aa1e413e355c4a/subsys/fs/fs.c#L246-L260
- LittleFS 后端每个文件系统一把 `fs->mutex`，open、close、readdir 内加锁：https://github.com/zephyrproject-rtos/zephyr/blob/d1b8eec76bfd14058854245d09aa1e413e355c4a/subsys/fs/littlefs_fs.c#L93-L100 、#L316-L321 、#L330-L338
- FAT 后端 `fatfs_read` 直接调用 `f_read`，本仓库内无锁：https://github.com/zephyrproject-rtos/zephyr/blob/d1b8eec76bfd14058854245d09aa1e413e355c4a/subsys/fs/fat_fs.c#L204-L215 ；FatFs 重入配置不在本仓库，未核实。

### 2.6 Linux（简要，概念参照）
- 三类 I/O 优先级：RT（"can starve the entire system"）、BE（默认）、IDLE（仅在磁盘空闲时服务）；支持的调度器为 bfq 与 mq-deadline：https://github.com/torvalds/linux/blob/6628e7d4c6b25c8d18f1345e3a20d21829aae7e4/Documentation/block/ioprio.rst#L11-L12 、#L17-L39
- 未阅读内核实现（按任务要求仅作概念参照）。

## 3. 未能核实的清单

- Rockbox：ATA DMA 等待的平台实现（`ata_dma_finish`）；MRSW 锁的读写优先语义，以及读路径上优先级继承的完整行为；插件是否实际创建线程；codec 线程优先级的动态调整（`codec_thread_set_priority` 在 apps/ 中无调用，生效路径未确认）；`IO_PRIORITY_*` 常量的残留使用点；缓冲在卷卸载后的完整恢复路径；插件与 UI 之外的隔离方式（是否有 MPU 等）。
- Flipper Zero：固件或 FAP 中的音频流通路；FAP 的文件访问是否同样经过 storage RPC；FatFs 对卸载后残留 FIL 的行为（TODO FL-3522 未解答）；卸载后 `storage->files` 列表是否清理；`storage_srv` 线程优先级（未显式设置，按 FuriThread 默认 Normal 推断）；GUI 阻塞风险为代码路径推断，未实测；应用堆与隔离程度。
- PebbleOS：`PBL_PRIO_IDLE` 与 `APP_TASK_PRIORITY` 的数值（定义文件未下载）；`CONFIG_APP_RAM_*_SEGMENT_SIZE` 的数值；PFS 底层 flash 驱动（`ftl_read`、`ftl_write`）是否用 DMA 及其等待方式；第三方 app 能否直接访问 pbl_fs（未见 syscall，SDK 文档未核实）；系统资源（resources/normal）的存放与加载路径；媒体元数据、曲库与封面的来源；扬声器 PCM 环形缓冲的大小常量（`PCM_STREAM_DEFAULT_SIZE_BYTES`）。
- ESP-ADF：v2.x 中 audio_thread 与 pipeline 的完整状态机；v3 中 ESP-GMF 管线（外部依赖，不在本仓库）；playlist 的写入频率及由应用还是服务驱动；SD 卡热插拔处理（esp_vfs_fat、sdmmc）未核实；v3 中 player 的前台数量。
- ESP-IDF：FatFs 卷卸载与 SD 热插拔的交互未核实。
- Zephyr：FatFs 的重入配置（外部模块）；`fs_*` 的完整线程安全语义。
- Linux：内核实现未核实（按计划仅作概念参照）。
- 提交历史：Rockbox 与 PebbleOS 仅读取了文件级提交列表与部分提交说明，未逐提交阅读 diff；Flipper 仅读取了文件级列表与部分说明。

## 4. 反复出现的模式（只陈述事实）

1. 访问形态分为两类。直接调用同步文件 API（Rockbox、PebbleOS 的 pbl_fs、ESP-ADF v2.x 的 element、ESP-ADF v3 服务内部、Zephyr、ESP-IDF FatFs）；单线程 RPC（Flipper）。PebbleOS 第三方 app 的持久化是系统调用，在调用线程内同步执行，不经独立服务线程。
2. 独立 I/O 线程的有无。Rockbox 有缓冲线程（音频预读）与存储线程（仅负责插拔事件、空闲回调）；Flipper 有存储线程（承担全部 FS 操作）；PebbleOS 与 ESP-ADF v2.x 的文件访问没有专门的 I/O 线程（ESP-ADF element 任务除外，它本身就是数据流线程）。
3. 锁的粒度各不相同：全局（Rockbox MRSW、PebbleOS PFS 与 persist、Flipper 单线程串行）；卷（ESP-IDF FatFs）；文件系统（Zephyr LittleFS）；设备（Rockbox ATA）；流（Rockbox 每流 mutex）。
4. UI 与应用的线程关系有两种：同线程（Rockbox 插件在主线程内运行）与分离（Flipper 的 gui_srv 与应用线程；PebbleOS 的 KernelMain 与 App）。ESP-ADF 核心无 UI 线程概念。
5. "同时只有一个前台应用"由代码中的单例或"已有则拒绝"实现：Rockbox（非 TSR 插件）、Flipper（loader 的应用线程指针）、PebbleOS（`s_app_task_context` 与 `s_worker_task_context`）。ESP-ADF 未限定（未核实）。
6. 音频与文件共存的两种实现：(a) 大环形缓冲加专用线程加水位（Rockbox 缓冲线程；ESP-ADF v2.x 的 ringbuf 连接各 element）；(b) 无文件流式通路，由调用方推送 PCM 到 RAM 环形缓冲（PebbleOS）。Flipper 未找到音频通路。
7. 元数据填充时机有三种：后台线程构建（Rockbox 的 tagcache 与 dircache）；加载时在播放或服务线程内解析（Rockbox 的 ID3 与封面）；以键值或表式文件保存（Flipper saved_struct、PebbleOS settings、ESP-ADF playlist 与配置）。
8. 插拔与卸载：Rockbox 与 Flipper 有显式卸载处理（Rockbox 的 FD_NONEXIST 与 ENXIO；Flipper 的 TODO 注释表明未关闭文件）；PebbleOS 的 PFS 位于内部 NOR flash，不适用；其余项目未核实。
9. 小数据写入分两种：同步直接覆盖（Flipper saved_struct、PebbleOS persist、ESP-ADF config 的双槽写）或临时文件加替换（ESP-ADF playlist 在提交点 fsync，ESP-ADF config 的 tmp 加 rename，Rockbox settings 的 tmp 加下次启动 rename）。延迟写的机制有两种：Rockbox settings 在存储空闲回调中写，Flipper dolphin 以 dirty 标记加 30 秒定时器。
10. 有明确方向调整说明的项目：Rockbox（2014 年文件系统重写，说明中写明旧代码无线程安全；2014 年移除 I/O 优先级）；PebbleOS（2026 年 9 月至 11 月的内核与隔离相关改动，文档称内核替换了 FreeRTOS shim）；ESP-ADF（master 为 v3，与 v2.x 不兼容，管线外移到 ESP-GMF）；Flipper（2024 年移除内部存储 LFS，引入事件循环）。
