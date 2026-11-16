/**
 ******************************************************************************
 * @file    test_volume_flash.c
 * @brief   Flash 启动任务的启动流程主机测试：挂载成功才发卷就绪，无文件系统按 AUTO_FORMAT 格式化，
 *          任何失败都不发事件、只记日志；发布的是 Flash 的字，不动 SD 的字。
 *
 * @details
 *          链接真实的 `volume_flash.c` 与 `volume_event.c`、共用 FreeRTOS 桩、Service Log 替身；
 *          Filesystem Service 的三个 Flash 入口（InitFlash、MountFlash、FormatAndMountFlash）用脚本化的假实现替换。
 *          任务创建、栈水位和自删在 `volume_flash_task.c`（只含 FreeRTOS 任务 API，不进主机测试，上板核对）。
 *          主机测试是单线程的：证明这个流程里每条路径发不发事件，不证明与卷任务、插拔卡的并发（R3.5 上板）。
 ******************************************************************************
 */

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "freertos_stub.h"
#include "lock_env_stub.h"

#include "APP/tasks/volume/flash/volume_flash.h"
#include "APP/tasks/volume/volume_event.h"
#include "Service/filesystem/filesystem_service.h"
#include "Service/log/log_service.h"

/** @brief 假 Filesystem Service 的脚本与观察值。 */
typedef struct
{
    Service_StatusTypeDef init_status;
    Service_StatusTypeDef mount_status;
    Service_StatusTypeDef format_status;
    uint32_t mount_advance_ticks;    /**< MountFlash 在假时间里“花”了多少 tick。 */
    char calls[16];                  /**< 调用顺序：I 初始化 M 挂载 F 格式化并挂载。 */
    uint32_t calls_count;
    uint32_t sequence_seen_at_format;
} Fake;

static Fake fake;

/**
 * @brief 记一次调用的顺序标记。
 * @param[in] mark 标记字符。
 */
static void note_call(char mark)
{
    assert(fake.calls_count + 1U < sizeof(fake.calls));
    fake.calls[fake.calls_count++] = mark;
    fake.calls[fake.calls_count] = '\0';
}

/**
 * @brief 假 InitFlash。
 * @return 脚本里的结果。
 */
Service_StatusTypeDef Service_Filesystem_InitFlash(void)
{
    note_call('I');
    return fake.init_status;
}

/**
 * @brief 假 MountFlash：让假时间前进脚本里的 tick 数（真实实现要 3.3 至 8.3 s）。
 * @return 脚本里的结果。
 */
Service_StatusTypeDef Service_Filesystem_MountFlash(void)
{
    note_call('M');
    freertos_stub_set_tick(xTaskGetTickCount() + fake.mount_advance_ticks);
    return fake.mount_status;
}

/**
 * @brief 假 FormatAndMountFlash：记下此刻 Flash 的卷序号（还没发卷就绪时应为 0）。
 * @return 脚本里的结果。
 */
Service_StatusTypeDef Service_Filesystem_FormatAndMountFlash(void)
{
    note_call('F');
    fake.sequence_seen_at_format = volume_status(VOLUME_ID_FLASH).Sequence;
    return fake.format_status;
}

/**
 * @brief 开始一个场景：清空脚本与日志，桩复位。
 */
static void begin_scenario(void)
{
    memset(&fake, 0, sizeof(fake));
    fake.init_status = SERVICE_OK;
    fake.mount_status = SERVICE_OK;
    fake.format_status = SERVICE_OK;
    freertos_stub_reset();
    lock_env_log_reset();
    freertos_stub_set_tick(500U);
}

/**
 * @brief 是否有一条日志，标签是 tag 且正文包含 needle。
 * @param[in] tag 标签。
 * @param[in] needle 子串。
 * @return 有为真。
 */
static bool has_log(const char *tag, const char *needle)
{
    for (uint32_t index = 0U; index < lock_env_log_count(); index++)
    {
        if ((strcmp(lock_env_log_tag(index), tag) == 0) && (strstr(lock_env_log_text(index), needle) != NULL))
        {
            return true;
        }
    }
    return false;
}

/**
 * @brief 挂载成功：发 Flash 的卷就绪（序号 0 到 1），日志写明走的是挂载路径和耗时；SD 的字不动。
 */
static void test_successful_mount_publishes_flash_ready(void)
{
    const uint32_t sd_sequence = volume_status(VOLUME_ID_SD).Sequence;

    begin_scenario();
    fake.mount_advance_ticks = 3300U;

    assert(volume_flash_boot());

    assert(strcmp(fake.calls, "IM") == 0);
    assert(volume_status(VOLUME_ID_FLASH).Mounted);
    assert(volume_status(VOLUME_ID_FLASH).Sequence == 1U);
    assert(volume_status(VOLUME_ID_SD).Sequence == sd_sequence);
    assert(has_log("FLASH", "Filesystem mounted."));
    assert(has_log("VOLUME", "flash boot start"));
    assert(has_log("VOLUME", "flash ready path=mount seq=0->1 ms=3300"));
}

/**
 * @brief 没有文件系统，AUTO_FORMAT 打开：格式化并挂载成功后才发卷就绪；格式化那一刻卷序号还是原值。
 */
static void test_no_filesystem_formats_then_publishes_ready(void)
{
    const uint32_t before = volume_status(VOLUME_ID_FLASH).Sequence;

    begin_scenario();
    fake.mount_status = SERVICE_NO_FILESYSTEM;

    assert(volume_flash_boot());

    assert(strcmp(fake.calls, "IMF") == 0);
    assert(fake.sequence_seen_at_format == before);
    assert(volume_status(VOLUME_ID_FLASH).Sequence == before + 1U);
    assert(volume_status(VOLUME_ID_FLASH).Mounted);
    assert(has_log("FLASH", "Formatting Flash volume..."));
    assert(has_log("VOLUME", "flash ready path=format"));
}

/**
 * @brief 格式化失败：不发事件，只记日志。
 */
static void test_format_failure_publishes_nothing(void)
{
    const uint32_t before = volume_status(VOLUME_ID_FLASH).Sequence;

    begin_scenario();
    fake.mount_status = SERVICE_NO_FILESYSTEM;
    fake.format_status = SERVICE_ERROR;

    assert(!volume_flash_boot());

    assert(strcmp(fake.calls, "IMF") == 0);
    assert(volume_status(VOLUME_ID_FLASH).Sequence == before);
    assert(has_log("FLASH", "Flash volume format failed."));
    assert(has_log("VOLUME", "flash boot failed path=format"));
}

/**
 * @brief 挂载因为别的原因失败（损坏、不兼容、忙、超时）：不自动格式化，不发事件，只记日志。
 */
static void test_other_mount_failures_do_not_format_and_publish_nothing(void)
{
    static const Service_StatusTypeDef failures[] = {SERVICE_ERROR, SERVICE_BUSY, SERVICE_TIMEOUT, SERVICE_NOT_READY};

    for (size_t index = 0U; index < sizeof(failures) / sizeof(failures[0]); index++)
    {
        const uint32_t before = volume_status(VOLUME_ID_FLASH).Sequence;

        begin_scenario();
        fake.mount_status = failures[index];

        assert(!volume_flash_boot());

        assert(strcmp(fake.calls, "IM") == 0);
        assert(volume_status(VOLUME_ID_FLASH).Sequence == before);
        assert(has_log("FLASH", "Volume unavailable; no automatic format."));
        assert(has_log("VOLUME", "flash boot failed path=mount"));
    }
}

/**
 * @brief 执行器初始化失败：不挂载，不发事件。
 */
static void test_init_failure_stops_before_mounting(void)
{
    const uint32_t before = volume_status(VOLUME_ID_FLASH).Sequence;

    begin_scenario();
    fake.init_status = SERVICE_NOT_READY;

    assert(!volume_flash_boot());

    assert(strcmp(fake.calls, "I") == 0);
    assert(volume_status(VOLUME_ID_FLASH).Sequence == before);
    assert(has_log("FLASH", "Flash executor initialization failed."));
    assert(has_log("VOLUME", "flash boot failed path=init"));
}

/**
 * @brief 所有日志行都放得进 Service Log 单条正文（127 字符），耗时很长（数万秒）时也一样。
 */
static void test_log_lines_fit_the_service_log_message_size(void)
{
    begin_scenario();
    fake.mount_advance_ticks = 0xFFFFFFF0U;
    fake.mount_status = SERVICE_NO_FILESYSTEM;

    (void)volume_flash_boot();

    assert(lock_env_log_longest_text() <= 127U);
}

/**
 * @brief 依次运行用例。
 * @return 全部通过时为 0，断言失败时进程中止。
 */
int main(void)
{
    test_successful_mount_publishes_flash_ready();
    test_no_filesystem_formats_then_publishes_ready();
    test_format_failure_publishes_nothing();
    test_other_mount_failures_do_not_format_and_publish_nothing();
    test_init_failure_stops_before_mounting();
    test_log_lines_fit_the_service_log_message_size();
    puts("volume_flash: all passed");
    return 0;
}
