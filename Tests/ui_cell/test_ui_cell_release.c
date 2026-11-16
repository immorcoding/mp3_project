/**
 ******************************************************************************
 * @file    test_ui_cell_release.c
 * @brief   ui_cell 发布构建（`-DNDEBUG`）的主机测试：持锁统计整段编译掉，读写行为不变（DIAG-6、#218）。
 *
 * @details
 *          `ui_cell` 源码单独带 `-DNDEBUG` 编译（CMake 的 OBJECT 库，与固件 Release 一致），本文件与桩仍带断言。
 *          只经 `ui_cell.h` 的公开函数观察：统计全为 0、汇总行一行都不投，读写、序号、不等入口照常。
 ******************************************************************************
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "freertos_stub.h"
#include "lock_env_stub.h"

#include "APP/ui_cell/ui_cell.h"

/** @brief 发布构建里读写照常，统计全为 0，汇总行一行也不投。 */
static void test_release_build_has_no_lock_statistics(void)
{
    static uint8_t storage[16];
    UiCellTypeDef cell;
    const uint8_t value[16] = {1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 9U, 10U, 11U, 12U, 13U, 14U, 15U, 16U};
    uint8_t copy[16];
    uint32_t seen = 0U;
    UiCell_StatsTypeDef stats;

    freertos_stub_reset();
    lock_env_counter_reset();
    lock_env_log_reset();
    ui_cell_init(&cell, storage, sizeof(storage), NULL);

    ui_cell_write(&cell, value);
    assert(ui_cell_try_write(&cell, value));
    assert(ui_cell_read_if_changed(&cell, copy, &seen));
    assert(memcmp(copy, value, sizeof(value)) == 0);

    ui_cell_stats_get(&stats);
    assert(stats.HoldMaxUs == 0U);
    assert(stats.Acquisitions == 0U);
    assert(stats.GuiTries == 0U);
    assert(stats.GuiBusy == 0U);

    ui_cell_stats_poll(10000U);
    ui_cell_stats_poll(60000U);
    assert(lock_env_log_count() == 0U);
}

/** @brief 主机测试入口。 @return 0 表示全部通过。 */
int main(void)
{
    test_release_build_has_no_lock_statistics();
    puts("ui_cell_release: OK");
    return 0;
}
