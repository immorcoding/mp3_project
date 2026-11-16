# Architecture · apps

应用的接入方式、前台与后台的单步模型、GUI 与任务之间的交接。

[返回 Architecture](architecture.md)

### Rules

- **ARC-22** · provisional · 后台作业是注册的单步函数，一步最多一次耗时文件调用或约 10 ms CPU；拆不开的 CPU 活可以一步做完，但必须能中途看最新需求并放弃；共用层不提供队列和取消，作业每步先看现状；新增作业只多一行注册。 _Why:_ 通用队列要管存储、去重、排队上限和按 id 取消，固定枚举每加一个应用都改共用层；回收和扫描都只需要“再叫我一次”。 _Source:_ [ADR-0023](../adr/0023-task-split-and-app-contract.md)、[后台任务的作业模型](https://github.com/immorcoding/mp3_project/issues/196)、[应用接入约定](https://github.com/immorcoding/mp3_project/issues/197)
- **ARC-23** · provisional · 应用在 APP 里的部分放在 `APP/apps/<app>/` 一个目录，接入共用层只靠注册表一行；所有 step 一直被调用，任务不知道谁在前台，离开前台由应用改自己的需求状态；应用之间不互相包含，设置值的只读头例外；音频读取任务目前是音乐专用的读文件入口，允许包含音乐曲库的公开头，加第二个需要流式读文件的应用时改成经注册表注入；Service/gui 的内容页按应用分区，不算共用层。 _Why:_ 再加一个应用不应改共用层；任务里不存“谁在前台”，就没有 enter/leave 的时序问题。 _Source:_ [ADR-0023](../adr/0023-task-split-and-app-contract.md)、[应用接入约定](https://github.com/immorcoding/mp3_project/issues/197)
- **ARC-24** · provisional · 界面数据和最新需求经 `ui_cell` 交接，只用于 GUI 与任务之间；锁不出模块，持锁时只拷贝内存；GUI 不等待；一格一个写者；需求是状态；结果过期就会显示错内容时才比需求序号，曲名和封面要比，列表窗口不比。 _Why:_ 旧的名称槽和窗口槽靠轮流持有避免同时访问，改成随时覆盖后要防读到写了一半的数据；锁藏在模块内，“持有界面数据锁时调用文件接口”写不出来。 _Source:_ [ADR-0023](../adr/0023-task-split-and-app-contract.md)、[界面数据与需求的交接形状](https://github.com/immorcoding/mp3_project/issues/193)、[应用接入约定](https://github.com/immorcoding/mp3_project/issues/197)、[模拟器假后端与主机测试的改造](https://github.com/immorcoding/mp3_project/issues/200)
- **ARC-25** · exploring · 约 8 KiB 以内的结果进格子，更大的数据由应用持有两块缓冲，格子只传该看哪一块，GUI 换用后经需求格告知才能重写旧缓冲。 _Why:_ 持锁只拷贝内存，大数据进格子会让 GUI 和任务都等；封面是第一个要走通的例子，还没有实现。 _Source:_ [界面数据与需求的交接形状](https://github.com/immorcoding/mp3_project/issues/193)、[应用接入约定](https://github.com/immorcoding/mp3_project/issues/197)
- **ARC-26** · provisional · 前台 step 一步最多读一个文件的一行：打开（定位符或路径）、取长度、至多 4 次读取、关闭，加约 1 ms CPU，不碰 Flash 卷；前台任务记录每个应用的最长耗时，超过阈值（50 ms）记 WARN，不停机；按路径打开的回退可能超过这个阈值，只记 WARN（第 7 次板测 53–62 ms）；要不要拆成每步一次调用留给 #229。 _Why:_ 所有应用共用一个前台任务，一步太长谁也发现不了；Flash 卷只有一把锁，后台写入时前台一等，所有应用都停。 _Source:_ [应用接入约定](https://github.com/immorcoding/mp3_project/issues/197)
- **ARC-27** · provisional · 只有前台应用可以跨步持有文件，最多 2 个，离开前台下一步就关；音频读取任务一直持有 1 个；文件令牌不跨任务；`SERVICE_BUSY` 一律过约 10 ms 重试，不算文件错误。 _Why:_ SD 只有 4 个文件槽：音频 1、前台应用最多 2、短暂读取共用 1，应用再多也不多占；槽用尽是瞬时的，不能把好歌当坏文件。 _Source:_ [应用接入约定](https://github.com/immorcoding/mp3_project/issues/197)
- **ARC-28** · provisional · 应用内存全部静态，默认放 `.apps_sdram`（ASSERT 卡总量），切前台时什么都不做；音频块池和曲库保持独立段。 _Why:_ 没有分配接口，就没有东西能挪用块池；超了在编译时报错。 _Source:_ [ADR-0023](../adr/0023-task-split-and-app-contract.md)、[应用接入约定](https://github.com/immorcoding/mp3_project/issues/197)

### Proposed

- 2026-11-16 · provisional · ARC-23 补一句“应用之间不互相包含由 `scripts/check-layer-includes.ps1` 的 `app` 种类强制（`APP/apps/<应用>/` 下的文件不得包含别的应用，含 `../` 写法；`app_table.*` 直接在 apps 下，不受限）；设置值的只读头例外等设置应用那张票按实际头名加白名单”。（queued: 无人值守 drain，#216 inbox）
- 2026-11-16 · provisional · ARC-24 补两句：① “Service 不得包含 `ui_cell`”由分层检查的 `service` 种类强制（含不带 `APP/` 前缀的写法）；② 读者读不到（锁被占）而序号已变时自己重试，不能指望再次被叫醒：`ui_cell_read_if_changed()` 在锁被占着时返回假且不改 `seen`，GUI 每帧都会再来，但前台任务读需求格没有人会再叫醒它，所以前台用 `ui_cell_changed(cell, seen)`（不拿锁比序号），读不到而序号变了就让 step 返回 1 ms 重试。_Why:_ 读不到与“没变化”看起来一样，漏掉就是需求被静默丢掉。（queued: 无人值守 drain，#217、#218 inbox）
- 2026-11-16 · provisional · ARC-26 补一句：WARN 只在该应用的最长耗时被刷新且超过阈值（初值 50 ms）时记，持续偏慢不刷屏；没超的汇总成每 10 s 至多一行 INFO；耗时只记录不停机。_Why:_ 同一个偏慢的 step 每圈都记会占满消息池（DIAG-3）。（queued: 无人值守 drain，#218 inbox）
- 2026-11-16 · provisional · ARC-22 补两句：① 扫描进行中作业每步返回 0，后台任务不阻塞，在这几秒里不让给 IDLE（只让给更高优先级和同级任务）；开机扫描那一批 Monitor 的 `idle=` 读 0 是预期，`Create Task`（Boot）自删后的 TCB 与栈要等 IDLE 回收，`Heap` 行的历史最小余量可能比稳态低约 0.9 KiB；② 作业的遍历状态放 `.apps_sdram`，由 `boot_init` 整块写入（NOLOAD 不清零）。_Why:_ 否则读数异常会被当成故障；用户 2026-11-07 在 #220 决定保留“扫描中后台任务不让出”。第 7 次板测 R6.11：大目录开机扫描 Debug 4.19–4.25 s、Release 1.58 s，Flash 就绪 8.2–8.9 s。（queued: 无人值守 drain，#220 inbox 两条合并）

### Signals

- 2026-11-16 · decision · ARC-26 · 用户裁决：规则改为“一步最多读一个文件的一行”并写明上界（打开、取长度、至多 4 次读取、关闭）；按路径打开回退超过 50 ms 只记 WARN；拆成每步一次调用留给 #229。
- 2026-11-16 · decision · ARC-23 · 用户裁决：音频读取任务目前是音乐专用的读文件入口，允许包含音乐曲库的公开头；加第二个需要流式读文件的应用时改成经注册表注入。
