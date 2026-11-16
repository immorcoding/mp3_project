/**
 ******************************************************************************
 * @file    test_music_display_stats_release.c
 * @brief   显示延迟汇总发布构建（`-DNDEBUG`）的主机测试：整段编译掉，调用点的内联空函数什么都不记、什么都不投（DIAG-6、#218）。
 ******************************************************************************
 */

#include <assert.h>
#include <stdio.h>

#include "lock_env_stub.h"

#include "APP/apps/music/music_display_stats.h"

/**
 * @brief 主机测试入口：所有记样本的入口都是空的，汇总一行也不投。
 * @return 0 表示通过。
 */
int main(void)
{
    lock_env_log_reset();
    music_display_stats_note_title(150U);
    music_display_stats_note_window(300U);
    music_display_stats_set_clear_origin(1000U);
    music_display_stats_note_clear(1095U);
    music_display_stats_poll(10000U);
    music_display_stats_poll(60000U);

    assert(MUSIC_DISPLAY_STATS_ENABLE == 0U);
    assert(lock_env_log_count() == 0U);
    puts("music_display_stats_release: OK");
    return 0;
}
