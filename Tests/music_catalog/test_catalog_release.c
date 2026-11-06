/**
 * @file test_catalog_release.c
 * @brief 发布构建（曲库源码带 -DNDEBUG）的曲库主机测试：持锁统计整段编译掉，发布规则不受开关影响。
 * @note  链接带 -DNDEBUG 编译的真实 catalog.c、catalog_sheet.c 与 catalog_name.c（本文件自己仍带断言）；Filesystem Service、Log Service、DWT Adapter 与卷状态函数
 *        由本文件的假实现替换，曲库短锁走共用 FreeRTOS 桩。目录扫描只靠“Music/ 打不开按空库发布”这一条路径走通发布。
 */

#include "APP/apps/music/catalog/catalog.h"
#include "APP/tasks/volume/volume_event.h"

#include "Adapters/cortex/cycle_counter/cortex_m7_cycle_counter_adapter.h"
#include "Service/filesystem/filesystem_directory.h"
#include "Service/log/log_service.h"

#include "freertos_stub.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static Volume_StatusTypeDef g_volume = {true, 3U};
static uint32_t g_log_count;
static uint32_t g_log_catlock_count;

/**
 * @brief 假周期计数器读数：恒 0。
 * @return 0。
 */
uint32_t CortexM7CycleCounter_Read(void)
{
    return 0U;
}

/**
 * @brief 假核心频率：1 MHz。
 * @return 1000000。
 */
uint32_t CortexM7CycleCounter_GetFrequencyHz(void)
{
    return 1000000U;
}

/**
 * @brief 假日志投递：只数条数和 CATLOCK 标签的条数。
 * @param[in] level 日志等级。
 * @param[in] tag 标签。
 * @param[in] text 文本。
 * @return 恒为 SERVICE_OK。
 */
Service_StatusTypeDef Service_Log_Post(Service_Log_LevelTypeDef level, const char *tag, const char *text)
{
    (void)level;
    (void)text;
    g_log_count++;
    if (strcmp(tag, "CATLOCK") == 0)
    {
        g_log_catlock_count++;
    }
    return SERVICE_OK;
}

/**
 * @brief 假卷状态函数。
 * @param[in] volume 卷，须为 SD。
 * @return 测试设定的状态。
 */
Volume_StatusTypeDef volume_status(Volume_IdTypeDef volume)
{
    assert(volume == VOLUME_ID_SD);
    return g_volume;
}

/**
 * @brief 假打开目录：恒报一般错误，曲库按“根目录打不开”发布空库。
 * @param[in] volume 卷。
 * @param[in] path 路径。
 * @param[out] directory 目录句柄。
 * @return 恒为 SERVICE_ERROR。
 */
Service_StatusTypeDef Service_Filesystem_OpenDirectory(Service_Filesystem_VolumeTypeDef volume,
                                                       const char *path,
                                                       Service_Filesystem_DirectoryHandleTypeDef *directory)
{
    (void)volume;
    (void)path;
    (void)directory;
    return SERVICE_ERROR;
}

/**
 * @brief 假读目录：本测试不会走到。
 * @param[in] directory 目录句柄。
 * @param[out] entry 目录项。
 * @return 恒为 SERVICE_ERROR。
 */
Service_StatusTypeDef Service_Filesystem_ReadDirectory(Service_Filesystem_DirectoryHandleTypeDef directory,
                                                       Service_Filesystem_DirectoryEntryTypeDef *entry)
{
    (void)directory;
    (void)entry;
    return SERVICE_ERROR;
}

/**
 * @brief 假关目录：本测试不会走到。
 * @param[in] directory 目录句柄。
 * @return 恒为 SERVICE_OK。
 */
Service_StatusTypeDef Service_Filesystem_CloseDirectory(Service_Filesystem_DirectoryHandleTypeDef directory)
{
    (void)directory;
    return SERVICE_OK;
}

/** @brief 发布构建里：读写曲库不投 CATLOCK 汇总，哪怕节拍早已过去；发布规则照常。 */
static void test_release_build_never_posts_lock_statistics(void)
{
    Catalog_TrackTypeDef track;

    freertos_stub_reset();
    catalog_boot_init();

    assert(catalog_music_init() == CATALOG_OK); /* 根目录打不开：按空库发布 */
    assert(catalog_generation() == 1U);

    freertos_stub_set_tick(100U * 1000U); /* 远超汇总节拍 */
    (void)catalog_read_track(0U, &track);
    (void)catalog_generation();
    (void)catalog_invalidate();

    assert(g_log_catlock_count == 0U);
    assert(catalog_generation() == 0U);
    assert(g_log_count >= 1U); /* 其他日志（WARN、扫描结束）照常 */

    g_volume.Sequence += 2U;
    assert(catalog_music_init() == CATALOG_OK); /* 序号变了也能在新序号下重新发布 */
    assert(catalog_generation() == 2U);
    g_volume.Sequence += 2U;
    assert(catalog_generation() == 0U); /* 取代次时序号不符就算 0 */
}

/**
 * @brief 运行全部用例。
 * @return 0 表示全部通过（失败由 assert 中止）。
 */
int main(void)
{
    test_release_build_never_posts_lock_statistics();
    puts("catalog_release: ok");
    return 0;
}
