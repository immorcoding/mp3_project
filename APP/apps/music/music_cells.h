/**
 * @file music_cells.h
 * @brief 音乐应用的界面数据格子定义：曲名与 Queue 窗口各一对（需求格、结果格），GUI 写需求、前台任务写结果。
 *
 * @details
 *          格子经 `ui_cell`（APP 私有的短锁模块）交接，只用于 GUI 与前台任务之间（ARC-24）：
 *          - **需求是状态不是动作**：曲名需求 = “我现在要看这首的名字”，窗口需求 = “我现在要看从这里起的一窗”，新的覆盖旧的；
 *            只有一个通用的“需求变了”通知（`ui_cell` 的叫醒函数 = `foreground_task_wake`），不按使用方分标志。
 *          - **一格一个写者**：需求格只有 GUI 写，结果格只有前台任务写（调试版断言）。
 *          - **结果过期就会显示错内容时才比需求序号**：曲名要比（结果带回所答的需求序号，GUI 只采信最新序号的结果）；
 *            列表窗口不比：它自带起点、代次和条数，又是整份拷贝，晚到也仍是列表上一段正确的内容（#200）。
 *          - **整窗才发布**：GUI 只见整窗（STOR-16）；窗口行只含显示文本，不含路径。
 *
 *          内存（#193、#202 E 节）：四个格子的本体与 GUI 一侧的副本放 AXI SRAM 新段 `.app_axi_ram`（NOLOAD，不自动清零，
 *          `music_cells_boot_init()` 在 Boot 里完整写入）；行缓存和组装缓冲放 `.apps_sdram`。窗口格和 GUI 窗口副本各约 6 KiB。
 *          约 8 KiB 以内进格子（ARC-25 的初值），窗口超过它时要改成两块缓冲。
 */

#ifndef MUSIC_CELLS_H
#define MUSIC_CELLS_H

#include <stdbool.h>
#include <stdint.h>

#include "APP/apps/music/catalog/catalog_name.h"
#include "APP/apps/music/music_config.h"
#include "APP/ui_cell/ui_cell.h"

/** @brief 一行的显示文本；不含路径（STOR-16）。 */
typedef struct
{
    char Title[CATALOG_NAME_BYTES];  /**< 曲目显示名：可用标题，否则文件主名；可直接显示的 UTF-8。 */
    char Artist[CATALOG_NAME_BYTES]; /**< 歌手；取不到为空串。 */
} MusicCells_RowTypeDef;

/** @brief 曲名需求（GUI 写）：现在要看哪一首的名字。 */
typedef struct
{
    uint32_t Sequence;   /**< GUI 持有的递增编号，每写一次加一；结果带回它，GUI 只采信等于最新编号的结果。 */
    uint32_t Generation; /**< 曲库代次；0 表示“当前”（冷启动时快照代次还没确认）。 */
    uint16_t Index;      /**< 播放列表位置。 */
    bool Wanted;         /**< 为假表示没有需求（没有当前曲），前台任务不读文件。 */
} MusicCells_TitleRequestTypeDef;

/** @brief 曲名结果（前台任务写）：所答需求的曲目显示名与歌手。 */
typedef struct
{
    uint32_t RequestSequence;      /**< 所答的需求编号。 */
    uint32_t Generation;           /**< 实际代次；0 表示取不到（曲库不可用、代次不符、越界、卷没了）。 */
    char Title[CATALOG_NAME_BYTES];  /**< 代次非 0 时为曲目显示名；否则空串。 */
    char Artist[CATALOG_NAME_BYTES]; /**< 代次非 0 时为歌手（取不到为空串）；否则空串。 */
} MusicCells_TitleResultTypeDef;

/** @brief Queue 窗口需求（GUI 写）：现在要看从哪一首起的一窗；只留起点。 */
typedef struct
{
    uint16_t Index; /**< 窗口在播放列表上的起点。 */
    bool Valid;     /**< 为假表示 GUI 还没有写过需求（格子的初值）。 */
} MusicCells_WindowRequestTypeDef;

/** @brief Queue 窗口结果（前台任务写）：整份窗口，GUI 只见整窗。 */
typedef struct
{
    bool Published;                /**< 为假表示前台任务还没有发布过（格子的初值），GUI 忽略。 */
    uint32_t Generation;           /**< 实际代次；0 表示空窗（曲库不可用、代次变了、卷没了）。 */
    uint16_t Index;                /**< 窗口起点；Rows[0] 对应列表第 Index 首。 */
    uint16_t Length;               /**< 本窗实际条数，GUI 按此构造行。 */
    MusicCells_RowTypeDef Rows[MUSIC_WINDOW_MAX_ENTRIES]; /**< 前 Length 行有效。 */
} MusicCells_WindowTypeDef;

/* 四个格子；`music_cells_boot_init()` 之后才可使用。 */
extern UiCellTypeDef music_cell_title_request;
extern UiCellTypeDef music_cell_title_result;
extern UiCellTypeDef music_cell_window_request;
extern UiCellTypeDef music_cell_window_result;

/* Boot 任务调用：完整写入四个格子的存储（NOLOAD 段不清零）并初始化。需求格的叫醒函数是 `foreground_task_wake`，结果格传空。 */
void music_cells_boot_init(void);

#endif /* MUSIC_CELLS_H */
