/**
 * @file music_cells.c
 * @brief 音乐应用的四个界面数据格子：存储放 AXI SRAM 新段，Boot 里完整写入并初始化。
 */

#include "APP/apps/music/music_cells.h"

#include <string.h>

#include "APP/tasks/foreground/foreground_task.h"

/*
 * 约 8 KiB 以内进格子（ARC-25 的初值）：窗口格约 6 KiB，超过门槛就要改成两块缓冲，编译期拦住。
 * 四个存储都在 NOLOAD 的 .app_axi_ram：不自动清零，music_cells_boot_init() 在第一次使用前整块写入（HWD-2）。
 */
_Static_assert(sizeof(MusicCells_WindowTypeDef) <= 8192U,
               "the window cell must stay within the 8 KiB cell threshold (ARC-25); larger data needs two buffers");
_Static_assert(sizeof(MusicCells_TitleResultTypeDef) <= 1024U, "the title result cell is about 0.5 KiB");

static MusicCells_TitleRequestTypeDef music_title_request_storage __attribute__((section(MUSIC_AXI_SECTION), aligned(4)));
static MusicCells_TitleResultTypeDef music_title_result_storage __attribute__((section(MUSIC_AXI_SECTION), aligned(4)));
static MusicCells_WindowRequestTypeDef music_window_request_storage __attribute__((section(MUSIC_AXI_SECTION), aligned(4)));
static MusicCells_WindowTypeDef music_window_result_storage __attribute__((section(MUSIC_AXI_SECTION), aligned(4)));

UiCellTypeDef music_cell_title_request;
UiCellTypeDef music_cell_title_result;
UiCellTypeDef music_cell_window_request;
UiCellTypeDef music_cell_window_result;

/**
 * @brief Boot 任务调用一次：整块写入四个存储（NOLOAD 段不清零，初值全零 = 没有需求、没有结果），再初始化格子。
 * @note  在创建 GUI 任务和前台任务之前调用（注册表的 `boot_init`）。需求格的叫醒函数是 `foreground_task_wake`：此刻前台任务还没
 *        创建，所以传函数而不是任务句柄；它在任务绑定句柄之前是空操作。结果格不叫醒 GUI（GUI 最慢 16 ms 看一次）。
 *        只做内存写入和一次静态互斥量创建，不碰文件接口，栈很浅（Boot 栈只有 128 word）。
 */
void music_cells_boot_init(void)
{
    (void)memset(&music_title_request_storage, 0, sizeof(music_title_request_storage));
    (void)memset(&music_title_result_storage, 0, sizeof(music_title_result_storage));
    (void)memset(&music_window_request_storage, 0, sizeof(music_window_request_storage));
    (void)memset(&music_window_result_storage, 0, sizeof(music_window_result_storage));

    ui_cell_init(&music_cell_title_request, &music_title_request_storage, sizeof(music_title_request_storage), foreground_task_wake);
    ui_cell_init(&music_cell_title_result, &music_title_result_storage, sizeof(music_title_result_storage), NULL);
    ui_cell_init(&music_cell_window_request, &music_window_request_storage, sizeof(music_window_request_storage), foreground_task_wake);
    ui_cell_init(&music_cell_window_result, &music_window_result_storage, sizeof(music_window_result_storage), NULL);
}
