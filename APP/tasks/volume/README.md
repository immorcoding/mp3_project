# 卷任务与 Flash 启动任务

卷任务（Volume Task）接管 SD 的热插拔和挂载：消抖、关闸、对账、卸载与挂载、发布卷事件。用户任何时候拔卡都马上停止，读取中拔卡不再被吞，换卡、轻触卡槽、快速拔插都停在正确状态。Flash 的启动（基准、挂载、无文件系统时格式化）放在一次性的 Flash 启动任务里，所以 `MountFlash`（实测 3.3 至 8.3 s）期间插拔卡照常响应。依据 [ADR-0024](../../../docs/adr/0024-file-interface-concurrency.md) 的“卷生命周期”一节和 [ADR-0023](../../../docs/adr/0023-task-split-and-app-contract.md)；规则 STOR-8、STOR-9、ARC-21；决定来源 [#195](https://github.com/immorcoding/mp3_project/issues/195)、[#189](https://github.com/immorcoding/mp3_project/issues/189)。

卷任务只管“卷在不在”，不碰具体的文件读写，也不碰曲库、音频流、界面数据：这些是订阅者的事（目前是音频读取任务、前台任务，见 [音频读取任务](../audio_reader/README.md)，和 Storage Task，见 [存储任务](../storage/README.md)）。加应用时卷任务和卷事件都不改。

## 目录

| 路径 | 内容 | 主机测试 |
|---|---|---|
| `volume_task.c/.h` | 任务入口：创建 Flash 启动任务、启动 SD，然后“等通知带超时 → 调一次 `volume_sd_step()`”的循环；`Volume_NotifyIndexTypeDef` | 否（`for(;;)` 没法测，只留创建和等待） |
| `volume_task_config.h` | 通知标志位、静默 30 ms、订阅者上限、假抖动钩子的开关与周期 | — |
| `volume_event.c/.h` | 卷事件：每卷一个状态字，订阅，发布（先更新再叫醒） | 是 |
| `volume_reconcile.c/.h` | 纯 C：对账表（5 行加刷新失败）、路径名、静默时间的剩余计算 | 是 |
| `sd/volume_sd.c/.h` | SD 部分：启动、边沿、关闸、静默、刷新与对账、卸载与挂载、发布、卷路径日志、假抖动钩子、卡检测回调（ISR） | 是（假 Filesystem Service） |
| `flash/volume_flash.c/.h` | Flash 启动流程：初始化、可选基准、挂载、格式化、发卷就绪 | 是（假 Filesystem Service） |
| `flash/volume_flash_task.c` | 一次性任务的静态创建、栈水位、自删 | 否（只含任务 API，上板核对） |
| `benchmark/` | 从 `APP/tasks/storage/benchmark/` 搬来的三个基准，见 [benchmark/README.md](benchmark/README.md) | 其中文件基准在 `Tests/flash_ftl` |

## 公开 Interface

- `volume_status(volume)`（`volume_event.h`）：返回 `{Mounted, Sequence}`，读一个 32 位字，**不拿锁**，任何任务可调用。它只回答“卷变过没有”，不用来判断现在能不能调文件接口（调用者看文件接口的返回值，`NO_MEDIA`、`NOT_READY`、`INVALID_HANDLE` 不是文件错误，先看卷序号）。取代了已在前台一次切（#218）删除的 `storage_task_sd_is_ready/mounted()`。
- `volume_subscribe(task, notify_index, flag)`：订阅者在**自己任务入口**调用一行；`VOLUME_SUBSCRIBER_MAX`（4，配置常量）个以内，完全相同的登记幂等，超出返回假。登记后**先比对一次序号**。
- `volume_publish_ready/gone(volume)`：只给发布者：SD 只有卷任务，Flash 只有 Flash 启动任务。不属于公开 Interface，订阅者不调用。
- `volume_task()`：由 `app_tasks.c` 静态创建的任务入口。

APP 内部接口，按 #202 M 节授权（“卷状态函数”）；没有改 Service 的公开接口。

## 优先级与启动

| 任务 | 优先级 | 栈 | 创建方式 |
|---|---|---|---|
| 播放 | 4（由 3 升） | 5120 word，DTCM | 静态，不变 |
| **卷** | **3** | **1024 word，AXI `.app_axi_ram`** | 静态，TCB 在 DTCM `.bss` |
| 定时器服务 | 3（`configTIMER_TASK_PRIORITY`，不改） | 128 word | 内核创建；应用没有软件定时器，它一直阻塞 |
| 前台、GUI、Log、Monitor（#218：GUI、Log、Monitor 由 1 升到 2） | 2 | 前台 1536 word（AXI），其余不变 | 见 [前台任务](../foreground/README.md) |
| **Flash 启动（一次性）** | **2** | **1024 word（约 4 KiB），AXI `.app_axi_ram`** | 静态，跑完自删 |
| Storage（#218：由 2 降到 1） | 1 | 1024 word | 动态，不变 |
| Boot、IDLE | 0 | Boot 128 word | 不变 |

`configMAX_PRIORITIES` 是 5（优先级 0 至 4），`FreeRTOSConfig.h` 一个字没改。**定时器服务任务和卷任务同为 3**：它与播放（4）的关系是播放抢占它，与卷任务同级（`configUSE_TIME_SLICING` 为 1，轮转）；因为应用没有软件定时器，它永远阻塞在队列上，不占 CPU，所以同级没有影响。引入软件定时器时回调须很短，并复核这三个优先级（本任务的消抖因此也**不用软件定时器**，用等通知带超时）。

**创建顺序**（`app_tasks.c` 的 `create_task`，即 Boot 任务）：Log Task → 可选的 SDRAM 自检（内存准备，做完才创建用到 SDRAM 的任务） → 曲库锁与音频块池、信箱（`audio_reader_boot_init()`，#215 起在 Boot 里，不再是 Storage Task 的第一件事） → Storage Task（运行到首次阻塞：登记卷事件订阅、先比对一次序号） → Audio Read Task（优先级 3，运行到第一次等待：登记订阅、先比对一次序号、看一次请求信箱） → **Volume Task** → Playback Task → Monitor → GUI。Boot 优先级最低，任何更高优先级的任务没阻塞时它不会继续，所以创建下一个任务时前面的都已经运行到首次阻塞。

**卷任务入口**（`volume_task()`）：先创建 Flash 启动任务（优先级 2，低于本任务，所以只是进入就绪；本任务一旦阻塞在 SD 初始化的 DMA 等待上，它就开始运行），**再**初始化 SD，不等 Flash。`volume_sd_start()`：`Service_Filesystem_StartSD()`（注册卡检测回调、初始化设备）→ 卡在就 `InitSD()` + `MountSD()` → **SD 基准（默认关闭）在发卷就绪之前跑**（别的任务还不会碰这个卷，独占条件成立）→ `volume_publish_ready(SD)`。无卡不发事件、序号保持 0；卡在但没就绪、挂载失败、卡上没有 FAT：不发事件，只记日志，SD 等下一次边沿再试。

**Flash 启动任务**（`volume_flash_boot()`）：`InitFlash()` → 可选物理基准 → `MountFlash()`（持 Flash 卷锁 3.3 至 8.3 s）→ 成功则可选文件基准、发 Flash 卷就绪；`SERVICE_NO_FILESYSTEM` 时按 `VOLUME_FLASH_AUTO_FORMAT`（1）格式化并挂载；**其他失败（损坏、不兼容、忙、超时）不格式化**，保留可诊断状态；挂载或格式化失败**不发事件，只记日志**。结束后任务自己打一行栈水位再 `vTaskDelete(NULL)`。`FormatAndMountFlash` 只有这条启动路径用到；`RecoverAndMountFlash` 目前 APP 没有调用者；运行中的恢复这次不加。

**Flash 启动任务和 SD 初始化的先后**：Flash 启动任务是被卷任务创建的、优先级更低，所以 SD 初始化（约 186 ms 的卡初始化加挂载）先占着 CPU 和 SD 卷锁，Flash 启动任务在卷任务第一次阻塞（等 DMA 完成信号量）时才开始 `InitFlash`。两者用两把不同的卷锁，互不等待：SD 在 Flash 的 3.3 至 8.3 s 里照常对账（R3.5）。

**Flash 卷事件的订阅者**：Storage Task 是 Flash 回收的调用者，回收要拿 Flash 卷锁，而 `MountFlash` 在锁内持有 3.3 至 8.3 s，所以 Storage Task 只在 Flash 卷已挂载（卷状态字的挂载位）时才回收，否则它会陪 `MountFlash` 等完整段，音频流、曲库扫描都被它拖住。

## 消抖：等通知带超时，不用软件定时器

- 卡检测 GPIO EXTI 的 ISR 回调 `volume_sd_detect_callback()` **只发一个任务通知**（`xTaskNotifyIndexedFromISR` 置 `VOLUME_NOTIFY_FLAG_SD_DETECT`，把唤醒标志交给 `portYIELD_FROM_ISR`），不格式化日志、不访问 SDMMC（ARC-10）。通知在置位时就合并，所以抖动再快也不会堆积。
- 卷任务每圈 `xTaskNotifyWaitIndexed(…, wait_ticks)`：醒来要么因为通知（边沿），要么因为超时（静默够了）。`volume_sd_step()` 返回下一圈最多能等多久：没有消抖在进行返回 `portMAX_DELAY`；消抖中返回距静默结束的剩余 tick（`volume_debounce_remaining()`，无符号差，tick 回绕也对）。
- **新边沿重新计时**：静默时间从**最后一个**边沿起算，每个新边沿把起点移到现在；闸只在一次过程的第一个边沿关一次（`PauseSD` 是标志不是计数）。连续抖动不另设上限，由播放侧 T_data（5 s）兜底。
- 第一圈等待时长为 0：启动期间（初始化约 186 ms）到达的边沿还留在通知槽里，不会丢。

## 收尾顺序（卷任务不等订阅者）

1. 检测边沿：关闸（`PauseSD`），记第一个边沿的时刻。文件和目录调用从这一刻起在 Filesystem 入口、**拿锁之前**等闸。
2. 静默 30 ms。
3. `RefreshSD`：在 **SD 卷锁内**刷新。**拿锁就是在等在途调用退出**（真拔卡时它 +1 ms 左右失败返回，持锁 2.0 至 4.2 ms；故障路径最长是 500 ms 超时加 100 ms 有上界的 Abort）。拿到锁后读卡检测脚、`Platform_SD_Process`、必要时重新初始化（约 186 ms）。
4. `volume_reconcile()` 按下表定路径：

   | 卷 | 卡 | 路径（日志 `path=`） | 动作 |
   |---|---|---|---|
   | 挂着 | 不在，或设备没就绪（含刷新失败） | `unmount` | 卸载（令牌全部失效、槽释放）→ 发卷消失 |
   | 挂着 | 在，但这次刷新重新初始化过（换卡或被吞的拔出） | `remount` | 卸载 → 发卷消失 → 挂载 → 发卷就绪 |
   | 挂着 | 在，没变化（假抖动） | `gate_only` | 无，只开闸 |
   | 没挂 | 在且就绪 | `mount` | 挂载 → 发卷就绪 |
   | 没挂 | 不在 | `idle` | 无 |

   刷新失败时卡状态不可信，**卷挂着一律卸载，卷没挂一律无动作**（不会因为结构里写着“就绪”就去挂载）。**取舍（#195 “只开闸”）**：暂停状态下 30 ms 内快速抽插，设备仍是 READY、没有重新初始化，对账走 `gate_only`；卡如果真的掉过电，要到下一次读才报错，需要再拔一次才恢复。这条先不改，R3.3 加了一步记录实际行为。挂载失败**不发卷就绪**，只记日志，等下一次边沿再试；卸载返回错误时 Service 也已使句柄失效，照常发卷消失。
5. 更新卷状态字、序号加 1、**逐个叫醒订阅者**（先更新，再叫醒）。
6. **开闸**（`ResumeSD`），等在闸前的调用被放行：卡还在的返回正常结果，卡已拔的得到 `NOT_READY` 或 `INVALID_HANDLE`。

**为什么不等订阅者**：卸载已经让旧令牌全部失效，订阅者晚点收尾也碰不到旧卡；等回话要多一套确认机制，还会被优先级低的订阅者拖住（后台任务最终是 1）；一个订阅者卡住不该让整个卷无法恢复。代价：开闸时订阅者可能还没处理完事件，它们在自己的任务里比对序号，调用先拿到失败返回再看序号。

**对账期间又来了新边沿**：每次对账结束，卷任务非阻塞地看一眼通知槽；有检测位就**不开闸**，从现在起重新消抖（日志 `gate=kept`），直到一次对账期间没有新边沿才开闸。刷新、卸载、挂载任何一步里来的边沿都一样，所以闸不会在对账中途被放开。

**每次 `PauseSD` 都配 `ResumeSD`**（Service 的闸没有超时放行，也没有看门狗，漏配会让所有 SD 调用者一直等）：不变量是“`volume_sd_step()` 返回 `portMAX_DELAY` 时闸一定是开的”；消抖中返回的一定是有限等待，最终必然再走到对账之后的开闸。主机测试 `test_every_path_pairs_pause_with_resume` 穷举 384 种组合（卷挂没挂 × 卡的四种现状 × 刷新成败 × 挂载三种结果 × 卸载成败 × 在刷新/卸载/挂载处来不来新边沿）逐条核对：闸收尾时开着，`PauseSD`、`ResumeSD` 各一次，卷状态字与假 Service 眼里的挂载状态一致，卷序号的变化与上表一致，订阅者被叫醒时闸始终关着。

**已知窗口**（#210 的 `RefreshSD` 和 `UnmountSD` 是两个独立的持锁入口）：刷新放锁到卸载拿锁之间，已经通过闸、正在等锁的调用可能抢到 SD 卷锁。Storage Task（优先级 1，#218 起；原来是 2，都低于卷任务 3）和前台任务（2）挤不进来：卷任务从刷新返回到调用卸载中间不阻塞，放锁只是把等待者放进就绪表。音频读取任务（#215）升到 3 之后与卷任务同级、时间片轮转，空隙理论上可以被插进去；**#215 评估过**（[音频读取任务 README](../audio_reader/README.md)“`RefreshSD` 与 `UnmountSD` 之间的空隙”）：要在空隙里拿到锁，调用者必须过闸之后、拿锁之前被挂起超过整个静默期，实际不成立；早就排在锁上的等待者排在卷任务前面，在 `RefreshSD` 之前拿锁。即使发生，后果是读 1 块新卡上按旧文件系统解释的垃圾字节，随旧流作废，读取任务只读不写，没有数据错乱和死锁。要彻底关掉需要 Service 提供“刷新加卸载”的合并入口，超出 spec M 节的授权，没有加，选项写在音频读取任务 README 与交付汇报里。

## 卷事件

- **每卷一个字**（`volatile uint32_t`）：最低位是挂载位，高 31 位是卷序号，读写都不拿锁；**每个字只有一个写者**（SD：卷任务，Flash：Flash 启动任务），所以发布没有读-改-写的竞争。
- **先更新再叫醒**：`volume_event_publish()` 先写状态字，再对每个订阅者 `xTaskNotifyIndexed(eSetBits)`，被叫醒的任务读到的一定是新状态（主机测试用桩的通知钩子在叫醒那一刻读状态字验证）。
- **只有卷就绪和卷消失两种事件**，没有“卷将要消失”：卷任务知道时卡已经没了，令牌由卸载统一作废。每个事件序号加 1。
- **跳 2 的含义**：换卡是“卸载 → 发卷消失（序号 n+1）→ 挂载 → 发卷就绪（序号 n+2）”，挂载位前后都是真，序号跳 2。订阅者上次见到 n、现在见到 n+2：序号变了就按“**旧的全部作废，再看现状**”处理，不需要知道中间发生了几次；叫醒是两次，但通知位合并，订阅者醒一次就看到最终状态。只留最新状态不会丢掉中间一次，因为序号告诉它“变过”。
- **订阅者晚登记怎么不漏**：登记（`volume_subscribe`）之前发布的事件不会补叫醒，但状态字已经是最新的；订阅者在入口**登记后先比对一次序号**（Storage Task 的 `storage_task_apply_sd_volume()`）。发布与登记的任何交错都安全：事件在登记前，比对就看到新序号；事件在登记后，通知会到；两者之间的窗口里既有通知又有新序号，只是多醒一次。
- **边沿时刻（#215，诊断）**：卷任务在每次对账的第一个卡检测边沿调 `volume_event_note_edge(VOLUME_ID_SD, tick)`，之后这次对账里的每个卷事件发布时先把它抄进“已发布的边沿”、再写状态字、再叫醒；订阅者用 `volume_event_published_edge()` 读，读状态字、读边沿、再读一次状态字，两次状态字不同（读的时候又发布了新事件）或没有边沿（开机挂载）就返回假。音频读取任务写 `CARD_REMOVED` 之后用它算 `CARD_REMOVED lat=<ms>ms` 一行（相对第一个边沿，1 ms 分辨率）。换卡的卸载与挂载两个事件共用同一个边沿。APP 内部接口，不是 Service 接口。
- **订阅表**：登记在临界区里先填表项再加计数，发布者读到计数时表项一定写完；只增不减，上限 `VOLUME_SUBSCRIBER_MAX`。订阅者最终是三个（音频读取、前台、后台），Storage Task 现在占一个，#218 后前台任务也登记了（Storage、读取、前台 3 个，后台再加 1 个正好 4）。

## 订阅者与正在进行的文件调用如何交错

下面以音频读取任务为例（#215 起读块在它那里；Storage Task 的扫描调用和前台任务的标签读取调用的交错与此相同）：它每圈**先比对 SD 卷序号**，再处理请求、读块。事件到达时它可能正在一次文件调用里：

- 调用在**闸前**等（消抖中）：闸开后返回，卡还在就返回正常结果（慢约 30 ms），卡已拔返回 `NOT_READY`（按路径/卷）或 `INVALID_HANDLE`（按句柄）。
- 调用已经**持锁在途**（读 32 KiB，正常 3 至 7 ms）：卷任务的 `RefreshSD` 等它放锁；真拔卡时它很快失败返回 `NO_MEDIA`（Service 在锁内看到卡检测脚显示卡不在，把 `ERROR` 改成 `NO_MEDIA`）。
- 调用返回 `NO_MEDIA`、`NOT_READY`、`INVALID_HANDLE`：**卷变了时不是文件错误**——读块得到 `NO_MEDIA`、`NOT_READY`、`INVALID_HANDLE`：先对同一个句柄 `CloseFile`（结果不看；它在 Filesystem 入口等闸，卡刚拔、边沿已到而 30 ms 消抖或对账还没结束时会一直等到本轮对账结束才返回，闸开着时立刻返回），再看 SD 卷：**序号和读取任务已处理的不同，或卷已不挂**，是卡没了，不写 `READ_ERROR`、不对这个句柄重试，FIL 已关、流停住（不空转），等卷事件写 `CARD_REMOVED`；**序号没变且卷仍挂着**，是卡还在、设备出了错（例如标签读取传输出错使设备进 ERROR，FatFs 的下一次读得到 `NOT_READY`），没有边沿就没有卷事件，立刻按读错误报，不等 T_data。流停住之后回到主循环顶部比对序号，序号变了就写 `CARD_REMOVED`（已读完、仍被播放任务持有的流也算）、作废曲库、收掉两个槽（含已经 READY 但 GUI 还没取走的旧窗口和旧名字，换卡后不会贴出旧卡的行）、卷挂着就重新扫描。**这就是用户可见的唯一变化：在途读取被拔卡打断时显示 “No SD card”，游标重置**（原来是读取错误，游标保留）。判断“卷变没变”的纯函数是 `audio_reader_classify_read()`（`Tests/audio_reader`）。
- 其他错误（`ERROR` 且卡检测脚显示卡还在，`BUSY`，`TIMEOUT`…）表示卡还在，立刻按原来的方式报错。已知漏洞（推断，板上没测到）：数据脚比检测脚先断时，第一次失败仍是 `ERROR`，显示读取错误，同样停播。
- 曲库扫描进行中卷消失：打开/读/关目录得到卷不可用，丢弃本次扫描，不发布（日志 `Scan done res=aborted`）。打开目录的第一次失败发生在卷任务对账之前，所以**再开一次**（它在 Filesystem 入口等闸，等到本轮对账结束）才下结论；读目录失败后紧跟的 `CloseDirectory` 同样在闸前等。扫描结束后 Storage 看 `catalog_scan_verdict()`：卷变了（卡没了或换了）不置就绪，留给下一圈的卷事件重扫，空库不会因此“就绪”约 30 ms；**卷没变**（卡还在、读不了，例如设备进了 ERROR）则没有卷事件可等，记一条 ERROR `Catalog scan aborted but the card is still there; reinsert the card.`，卷挂着就绪、曲库为空（与原来扫描失败的做法一致），拔插卡（边沿）后重新对账、重扫。其余失败仍按 STOR-7 处理。

## 开机曲库比以前提前出现

以前 Storage Task 先挂 Flash（`MountFlash` 3.3 至 8.3 s）才初始化 SD、扫描曲库；现在 Flash 在 Flash 启动任务里并行，SD 的挂载和曲库扫描开机后约 0.3 至 0.6 s 就开始。**插着卡开机时，曲库（`Scan done`）比以前早 3 至 8 s 出现**，点播放也更早能响。这是异步 Flash 的必然结果，不是 bug；上板时确认（`Scan done` 的 tick，和 R6.1 的扫描耗时口径一致）。

## 优先级反转在卷锁上的表现

卷任务（3）要 SD 卷锁做刷新、卸载、挂载；锁可能被 Storage Task（1，#218 起；扫描时）、前台任务（2，读标签）或音频读取任务（3）持有。音频读取任务与卷任务同级，没有优先级继承可言，卷任务等它那一次调用做完（正常 3 至 7 ms，拔卡 +1 ms 失败返回，故障路径约 0.6 s），放锁后读取任务的下一次调用在入口等闸。Storage Task（2）持锁时：

- 互斥锁带**优先级继承**：卷任务等锁时，持锁的 Storage Task 或前台任务临时升到 3，所以优先级 1、2 和 3 之间没有能抢走它的任务（Flash 启动任务也是 2，但它只持 Flash 卷锁）。反转的上界就是那一次调用的持锁时间：正常读 3 至 7 ms，拔卡时 +1 ms 失败返回，故障路径（卡不响应）500 ms 等待加 100 ms Abort，约 0.6 s。
- 播放任务（4）**不碰文件、不拿任何文件锁**（调用者拒绝名单，入口断言停机），所以它不会被卷锁拖住；这也是播放升到 4 的原因，卷任务（3）做对账时播放仍能抢占。
- **Flash 启动任务持 Flash 卷锁 3.3 至 8.3 s 时插拔卡不受影响**（R3.5）：卷任务从不拿 Flash 卷锁（Flash 不归它挂载，也没有拔插）；SD 与 Flash 各一把卷锁，互不影响。唯一可能陪 `MountFlash` 等锁的是 Storage Task 的回收，已按“Flash 卷已挂载才回收”挡住。文件和目录入口在拿锁之前先看一次“卷没挂”，没挂立刻返回 `NOT_READY`，不陪等。
- 闸上的等待是睡眠，不占 CPU，也不持锁；持锁等闸被断言禁止（卷任务要这把锁才能开闸）。

## 卷路径日志（DIAG-6，常开）

串口里每行是 `<时间> <I|W|E> (<tick>) <标签>: <正文>`，**标签后面有冒号**。卷路径日志的标签是 `VOLUME`；沿用的人读日志标签是 `SD`、`FLASH`；钩子行标签是 `HOOK`。下面只写正文：

| 正文 | 等级 | 含义 |
|---|---|---|
| `sd edge src=<irq\|hook>` | I | 一次消抖过程的第一个边沿（闸已关）。之后的边沿只计数，抖动不会刷屏 |
| `sd edge during settle, gate kept` | I | 对账期间又来了一个边沿，闸保持关闭、重新消抖 |
| `sd settle path=<路径> refresh=<ok\|fail> card=<pri> mount=<ok\|fail\|-> seq=<前>-><后> edges=<n> ref=<ms>ms end=<ms>ms gate=<open\|kept>` | I | 对账结果，见下 |
| `sd boot path=<路径> card=<pri> mount=<ok\|fail\|-> seq=<前>-><后>` | I | 开机时的启动结果（路径只会是 `mount` 或 `idle`） |
| `flash boot start` | I | Flash 启动任务开始；`MountFlash` 持锁窗口从这一行起 |
| `flash ready path=<mount\|format> seq=<前>-><后> ms=<n>` | I | Flash 发了卷就绪，`ms` 是从开始到现在（含 `MountFlash`） |
| `flash boot failed path=<init\|mount\|format> ms=<n>` | E | Flash 启动失败，不发事件 |
| `Flash Boot Task: total=4096 B, min free=… B, peak=…%` | I | Flash 启动任务自删前自打的栈水位（与 Monitor 的栈行同格式） |

`path` 取 `idle`、`gate_only`、`mount`、`unmount`、`remount`（含义见上表）。`card` 三位依次是 present、ready、reinit，成立写 `p`、`r`、`i`，不成立写 `-`（例：`pri` 是卡在、就绪、重新初始化过；`---` 是卡不在）。`seq=a->b` 是这次对账前后的卷序号，**`remount` 是 `a->a+2`**。`ref` 是 `RefreshSD` 花的时间（等在途调用退出，加可能的重新初始化约 186 ms）。**`end` 是从第一个边沿到闸重新打开的时间，即收尾时刻**（spike 参考 +34 至 35 ms：30 ms 静默加几毫秒）。`edges`、`ref`、`end` 超过 9999 都写 9999。`gate=kept` 表示这一轮没有开闸，之后会有下一条 `settle` 行。

保留的人读日志（`SD` 标签）：`Card ready: … MB`、`Card inserted: … MB`、`Filesystem mounted.`、`No FAT filesystem found.`、`Filesystem mount failed.`、`Card removed.`、`No card inserted.`、`Hotplug refresh failed: device=…, port=…`。Storage Task 在读取被拔卡打断时记一行 `Audio: Audio read interrupted: volume unavailable, waiting for volume event.`——看到它说明拔卡落进了在途读取（R3.1）。

### 怎么从日志判断 R3.1 至 R3.5

- **R3.1 读取中拔卡**：播放中拔卡。通过：`VOLUME: sd edge src=irq` → `VOLUME: sd settle path=unmount …`（`seq=a->a+1`）→ `SD: Card removed.` → 播放侧 `Stop reason=CARD_REMOVED`，屏幕 “No SD card”、Queue 清空、游标重置。记录 `end=`（参考 +34 至 35 ms）。如果拔卡正好落在在途读取里，还会有 `Audio: Audio read interrupted: …` 一行（先于 settle）；落在两次读之间则没有，这两种都是对的，但至少做 10 次里要见到一次 `Audio read interrupted`，才证明“读取中拔卡不再被吞”真的被走到。
- **R3.2 只收到 INSERTED 的换卡**：快速把卡抽出再按回（约 20 ms 内，最好正在播放）。路径是 `path=remount card=pri … seq=a->a+2`，随后 `Card inserted`、`Filesystem mounted.`；播放侧 `CARD_REMOVED`（旧流作废），曲库重新扫描。
- **R3.3 消抖内快速拔插、插卡初始化中途再拔**：各做 10 次。命中的标志：同一次消抖里 `edges=` 大于 1（消抖内快速拔插）；`gate=kept` 或 `sd edge during settle`（对账或初始化期间又来边沿）。最终状态正确：卡在位时 `Mounted`（有 `Filesystem mounted.`、能播放），卡不在位时 `Card removed.` 之后没有新的 `mount`。至少命中一次算通过，一次都没命中记“部分”。
- **R3.4 假抖动**：见下一节。
- **R3.5 `MountFlash` 期间插拔卡**：上电后在 `flash boot start` 与 `flash ready` 两行之间拔插卡。通过：这期间的 `sd settle` 行照常出现、`end=` 与平时同量级（约 35 ms，不被 Flash 拖到秒级），状态正确；`flash ready` 的 `ms=` 记录 `MountFlash` 实测耗时（R6.5）。

## 假抖动测试钩子（默认关闭，DIAG-7）

- 开关：`VOLUME_HOOK_FAKE_BOUNCE`（`volume_task_config.h`，默认 `0U`），代码写成 `if (VOLUME_HOOK_FAKE_BOUNCE != 0U)`，默认构建也编译；根 `CMakeLists.txt` 的缓存变量 `MP3_HOOK_FAKE_BOUNCE`，**只允许 Debug**（Release 等打开直接 CMake 报错）。打开：`cmake --preset firmware-debug -DMP3_HOOK_FAKE_BOUNCE=1` 后构建；关掉要显式 `-DMP3_HOOK_FAKE_BOUNCE=0`（缓存变量留在构建目录里）。
- 启动日志：卷任务启动时一行 WARN，串口里是 `W (<tick>) HOOK: fake_bounce ON period=5000ms`；没有这一行就是没开。
- 做什么：卷任务每隔 `VOLUME_HOOK_FAKE_BOUNCE_PERIOD_MS`（5000）自己调用一次边沿入口，就像卡检测脚抖了一下；卡其实没动。每次是两行：`I (…) VOLUME: sd edge src=hook` 和 `I (…) VOLUME: sd settle path=gate_only refresh=ok card=pr- mount=- seq=N->N edges=1 ref=0ms end=3Xms gate=open`。
- 判定：播放 3 分钟以上，**连续 ≥ 20 次**都是 `path=gate_only`，序号不变（`seq=N->N`），没有 `Stop reason`、没有 `CARD_REMOVED`、Queue 窗口不闪、听感不断音，Monitor 的 `underrun` 增量为 0。再在钩子构建上手动轻触卡槽一两次（`src=irq`）：多数也应是 `path=gate_only`；触点真的断开过则是 `unmount` 加 `mount` 两个过程。
- 钩子构建会改变被测路径的时序（每 5 s 一次 30 ms 的闸），R3.1 的收尾时刻、R4.1 一类时序数字要在钩子关闭的构建上量。

## Boot 栈核算（SDRAM 自检搬到 Boot）

Boot 任务只有 `APP_BOOT_TASK_STACK_WORDS = 128` word（512 B）。SDRAM 自检（`STORAGE_SDRAM_BENCHMARK_ENABLE`，默认 0）现在在 Boot 任务里、创建其他任务之前运行（独占 SDRAM，HWD-2）。核算（`-fstack-usage`，`create_task` 与 `storage_sdram_benchmark_run` 用工程的真实编译命令，libc 函数从 Debug ELF 反汇编数 `push`/`sub sp`）：

| 调用链上的函数 | Debug（-O0） | Release（-Os） |
|---|---|---|
| `create_task` | 48 B | 32 B |
| `storage_sdram_benchmark_run`（128 B 文本缓冲加诊断结构） | 200 B | 184 B |
| `snprintf`（`sniprintf`，含 FILE 结构） | 136 B | 136 B |
| `_svfiprintf_r` | 144 B | 144 B |
| `_printf_i` → `_printf_common` → `__ssputs_r` → `memcpy` | 48 + 32 + 32 + 8 = 120 B | 120 B |
| **合计（最深一条：`snprintf` 链）** | **648 B** | **616 B** |
| 一次任务切换最坏要在任务栈上留的 | 约 204 B（硬件压栈 26 word 加软件保存 25 word，Cortex-M7 带 FPU） | 约 204 B |
| **需要** | **约 852 B** | **约 820 B** |

另一条链 `Platform_SDRAM_RunDiagnostic`（16 加 56 加 24 加 24，约 120 B）加 `storage_sdram_benchmark_run` 200 B，约 370 B，比 `snprintf` 链浅。**结论：512 B 不够，缺约 340 B，所以打开 SDRAM 自检时 Boot 栈放不下。** 默认构建自检关闭，所以不影响产品固件和本票的板测；我没有加大 Boot 栈（那是 `app_config.h` 里的决定，不在这张票的授权里），而是在 `app_tasks.c` 加了一个编译期断言：打开 `STORAGE_SDRAM_BENCHMARK_ENABLE` 且 `APP_BOOT_TASK_STACK_WORDS < 320` 时直接编译失败，提醒先决定怎么办（比如 Boot 栈加到 320 word 以上，或把自检放进别的有足够栈的地方）。320 word（1280 B）是按上表数字给的最小建议，不是已决定的值。

## 内存与 HWD-2

- 新链接段 `.app_axi_ram`（AXI SRAM，用户在 #206 手改，NOLOAD、不清零、只经 CPU 读写、不作 DMA，所以不需要 Cache 维护）放两个任务栈：卷任务 4096 B + Flash 启动任务 4096 B，共 8 KiB（map：`0x2404b600` 起 `0x2000`）；TCB 在 DTCM `.bss`（和播放任务一样）。链接脚本一个字没改；`.storage_benchmark` 已匹配 `APP/tasks/volume/benchmark/`，基准代码不会悄悄落回 FLASH 第一片。
- **NOLOAD 不清零，HWD-2 要求首次使用前完整写入**：静态任务栈由 `xTaskCreateStatic` 在创建时整块填成已知值（`tskSET_NEW_STACKS_TO_KNOWN_VALUE`，当前配置 `configCHECK_FOR_STACK_OVERFLOW` 为 2、`configUSE_TRACE_FACILITY` 为 1，任一个就打开），所以栈首次使用前已完整写入。`app_tasks.c` 和 `volume_flash_task.c` 各有一个 `_Static_assert`：配置让这个前提失效时在编译期报错。这个段里**只有这两个栈**，没有别的状态；以后往里放任务间数据（`ui_cell` 格子等）时，由拥有者在首次使用前完整写入。
- 卷事件的状态字和订阅表放 DTCM `.bss`（约 8 + 48 B）。
- **HWD-2 对新段的审查结论**：① 启动：栈在 `xTaskCreateStatic` 里整块填成已知值（上一条）；任务在调度器启动后才创建，晚于 `Platform_Init()` 的 MPU 与 Cache 配置。② MPU 与 Cache：`Core/Src/main.c` 只配了 SDRAM 一个 MPU 区域（Region 2，0xC0000000 起 32 MB），AXI SRAM（0x24000000 起）走默认内存映射；`.app_axi_ram` 只由单核 CPU 读写栈，没有 DMA 与 Cache 一致性问题，不需要 Cache 维护，也没有为这个段新增或修改 MPU 区域。③ DMA：栈不是 DMA 源或目标，SD/QSPI 的 DMA 缓冲仍在 `.sd_dma_buffer`、`.flash_write_buffer`。

## Monitor 登记与 CPU 行

每张新任务票都要在 Monitor 的任务登记表加一行，漏了会出 `Task not registered in monitor` WARN。本票加了 `Volume Task`（1024 word）和 `Flash Boot Task`（1024 word）。Flash 启动任务只在开机后 `MountFlash` 期间存在，自删之后 Monitor 读不到它的栈，所以它自删前自己打一行水位（见日志表）。

**CPU 占比行不能超过 127 字符**（`monitor_cpu_format` 写满会打 `~`；#218 起 12 个任务同批时一位小数放不下，改整数百分比，见 [Monitor](../monitor/README.md)）：任务短键取名字第一个词的前 4 个小写字符，新增的是 `volu` 和 `flas`（#213 又加了 `back`，11 个任务时最长 120 字符，见 [后台任务](../background/README.md)），与已有的 `crea`、`log`、`stor`、`moni`、`gui`、`play`、`idle`、`tmr` 互不相同。`Tests/monitor_warn` 的 `test_every_registered_task_fits_one_cpu_line` 用全部 10 个任务核对：最坏（10 个任务各占 10.0 %）和一个任务占 100.0 % 两种情形都不超过 127 字符、没有 `~`；实际最长 111 字符。Monitor 一批的行数：稳态 8 个任务（`IDLE`、`Tmr Svc`、`Log Task`、`Storage Task`、`Volume Task`、`Playback Task`、`Monitor Task`、`GUI Task`）加 5 行固定行共 13 行；开机后 `MountFlash` 期间多一个 `Flash Boot Task`，9 个任务共 14 行。都放得进 30 条的消息池。

## 日志消息池核算（DIAG-3，汇总类日志注明节拍）

Service Log 消息池当时 30 条（#218 加大到 45 块，最新核算见 [前台任务](../foreground/README.md)“日志与消息池核算”），Log Task 每 5 ms 转交一条（约 75 ms 排空 15 行）。本票新增的日志：

- 一次消抖过程：`sd edge`（1 行）加 `sd settle`（1 行）加人读日志（`Card removed.`；或 `Card inserted`、`Filesystem mounted.`；刷新失败再加 `Hotplug refresh failed`）共 2 至 5 行，**事件驱动，不是周期的**；抖动只记第一个边沿，所以不会刷屏。读取被拔卡打断时再加 1 行 `Audio read interrupted`（每个流最多一行，之后读取停住）。
- 开机：`flash boot start`、`Filesystem mounted.`（FLASH）、`flash ready`、`Flash Boot Task: …` 栈水位共 4 行（Flash，间隔 3.3 至 8.3 s 的两头）；SD 的 `Card ready`、`Filesystem mounted.`、`sd boot` 共 3 行。
- 假抖动钩子（仅钩子构建）：**每 5 s 两行**（`sd edge src=hook` 加 `sd settle … gate_only`）。
- 最坏的同时到达（卷任务票当时的核算；#213 加后台任务之后 Monitor 稳态 14 行、开机后 15 行，最新数字见 [后台任务](../background/README.md)“消息池”）：Monitor 5 s 一批（稳态 13 行，开机后 `MountFlash` 期间 14 行），加持锁统计（Debug 每 10 s 3 行），加一次拔卡对账 5 行，最坏共 22 行，留 8 块给同一时刻的其他任务；与 #208 核算的 15 行相比多了 7 行，仍小于 30；排空约 110 ms。每行正文都不超过 127 字符：`sd settle` 行把 `edges`、`ref`、`end` 截在 9999，序号取 31 位最大值、又恰好是刷新失败加挂载失败时最长，也正好 127 字符（序号要发布 2^31 次才会到那么大，实际不到 100 字符）；主机测试用极端取值核对。

## 主机测试证明什么、不证明什么（HAR-5）

`Tests/volume`（14 个 CTest）加 `Tests/flash_ftl` 的 `sd_removal_chain`经公开接口观察：

- `volume_reconcile`：5 行表、刷新失败（卡在或不在）、路径名、静默时间含 tick 回绕。纯函数，没有单线程假设。
- `volume_event`（每个用例一个进程，#215 新增 `published_edge`：边沿时刻随它所属的事件发布，叫醒的那一刻读得到，换卡两个事件共用同一个边沿，没有边沿读不到，tick 0 合法）：序号逐次加 1、两个卷互相独立、通知落在订阅者登记的槽和标志位上、**先更新再叫醒**（桩的通知钩子在叫醒那一刻读状态字）、换卡序号跳 2 且挂载位保持、晚登记靠比对序号不漏、订阅者上限、重复登记幂等。
- `volume_sd`（默认、SD 基准、假抖动钩子三个配置各编一份）：启动（有卡、无卡、挂载失败、驱动未就绪、卡未就绪、启动失败都不发错事件），5 行表的每条路径的**调用顺序**（刷新 → 卸载 → 发消失 → 挂载 → 发就绪 → 开闸）与每一步调用那一刻的卷状态字，挂载失败不发卷就绪，抖动重新计时且闸只关一次，对账期间来的新边沿让闸保持关闭，穷举 384 种组合的“每次 `PauseSD` 都配 `ResumeSD`”，卷路径日志内容与 127 字符上限，ISR 回调只发通知，钩子默认关闭且打开时按节拍造边沿。
- `volume_flash`：挂载成功才发卷就绪；无文件系统按 AUTO_FORMAT 格式化；格式化失败、其他挂载失败、初始化失败都不发事件，且其他失败不触发格式化；只动 Flash 的字。
- `sd_removal_chain`（`Tests/flash_ftl`）：真实的 `sd_diskio.c`、BSP、DMA 执行器、Service、FatFs、Platform、Component 与 `volume_sd`，硬件一侧用替身：读时拔卡（超时中止得 `no_media`，或 HAL 先给错误事件）返回 `NO_MEDIA`、对账恰好卸载一次、旧令牌失效、再插卡恰好挂载一次；中止停不住（`stuck`）保持 ERROR，轻触卡槽后卸载，拔插恢复；静默拔出又插回走 `remount`；假抖动不动卷和已打开的文件。

**主机测试是单线程的，通过不等于并发没问题。** 不证明：真实的任务抢占与优先级继承、闸开时多个等待者按优先级怎样醒、订阅者与发布者的真实交错、卡检测 ISR 与中断优先级、边沿到对账的真实时延（R3.1 的收尾时刻）、Flash 启动任务与 SD 初始化真实重叠时的时序（R3.5）、两个新任务的栈余量（R3.6）、`MountFlash` 实际耗时、真实 Abort 与重新初始化。这些靠第 3 次上板（清单 3 的 R3.1 至 R3.6）。

## 约束

- 卷任务不做具体的文件读写，不碰曲库、音频流、界面数据，不等订阅者，不拿 Flash 卷锁。
- 持 Filesystem 的任何锁时不得等闸（断言停机）；卷任务自己的挂载、卸载、刷新不受闸挡。
- 新增订阅者只调 `volume_subscribe()` 一行加入口比对，卷任务和卷事件不改；订阅者上限是配置常量。
- 往 `.app_axi_ram` 放东西须由拥有者在首次使用前完整写入（HWD-2）。
