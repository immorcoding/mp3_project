/**
 ******************************************************************************
 * @file    background_flash_diag.c
 * @brief   Flash 失败诊断的日志片段实现（见 background_flash_diag.h）。
 ******************************************************************************
 */

#include "APP/tasks/background/background_flash_diag.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* 失败次数在日志里最多写到 999，免得一个数字撑长整行（日志行不超过 127 字符）。 */
#define BACKGROUND_FLASH_DIAG_COUNT_MAX 999U

/**
 * @brief  FTL 失败分类的短名，写进日志。
 * @param[in] failure 分类；越界值落到 `OTHER`。
 * @return 静态字符串（见 `background_flash_diag.h`）。
 */
const char *background_flash_diag_failure_name(Platform_Flash_FailureTypeDef failure)
{
    static const char *const names[] = {
        "none", "INVALID", "NOT_READY", "NO_SPACE", "IO_ERROR", "CORRUPT", "INCOMPAT", "INCOMPL", "UNFORMAT", "OTHER",
    };

    if ((unsigned)failure >= (sizeof(names) / sizeof(names[0])))
    {
        return "OTHER";
    }
    return names[(unsigned)failure];
}

/**
 * @brief  失败时 FTL 请求类别的短名，写进日志。
 * @param[in] operation 请求类别；越界值落到 `-`。
 * @return 静态字符串（见 `background_flash_diag.h`）。
 */
const char *background_flash_diag_operation_name(Platform_Flash_FailedOperationTypeDef operation)
{
    static const char *const names[] = {"-", "open", "format", "read", "write", "sync", "reclaim"};

    if ((unsigned)operation >= (sizeof(names) / sizeof(names[0])))
    {
        return "-";
    }
    return names[(unsigned)operation];
}

/**
 * @brief  FTL 原始访问结果的短名。
 * @param[in] raw 0 成功、1 忙、2 错误、3 超时；别的值（含读不到时的 `UINT32_MAX`）写问号。
 * @return 静态字符串。
 */
static const char *background_flash_diag_raw_name(uint32_t raw)
{
    static const char *const names[] = {"ok", "busy", "error", "timeout"};

    return (raw < (sizeof(names) / sizeof(names[0]))) ? names[raw] : "?";
}

/**
 * @brief  往缓冲末尾追加一段格式化文字；放不下就截断，缓冲始终以 NUL 结尾。
 * @param[in,out] buffer 缓冲，已含前面的文字（以 NUL 结尾）。
 * @param[in] size 缓冲容量，含 NUL。
 * @param[in] format printf 格式。
 */
static __attribute__((format(printf, 3, 4))) void background_flash_diag_cat(char *buffer, size_t size, const char *format, ...)
{
    const size_t used = strlen(buffer);
    va_list args;

    if (used < size)
    {
        va_start(args, format);
        (void)vsnprintf(&buffer[used], size - used, format, args);
        va_end(args);
    }
}

/**
 * @brief  读 Platform 的失败诊断，格式化成日志片段并追加到缓冲末尾（格式见 `background_flash_diag.h` 文件头）。
 * @param[in,out] buffer 缓冲，已含前面的文字（以 NUL 结尾）；空指针或没有 NUL 结尾（已满）时什么也不做。
 * @param[in] size 缓冲容量，含结尾 NUL；放不下时截断，结果始终以 NUL 结尾。
 * @param[in] extra 调用者的额外文字，插在必有部分与 `rej`、`hw` 之间，要自带开头的空格；可为 NULL。
 * @note   Platform 读不到（FTL 还没绑定）时追加 ` diag=n/a`，同样带上额外文字。
 */
void background_flash_diag_append(char *buffer, size_t size, const char *extra)
{
    Platform_Flash_VolumeDiagnosticsTypeDef local;
    unsigned long count;

    if ((buffer == NULL) || (size == 0U) || (strlen(buffer) >= size))
    {
        return;
    }

    if (Platform_Flash_GetVolumeDiagnostics(&local) != PLATFORM_OK)
    {
        background_flash_diag_cat(buffer, size, " diag=n/a");
        if (extra != NULL)
        {
            background_flash_diag_cat(buffer, size, "%s", extra);
        }
        return;
    }

    count = (local.FailureCount > BACKGROUND_FLASH_DIAG_COUNT_MAX) ? BACKGROUND_FLASH_DIAG_COUNT_MAX : (unsigned long)local.FailureCount;
    if (local.LastFailure == PLATFORM_FLASH_FAILURE_NONE)
    {
        background_flash_diag_cat(buffer, size, " ftl=none");
    }
    else
    {
        background_flash_diag_cat(buffer,
                                  size,
                                  " ftl=%s op=%s raw=%s",
                                  background_flash_diag_failure_name(local.LastFailure),
                                  background_flash_diag_operation_name(local.LastFailureOperation),
                                  background_flash_diag_raw_name(local.LastFailureRawStatus));
    }
    background_flash_diag_cat(buffer, size, " n=%lu free=%lu", count, (unsigned long)local.FreeBlocks);
    if ((local.ValidGroups != 0U) || (local.StaleBlocks != 0U))
    {
        background_flash_diag_cat(buffer, size, " valid=%lu stale=%lu", (unsigned long)local.ValidGroups, (unsigned long)local.StaleBlocks);
    }
    if (extra != NULL)
    {
        background_flash_diag_cat(buffer, size, "%s", extra);
    }
    if (local.LastRejection != PLATFORM_FLASH_FAILURE_NONE)
    {
        background_flash_diag_cat(buffer, size, " rej=%s", background_flash_diag_failure_name(local.LastRejection));
    }
    if ((local.LastFailureDeviceError != 0U) || (local.LastFailurePortStatus != 0U))
    {
        background_flash_diag_cat(buffer,
                                  size,
                                  " hw=%lx/%lx",
                                  (unsigned long)local.LastFailureDeviceError,
                                  (unsigned long)local.LastFailurePortStatus);
    }
}
