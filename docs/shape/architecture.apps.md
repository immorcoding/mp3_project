# Architecture · apps

应用的接入方式、前台与后台的单步模型、GUI 与任务之间的交接。

[返回 Architecture](architecture.md)

### Rules

- **ARC-22** · provisional · 后台作业是注册的单步函数，一步最多一次耗时文件调用或约 10 ms CPU；拆不开的 CPU 活可以一步做完，但必须能中途看最新需求并放弃；共用层不提供队列和取消，作业每步先看现状；新增作业只多一行注册。 _Why:_ 通用队列要管存储、去重、排队上限和按 id 取消，固定枚举每加一个应用都改共用层；回收和扫描都只需要“再叫我一次”。 _Source:_ [ADR-0023](../adr/0023-task-split-and-app-contract.md)、[后台任务的作业模型](https://github.com/immorcoding/mp3_project/issues/196)、[应用接入约定](https://github.com/immorcoding/mp3_project/issues/197)
- **ARC-23** · provisional · 应用是 `APP/apps/<app>/` 下的一个目录，接入共用层只靠注册表一行；所有 step 一直被调用，任务不知道谁在前台，离开前台由应用改自己的需求状态；前台 step 一步最多一次文件调用加约 1 ms CPU，不碰 Flash 卷，耗时由前台任务记录；应用之间不互相包含，设置值的只读头例外；应用内存全部静态，放 `.apps_sdram`，音频块池和曲库保持独立段；只有正在显示的应用可以跨步持有文件，最多 2 个；`SERVICE_BUSY` 一律重试。Service/gui 的内容页按应用分区，不算共用层。 _Why:_ 所有应用共用一个前台任务，一步太长谁也发现不了；再加一个应用不应改共用层。 _Source:_ [ADR-0023](../adr/0023-task-split-and-app-contract.md)、[应用接入约定](https://github.com/immorcoding/mp3_project/issues/197)
- **ARC-24** · provisional · 界面数据和最新需求经 `ui_cell` 交接，只用于 GUI 与任务之间；锁不出模块，持锁时只拷贝内存；GUI 不等待；一格一个写者；需求是状态；结果过期就会显示错内容时才比需求序号，曲名和封面要比，列表窗口不比；约 8 KiB 以内进格子，更大的数据只传缓冲选择（这一点 exploring）；前台 step 的步长见 ARC-23。 _Why:_ 旧的三态靠轮流持有避免同时访问，改成随时覆盖后要防读到写了一半的数据；锁藏在模块内，“持有界面数据锁时调用文件接口”写不出来。 _Source:_ [ADR-0023](../adr/0023-task-split-and-app-contract.md)、[界面数据与需求的交接形状](https://github.com/immorcoding/mp3_project/issues/193)、[应用接入约定](https://github.com/immorcoding/mp3_project/issues/197)、[模拟器假后端与主机测试的改造](https://github.com/immorcoding/mp3_project/issues/200)
