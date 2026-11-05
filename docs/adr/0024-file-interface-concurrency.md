# ADR-0024：文件接口的并发模型

- 状态：已接受，实施中（[0.10.0 spec](https://github.com/immorcoding/mp3_project/issues/202)）。替代 [ADR-0005](0005-sd-filesystem-task-ownership.md) 第 3 条，以及 [ADR-0010](0010-fatfs-user-diskio-service-ownership.md)、[ADR-0014](0014-filesystem-volume-aware-file-interface.md) 里“唯一 Storage Task”的部分。
- 日期：2026-11-04
- 相关 ticket：[FatFs 跨卷并发](https://github.com/immorcoding/mp3_project/issues/187)、[共享状态与完成通知](https://github.com/immorcoding/mp3_project/issues/188)、[上板验证：加锁](https://github.com/immorcoding/mp3_project/issues/191)、[文件接口的锁边界](https://github.com/immorcoding/mp3_project/issues/192)、[卷生命周期与卷事件](https://github.com/immorcoding/mp3_project/issues/195)、[上板验证：表锁](https://github.com/immorcoding/mp3_project/issues/199)
- 相关决定：[ADR-0023](0023-task-split-and-app-contract.md)、[ADR-0022](0022-fatfs-open-by-locator-patch.md)；规则 ARC-11、ARC-12、ARC-21、STOR-8、STOR-9

## 背景

ADR-0023 让多个任务直接调用文件接口。现状不允许：Service 内部没有锁，有 13 个静态可写状态；SD、Flash 执行器都把唯一任务固化成 owner；FatFs R0.12c 的 `_USE_LFN 1` 是静态缓冲，全局 `Files[]` 跨卷没有保护，`_FS_REENTRANT 1` 只保护同卷，还不能和 `_USE_LFN 1` 同时用。

拔卡也要重新设计：板测里别的任务读卡时拔卡，`SDCard_FailTransfer` 先把状态写成无卡，后面比较前后状态相同，拔出事件就被吞了。

## 决定

**1. 每卷一把锁，只在 Service 公开函数入口拿。** 一次公开调用拿一次，多步函数中途不放，句柄有效性在锁内检查。`_USE_LFN` 改 2，`_FS_REENTRANT` 保持 0。不开递归互斥量，公开函数之间不互相调用。

**2. `Files[]` 用窄表锁，不改 `ff.c`。** 只有 `f_open`、`f_opendir` 和按定位符打开在卷锁内再拿表锁。依据是读 `ff.c`：跨卷会冲突的只有 `inc_lock` 里“找空位并占用”这一步，`dec_lock`、`clear_lock` 只写本卷条目，`chk_lock`、`enq_lock` 只读。板测表锁最长持有 10.1 ms，但这不是并发证明。

**3. 槽按卷固定分配。** SD 4 个文件槽、2 个目录槽，Flash 2 个文件槽、1 个目录槽，每个槽由所属卷的锁保护，不要第三把锁。`_FS_LOCK` 改 9，静态断言不小于槽总数。

**4. 持锁上界。** 单次读写不超过 32 KiB，超过返回错误，不悄悄截短。拆不开的长操作（按路径打开大目录、删大文件）不强行设时限，靠调用者约定，上界表写进 Filesystem Service 的 README，由持锁统计核对。

**5. 调用者限制。** APP 启动时登记 GUI 和播放任务，入口比较当前任务句柄，违反就断言停机，Release 也保留。用禁用名单是为了新增任务时共用层不用改。

**6. 完成通知每卷一个信号量。** 持卷锁的任务等待，删掉 `notify_index` 参数和 owner 检查。owner 检查必须和加锁一起改，否则非 owner 时磁盘状态报 `STA_NOINIT`，可能触发 FatFs 重挂（推断）。

**7. 迟到的 DMA 完成事件用有上界的显式 Abort。** 不改 Vendor，Adapter 新增中止函数，到上界还没停就 `HAL_SD_DeInit`，卷保持 ERROR。

**8. 死锁规则。** 绝不同时持两把卷锁，跨卷操作由调用者分成多次调用。顺序固定是卷锁 → 表锁，表锁内不拿别的锁。持文件锁时不调用会阻塞的接口。界面数据的锁和文件锁不嵌套，由 `ui_cell` 的结构保证。

**9. 退路。** 换不换回请求队列由用户决定，不设自动判据。文件访问任务调大栈后最低剩余仍不足 256 B 或 AXI SRAM 新段放不下、非故障时音频读取等 SD 锁超过 1 s 或欠载确实由等锁造成、死锁或数据损坏的根因在锁方案本身，这些情况出现时把数字报给用户。

### 卷生命周期

**10. 消抖期调用先等着。** 卷任务在检测边沿处关闸，文件和目录接口在拿锁之前等闸打开，闸不挡卷任务自己。应用不写消抖代码。

**11. 消抖后对账，不靠状态变化事件。** 只看卷是否挂着和卡的现状，这样被吞的拔出也能接住。

**12. 卷事件只有卷就绪和卷消失，形式是叫醒加卷序号。** 每卷一个字存是否挂载和递增的序号，订阅者比对序号，“旧的全部作废，再看现状”。卷任务卸载后不等订阅者：令牌已经失效，它们晚点收尾也碰不到旧卡。

**13. 卡不在时读写返回 `NO_MEDIA`。** 调用者收到 `NO_MEDIA`、`NOT_READY`、`INVALID_HANDLE` 都不当文件错误，先看卷序号。

## 未采用的方案

- **全局一把锁**：同步配置不用改，但板测最坏等待 2.19 s，超过当时 2 s 的 T_data，Flash 写入中切歌最慢 1531 ms；每卷一把时 SD 锁等待 1 µs，切歌最慢 30.8 ms。
- **宽表锁**（把删除、改名也放进表锁）：删 1 MiB 占 262.7 ms，删约 15.4 MiB 占 2135.7 ms，这段时间 SD 打开文件要等。
- **给 `ff.c` 加补丁，把 `inc_lock` 改成原子操作**：用户不想再动 `ff.c`，窄表锁已够用。
- **`_FS_LOCK 0` 加 Service 查重**：要自己实现查重和 `FR_LOCKED`。Flipper Zero 这样做，但它只有一个卷且全部串行，不能作为跨卷依据。
- **`_FS_REENTRANT 1`**：只保护同卷，也保护不了 `Files[]`；多步函数中途还会放锁。
- **传输序号**：中断说不出是哪次传输结束的，也挡不住旧 DMA 覆盖 bounce buffer。
- **`HAL_SD_Abort`**：它的超时常量是 `0xFFFFFFFF`，等于没有上界。
- **消抖期返回“忙”**：每个调用者都要写重试和消抖判断，应用就不再是不写消抖代码了。
- **“卷将要消失”事件**：卷任务知道时卡已经没了，令牌由卸载统一作废。
- **卷事件队列或只留最新状态**：队列要管存储和上限；只留状态会丢掉快速拔插的中间一次。序号跳 2 也不会丢。
- **卷任务等订阅者回话**：要多一套确认机制，还会被优先级 1 的后台任务拖住。

## 后果

- 每条进入 FatFs 的调用链多 464–540 B 栈（板测）。
- 窄表锁依赖 FatFs 内部行为：升级时复核 `inc_lock`、`dec_lock`、`clear_lock`；实施时看 Release 反汇编，确认 `dec_lock` 先写 `ctr`、后写 `fs`。ADR-0022 不用改，因为没有新增补丁。
- Abort 要自己恢复 HAL 句柄的 `State`、`Context`，HAL 升级时复核。
- 大目录按路径打开最长持 SD 锁 2.05 s，所以曲库文件一律走定位符。T_data 的依据要算上等锁，改成 5 s（ADR-0023 的音频读取部分，见 [#194](https://github.com/immorcoding/mp3_project/issues/194)）。
- 已知漏洞（推断）：数据脚比检测脚先断时，第一次失败仍是 `ERROR`，显示读取错误而不是 “No SD card”，同样会停播。
- 窄表锁的并发正确性、迟到 DMA、闸和对账都还没有在产品代码上验证，由 0.10.0 板级验收负责。
