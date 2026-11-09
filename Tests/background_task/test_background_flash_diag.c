/**
 ******************************************************************************
 * @file    test_background_flash_diag.c
 * @brief   Flash 失败诊断日志片段的主机测试（#220，第 6 次上板追加）：每种 FTL 失败的名字、没有失败过、读不到诊断、
 *          可选段的省略与顺序、失败次数封顶、缓冲放不下时的截断。
 *
 * @details
 *          链接真实的 `background_flash_diag.c`；`Platform_Flash_GetVolumeDiagnostics` 在本文件里用可设置的假实现替换，
 *          可以直接给出 Platform 记下的最近一次 FTL 失败、请求类别、原始访问结果、芯片后端快照和块计数。
 *          期望的文字来自 `background_flash_diag.h` 文件头约定的格式（上板的人读日志也靠它），不用代码里的表达式重算。
 *          主机测试证明格式与边界，不证明真实 FTL 在哪一步记下哪个结果（Platform 的记录靠上板的“压到底复现”读出）。
 ******************************************************************************
 */

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "APP/tasks/background/background_flash_diag.h"
#include "Platform/flash/platform_flash.h"

/** @brief 假实现给出的诊断快照。 */
static Platform_Flash_VolumeDiagnosticsTypeDef g_diag;

/** @brief 假实现是否读失败（FTL 还没绑定）。 */
static bool g_diag_fails;

/**
 * @brief  假的 Platform 卷诊断：复制设置好的快照，或按要求失败。
 * @param[out] diagnostics 接收快照。
 * @return 要求失败时 PLATFORM_FLASH_ERROR，否则 PLATFORM_OK。
 */
Platform_StatusTypeDef Platform_Flash_GetVolumeDiagnostics(Platform_Flash_VolumeDiagnosticsTypeDef *diagnostics)
{
    if (g_diag_fails)
    {
        return PLATFORM_FLASH_ERROR;
    }
    *diagnostics = g_diag;
    return PLATFORM_OK;
}

/** @brief 开始一个场景：快照清零，读取成功。 */
static void begin_scenario(void)
{
    memset(&g_diag, 0, sizeof(g_diag));
    g_diag_fails = false;
}

/**
 * @brief  用 prefix 起头、追加诊断片段，返回追加后的缓冲内容。
 * @param[out] buffer 缓冲。
 * @param[in] size 缓冲容量。
 * @param[in] prefix 起头的文字。
 * @param[in] extra 额外文字，可为 NULL。
 * @return buffer。
 */
static const char *append_to(char *buffer, size_t size, const char *prefix, const char *extra)
{
    (void)snprintf(buffer, size, "%s", prefix);
    background_flash_diag_append(buffer, size, extra);
    return buffer;
}

/** @brief 没有失败过：`ftl=none`，不带请求类别和原始结果；次数与空闲块照写。 */
static void test_no_failure_recorded(void)
{
    char text[128];

    begin_scenario();
    g_diag.FreeBlocks = 554U;
    assert(strcmp(append_to(text, sizeof(text), "x", NULL), "x ftl=none n=0 free=554") == 0);
}

/** @brief 第 6 次上板的现象：写时失败，FTL 因此不就绪，空闲块 3，有效/失效块数读作 0（所以省略）。 */
static void test_write_failure_after_fault(void)
{
    char text[128];

    begin_scenario();
    g_diag.LastFailure = PLATFORM_FLASH_FAILURE_CORRUPT;
    g_diag.LastFailureOperation = PLATFORM_FLASH_FAILED_OP_WRITE;
    g_diag.LastFailureRawStatus = 0U;
    g_diag.FailureCount = 1U;
    g_diag.FreeBlocks = 3U;
    assert(strcmp(append_to(text, sizeof(text), "stopped at=write status=1", NULL),
                  "stopped at=write status=1 ftl=CORRUPT op=write raw=ok n=1 free=3") == 0);
}

/** @brief FTL 仍就绪（有效/失效块数非零）时带上它们；额外文字在它们之后、`rej`、`hw` 之前。 */
static void test_optional_parts_and_their_order(void)
{
    char text[128];

    begin_scenario();
    g_diag.LastFailure = PLATFORM_FLASH_FAILURE_IO_ERROR;
    g_diag.LastFailureOperation = PLATFORM_FLASH_FAILED_OP_RECLAIM;
    g_diag.LastFailureRawStatus = 3U;
    g_diag.FailureCount = 7U;
    g_diag.FreeBlocks = 2U;
    g_diag.ValidGroups = 980U;
    g_diag.StaleBlocks = 12U;
    g_diag.LastRejection = PLATFORM_FLASH_FAILURE_NOT_READY;
    g_diag.LastFailureDeviceError = 0x2U;
    g_diag.LastFailurePortStatus = 0xabU;
    assert(strcmp(append_to(text, sizeof(text), "p", " pre=3/980/12 req=65536"),
                  "p ftl=IO_ERROR op=reclaim raw=timeout n=7 free=2 valid=980 stale=12 pre=3/980/12 req=65536 rej=NOT_READY hw=2/ab") == 0);
}

/** @brief 只有失效块数非零也算就绪，两个都写出来；只有端口状态非零也写 `hw`。 */
static void test_zero_checks_use_either_value(void)
{
    char text[128];

    begin_scenario();
    g_diag.LastFailure = PLATFORM_FLASH_FAILURE_NO_SPACE;
    g_diag.LastFailureOperation = PLATFORM_FLASH_FAILED_OP_WRITE;
    g_diag.FailureCount = 1U;
    g_diag.StaleBlocks = 5U;
    g_diag.LastFailurePortStatus = 1U;
    assert(strcmp(append_to(text, sizeof(text), "", NULL),
                  " ftl=NO_SPACE op=write raw=ok n=1 free=0 valid=0 stale=5 hw=0/1") == 0);
}

/** @brief 每种分类、每种请求类别、每种原始结果都有短名；越界值不越界读，落到兜底名。 */
static void test_every_name(void)
{
    static const char *const failures[] = {"none", "INVALID", "NOT_READY", "NO_SPACE", "IO_ERROR",
                                           "CORRUPT", "INCOMPAT", "INCOMPL", "UNFORMAT", "OTHER"};
    static const char *const operations[] = {"-", "open", "format", "read", "write", "sync", "reclaim"};
    char text[128];

    for (unsigned index = 0U; index < (sizeof(failures) / sizeof(failures[0])); index++)
    {
        assert(strcmp(background_flash_diag_failure_name((Platform_Flash_FailureTypeDef)index), failures[index]) == 0);
    }
    for (unsigned index = 0U; index < (sizeof(operations) / sizeof(operations[0])); index++)
    {
        assert(strcmp(background_flash_diag_operation_name((Platform_Flash_FailedOperationTypeDef)index), operations[index]) == 0);
    }
    assert(strcmp(background_flash_diag_failure_name((Platform_Flash_FailureTypeDef)99), "OTHER") == 0);
    assert(strcmp(background_flash_diag_operation_name((Platform_Flash_FailedOperationTypeDef)99), "-") == 0);

    begin_scenario();
    g_diag.LastFailure = PLATFORM_FLASH_FAILURE_INVALID_PARAM;
    g_diag.FailureCount = 1U;
    g_diag.LastFailureRawStatus = 1U;
    assert(strstr(append_to(text, sizeof(text), "", NULL), "raw=busy") != NULL);
    g_diag.LastFailureRawStatus = 2U;
    assert(strstr(append_to(text, sizeof(text), "", NULL), "raw=error") != NULL);
    g_diag.LastFailureRawStatus = UINT32_MAX; /* Platform 读不到 FTL 诊断时记的值。 */
    assert(strstr(append_to(text, sizeof(text), "", NULL), "raw=?") != NULL);
}

/** @brief 失败次数在日志里最多写到 999，一个数字不撑长整行。 */
static void test_failure_count_is_capped(void)
{
    char text[128];

    begin_scenario();
    g_diag.LastFailure = PLATFORM_FLASH_FAILURE_CORRUPT;
    g_diag.FailureCount = 4000000000U;
    assert(strstr(append_to(text, sizeof(text), "", NULL), " n=999 ") != NULL);
    g_diag.FailureCount = 999U;
    assert(strstr(append_to(text, sizeof(text), "", NULL), " n=999 ") != NULL);
    g_diag.FailureCount = 1000U;
    assert(strstr(append_to(text, sizeof(text), "", NULL), " n=999 ") != NULL);
}

/** @brief 读不到诊断（FTL 还没绑定）：写 `diag=n/a`，额外文字照样带上，不假装有数据。 */
static void test_diagnostics_unavailable(void)
{
    char text[128];

    begin_scenario();
    g_diag_fails = true;
    assert(strcmp(append_to(text, sizeof(text), "y", NULL), "y diag=n/a") == 0);
    assert(strcmp(append_to(text, sizeof(text), "y", " req=1 file=2"), "y diag=n/a req=1 file=2") == 0);
}

/** @brief 缓冲再小也不越界、始终以 NUL 结尾：从 1 字节到比最长内容大，逐个容量检查，并且结果总是完整内容的前缀。 */
static void test_truncation_is_safe_at_every_size(void)
{
    char reference[256];
    char guarded[130];

    begin_scenario();
    g_diag.LastFailure = PLATFORM_FLASH_FAILURE_INCOMPLETE;
    g_diag.LastFailureOperation = PLATFORM_FLASH_FAILED_OP_RECLAIM;
    g_diag.LastFailureRawStatus = 3U;
    g_diag.FailureCount = 12345U;
    g_diag.FreeBlocks = 4094U;
    g_diag.ValidGroups = 4094U;
    g_diag.StaleBlocks = 4094U;
    g_diag.LastRejection = PLATFORM_FLASH_FAILURE_UNFORMATTED;
    g_diag.LastFailureDeviceError = 0xffffffffU;
    g_diag.LastFailurePortStatus = 0xffffffffU;
    (void)append_to(reference, sizeof(reference), "stopped at=write status=1", " pre=4094/4094/4094 req=65536 file=16777216");
    assert(strlen(reference) > 127U);

    for (size_t size = 1U; size <= 128U; size++)
    {
        memset(guarded, 0x5a, sizeof(guarded));
        (void)snprintf(guarded, size, "%s", "stopped at=write status=1");
        background_flash_diag_append(guarded, size, " pre=4094/4094/4094 req=65536 file=16777216");
        assert(guarded[size - 1U] == '\0' || strlen(guarded) < size);
        assert(strlen(guarded) < size);
        assert(strncmp(guarded, reference, strlen(guarded)) == 0);
        for (size_t tail = size; tail < sizeof(guarded); tail++)
        {
            assert((unsigned char)guarded[tail] == 0x5aU); /* 缓冲之外一个字节也没写 */
        }
    }
}

/** @brief 参数无效或缓冲已满时什么也不做，不崩。 */
static void test_invalid_arguments_are_ignored(void)
{
    char full[4] = {'a', 'b', 'c', 'd'};

    begin_scenario();
    background_flash_diag_append(NULL, 10U, NULL);
    background_flash_diag_append(full, 0U, NULL);
    background_flash_diag_append(full, sizeof(full), NULL); /* 没有 NUL 结尾：视为已满，不写 */
    assert(memcmp(full, "abcd", 4U) == 0);
}

/**
 * @brief 用例表。
 */
static const struct
{
    const char *name;
    void (*run)(void);
} cases[] = {
    {"no_failure_recorded", test_no_failure_recorded},
    {"write_failure_after_fault", test_write_failure_after_fault},
    {"optional_parts_and_order", test_optional_parts_and_their_order},
    {"zero_checks_use_either_value", test_zero_checks_use_either_value},
    {"every_name", test_every_name},
    {"failure_count_is_capped", test_failure_count_is_capped},
    {"diagnostics_unavailable", test_diagnostics_unavailable},
    {"truncation_is_safe", test_truncation_is_safe_at_every_size},
    {"invalid_arguments", test_invalid_arguments_are_ignored},
};

/**
 * @brief 运行命令行指定的用例。
 * @param[in] argc 参数个数。
 * @param[in] argv 参数。
 * @return 0 表示通过（失败由 assert 中止）。
 */
int main(int argc, char **argv)
{
    if (argc != 2)
    {
        (void)fprintf(stderr, "usage: %s <case>\n", argv[0]);
        return 2;
    }

    for (size_t index = 0U; index < sizeof(cases) / sizeof(cases[0]); index++)
    {
        if (strcmp(cases[index].name, argv[1]) == 0)
        {
            cases[index].run();
            (void)printf("background_flash_diag %s: OK\n", argv[1]);
            return 0;
        }
    }

    (void)fprintf(stderr, "unknown case: %s\n", argv[1]);
    return 2;
}
