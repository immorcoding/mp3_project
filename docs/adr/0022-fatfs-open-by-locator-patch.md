# ADR-0022：给 FatFs `ff.c` 加按目录项位置打开的补丁

- 状态：已接受。
- 日期：2026-10-31
- 相关 ticket：[打开文件提速的做法与边界](https://github.com/immorcoding/mp3_project/issues/167)、[FatFs 按目录位置打开的代价](https://github.com/immorcoding/mp3_project/issues/162)、[内存目录下 f_open 的 CPU 耗时实测](https://github.com/immorcoding/mp3_project/issues/170)、[0.9.0 spec](https://github.com/immorcoding/mp3_project/issues/172)
- 相关决定：[ADR-0005](0005-sd-filesystem-task-ownership.md)、[ADR-0014](0014-filesystem-volume-aware-file-interface.md)；规则 ARC-2（例外 Signal）、ARC-12、ARC-19、HAR-15、STOR-17

## 背景

FatFs R0.12c 的 `f_open` 每次从文件夹第 0 个目录项开始逐项比较长文件名，耗时随文件在文件夹内的位置线性增长（实测见 #143 I10、#162）。0.9.0 保证 12000 首可同在一个文件夹，要求单次打开 ≤ 约 30 ms、与位置无关；名称槽、Queue 窗口行、开流与跳转重开流都要打开文件。FatFs 是 Vendor，ARC-2（settled）规定只经上游更新；`ffconf.h` 的任何选项都不改变逐项查找，上游也没有目录索引。

## 决定

1. **按目录项位置打开。** Filesystem Service 在曲库扫描时随目录条目给出不透明的文件定位符（内容为定位与核对所需的目录项位置、起始簇与长度，归 Service 私有），Storage 曲库每条存一份。打开时按定位符直接读出目录项、核对，像 `f_open` 结尾那样填 FIL 并登记锁；核对不符退回按路径打开。
2. **给 `ff.c` 加且只加一个函数**完成第 1 条的 FatFs 内部部分，`_FS_LOCK` 由 FatFs 照常处理。函数原型放 Filesystem Service 私有头，Storage 与其他调用者看不到 FatFs 类型（ARC-12）。这是 ARC-2 的唯一例外，经用户授权。
3. **补丁以 patch 文件维护。** patch 存仓库根 `patches/fatfs/`；`Middlewares/Third_Party/FatFs/` 的改动由用户设 `ALLOW_GENERATED_UPDATE` 后提交，agent 不写该目录、不设该变量（HAR-4）。
4. **防覆盖。** 提交前检查补丁完整生效、重新导出后用重打脚本恢复，具体流程见 HAR-15。补丁被覆盖时链接也会失败，不会静默退回慢路径。
5. **补丁行为以主机测试守护**（进 FULL），范围见 #172 Testing Decisions。

## 未采用的方案

- **目录扇区缓存（只改自维护的 diskio 层，不碰 FatFs）。** 目录扇区全部命中缓存后，CPU 侧逐项比较长名仍随位置线性增长，文件夹末尾达不到预算，且缓存要占大块 SDRAM（实测与仿真见 #170）。
- **按 8.3 短名打开。** R0.12c 的查找路径照样逐项比较长名，不更快。
- **在 FatFs 外手拼 FIL。** 自建 FIL 的 `lockid` 为 0，`f_close` 会返回 `FR_INT_ERR`；绕开锁表又违背 `_FS_LOCK` 的保护。
- **`#include "ff.c"` 到自维护文件里扩展。** 要改 CubeMX 生成的 CMake 源文件列表，同样是受保护的生成产物。
- **限制单文件夹规模。** 与“12000 首可同在一个文件夹”的产品保证冲突。
- **升级 FatFs。** 上游没有目录索引或按位置打开的公开入口。

## 后果

- FatFs 升级或 CubeMX 重新导出多一步：跑重打脚本；补丁冲突时要人工改写 patch。
- 定位符只在同一曲库代次内有效；固件不写音乐卷、换卡即重扫，正常情况下核对不会失败，失败时记 `Locator miss` WARN（DIAG-5）。
- 曲库每首多存一份定位符（SDRAM，占用以实施票的 `.map` 为准）。
- 若将来上游提供等价能力，或改用其他文件系统，应以新 ADR 取代本决定并撤掉补丁。
