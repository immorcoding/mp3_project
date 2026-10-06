# 曲库与播放列表

> 状态：扫描、顺序表、Queue 窗口单槽、GUI 滑窗、播放列表游标、点按假切歌与上一首/下一首环形步进已落地；Playback 打开、元数据未做。  
> 相关决定：[ADR-0015](adr/0015-volume-roles-and-resource-install.md)  
> 实现：[`APP/tasks/storage/catalog/`](../APP/tasks/storage/catalog/)  
> 术语：[GLOSSARY.md](../GLOSSARY.md) **曲库**、**播放列表**

## 1. 所有权

曲库与播放列表属于 `APP/tasks/storage` 的 Storage Task，目录 `catalog/` 是该 Task 的私有分区，不是独立 Service Module，也不是「曲目元数据」库。扫描只经 Filesystem 的卷感知目录接口，且写死 `SERVICE_FILESYSTEM_VOLUME_SD`。GUI 不得直接调用同步文件/目录接口，也拿不到整表指针。

## 2. 公开 Interface

仅供 Storage Task 在挂载/拔卡时调用的建表接口，以及 GUI Task 的窗口单槽：

| 函数 | 作用 |
| --- | --- |
| `storage_catalog_init()` | 重建 Music Catalog，成功后立即建顺序播放列表 |
| `storage_catalog_music_init()` | 扫 SD `Music/` 并建表 |
| `storage_catalog_books_init()` | 空桩，恒返回 `STORAGE_OK`；`storage_catalog_init()` 内未调用 |
| `storage_catalog_invalidate()` | 作废 Catalog，并作废播放列表与游标 |
| `storage_sheet_init(index_num, generation)` | 填写恒等 `SeqList[i] = i`，记下 Catalog 代次 |
| `storage_sheet_invalidate()` | 只把 Sheet `Generation` 置 0，不清数组 |
| `storage_listbuffer_request()` | GUI Task：仅 `storage_task_sd_is_ready()` 且 `IDLE` 时写入起点/条数/代次，打成 `PENDING` 并置 `STORAGE_NOTIFY_FLAG_LISTBUFFER` |
| `storage_listbuffer_load()` | Storage Task：仅 `PENDING` 时填窗口；当前拷路径。落地后按曲库路径调解析器写标题/歌手。代次不符则 `Generation=0`、空窗，最后打 `READY` |
| `storage_playback_cursor_init(index_num, generation)` | 扫描成功后重建游标；空库或代次 0 则无当前曲，非空库从 0 起 |
| `storage_playback_cursor_invalidate()` | 作废游标 |
| `storage_playback_cursor_get(index, generation)` | GUI Task：读当前播放列表下标与代次；无当前曲返回 `STORAGE_ERROR` |
| `storage_playback_cursor_set(index)` | GUI Task：同一代次内改当前下标；作废、空库或越界失败。不打开文件 |
| `storage_playback_cursor_previous()` | GUI Task：环形上一首；作废或空库失败。不打开文件 |
| `storage_playback_cursor_next()` | GUI Task：环形下一首；作废或空库失败。不打开文件 |

公开头不暴露字符串池、条目数组或 `SeqList`。窗口载荷在 `storage_listbuffer` 单槽：`Index` + `Length` + `Generation` + `Buffer[][]`。`request` 只提交起点、条数和代次，不传路径也不传曲名。GUI 看见 `READY` 后整窗消费，再把 `Status` 写回 `IDLE`。当前 `Buffer` 仍是曲库路径，Queue 原样显示；标题/歌手等 `load` 调解析器，见第 5 节。曲库仍只存相对路径。`0` 代次表示请求方无快照或应答已作废。

`Index` 是窗口在播放列表上的起点，不是条数。`request` 时 `Length` 为请求条数（1..`STORAGE_LISTBUFFER_MAX_ENTRIES`）；`load` 成功后改成实际写入 `Buffer` 的条数，可能更短。`Buffer[i]` 对应列表位置 `Index + i`。槽位容量现为 12。GUI Queue 用固定数量的 panel 槽显示这一窗；窗口滑动时回收 panel，不把本槽改成环形数组。`Index` 已是列表起点。GUI Task 的 `music/` 分区在滑动过程中按 `QueueTab` 滚出顶部的整行数改 `Index` 再 `request`：列表起点不留上一窗，其余留一行给回滑；末窗 `Length` 不足上限则不再往外推。换窗从当前 `scroll_y` 扣整行高度并保留剩余像素，不吸回整页，也不按 MainPager 那样等 `SCROLL_END` 吸附。

当前播放位置是**播放列表下标**（与 `Index` 同一坐标系），必须携带与 Catalog/Sheet 相同的 `Generation`。拔卡或 `storage_catalog_invalidate()` 后 Sheet 代次为 0，游标作废，没有当前行。重新扫描成功后旧下标作废；非空库从 0 起，空库仍无当前曲。问询窗口与判定高亮行都要核代次。游标不放在 `storage_listbuffer` 里，由 `storage_playback_cursor_*()` 持有。

尺寸宏在 `storage_catalog_config.h`（`STORAGE_CATALOG_MUSIC_POOL_SIZE` 4 MiB，`STORAGE_CATALOG_MUSIC_MAX_NUM` 32000，`STORAGE_LISTBUFFER_MAX_ENTRIES` 12）。

## 3. 扫描与作废

插卡：`MountSD` 成功后 `storage_catalog_init()`。拔卡：先 `storage_catalog_invalidate()`，再 `UnmountSD`。

Music 扫描从相对路径 `Music` 递归子目录，同时只开一个目录句柄；路径栈深 128（`STORAGE_CATALOG_DIR_STACK_MAX`）。收录后缀 `.mp3` / `.MP3`，跳过 `.` 开头名。打开根 `Music` 失败 → 空表且 `STORAGE_OK`。条数满或字符串池满 → 截断已收录部分，仍 `STORAGE_OK`。读/关目录失败 → `STORAGE_ERROR`。栈满则跳过该子目录并告警。

条目只存 UTF-8 相对路径（形如 `Music/.../file.mp3`）在字符串池中的偏移与长度，**不存卷枚举**。Flash 卷不进入曲库。未插卡或缺少 `Music/` 时曲库为空，不自动创建该目录。标题、歌手、封面不是曲库字段，也不从文件名用 ` - ` 切开冒充元数据。

启动不把 `.storage_catalog` / `.music_sheet`（SDRAM NOLOAD）并入 `.bss` 清零。扫描前只重置表头：内部 `.bss` 代次 `music_catalog_generation` 加一，清 `IndexNum`/`Tail`，不 `memset` 整池。SDRAM 里的 `Generation` 是该计数的副本。Sheet 有效长度不另存，等于建表时的 Catalog `IndexNum`。作废后 Catalog 代次继续递增（空表），Sheet `Generation = 0` 表示无效。

## 4. 未落地

- Playback 打开/预开（点 Queue 行、上一首/下一首已可改游标，仍不解码）
- 随机列表、心动列表（结构体里已注释）
- 标题/歌手/封面：等 MP3 **解析组件**，见第 5 节
- Books Catalog 扫描
- 设备侧 Resource Pack 安装（与曲库分开，见 ADR-0015）

演进时若增加问询：必须带代次，0 表示已作废；不得把整表指针交给 GUI。随机或心动只替换下标序列，不重扫盘。

## 5. 元数据与 MP3 解析（未落地）

Queue 当前把 `Buffer` 里的曲库路径原样显示，歌手 Label 为空。GUI 不裁 `Music/`、不裁 `.mp3`、也不按文件名里的 ` - ` 切开。那不是产品元数据。

`request` 始终只带窗口在播放列表上的 `Index`、条数和代次。路径留在曲库里，供 `load` 打开文件；GUI 从不在 request 里传路径或曲名。

落地时：

- **解析器**（待建 Component）：读 MP3 文件结构，至少包括 ID3v2/ID3v1 标题与歌手；帧边界、时长等随解析组件一并设计。GUI 与曲库都不直接拆标签。
- **解码器**（Helix 等）：只吃解析器给出的音频载荷，输出 PCM。现有 `Components/audio` 只做 PCM 发送，不是解码器，也不是解析器。`load` 只调解析器取标签，不走进解码器。
- 流水线是 **解析器 → 解码器**，再进 Playback 缓冲与 I2S。不要把 Helix 和 ID3 揉进同一个 Module。
- **何时读**：`storage_listbuffer_load()` 填这一窗（至多 `STORAGE_LISTBUFFER_MAX_ENTRIES` 条）时，用曲库路径打开文件并调解析器，把标题/歌手写入窗口载荷（槽位布局届时改，不再把路径交给 GUI）。不在全库扫描时为三万首写标题池。曲库仍只存路径。GUI Task 的 `music/` 原样把窗口里的曲名/歌手交给 `Service_GUI_QueueApply`，仍不得打开文件，`Service/gui` 仍不得包含 `storage_listbuffer.h`。
- **封面**（APIC 等）属于 Now Playing，不进 Queue 行。
- 标签缺失或读失败时的显示文本由 `load`/解析器决定，不回到 GUI 裁路径。

现在不创建空的解析/解码目录，也不改 listbuffer 槽位布局。
