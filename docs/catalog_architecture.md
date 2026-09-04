# 曲库与播放列表

> 状态：扫描与顺序表已落地（2026-09-03）；窗口问询、导航、元数据未做。  
> 相关决定：[ADR-0015](adr/0015-volume-roles-and-resource-install.md)  
> 实现：[`APP/tasks/storage/catalog/`](../APP/tasks/storage/catalog/)  
> 术语：[CONTEXT.md](../CONTEXT.md) **曲库**、**播放列表**

## 1. 所有权

曲库与播放列表属于 `APP/tasks/storage` 的 Storage Task，目录 `catalog/` 是该 Task 的私有分区，不是独立 Service Module，也不是「曲目元数据」库。扫描只经 Filesystem 的卷感知目录接口，且写死 `SERVICE_FILESYSTEM_VOLUME_SD`。GUI 不得直接调用同步文件/目录接口，也拿不到整表指针。

## 2. 公开 Interface

仅供同一 Storage Task 在挂载成功或拔卡时调用：

| 函数 | 作用 |
| --- | --- |
| `storage_catalog_init()` | 重建 Music Catalog，成功后立即建顺序播放列表 |
| `storage_catalog_music_init()` | 扫 SD `Music/` 并建表 |
| `storage_catalog_books_init()` | 空桩，恒返回 `STORAGE_OK`；`storage_catalog_init()` 内未调用 |
| `storage_catalog_invalidate()` | 作废 Catalog，并作废播放列表 |
| `storage_sheet_init(index_num, generation)` | 填写恒等 `SeqList[i] = i`，记下 Catalog 代次 |
| `storage_sheet_invalidate()` | 只把 Sheet `Generation` 置 0，不清数组 |

公开头不暴露字符串池、条目数组或 `SeqList`。没有按代次问行、窗口切片、按 Sheet 下标取路径、上一首/下一首 API。

常量（公开头）：`STORAGE_CATALOG_MUSIC_POOL_SIZE` 为 4 MiB；`STORAGE_CATALOG_MUSIC_MAX_NUM` 为 32000，须能放入 `uint16_t`。

## 3. 扫描与作废

插卡：`MountSD` 成功后 `storage_catalog_init()`。拔卡：先 `storage_catalog_invalidate()`，再 `UnmountSD`。

Music 扫描从相对路径 `Music` 递归子目录，同时只开一个目录句柄；路径栈深 128（`STORAGE_CATALOG_DIR_STACK_MAX`）。收录后缀 `.mp3` / `.MP3`，跳过 `.` 开头名。打开根 `Music` 失败 → 空表且 `STORAGE_OK`。条数满或字符串池满 → 截断已收录部分，仍 `STORAGE_OK`。读/关目录失败 → `STORAGE_ERROR`。栈满则跳过该子目录并告警。

条目只存 UTF-8 相对路径（形如 `Music/.../file.mp3`）在字符串池中的偏移与长度，**不存卷枚举**。Flash 卷不进入曲库。

启动不把 `.storage_catalog` / `.music_sheet`（SDRAM NOLOAD）并入 `.bss` 清零。扫描前只重置表头：内部 `.bss` 代次 `music_catalog_generation` 加一，清 `IndexNum`/`Tail`，不 `memset` 整池。SDRAM 里的 `Generation` 是该计数的副本。Sheet 有效长度不另存，等于建表时的 Catalog `IndexNum`。作废后 Catalog 代次继续递增（空表），Sheet `Generation = 0` 表示无效。

## 4. 未落地

- 带代次的窗口问询、Queue 行预取
- 上一首/下一首游标、Playback 打开/预开
- 随机列表、心动列表（结构体里已注释）
- 标题/歌手/封面等元数据
- Books Catalog 扫描
- 设备侧 Resource Pack 安装（与曲库分开，见 ADR-0015）

演进时若增加问询：必须带代次，0 表示已作废；不得把整表指针交给 GUI。随机或心动只替换下标序列，不重扫盘。
