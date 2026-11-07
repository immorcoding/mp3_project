/**
  ******************************************************************************
  * @file    app_table_boot.c
  * @brief   注册表的 Boot 遍历：逐行调用 `boot_init`，空字段跳过。
  *
  * @details
  *          独立成文件，是为了让主机测试能用任意表验证“顺序、空字段”，而不必链接真实的应用。
  ******************************************************************************
  */

#include "APP/apps/app_table.h"

/**
 * @brief 按表的顺序逐行调用 `boot_init`。
 * @param[in] rows 注册表行；`count` 为 0 时可以是 NULL。
 * @param[in] count 行数。
 * @note  Boot 任务调用，单线程、任务都还没创建；`boot_init` 不得阻塞、不得碰文件接口。
 */
void app_table_boot_init_rows(const AppTable_EntryTypeDef *rows, size_t count)
{
    for (size_t index = 0U; (rows != NULL) && (index < count); index++)
    {
        if (rows[index].boot_init != NULL)
        {
            rows[index].boot_init();
        }
    }
}
