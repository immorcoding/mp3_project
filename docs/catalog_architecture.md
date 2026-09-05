# 曲库与播放列表

> 状态：扫描、顺序表与 Queue 窗口单槽已落地；GUI 按 Length 复制行已用假数据落地，尚未接线 `storage_listbuffer`；导航、元数据未做。  
> 相关决定：[ADR-0015](adr/0015-volume-roles-and-resource-install.md)  
> 实现：[`APP/tasks/storage/catalog/`](../APP/tasks/storage/catalog/)  
> 术语：[CONTEXT.md](../CONTEXT.md) **曲库**、**播放列表**

## 1. 所有权

曲库与播放列表属于 `APP/tasks/storage` 的 Storage Task，目录 `catalog/` 是该 Task 的私有分区，不是独立 Service Module，也不是「曲目元数据」库。扫描只经 Filesystem 的卷感知目录接口，且写死 `SERVICE_FILESYSTEM_VOLUME_SD`。GUI 不得直接调用同步文件/目录接口，也拿不到整表指针。

## 2. 公开 Interface

仅供 Storage Task 在挂载/拔卡时调用的建表接口，以及 GUI Task 的窗口单槽：

| 函数 | 作用 |
| --- | --- |
| `storage_catalog_init()` | 重建 Music Catalog，成功后立即建顺序播放列表 |
| `storage_catalog_music_init()` | 扫 SD `Music/` 并建表 |
| `storage_catalog_books_init()` | 空桩，恒返回 `STORAGE_OK`；`storage_catalog_init()` 内未调用 |
| `storage_catalog_invalidate()` | 作废 Catalog，并作废播放列表 |
| `storage_sheet_init(index_num, generation)` | 填写恒等 `SeqList[i] = i`，记下 Catalog 代次 |
| `storage_sheet_invalidate()` | 只把 Sheet `Generation` 置 0，不清数组 |
| `storage_listbuffer_request()` | GUI Task：仅 `storage_task_sd_is_ready()` 且 `IDLE` 时写入起点/条数/代次，打成 `PENDING` 并置 `STORAGE_NOTIFY_FLAG_LISTBUFFER` |
| `storage_listbuffer_load()` | Storage Task：仅 `PENDING` 时填路径拷贝；代次不符则 `Generation=0`、空窗，最后打 `READY` |

公开头不暴露字符串池、条目数组或 `SeqList`。窗口载荷在 `storage_listbuffer` 单槽：`Index` + `Length` + `Generation` + `Buffer[][]`。GUI 看见 `READY` 后整窗消费，再把 `Status` 写回 `IDLE`。`0` 代次表示请求方无快照或应答已作废。

`Index` 是窗口在播放列表上的起点，不是条数。`request` 时 `Length` 为请求条数（1..`STORAGE_LISTBUFFER_MAX_ENTRIES`）；`load` 成功后改成实际写入 `Buffer` 的条数，可能更短。`Buffer[i]` 对应列表位置 `Index + i`。槽位容量现为 8。GUI Queue 用固定数量的 panel 槽显示这一窗；窗口滑动时回收 panel，不把本槽改成环形数组。`Index` 已是列表起点。

当前播放位置是**播放列表下标**（与 `Index` 同一坐标系），必须携带与 Catalog/Sheet 相同的 `Generation`。拔卡或 `storage_catalog_invalidate()` 后 Sheet 代次为 0，游标作废，没有当前行。重新扫描成功后旧下标作废；非空库从 0 起，空库仍无当前曲。问询窗口与判定高亮行都要核代次。游标不放在 `storage_listbuffer` 里，实现尚未落地。

尺寸宏在 `storage_catalog_config.h`（`STORAGE_CATALOG_MUSIC_POOL_SIZE` 4 MiB，`STORAGE_CATALOG_MUSIC_MAX_NUM` 32000，`STORAGE_LISTBUFFER_MAX_ENTRIES` 8）。

## 3. 扫描与作废

插卡：`MountSD` 成功后 `storage_catalog_init()`。拔卡：先 `storage_catalog_invalidate()`，再 `UnmountSD`。

Music 扫描从相对路径 `Music` 递归子目录，同时只开一个目录句柄；路径栈深 128（`STORAGE_CATALOG_DIR_STACK_MAX`）。收录后缀 `.mp3` / `.MP3`，跳过 `.` 开头名。打开根 `Music` 失败 → 空表且 `STORAGE_OK`。条数满或字符串池满 → 截断已收录部分，仍 `STORAGE_OK`。读/关目录失败 → `STORAGE_ERROR`。栈满则跳过该子目录并告警。

条目只存 UTF-8 相对路径（形如 `Music/.../file.mp3`）在字符串池中的偏移与长度，**不存卷枚举**。Flash 卷不进入曲库。

启动不把 `.storage_catalog` / `.music_sheet`（SDRAM NOLOAD）并入 `.bss` 清零。扫描前只重置表头：内部 `.bss` 代次 `music_catalog_generation` 加一，清 `IndexNum`/`Tail`，不 `memset` 整池。SDRAM 里的 `Generation` 是该计数的副本。Sheet 有效长度不另存，等于建表时的 Catalog `IndexNum`。作废后 Catalog 代次继续递增（空表），Sheet `Generation = 0` 表示无效。

## 4. 未落地

- 带代次的上一首/下一首游标（生命周期已约定与 Catalog 相同，结构未做）、Playback 打开/预开
- GUI Queue 接入 `storage_listbuffer`（假数据循环生成已落地；`Service/gui` 不得包含该头）
- 随机列表、心动列表（结构体里已注释）
- 标题/歌手/封面等元数据
- Books Catalog 扫描
- 设备侧 Resource Pack 安装（与曲库分开，见 ADR-0015）

演进时若增加问询：必须带代次，0 表示已作废；不得把整表指针交给 GUI。随机或心动只替换下标序列，不重扫盘。
