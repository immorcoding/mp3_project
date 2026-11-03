# FatFs 跨卷并发的事实

> 票：#187（地图 #186，0.10.0 应用架构 C）　日期：2026-11-03　分支：`research/fatfs-cross-volume`
> 范围：仓库 `Middlewares/Third_Party/FatFs`（R0.12c，`ff.h:2`，`_FATFS 68300`，`ff.h:22`）与现行配置 `FATFS/Target/ffconf.h`。外部对照：官方 appnote 与 Flipper Zero 指定提交。
> 只记事实。推断标“推断”；读不到或未验证的列入末尾“未能核实”。

## 结论摘要

1. `_USE_LFN` 1 改为 2：7 个公开函数的栈帧各增加 512 B，最深调用链增加 512 B（-O0 实测）；BSS 的 `LfnBuf` 减少 512 B。改为 3：栈帧增加 0–8 B，每次调用在堆上分配 512 B。
2. `Files[]`（`ff.c:541`）是所有卷共用的全局表。`chk_lock`、`inc_lock`、`dec_lock`、`clear_lock` 对它的读写只在同卷卷锁内发生，跨卷没有保护；`f_mount` 调用的 `clear_lock`（`ff.c:3279`）不在任何卷锁内执行。
3. `_FS_REENTRANT 1` 只保护同卷；它与 `_USE_LFN 1` 互斥（`ff.c:459–462`）。
4. `_FS_LOCK 2` 把同时打开的文件加非根目录合计限制为 2 个（跨两卷）；主机实测第三个返回 `FR_TOO_MANY_OPEN_FILES`（18）。
5. 主机实测：打开中的子目录可被 `f_unlink`、`f_rename` 删除或改名（返回 0）。原因是 `f_opendir` 登记的标识与 `chk_lock` 使用的标识不同（`ff.c:4288–4300` 对 `4636–4641`、`4827–4831`）。
6. `_FS_LOCK 0` 时，重复打开、删除或重命名打开中的对象、打开数上限全部不检查（主机实测均返回 0）。
7. 除 `Files[]` 外的跨卷共享状态：`Fsid`（`ff.c:534`、`3192`）、模式 1 的 `LfnBuf`（`ff.c:559`、`3194`）、全局 `disk` 分发表（`ff_gen_drv.c:24`）。本配置无 `CurrVol`（`_FS_RPATH 0`）。Unicode 表全部为只读 const。
8. 官方 appnote 称不同卷的文件操作无需互斥即可并发（模式 1 除外），与 R0.12c 中 `Files[]` 的读写不一致；版本差异未核实。
9. Flipper Zero（同一提交）以完整路径字符串查重（`storage_glue.c:99–115`），所有文件操作由单线程 `storage_srv` 串行（`storage.c:100–108`）。

## 方法与版本

- 源码阅读：`ff.c`、`ff.h`、`FATFS/Target/ffconf.h`、`diskio.c`、`ff_gen_drv.c`、`option/syscall.c`、`option/cc936.c`、`patches/fatfs/0001-open-by-dirent.patch`、`Service/filesystem/`、`FATFS/App/fatfs.c`、`Core/Src/main.c`。
- 编译测量：`arm-none-eabi-gcc` 15.2.1，参数 `-mcpu=cortex-m7 -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -fstack-usage -fcallgraph-info=su`，优化级别分别为 `-O0 -g0`（对应 CMake Debug 的 `-O0 -g3`，`cmake/gcc-arm-none-eabi.cmake:32`）与 `-Os -g0`（对应 Release，`:35`）。帧大小取 `.su`；调用链深度由 `.ci` 调用图递推：本帧加被调用者的最深链。`.ci` 中没有动态大小节点。原始输出未入库。
- 主机实测：MinGW gcc + 内存盘（两卷各 4 MiB）+ 仓库 `ff.c` 与 `option/cc936.c`。配置与现行一致（`_USE_LFN 1`、`_FS_LOCK 2`、`_FS_REENTRANT 0`、`_VOLUMES 2`），另做 `_FS_LOCK 0` 对照。唯一差异是 `_LFN_UNICODE 0`：MinGW 头文件先定义了 `_INC_TCHAR`，使 `ff.h` 跳过了 UTF-16 的 `TCHAR` 定义；路径字符串类型不影响锁逻辑。测试代码未入库。
- 版本：仓库为 R0.12c（ST 回移植，`Middlewares/Third_Party/FatFs/src/st_readme.txt`）。官方 appnote 页面标注 R0.16 选项说明，命名为 `FF_*`，互斥函数为 `ff_mutex_*`，与本仓库不同。下文以仓库代码为准。

## 一、长文件名缓冲（问题 1）

### 1.1 配置与三种模式

- 现行配置：`_USE_LFN 1`（`FATFS/Target/ffconf.h:112`），`_MAX_LFN 255`（`:113`），`_LFN_UNICODE 1`（`:129`），`_FS_EXFAT 0`（`:212`）。`ffconf.h:122` 注释与 appnote（LFN 节）都给出工作缓冲为 `(_MAX_LFN+1)*2` 字节，与代码一致。
- 模式 1：静态 `static WCHAR LfnBuf[_MAX_LFN + 1]`（`ff.c:559`，BSS）；`DEF_NAMBUF`、`INIT_NAMBUF` 为空（`ff.c:560–561`）。
- 模式 2：每个函数栈上 `WCHAR lbuf[_MAX_LFN+1]`（`ff.c:570`），`INIT_NAMBUF` 把它记入 `fs->lfnbuf`（`ff.c:571`）。
- 模式 3：`WCHAR *lfn;` 与 `ff_memalloc((_MAX_LFN+1)*2)`（`ff.c:581–582`），`FREE_NAMBUF` 为 `ff_memfree(lfn)`（`ff.c:583`）。

### 1.2 受影响的函数

本配置中 `DEF_NAMBUF` 生效的是以下 7 个函数：

| 函数 | `DEF_NAMBUF` | `INIT_NAMBUF` | `FREE_NAMBUF` |
|---|---|---|---|
| f_open | 3321 | 3331 | 3496 |
| f_opendir | 4262 | 4272 | 4309 |
| f_readdir | 4364 | 4372 | 4380 |
| f_stat | 4452 | 4458 | 4467 |
| f_unlink | 4628 | 4635 | 4699 |
| f_mkdir | 4722 | 4729 | 4796 |
| f_rename | 4819 | 4826 | 4899 |

不生效的 `DEF_NAMBUF`：`f_sync`（`ff.c:3834`，位于 `#if _FS_EXFAT`，`:3832`）、`f_chdir`（`:3968`，`_FS_RPATH >= 1`）、`f_getcwd`（`:4025`，`_FS_RPATH >= 2`）、`f_chmod`（`:4926`）与 `f_utime`（`:4972`，`_USE_CHMOD && !_FS_READONLY`，`:4912`）。`f_getlabel`、`f_setlabel`、`f_expand`、`f_forward`、`f_findfirst`、`f_findnext`、`f_fdisk` 的守卫为假，在编译测量的符号清单中不存在。`f_open_dirent`（补丁，`ff.c:3521–3594`）既无 `DEF_NAMBUF`，也不调用名称相关函数，不受影响。

### 1.3 栈与堆增量（编译器实测，-O0）

| 函数 | 模式 1 帧 | 模式 2 帧 | 模式 3 帧 | 模式 1 最深链 | 模式 2 最深链 | 模式 3 最深链 |
|---|---|---|---|---|---|---|
| f_open | 112 | 624 | 112 | 376 | 888 | 376 |
| f_opendir | 32 | 544 | 32 | 272 | 784 | 272 |
| f_readdir | 24 | 536 | 32 | 232 | 744 | 240 |
| f_stat | 72 | 584 | 80 | 312 | 824 | 320 |
| f_unlink | 136 | 648 | 136 | 376 | 888 | 376 |
| f_mkdir | 104 | 616 | 104 | 368 | 880 | 368 |
| f_rename | 168 | 680 | 176 | 432 | 944 | 440 |

- 三种模式下不变的函数（-O0 最深链，字节）：`f_open_dirent` 256，`f_read` 152，`f_write` 200，`f_sync` 88，`f_close` 112，`f_closedir` 48，`f_lseek` 184，`f_truncate` 176，`f_getfree` 248，`f_mkfs` 152，`f_mount` 208，`f_gets` 200，`f_puts` 344，`f_printf` 444。
- -Os（Release）：模式 2 的帧增量为 512 B，其中 `f_opendir`、`f_stat`、`f_rename` 为 520 B；最深链增量同为 512 或 520 B（`f_open` 360→872，`f_rename` 424→944）。模式 3 为 0–8 B。
- 算法：帧增量等于 `lbuf` 数组大小 `(_MAX_LFN+1)*2` = 512 B。-Os 下另外的 8 B 来自编译器的额外溢出槽（推断）。最深链 = 本函数帧 + 其 ff.c 内被调用者的最深链之和。模式 2 下一条调用路径只会经过一个声明了 `DEF_NAMBUF` 的函数：`f_close → f_sync`（`ff.c:3913`）、`f_gets → f_read`（`ff.c:5937`）、`f_puts`/`f_printf` 经 `putc_bfd`/`putc_flush` 到 `f_write`（`ff.c:6056`、`6072`）的被调用者都不声明 `DEF_NAMBUF`。因此最深链增量为 512 B，不叠加。
- 代码体积（-O0，`.text`）：模式 1 为 20065 B，模式 2 为 21141 B（+1076），模式 3 为 20285 B（+220）。
- BSS（`nm`）：模式 1 中 `LfnBuf` 为 0x200（512 B）、`Files` 0x20、`Fsid` 2 B、`FatFs` 8 B，合计 554 B；模式 2、3 为 42 B（无 `LfnBuf`）。
- 测量范围：编译器静态帧，不含 `disk_*`、`ff_convert`、`ff_memalloc` 等外部函数的栈；未在板上测量。

### 1.4 模式 2、3 的附带事实

- 模式 2：`fs->lfnbuf` 每次 INIT 指向当前栈帧（`ff.c:571`、`3331`、`4272` 等），函数返回后仍指向已失效的栈（`FATFS.lfnbuf`，`ff.h:97–99`）。本配置中，凡调用 `follow_path`、`dir_find`、`dir_read`、`dir_register`、`create_name`、`get_fileinfo` 的公开函数，都在同一函数内先执行 INIT（即上表 7 个函数）。`dir_sdi`（`ff.c:1470`）的函数体不读取 `lfnbuf`（grep 无命中）。未发现不经 INIT 而读取 `lfnbuf` 的路径（推断：基于调用点审查，未用运行测试穷尽）。
- 模式 2 的 `lfnbuf` 记在每卷的 `FATFS` 上（`ff.h:97–99`），因此两卷之间不共享该缓冲；同卷内无锁时会互相覆盖（推断）。
- 模式 3：`ff_memalloc`、`ff_memfree` 的声明在 `ff.h:303–306`；仓库实现在 `Middlewares/Third_Party/FatFs/src/option/syscall.c:158–175`，CMake 的 FatFs 源列表编译该文件（`cmake/stm32cubemx/CMakeLists.txt:115`）。它们调用 `ff_malloc`/`ff_free`，即标准库 `malloc`/`free`（`FATFS/Target/ffconf.h:264–265`）。堆的来源与容量未核实。
- 模式 3 中 INIT 失败时返回 `FR_NOT_ENOUGH_CORE`（`ff.c:582`，`ff.h:232`）。所有 INIT 与 FREE 之间都没有 `return`、`LEAVE_FF` 或 `ABORT`（逐函数 grep 核对）。
- 调用方栈上的 `FILINFO` 在模式 1、2、3 中大小相同：`_USE_LFN != 0` 时已含 `fname[_MAX_LFN+1]`（`ff.h:202–207`），切换模式不改变其大小。`DIR` 的 `blk_ofs` 同理（`ff.h:185–187`）。

## 二、文件锁表（问题 2）

### 2.1 对象与标识

- `FILESEM { FATFS *fs; DWORD clu; DWORD ofs; WORD ctr; }`（`ff.c:508–513`），`static FILESEM Files[_FS_LOCK];`（`ff.c:540–541`），共 2 × 16 = 32 B（`nm` 0x20）。
- 标识 (fs, clu, ofs)：文件为 (所在目录首簇, 目录项字节偏移)，取自 `dp->obj.sclust`、`dp->dptr`（`ff.c:781–783`、`817–819`）；子目录为 (自身首簇, 0)（`ff.c:4288`、`4296`、`4300`）。
- `ctr`：0x01–0xFF 为读打开计数，0x100 为写打开（`ff.c:512`、`793`、`833`）。

### 2.2 每一处读写与保护

| 函数（行） | 访问 | 调用处 | `_FS_REENTRANT 0`（现行） | `_FS_REENTRANT 1`（同卷） | 跨卷 |
|---|---|---|---|---|---|
| chk_lock 779–783 | 读 fs、clu、ofs | f_open 3340；f_open_dirent 3559；f_unlink 4641（acc=2）；f_rename 4831（acc=2） | 无 | 调用方持卷锁 | 无 |
| chk_lock 793 | 读 ctr | 同上 | 无 | 同上 | 无 |
| enq_lock 802 | 读 fs（扫描） | f_open 3349 | 无 | 同上 | 无 |
| inc_lock 816–819 | 读 fs、clu、ofs | f_open 3428；f_open_dirent 3567；f_opendir 4300 | 无 | 同上 | 无 |
| inc_lock 823 | 读 fs（找空槽） | 同上 | 无 | 同上 | **无**（见 W1） |
| inc_lock 825–828 | 写 fs、clu、ofs、ctr（登记） | 同上 | 无 | 同上 | **无**（见 W1） |
| inc_lock 831、833 | 读写 ctr | 同上 | 无 | 同上 | 无 |
| dec_lock 849–853 | 读写 ctr；清零 fs | f_close 3920；f_closedir 4336 | 无 | f_close 在 validate（3917）取卷锁；f_closedir 在 validate（4332）取卷锁 | 无（见 W4） |
| clear_lock 869–870 | 读 fs；写 fs=0 | find_volume 3203；f_mount 3279 | 无 | find_volume：卷锁内；**f_mount：无锁** | 无（见 W5） |

- 现行配置 `_FS_REENTRANT 0`：`ENTER_FF`、`LEAVE_FF` 为空（`ff.c:466–467`），`Files[]` 的所有访问都无保护。
- `_FS_REENTRANT 1`：卷锁为 `FATFS.sobj`（`ff.h:103–105`）。取锁点：`find_volume` 的 `ENTER_FF`（`ff.c:3017`）与 `validate` 的 `lock_fs`（`ff.c:3226`）。释放点：`LEAVE_FF`（`ff.c:464`）、`unlock_fs`（`ff.c:3927`、`4344`）。`f_mount`（`ff.c:3260–3299`）中没有取锁点。

### 2.3 `_FS_REENTRANT` 能否保护 `Files[]`

- 同卷：各公开函数在取卷锁期间访问 `Files[]`，同卷的两次调用串行（从 `find_volume` 到 `LEAVE_FF`，如 `f_open` 3308–3502）。
- 跨卷：不能。锁按卷存放（`ff.h:103–105`），`Files[]` 是全局表（`ff.c:541`）。appnote（Re-entrancy 节）也说明 `FF_FS_REENTRANT` 只保证同一文件系统对象的独占使用。
- `f_mount` 路径：不能。`clear_lock(cfs)`（`ff.c:3279`）在 `ff_del_syncobj`（`:3282`）之前执行，`f_mount` 中没有取锁点。`ffconf.h:246–248` 与 appnote 都说明 `f_mount`、`f_mkfs`、`f_fdisk` 不可重入。
- 配置约束：`_FS_REENTRANT 1` 与 `_USE_LFN 1` 互斥（`ff.c:459–462`，`#error`）。

### 2.4 竞争窗口（代码读取，未做并发实测）

- W1，`inc_lock` 找空槽与登记（`ff.c:822–828`）：`823` 读取 `Files[i].fs == 0`，`825–828` 才写入。两卷之间没有保护，可能取到同一索引，后写者覆盖前者的 fs、clu、ofs、ctr（推断）。
- W2，`f_open` 的 `enq_lock`（`ff.c:3349`）与 `inc_lock`（`:3428`）之间：中间有 `dir_register` 的磁盘写入。另一卷可以占用最后一个空槽，使 `inc_lock` 返回 0，`f_open` 返回 `FR_INT_ERR`（`:3429`）。此时目录项已由 `dir_register` 写入（`:3349` 先于 `:3428`），文件已建立但未打开（推断）。
- W3，同卷的 `chk_lock`（`:3340`）与 `inc_lock`（`:3428`）之间：`_FS_REENTRANT 1` 时由卷锁覆盖；`_FS_REENTRANT 0` 时无保护。
- W4，`dec_lock` 清空槽与他卷复用：槽归属单卷，结构上不存在两卷同时写同一项（推断）。悬挂的 `lockid` 由 `validate` 的 `fs_type` 与 `id` 检查拦截（`ff.c:3224`、`3917`）。
- W5，`f_mount` 的 `clear_lock`（`ff.c:3279`）与他卷的扫描：`clear_lock` 只写本卷条目（`ff.c:870` 比较 `Files[i].fs == fs`），他卷只读取不匹配本卷的条目（推断：不会错误归属）。`f_mount` 自身不取卷锁（`ff.c:3277–3293`）。

### 2.5 `_FS_LOCK 0` 后失去的保护

- 编译期删除的代码（`#if _FS_LOCK != 0`）：`f_open` 的 `chk_lock`、`enq_lock`、`inc_lock`（`ff.c:3338–3352`、`3427–3430`）；`f_open_dirent` 的检查与登记（`:3558–3569`）；`f_opendir` 的子目录登记（`:4297–4306`）；`f_close` 的计数释放（`:3919–3922`）；`f_closedir`（`:4334–4337`）；`f_unlink`（`:4640–4641`）与 `f_rename`（`:4829–4831`）的打开检查；`find_volume` 与 `f_mount` 的 `clear_lock`（`:3202–3204`、`3278–3280`）。
- 随之消失的返回码：`FR_LOCKED`（唯一产生点 `ff.c:793`）与 `FR_TOO_MANY_OPEN_FILES`（`:789`、`3349`、`4301`）。
- 结构体：`_FDID.lockid`（`ff.h:146–148`）消失，FIL 与 DIR 的大小改变（推断：每个对象减少 4 B，未测量）。`Files[]` 的 32 B BSS 消失。
- 主机实测（配置 B，`_FS_LOCK 0`）：重复写打开（0）、读打开于写打开的文件之上（0）、删除打开中的文件（0，文件被删）、超过两个对象同时打开（0）。

### 2.6 上层需要满足的约束（官方文档要点）

- appnote（Duplicated File Open 节）：写模式的重复打开禁止；重复打开只允许全部为读模式；打开中的文件不得被重命名或删除；违反会损坏数据。
- `ffconf.h:235–236`（`_FS_LOCK 0` 的注释）：应用程序应避免对打开对象的非法打开、删除与重命名。
- 以上是 `_FS_LOCK 0` 时由应用承担的约束；FatFs 自身不检查。

### 2.7 Flipper Zero 的做法（commit `9981d9d4aa5ec7b14bb46cf15763dd293a7dec6a`）

- `targets/f7/fatfs/ffconf.h`：`_USE_LFN 2`（`:117`）、`_VOLUMES 1`（`:162`）、`_FS_LOCK 0`（`:232`）、`_FS_REENTRANT 0`（`:243`）、`_FS_TIMEOUT 1000`（`:244`）。
- 查重函数 `storage_path_already_open`（`applications/services/storage/storage_glue.c:99–115`）：遍历本存储的打开列表，用 `furi_string_cmp(storage_file->path, path) == 0` 比较完整路径字符串（`:108`）。
- 检查点：文件打开（`storage_processing.c:115–116`，返回 `FSE_ALREADY_OPEN`）；目录打开（`:272–273`）；删除（`storage_process_common_remove`，`:363–364`）。与 FatFs 不同，任何模式的重复打开都被拒绝。
- SD 卸载：列表中有任何打开对象（文件或目录）时返回 `FSE_DENIED`（`storage_processing.c:456`；计数函数 `storage_open_files_count` 见 `storage_glue.c:155–158`）。
- 列表增删：打开时 `storage_push_storage_file`（`storage_glue.c:129–134`），关闭时 `storage_pop_storage_file`（`:136–153`）。列表无容量上限（推断：代码中未见上限检查）。
- 下载文件中没有重命名的处理（`storage_processing.c` 中无 rename 字样）。
- 路径规范化：去尾斜杠的函数仅在 `storage_processing.c:693–694` 的路径等价判断中调用；打开与删除的比较没有大小写折叠。
- 存储类型：`storage_get_data` 中有 `furi_assert(type == ST_EXT)`（`storage_processing.c:82`）。下载文件内的文件操作只指向 SD；`ST_INT` 的实现未核实。
- 线程：`storage_srv` 循环从 8 项消息队列取消息并调用 `storage_process_message`（`applications/services/storage/storage.c:36`、`100–108`）。
- 与 FatFs 标识的差异：Flipper 比较路径字符串，FatFs 比较 (fs, clu, ofs)。FAT 文件名大小写不敏感（`ff.c:1693` 的 `cmp_lfn` 用 `ff_wtoupper` 比较；`ff.c:2701` 在 `create_name` 中把 SFN 转大写），因此大小写不同的两个路径可指向同一对象，而 Flipper 的字符串比较不会匹配（推断，未在设备上实测）。

### 2.8 子目录标识与 `f_unlink`、`f_rename`（主机实测）

- `f_opendir`：`obj->sclust = ld_clust(fs, dp->dir)`（`ff.c:4288`），之后 `dir_sdi(dp, 0)`（`:4296`），登记标识为 (子目录首簇, 0)（`:4300`）。
- `follow_path` 的最后一段不改写 `obj->sclust`：`ff.c:2872` 在最后一段命中时直接 break，`:2888` 只在下降进入子目录时写入。因此 `f_unlink`（`:4636`、`4641`）与 `f_rename`（`:4827`、`4831`）的 `chk_lock` 使用的是 (父目录首簇, 目录项偏移)，与 `f_opendir` 的标识不同。
- 主机实测（配置 A）：打开中的空子目录 `d1` 被 `f_unlink` 删除（返回 0，之后 `f_stat` 返回 `FR_NO_FILE`）；打开中的 `d2` 被 `f_rename` 改名（返回 0）。两项与代码推断一致。
- 测量条件：单线程；只覆盖空目录。

### 2.9 打开数上限（跨卷合计）

- 上限为 `_FS_LOCK` 项，文件与非根目录共用，两卷共用（`ff.c:789`、`3349`、`4301`）。根目录不占项（`ff.c:4303`，`lockid = 0`）。
- 主机实测（配置 A）：卷 0 的 `b.txt` 与卷 1 的 `c.txt` 占满两项后，卷 0 的 `a.txt` 返回 18（`FR_TOO_MANY_OPEN_FILES`）。打开根目录不占项；打开子目录占一项（测试项 T4、T5、T5b）。

## 三、其他跨卷共享状态（问题 3）

| 状态 | 位置 | 写入或读取点 | 跨卷共享 | 保护 |
|---|---|---|---|---|
| `FatFs[]` 卷指针表 | `ff.c:533` | `f_mount` 写 `:3293`；`find_volume` 读 `:3014`；`f_mkfs` 读 `:5421` | 是（数组） | `f_mount` 无锁 |
| `Fsid` 挂载 ID | `ff.c:534` | `find_volume`：`fs->id = ++Fsid`（`:3192`） | 是 | 无（非原子递增，推断可能重号） |
| `CurrVol` | `ff.c:536–538` | — | 本配置不存在（`_FS_RPATH 0`） | — |
| `Files[]` | `ff.c:541` | 见第二节 | 是 | 同卷锁 |
| `LfnBuf` | `ff.c:559` | `find_volume`：`fs->lfnbuf = LfnBuf`（`:3193–3194`） | 是（仅模式 1） | 无 |
| `DirBuf` | `ff.c:557` | — | 仅 exFAT，本配置不存在 | — |
| `FATFS` 内的 `win`、`lfnbuf`、`dirbuf` | `ff.h:97–101`、`125` | 各卷自身 | 否（每卷一份） | 同卷锁 |
| `disk` 驱动表 | `ff_gen_drv.c:24` | `FATFS_LinkDriverEx`（`:39–58`）写 `nbr`、`drv`、`lun`、`is_initialized` | 是（全局结构） | 无 |
| `disk_initialize` 的读改写 | `diskio.c:57–62` | `is_initialized[pdrv]` | 按驱动号分元素 | 无 |

- `disk_*` 分发层（`diskio.c:36–130`）没有自身状态，只经 `disk.drv[pdrv]` 转发。`pdrv = vol`（`ff.c:476`，`_MULTI_PARTITION 0`）。`get_fattime`（`diskio.c:138`）在 `_FS_NORTC 1` 时不被调用（`ff.c:497`）。
- appnote 的表格列出 `disk_*` 在“同卷”“不同卷同驱动”“不同卷不同驱动”三种情况下的可重入性；Remarks 段指出设备控制层需对不同卷保持线程安全。仓库中驱动实现（`FATFS/Target/`、`Service/filesystem/sd|flash/`）的并发性未核实。
- `FATFS_LinkDriverEx` 的调用点是 `FATFS/App/fatfs.c:32`、`34`，由 `Core/Src/main.c:122` 的 `MX_FATFS_Init()` 调用；调度器启动与该调用的先后顺序未核实。
- Unicode：`option/cc936.c` 的 `uni2oem`（`:14`）与 `oem2uni`（`:5468`）为 const。`ff_convert`（`:10923–10958`）只读这两张表。`ff_wtoupper`（`:10960` 起）只读 `static const cvt1`（`:10965`）与 `cvt2`（`:10989`）。无可写状态。`Service/filesystem/filesystem_text.c:19` 直接调用 `ff_convert`，不经文件锁；其注释写明不访问介质。
- 函数内 `static` 变量全部为 const：`ff.c:2912`（`volid`）、`:5115`（`badchr`）、`:5403–5404`（`cst`、`cst32`）。
- 补丁 `patches/fatfs/0001-open-by-dirent.patch`：新增 `f_open_dirent`（补丁 `+9` 至 `+96`，合入后为 `ff.c:3504–3600`），没有新的全局或静态变量。它增加了两处共享表的调用（`ff.c:3559`、`3567`）和一个指向本卷窗口的指针（`fp->dir_ptr = dj.dir`，`:3564`；`dj.dir` 指向 `fs->win`）。
- Service 层：`filesystem_file_slots[]`（`Service/filesystem/filesystem_handle.c:35`）是 Service 自己的全局句柄表，`filesystem_handle_volume_has_open` 在 `:152–166` 遍历它。它不是 FatFs 状态。

## 四、同卷的保护（问题 4）

- 4.1 锁的粒度：`_FS_REENTRANT 1` 时锁为每卷一个（`FATFS.sobj`，`ff.h:103–105`）。`ff_req_grant` 失败时返回 `FR_TIMEOUT`（`ff.c:463–464`、`3226–3234`），超时长度由 `_FS_TIMEOUT` 决定（`ffconf.h:242`）。
- 4.2 取锁点不是在每个公开函数入口统一做的：`find_volume` 在 `:3017` 取锁，`validate` 在 `:3226` 取锁；`f_mount`、`f_mkfs` 不取锁。`find_volume` 在成功与若干失败返回路径上都保留锁，依赖调用方的 `LEAVE_FF(fs, …)` 释放（`:3018` 设置 `*rfs`；`f_open` 在 `:3501` 释放）。
- 4.3 FIL 与 DIR 对象：`FIL`（`ff.h:155–172`）与 `DIR`（`ff.h:178–191`）没有独立的锁字段。卷锁只覆盖单次调用，跨调用的对象状态不受 FatFs 保护（推断：例如两个任务共用一个 `FIL` 时的 `fptr` 与 `buf`，代码未加保护）。
- 4.4 样例实现 `option/syscall.c`（仅 `_FS_REENTRANT` 时有效，`:27`）：`ff_cre_syncobj` 在 `_USE_MUTEX` 未定义时使用信号量分支（`:43`、`:52–58`）；`ff_req_grant` 使用 `osSemaphoreAcquire(sobj, _FS_TIMEOUT)`（`:116`，v2 分支，`:113–116`）。`FATFS/Target/ffconf.h` 中没有 `_USE_MUTEX`；`ffconf_template.h:265` 有定义，但它不是项目使用的文件。本工程的 `osCMSIS` 取值未核实（`Drivers/CMSIS/RTOS2/Template/cmsis_os.h:138` 为 0x20001U，但未核实本工程实际包含的头文件）。
- 4.5 `f_mount` 的代码行为：`f_mount`（`ff.c:3260–3299`）不检查卷上是否有打开对象，直接执行 `clear_lock`（`:3279`），在 `ff_del_syncobj`（`:3281–3283`）前不检查是否有其他任务等待，然后清零 `fs_type`（`:3284`）并设置 `FatFs[vol]`（`:3293`）。`opt == 1` 时调用 `find_volume`（`:3297`）。
- 4.6 `f_mkfs` 的代码行为：`f_mkfs`（`ff.c:5393`）不调用 `find_volume`，没有 `ENTER_FF` 或 `validate`；若 `FatFs[vol]` 非空则清零其 `fs_type`（`:5421`），并调用 `disk_initialize`（`:5426`）与 `disk_write`（`:5540` 起）。`f_fdisk`（`:5850`）在 `_MULTI_PARTITION 0` 时未编译。
- 4.7 Service 的现行调用（事实）：`Service/filesystem/filesystem_service.c` 在挂载前检查该卷有无打开句柄（`:96–98`、`:166–168`）；卸载调用 `f_mount(NULL, …, 0)`（`:138`、`:208`）；格式化前先注销（`:222`），然后 `f_mkfs`（`:239–243`），最后重新挂载（`:249`）。
- 4.8 `validate` 取锁前的检查（`ff.c:3224`）读取 `obj->fs->fs_type` 与 `obj->id`；取锁后不再复核（`:3226–3231`）。

## 五、退路：单把全局锁（问题 5）

- 5.1 不需要改 `ffconf.h` 的部分：`_FS_REENTRANT 0` 时 FatFs 不调用 `ff_req_grant`、`ff_rel_grant`（`ff.c:466–467` 为空宏）；`option/syscall.c` 中的同步函数在 `_FS_REENTRANT` 为 0 时不编译（`:27`）。
- 5.2 单把全局锁覆盖所有 `f_*`、`f_mount` 与 `f_mkfs` 时，第三节的共享状态都在锁内访问。`_USE_LFN 1` 的 `LfnBuf` 也只在调用内使用。`ffconf.h:117` 的注释写明模式 1 “Always NOT thread-safe”；该注释针对的是非串行调用（推断）。
- 5.3 与并发相关、且约束应用行为的 `ffconf.h` 值：
  - `_FS_LOCK 2`（`:230`）：同时打开的对象合计上限为 2（见 2.9）。`APP/tasks/storage/catalog/storage_track_text.c:91` 的注释写明“不超过 FatFs `_FS_LOCK`（2）”。
  - `_FS_TIMEOUT 1000`（`:242`）、`_SYNC_t NULL`（`:243`）：`_FS_REENTRANT 0` 时无效。
  - `_VOLUMES 2`（`:157`）：两卷，对应 SD 与 Flash（`FATFS/App/fatfs.c:18`、`22`）。
  - `_FS_RPATH 0`（`:145`）：没有当前目录与当前驱动状态。
  - `_FS_TINY 0`（`:206`）：每个 FIL 含 512 B 缓冲（`ff.h:170`）。
- 5.4 “是否够用”取决于应用同时打开的对象数，本票未给出该数值。按 2.9 的实测，超过 2 时第三个对象打开失败。
- 5.5 `ffconf.h` 中没有任何值需要为单把全局锁方案改动（事实；`_FS_LOCK` 的数值是否合适不在本票判断）。

## 外部来源对照（版本差异）

- appnote（R0.16 选项说明）与仓库 R0.12c 的文字差异：
  - 配置名：`FF_USE_LFN`、`FF_FS_LOCK`、`FF_FS_REENTRANT`、`FF_TIMEOUT`，对应仓库的 `_USE_LFN`、`_FS_LOCK`、`_FS_REENTRANT`、`_FS_TIMEOUT`（`ffconf.h:112`、`230`、`241`、`242`）。
  - 互斥函数：appnote 为 `ff_mutex_create/delete/take/give`；仓库为 `ff_cre_syncobj/ff_del_syncobj/ff_req_grant/ff_rel_grant`（`ff.h:311–314`）。
  - 字符转换：appnote 为 `ff_uni2oem`、`ff_oem2uni`；仓库为 `ff_convert`（`ff.h:301`）。
  - 可重入：appnote（Re-entrancy 节）写明不同卷的文件操作可并发，无需互斥（`FF_USE_LFN = 1` 除外）。仓库 `ffconf.h:244–248` 也有“不同卷的文件访问总是可重入”的注释；但 `Files[]` 为全局表（`ff.c:541`），且 `_FS_LOCK != 0` 时存在第 2.4 节所列窗口。R0.16 是否修正了这一点未核实。
- Flipper Zero（commit `9981d9d4aa5ec7b14bb46cf15763dd293a7dec6a`）：`ffconf.h` 的文件头为 R0.12c（`ffconf.h:4`）。`_FS_LOCK 0` 与查重方式见 2.7。

## 未能核实

- R0.16 实现的 `Files[]` 与 `chk_lock` 等是否修正了跨卷问题（未下载 R0.16 源码）。
- `_FS_REENTRANT 1` 在本工程的 `osCMSIS` 取值与 `osSemaphoreAcquire` 的可用性（`syscall.c` 有 v1、v2 两个分支）。
- 模式 3 的堆来源、容量与碎片（`malloc` 与链接脚本的堆大小未核实）。地图 #186 记录了 0.9.0 板测的堆最低余量 15,424 B，本票未复测。
- 栈测量是编译器静态帧（arm-none-eabi-gcc 15.2.1），未在本工程的 CMake 构建中复现，未在板上测量，不含外部函数的栈。
- 子目录 `f_unlink`、`f_rename` 的实测只覆盖空目录与单线程。跨卷竞争窗口（W1–W5）只有代码推断，没有并发实测。
- `FATFS_LinkDriverEx` 与调度器启动的先后顺序（`Core/Src/main.c:122` 之后的启动代码未读）。
- 驱动层（`FATFS/Target/bsp_driver_sd.c`、`sd_diskio.c`、`Service/filesystem/sd|flash/*`）的并发性与共享状态未逐一核实。
- Flipper 的 `ST_INT` 实现（未下载对应文件），以及 `storage_srv` 是否为所有文件操作的唯一入口（API 层未读）。
- Flipper 对目录删除是否检查其下打开的文件（代码只做路径全等比较，推断为否）。
- `filesystem_file_slots` 是否计入目录句柄（未读 `filesystem_handle.c` 的全部代码）。
- 结构体大小差异（`_FS_LOCK 0` 时每个对象减少 4 B）未经编译测量确认。
