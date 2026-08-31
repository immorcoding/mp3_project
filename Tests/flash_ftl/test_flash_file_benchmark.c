/**
 * @file test_flash_file_benchmark.c
 * @brief 真实 APP/Service/FatFs/USER/FTL 文件回归；仅替换任务、日志和故障边界。
 */
#include "APP/tasks/storage/benchmark/storage_flash_benchmark.h"
#include "APP/tasks/storage/benchmark/storage_flash_benchmark_config.h"
#include "Service/filesystem/filesystem_service.h"
#include "Service/filesystem/flash/filesystem_flash_file.h"
#include "Service/log/log_service.h"
#include "FATFS/App/fatfs.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char USERPath[4];
uint8_t retUSER;
static FATFS test_fs;
static bool owner = true;
static const char *scenario;
static unsigned removes, closes, writes;
static bool passed, failed, cleanup_failed;

/** @brief 替代任务所有权检查。 @return 用例设置的所有者状态。 */
bool filesystem_flash_transfer_is_owner(void)
{
    return owner;
}

/** @brief 提供假 tick，不测量硬件性能。 @return 每次调用递增 10 的 tick。 */
uint32_t xTaskGetTickCount(void)
{
    static uint32_t tick;
    return tick += 10U;
}

/**
 * @brief 收集 APP 基准报告。
 * @param[in] level 日志等级。
 * @param[in] tag 模块标签。
 * @param[in] text 待核对的完整日志文本。
 * @return 固定 SERVICE_OK。
 */
Service_StatusTypeDef Service_Log_Post(Service_Log_LevelTypeDef level,
                                       const char *tag,
                                       const char *text)
{
    (void)level;
    assert(!strcmp(tag, "FLASH"));
    puts(text);
    passed |= strstr(text, "passed:") != NULL;
    failed |= strstr(text, "failed at") != NULL;
    cleanup_failed |= strstr(text, "cleanup failed") != NULL;
    return SERVICE_OK;
}

Service_StatusTypeDef __real_Service_Filesystem_WriteFile(Service_Filesystem_FileTypeDef file,
                                                          const void *data,
                                                          uint32_t length,
                                                          uint32_t *transferred);
Service_StatusTypeDef __real_Service_Filesystem_ReadFile(Service_Filesystem_FileTypeDef file,
                                                         void *data,
                                                         uint32_t length,
                                                         uint32_t *transferred);
Service_StatusTypeDef __real_Service_Filesystem_SyncFile(Service_Filesystem_FileTypeDef file);
Service_StatusTypeDef __real_Service_Filesystem_CloseFile(Service_Filesystem_FileTypeDef *file);
Service_StatusTypeDef __real_Service_Filesystem_RemoveFlashFile(const char *name);

/**
 * @brief 首次写入时按场景注入短写或部分写入后报错。
 * @param[in] file 真实句柄。
 * @param[in] data 有效输入。
 * @param[in] length 请求长度。
 * @param[out] transferred 真实已写入长度。
 * @return write_error 为 ERROR，其余返回真实文件结果。
 */
Service_StatusTypeDef __wrap_Service_Filesystem_WriteFile(Service_Filesystem_FileTypeDef file,
                                                          const void *data,
                                                          uint32_t length,
                                                          uint32_t *transferred)
{
    bool inject =
        ++writes == 1U && (!strcmp(scenario, "short_write") || !strcmp(scenario, "write_error"));
    Service_StatusTypeDef status =
        __real_Service_Filesystem_WriteFile(file, data, inject ? length / 2U : length, transferred);
    return inject && !strcmp(scenario, "write_error") ? SERVICE_ERROR : status;
}

/**
 * @brief 基于真实读取注入返回错误或内容损坏。
 * @param[in] file 当前文件。
 * @param[out] data 读取及可选扰动后的内容。
 * @param[in] length 请求字节数。
 * @param[out] transferred 实际长度。
 * @return read_error 为 ERROR，其余保留真实结果。
 */
Service_StatusTypeDef __wrap_Service_Filesystem_ReadFile(Service_Filesystem_FileTypeDef file,
                                                         void *data,
                                                         uint32_t length,
                                                         uint32_t *transferred)
{
    Service_StatusTypeDef status =
        __real_Service_Filesystem_ReadFile(file, data, length, transferred);
    if (!strcmp(scenario, "corrupt") && *transferred)
    {
        ((uint8_t *)data)[0] ^= 1U;
    }
    return !strcmp(scenario, "read_error") ? SERVICE_ERROR : status;
}

/**
 * @brief 在真实同步后按用例报错。
 * @param[in] file 当前文件。
 * @return sync_error 为 ERROR，其余为真实同步结果。
 */
Service_StatusTypeDef __wrap_Service_Filesystem_SyncFile(Service_Filesystem_FileTypeDef file)
{
    Service_StatusTypeDef status = __real_Service_Filesystem_SyncFile(file);
    return !strcmp(scenario, "sync_error") ? SERVICE_ERROR : status;
}

/**
 * @brief 注入一次或持续关闭失败，检查槽和句柄不能被提前释放。
 * @param[in,out] file 失败时保留原句柄。
 * @return 注入时为 ERROR，否则执行真实关闭。
 */
Service_StatusTypeDef __wrap_Service_Filesystem_CloseFile(Service_Filesystem_FileTypeDef *file)
{
    closes++;
    if (!strcmp(scenario, "close_error") || (!strcmp(scenario, "close_once") && closes == 1U))
    {
        return SERVICE_ERROR;
    }
    return __real_Service_Filesystem_CloseFile(file);
}

/**
 * @brief 记录删除尝试并模拟无法删除。
 * @param[in] name 请求删除的根目录文件名。
 * @return delete_error 为 ERROR，其余执行真实删除。
 */
Service_StatusTypeDef __wrap_Service_Filesystem_RemoveFlashFile(const char *name)
{
    removes++;
    return !strcmp(scenario, "delete_error") ? SERVICE_ERROR
                                             : __real_Service_Filesystem_RemoveFlashFile(name);
}

/**
 * @brief 核对所有者、路径、同名保护、文件槽和旧句柄失效。
 * @note 使用真实 FatFs/FTL；任一断言失败终止测试。
 */
static void test_contract(void)
{
    Service_Filesystem_FileTypeDef file = {0}, stale, other = {0};
    uint8_t data[16] = {1, 2, 3};
    uint32_t transferred;
    owner = false;
    assert(Service_Filesystem_OpenFlashFile(
               "test.bin", SERVICE_FILESYSTEM_FILE_CREATE_NEW, &file) == SERVICE_NOT_READY);
    owner = true;
    assert(Service_Filesystem_OpenFlashFile("../bad", SERVICE_FILESYSTEM_FILE_CREATE_NEW, &file) ==
           SERVICE_INVALID_PARAM);
    assert(Service_Filesystem_OpenFlashFile("0:/bad", SERVICE_FILESYSTEM_FILE_CREATE_NEW, &file) ==
           SERVICE_INVALID_PARAM);
    assert(Service_Filesystem_OpenFlashFile(
               "test.bin", SERVICE_FILESYSTEM_FILE_CREATE_NEW, &file) == SERVICE_OK);
    assert(Service_Filesystem_OpenFlashFile(
               "other.bin", SERVICE_FILESYSTEM_FILE_CREATE_NEW, &other) == SERVICE_BUSY);
    assert(Service_Filesystem_RemoveFlashFile("test.bin") == SERVICE_BUSY);
    assert(Service_Filesystem_WriteFile(file, data, sizeof(data), &transferred) == SERVICE_OK);
    assert(transferred == sizeof(data));
    stale = file;
    assert(Service_Filesystem_CloseFile(&file) == SERVICE_OK && file.Token == 0U);
    assert(Service_Filesystem_OpenFlashFile(
               "test.bin", SERVICE_FILESYSTEM_FILE_CREATE_NEW, &file) == SERVICE_BUSY);
    assert(Service_Filesystem_OpenFlashFile("test.bin", SERVICE_FILESYSTEM_FILE_READ, &file) ==
           SERVICE_OK);
    assert(Service_Filesystem_ReadFile(stale, data, sizeof(data), &transferred) ==
           SERVICE_INVALID_PARAM);
    assert(transferred == 0U);
    assert(Service_Filesystem_ReadFile(file, data, sizeof(data), &transferred) == SERVICE_OK);
    assert(transferred == sizeof(data) && data[0] == 1U && data[1] == 2U && data[2] == 3U);
    assert(Service_Filesystem_ReadFile(file, data, 1, &transferred) == SERVICE_OK &&
           transferred == 0U);
    stale = file;
    assert(Service_Filesystem_CloseFile(&file) == SERVICE_OK);
    assert(Service_Filesystem_RemoveFlashFile("test.bin") == SERVICE_OK);
    assert(Service_Filesystem_RemoveFlashFile("test.bin") == SERVICE_OK);
    assert(f_mount(NULL, (TCHAR[]){(TCHAR)USERPath[0], ':', 0}, 0) == FR_OK);
    filesystem_flash_file_set_mounted(false);
    assert(Service_Filesystem_ReadFile(stale, data, 1, &transferred) == SERVICE_NOT_READY);
    assert(f_mount(&test_fs, (TCHAR[]){(TCHAR)USERPath[0], ':', 0}, 1) == FR_OK);
    filesystem_flash_file_set_mounted(true);
    assert(Service_Filesystem_OpenFlashFile("new.bin", SERVICE_FILESYSTEM_FILE_CREATE_NEW, &file) ==
               SERVICE_OK &&
           file.Token != stale.Token);
    assert(Service_Filesystem_ReadFile(stale, data, 1, &transferred) == SERVICE_INVALID_PARAM);
    assert(Service_Filesystem_CloseFile(&file) == SERVICE_OK);
    assert(Service_Filesystem_RemoveFlashFile("new.bin") == SERVICE_OK);
}

/**
 * @brief 在现有 Fake NOR 格式化后运行一个文件 benchmark 场景。
 * @return 全部断言通过为零，否则终止进程。
 * @note 每场景独立进程，真实执行 Service/FatFs/USER/FTL，不访问实际设备。
 */
int test_flash_file_benchmark(void)
{
    scenario = getenv("FLASH_FILE_SCENARIO");
    assert(scenario);
    char placeholder[4];
    uint8_t work[4096];
    assert(FATFS_LinkDriver(&USER_Driver, placeholder) == 0);
    assert(FATFS_LinkDriver(&USER_Driver, USERPath) == 0);
    TCHAR drive[] = {(TCHAR)USERPath[0], ':', 0};
    assert(f_mkfs(drive, FM_FAT | FM_SFD, 0, work, sizeof(work)) == FR_OK);
    assert(f_mount(&test_fs, drive, 1) == FR_OK);
    filesystem_flash_file_set_mounted(true);
    if (!strcmp(scenario, "contract"))
    {
        test_contract();
        return 0;
    }
    TCHAR path[40] = {(TCHAR)USERPath[0], ':', '/'};
    const char *name = STORAGE_FLASH_BENCHMARK_FILE_NAME;
    for (size_t i = 0; i <= strlen(name); i++)
    {
        path[i + 3U] = (TCHAR)(unsigned char)name[i];
    }
    if (!strcmp(scenario, "exists"))
    {
        FIL file;
        UINT bytes;
        assert(f_open(&file, path, FA_CREATE_NEW | FA_WRITE) == FR_OK);
        assert(f_write(&file, "keep", 4, &bytes) == FR_OK && bytes == 4U);
        assert(f_close(&file) == FR_OK);
    }
    DWORD free_before;
    FATFS *free_fs;
    assert(f_getfree(drive, &free_before, &free_fs) == FR_OK);
    owner = strcmp(scenario, "nonowner") != 0;
    storage_flash_benchmark_run_file();
    unsigned attempts = removes;
    storage_flash_benchmark_run_file();
    assert(removes == attempts);
    owner = true;
    FILINFO info;
    FRESULT exists = f_stat(path, &info);
    if (!strcmp(scenario, "exists"))
    {
        FIL file;
        char data[4];
        UINT bytes;
        assert(exists == FR_OK && info.fsize == 4U && removes == 0U);
        assert(f_open(&file, path, FA_READ) == FR_OK);
        assert(f_read(&file, data, 4, &bytes) == FR_OK && bytes == 4U);
        assert(!memcmp(data, "keep", 4) && f_close(&file) == FR_OK);
        assert(!passed && failed);
    }
    else if (!strcmp(scenario, "close_error") || !strcmp(scenario, "delete_error"))
    {
        assert(exists == FR_OK && removes == 1U && !passed && cleanup_failed);
        assert(!strcmp(scenario, "delete_error") || filesystem_flash_file_is_open());
    }
    else
    {
        assert(exists == FR_NO_FILE);
        assert(removes == (!strcmp(scenario, "nonowner") ? 0U : 1U));
        assert(passed == !strcmp(scenario, "success"));
        assert(failed == (strcmp(scenario, "success") != 0));
        assert(!filesystem_flash_file_is_open());
    }
    if (!strcmp(scenario, "success"))
    {
        DWORD free_after;
        assert(f_mount(NULL, drive, 0) == FR_OK);
        filesystem_flash_file_set_mounted(false);
        assert(f_mount(&test_fs, drive, 1) == FR_OK);
        filesystem_flash_file_set_mounted(true);
        assert(f_stat(path, &info) == FR_NO_FILE);
        assert(f_getfree(drive, &free_after, &free_fs) == FR_OK);
        assert(free_after == free_before);
    }
    return 0;
}
