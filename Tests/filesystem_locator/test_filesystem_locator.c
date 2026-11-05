/**
 * @file test_filesystem_locator.c
 * @brief 经 Filesystem Service 的目录扫描与“按定位符打开”入口验证文件定位符的行为。
 *
 * @details
 *          被测的是 `Service_Filesystem_ReadDirectory` 随条目给出的定位符，以及
 *          `Service_Filesystem_OpenFileByLocator`；不直接断言 `ff.c` 内部。介质是内存里的 FAT32 镜像
 *          （32 KiB 簇），12000 首长名文件同在 `Music/` 一个文件夹；`ff.c` 是打过
 *          `patches/fatfs/` 补丁的副本，配置取自仓库 `ffconf.h`（见 CMakeLists.txt）。
 */

#include "Service/filesystem/filesystem_card.h"
#include "Service/filesystem/filesystem_config.h"
#include "Service/filesystem/filesystem_directory.h"
#include "Service/filesystem/filesystem_file.h"
#include "Service/filesystem/filesystem_locator.h"
#include "Service/filesystem/filesystem_service.h"

#include "FATFS/App/fatfs.h"
#include "Middlewares/Third_Party/FatFs/src/ff.h"
#include "Middlewares/Third_Party/FatFs/src/ff_gen_drv.h"
#include "Tests/filesystem_locator/ram_disk.h"

#include "freertos_stub.h"
#include "lock_env_stub.h"

#include <assert.h>
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>

char SDPath[4];
char USERPath[4];
uint8_t retSD;
uint8_t retUSER;
FATFS SDFatFS;
FATFS USERFatFS;

#define TRACK_COUNT 12000U
#define NAME_BYTES 64U
#define CONTENT_MAX_BYTES 128U
#define CLUSTER_BYTES 32768U

static char track_names[TRACK_COUNT][NAME_BYTES];
static Service_Filesystem_FileLocatorTypeDef track_locators[TRACK_COUNT];

/**
 * @brief SD 传输层替身：初始化恒成功。
 * @return 恒为 true。
 */
bool filesystem_sd_transfer_init(void)
{
    return true;
}

/**
 * @brief SD 传输层替身：恒已就绪。
 * @return 恒为 true。
 */
bool filesystem_sd_transfer_is_ready(void)
{
    return true;
}

/**
 * @brief SD 传输层替身：卡检测脚，恒表示有卡（读写失败时不会被改成 NO_MEDIA）。
 * @return 恒为 true。
 */
bool filesystem_sd_transfer_card_present(void)
{
    return true;
}

/**
 * @brief Flash 传输层替身：初始化恒成功；本测试不使用 Flash 卷。
 * @return 恒为 true。
 */
bool filesystem_flash_transfer_init(void)
{
    return true;
}

/**
 * @brief Flash 传输层替身：恒已就绪。
 * @return 恒为 true。
 */
bool filesystem_flash_transfer_is_ready(void)
{
    return true;
}

/**
 * @brief Flash 传输层替身：打开恒成功。
 * @return 恒为 SERVICE_OK。
 */
Service_StatusTypeDef filesystem_flash_transfer_open(void)
{
    return SERVICE_OK;
}

/**
 * @brief Flash 传输层替身：格式化恒成功。
 * @return 恒为 SERVICE_OK。
 */
Service_StatusTypeDef filesystem_flash_transfer_format(void)
{
    return SERVICE_OK;
}

/**
 * @brief Flash 传输层替身：恢复恒成功。
 * @return 恒为 SERVICE_OK。
 */
Service_StatusTypeDef filesystem_flash_transfer_recover(void)
{
    return SERVICE_OK;
}

/**
 * @brief Flash 传输层替身：回收恒成功。
 * @return 恒为 SERVICE_OK。
 */
Service_StatusTypeDef filesystem_flash_transfer_reclaim(void)
{
    return SERVICE_OK;
}

static uint64_t rng_state = 0x9E3779B97F4A7C15ULL;

/**
 * @brief xorshift 伪随机数；固定种子使镜像逐字节可复现。
 * @return 伪随机数。
 */
static uint32_t next_random(void)
{
    rng_state ^= rng_state << 13U;
    rng_state ^= rng_state >> 7U;
    rng_state ^= rng_state << 17U;
    return (uint32_t)(rng_state >> 16U);
}

/**
 * @brief 生成形如“歌手 - 曲名.mp3”的长文件名，总长 22 到 59 个字符。
 * @param[out] name 至少 NAME_BYTES 字节。
 */
static void generate_name(char *name)
{
    static const char alphabet[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    uint32_t total = 22U + (next_random() % 38U);
    uint32_t body = total - 4U;
    uint32_t artist = 2U + (next_random() % 6U);

    for (uint32_t index = 0U; index < body; index++)
    {
        char c = alphabet[next_random() % (sizeof(alphabet) - 1U)];

        if ((index >= artist) && (index < artist + 3U))
        {
            c = (index == artist + 1U) ? '-' : ' ';
        }
        else if ((index > artist + 3U) && (index + 1U < body) && ((next_random() % 6U) == 0U))
        {
            c = ' ';
        }
        name[index] = c;
    }
    memcpy(&name[body], ".mp3", 5U);
}

/**
 * @brief 第 index 首的文件内容：长度 8 到 104 字节，按序号确定。
 * @param[in] index 曲目序号。
 * @param[out] content 至少 CONTENT_MAX_BYTES 字节。
 * @return 内容字节数。
 */
static uint32_t make_content(uint32_t index, uint8_t *content)
{
    uint32_t length = 8U + (index % 97U);

    for (uint32_t offset = 0U; offset < length; offset++)
    {
        content[offset] = (uint8_t)((index * 31U) + (offset * 7U));
    }
    return length;
}

/**
 * @brief 把一个已打开文件读到 EOF。
 * @param[in] file 已打开的句柄。
 * @param[out] buffer 接收内容。
 * @param[in] capacity 缓冲字节数。
 * @return 读到的字节数。
 */
static uint32_t read_all(Service_Filesystem_FileHandleTypeDef file, uint8_t *buffer, uint32_t capacity)
{
    uint32_t total = 0U;
    uint32_t transferred = 0U;

    do
    {
        assert(Service_Filesystem_ReadFile(file, &buffer[total], capacity - total, &transferred) ==
               SERVICE_OK);
        total += transferred;
    } while ((transferred != 0U) && (total < capacity));
    return total;
}

/**
 * @brief 建立 SD 卷、格式化为 32 KiB 簇的 FAT32，并在 `Music/` 里创建 12000 首长名文件。
 */
static void build_image(void)
{
    static uint8_t work[4096];
    TCHAR sd_drive[] = {'0', ':', 0};
    uint8_t content[CONTENT_MAX_BYTES];

    ram_disk_reset();
    assert(FATFS_LinkDriverEx(&ram_disk_driver, SDPath, 0) == 0);
    assert(FATFS_LinkDriverEx(&ram_disk_driver, USERPath, 1) == 0);
    assert(SDPath[0] == '0');
    assert(Service_Filesystem_InitSD() == SERVICE_OK);
    assert(f_mkfs(sd_drive, FM_FAT32 | FM_SFD, CLUSTER_BYTES, work, sizeof(work)) == FR_OK);
    assert(Service_Filesystem_MountSD() == SERVICE_OK);
    assert(Service_Filesystem_CreateDirectory(SERVICE_FILESYSTEM_VOLUME_SD, "Music") == SERVICE_OK);

    for (uint32_t index = 0U; index < TRACK_COUNT; index++)
    {
        char path[NAME_BYTES + 8U];
        Service_Filesystem_FileHandleTypeDef file = {0};
        Service_StatusTypeDef status;
        uint32_t length = make_content(index, content);
        uint32_t transferred = 0U;

        do
        {
            generate_name(track_names[index]);
            snprintf(path, sizeof(path), "Music/%s", track_names[index]);
            status = Service_Filesystem_OpenFile(SERVICE_FILESYSTEM_VOLUME_SD,
                                                 path,
                                                 SERVICE_FILESYSTEM_FILE_MODE_CREATE_NEW,
                                                 &file);
        } while (status == SERVICE_BUSY);
        assert(status == SERVICE_OK);
        assert(Service_Filesystem_WriteFile(file, content, length, &transferred) == SERVICE_OK);
        assert(transferred == length);
        assert(Service_Filesystem_CloseFile(file) == SERVICE_OK);
    }
}

/**
 * @brief 扫描 `Music/`，核对条目顺序与名称，并收下每首的定位符。
 */
static void scan_music_folder(void)
{
    Service_Filesystem_DirectoryHandleTypeDef directory = {0};
    Service_Filesystem_DirectoryEntryTypeDef entry;
    uint32_t count = 0U;

    assert(Service_Filesystem_OpenDirectory(SERVICE_FILESYSTEM_VOLUME_SD, "Music", &directory) ==
           SERVICE_OK);
    for (;;)
    {
        assert(Service_Filesystem_ReadDirectory(directory, &entry) == SERVICE_OK);
        if (entry.Name[0] == '\0')
        {
            break;
        }
        assert(count < TRACK_COUNT);
        assert(!entry.IsDirectory);
        assert(strcmp(entry.Name, track_names[count]) == 0);
        track_locators[count] = entry.Locator;
        count++;
    }
    assert(count == TRACK_COUNT);
    assert(Service_Filesystem_CloseDirectory(directory) == SERVICE_OK);
}

/**
 * @brief 按路径打开并读出第 index 首的全部内容。
 * @param[in] index 曲目序号。
 * @param[out] content 至少 CONTENT_MAX_BYTES 字节。
 * @return 读到的字节数。
 */
static uint32_t read_track_by_path(uint32_t index, uint8_t *content)
{
    char path[NAME_BYTES + 8U];
    Service_Filesystem_FileHandleTypeDef file = {0};
    uint32_t length;

    snprintf(path, sizeof(path), "Music/%s", track_names[index]);
    assert(Service_Filesystem_OpenFile(SERVICE_FILESYSTEM_VOLUME_SD,
                                       path,
                                       SERVICE_FILESYSTEM_FILE_MODE_READ,
                                       &file) == SERVICE_OK);
    length = read_all(file, content, CONTENT_MAX_BYTES);
    assert(Service_Filesystem_CloseFile(file) == SERVICE_OK);
    return length;
}

/**
 * @brief 按定位符打开并读出第 index 首的全部内容。
 * @param[in] index 曲目序号。
 * @param[out] content 至少 CONTENT_MAX_BYTES 字节。
 * @return 读到的字节数。
 */
static uint32_t read_track_by_locator(uint32_t index, uint8_t *content)
{
    Service_Filesystem_FileHandleTypeDef file = {0};
    Service_StatusTypeDef status = SERVICE_ERROR;
    uint32_t length;

    assert(Service_Filesystem_OpenFileByLocator(SERVICE_FILESYSTEM_VOLUME_SD,
                                                &track_locators[index],
                                                &file,
                                                &status) == SERVICE_FILESYSTEM_LOCATOR_OPENED);
    assert(status == SERVICE_OK);
    length = read_all(file, content, CONTENT_MAX_BYTES);
    assert(Service_Filesystem_CloseFile(file) == SERVICE_OK);
    return length;
}

/**
 * @brief 第 1 首与第 12000 首按定位符打开，内容与按路径打开一致。
 */
static void test_first_and_last_open_by_locator_match_path_open(void)
{
    const uint32_t picks[] = {0U, TRACK_COUNT - 1U};

    for (uint32_t pick = 0U; pick < (sizeof(picks) / sizeof(picks[0])); pick++)
    {
        uint8_t by_path[CONTENT_MAX_BYTES];
        uint8_t by_locator[CONTENT_MAX_BYTES];
        uint8_t expected[CONTENT_MAX_BYTES];
        uint32_t path_length = read_track_by_path(picks[pick], by_path);
        uint32_t locator_length = read_track_by_locator(picks[pick], by_locator);
        uint32_t expected_length = make_content(picks[pick], expected);

        assert(path_length == expected_length);
        assert(locator_length == expected_length);
        assert(memcmp(by_path, expected, expected_length) == 0);
        assert(memcmp(by_locator, expected, expected_length) == 0);
    }
}

/**
 * @brief 目录里每一首（含落在扇区边界、簇边界的）都带可用的定位符，按它打开读到的内容正确。
 */
static void test_every_track_has_a_working_locator(void)
{
    for (uint32_t index = 0U; index < TRACK_COUNT; index++)
    {
        uint8_t actual[CONTENT_MAX_BYTES];
        uint8_t expected[CONTENT_MAX_BYTES];
        uint32_t expected_length = make_content(index, expected);

        assert(read_track_by_locator(index, actual) == expected_length);
        assert(memcmp(actual, expected, expected_length) == 0);
    }
}

/**
 * @brief 按定位符打开的读扇区数有上界（至多目录簇数加 2，随目标所在目录簇序号增长而非随目录项数线性增长），远小于按路径打开。
 */
static void test_locator_open_reads_are_bounded_and_far_below_path_open(void)
{
    Service_Filesystem_FileHandleTypeDef file = {0};
    char path[NAME_BYTES + 8U];
    uint32_t max_reads = 0U;
    uint32_t first_reads;
    uint32_t last_reads;
    uint32_t last_path_reads;
    uint32_t directory_clusters;

    for (uint32_t index = 0U; index < TRACK_COUNT; index++)
    {
        Service_Filesystem_FileHandleTypeDef other = {0};

        /* 先打开别处的文件，使 FatFs 窗口停在无关扇区，避免偶然命中。 */
        snprintf(path, sizeof(path), "Music/%s", track_names[(index + 5003U) % TRACK_COUNT]);
        assert(Service_Filesystem_OpenFile(SERVICE_FILESYSTEM_VOLUME_SD,
                                           path,
                                           SERVICE_FILESYSTEM_FILE_MODE_READ,
                                           &other) == SERVICE_OK);
        assert(Service_Filesystem_CloseFile(other) == SERVICE_OK);

        ram_disk_clear_counters();
        assert(Service_Filesystem_OpenFileByLocator(SERVICE_FILESYSTEM_VOLUME_SD,
                                                    &track_locators[index],
                                                    &file,
                                                    NULL) == SERVICE_FILESYSTEM_LOCATOR_OPENED);
        if (ram_disk_sectors_read() > max_reads)
        {
            max_reads = ram_disk_sectors_read();
        }
        if (index == 0U)
        {
            first_reads = ram_disk_sectors_read();
        }
        if (index == (TRACK_COUNT - 1U))
        {
            last_reads = ram_disk_sectors_read();
        }
        assert(Service_Filesystem_CloseFile(file) == SERVICE_OK);
    }

    snprintf(path, sizeof(path), "Music/%s", track_names[TRACK_COUNT - 1U]);
    ram_disk_clear_counters();
    assert(Service_Filesystem_OpenFile(SERVICE_FILESYSTEM_VOLUME_SD,
                                       path,
                                       SERVICE_FILESYSTEM_FILE_MODE_READ,
                                       &file) == SERVICE_OK);
    last_path_reads = ram_disk_sectors_read();
    directory_clusters = (last_path_reads + (CLUSTER_BYTES / 512U) - 1U) / (CLUSTER_BYTES / 512U);
    assert(Service_Filesystem_CloseFile(file) == SERVICE_OK);

    printf("sectors read per open: locator first=%u last=%u max=%u, path last=%u\n",
           (unsigned)first_reads, (unsigned)last_reads, (unsigned)max_reads, (unsigned)last_path_reads);
    fflush(stdout);
    /* 按定位符只需沿目录的簇链走到目标簇：每个目录簇至多一次 FAT 扇区读，再加目录项所在扇区；
     * 按路径则要读到该项之前的全部目录扇区。 */
    assert(first_reads <= 2U);
    assert(max_reads <= (directory_clusters + 2U));
    assert(last_path_reads >= (20U * max_reads));
}

/**
 * @brief 取出第 index 首定位符的副本，供篡改。
 * @param[in] index 曲目序号。
 * @return 定位符副本。
 */
static Service_Filesystem_FileLocatorTypeDef copy_locator(uint32_t index)
{
    return track_locators[index];
}

/**
 * @brief 断言按给定定位符打开被判为核对不符，且不返回句柄。
 * @param[in] locator 待检查的定位符。
 */
static void assert_locator_mismatch(const Service_Filesystem_FileLocatorTypeDef *locator)
{
    Service_Filesystem_FileHandleTypeDef file = {0};
    Service_StatusTypeDef status = SERVICE_ERROR;

    assert(Service_Filesystem_OpenFileByLocator(SERVICE_FILESYSTEM_VOLUME_SD, locator, &file, &status) ==
           SERVICE_FILESYSTEM_LOCATOR_MISMATCH);
    assert(status == SERVICE_OK);
    assert(file.Token == 0U);

}

/**
 * @brief 篡改记录被拒：全零、起始簇不符、长度不符、位置指向别的目录项、位置越界或未对齐、文件夹簇错误。
 */
static void test_tampered_locators_are_rejected(void)
{
    Service_Filesystem_FileLocatorTypeDef locator;

    memset(&locator, 0, sizeof(locator));
    assert_locator_mismatch(&locator);

    locator = copy_locator(100U);
    locator.Opaque[FILESYSTEM_LOCATOR_WORD_START] += 1U;
    assert_locator_mismatch(&locator);

    locator = copy_locator(100U);
    locator.Opaque[FILESYSTEM_LOCATOR_WORD_LENGTH] += 1U;
    assert_locator_mismatch(&locator);

    /* 位置取自另一首，起始簇与长度仍是原来这首的。 */
    locator = copy_locator(100U);
    locator.Opaque[FILESYSTEM_LOCATOR_WORD_OFFSET] = track_locators[101U].Opaque[FILESYSTEM_LOCATOR_WORD_OFFSET];
    assert_locator_mismatch(&locator);

    /* 位置落在长名项上（主目录项之前一项）：不是短名文件项。 */
    locator = copy_locator(100U);
    locator.Opaque[FILESYSTEM_LOCATOR_WORD_OFFSET] -= 32U;
    assert_locator_mismatch(&locator);

    locator = copy_locator(100U);
    locator.Opaque[FILESYSTEM_LOCATOR_WORD_OFFSET] = 0x00FFFFE0U;
    assert_locator_mismatch(&locator);

    locator = copy_locator(100U);
    locator.Opaque[FILESYSTEM_LOCATOR_WORD_OFFSET] += 1U;
    assert_locator_mismatch(&locator);

    locator = copy_locator(100U);
    locator.Opaque[FILESYSTEM_LOCATOR_WORD_FOLDER] = 0x0FFFFFF0U;
    assert_locator_mismatch(&locator);
}

/**
 * @brief 子目录条目的定位符被拒：它指向的目录项是目录而不是文件。
 */
static void test_directory_entry_locator_is_rejected(void)
{
    Service_Filesystem_DirectoryHandleTypeDef directory = {0};
    Service_Filesystem_DirectoryEntryTypeDef entry;
    Service_Filesystem_FileLocatorTypeDef directory_locator;
    bool found = false;

    assert(Service_Filesystem_CreateDirectory(SERVICE_FILESYSTEM_VOLUME_SD, "Music/Album") == SERVICE_OK);
    assert(Service_Filesystem_OpenDirectory(SERVICE_FILESYSTEM_VOLUME_SD, "Music", &directory) == SERVICE_OK);
    for (;;)
    {
        assert(Service_Filesystem_ReadDirectory(directory, &entry) == SERVICE_OK);
        if (entry.Name[0] == '\0')
        {
            break;
        }
        if (entry.IsDirectory)
        {
            assert(strcmp(entry.Name, "Album") == 0);
            directory_locator = entry.Locator;
            found = true;
        }
    }
    assert(Service_Filesystem_CloseDirectory(directory) == SERVICE_OK);
    assert(found);
    assert_locator_mismatch(&directory_locator);
}

/**
 * @brief 文件被删除后，旧定位符被拒。
 */
static void test_deleted_file_locator_is_rejected(void)
{
    char path[NAME_BYTES + 8U];

    snprintf(path, sizeof(path), "Music/%s", track_names[7000U]);
    assert(Service_Filesystem_RemoveFile(SERVICE_FILESYSTEM_VOLUME_SD, path) == SERVICE_OK);
    assert_locator_mismatch(&track_locators[7000U]);
}

/**
 * @brief 核对不符与打开失败都不泄漏文件槽。
 */
static void test_failed_locator_open_does_not_leak_file_slots(void)
{
    Service_Filesystem_FileLocatorTypeDef none;
    Service_Filesystem_FileLocatorTypeDef wrong_length = copy_locator(300U);
    uint8_t content[CONTENT_MAX_BYTES];

    memset(&none, 0, sizeof(none));
    wrong_length.Opaque[FILESYSTEM_LOCATOR_WORD_LENGTH] += 1U;
    for (uint32_t attempt = 0U; attempt < (2U * FILESYSTEM_FILE_SLOT_COUNT); attempt++)
    {
        assert_locator_mismatch(&none);
        assert_locator_mismatch(&wrong_length);
    }
    assert(read_track_by_locator(300U, content) == make_content(300U, content));
}

/**
 * @brief 把第 index 首直接交给 FatFs 打开（不经 Service，所以不占 Service 的槽），只为占用 FatFs 锁表的一项。
 * @param[out] fp 接收 FatFs 文件对象。
 * @param[in] index 曲目序号。
 * @note  主机单线程，直接调 FatFs 不需要拿 Service 的锁。
 */
static void open_directly_with_fatfs(FIL *fp, uint32_t index)
{
    TCHAR path[NAME_BYTES + 16U];
    uint32_t length = 0U;
    const char prefix[] = "0:/Music/";

    for (uint32_t i = 0U; prefix[i] != '\0'; i++)
    {
        path[length++] = (TCHAR)prefix[i];
    }
    for (uint32_t i = 0U; track_names[index][i] != '\0'; i++)
    {
        path[length++] = (TCHAR)(uint8_t)track_names[index][i];
    }
    path[length] = 0;
    assert(f_open(fp, path, FA_READ) == FR_OK);
}

/**
 * @brief 按定位符打开的文件和按路径打开的文件共用 FatFs 锁表：锁计数、写锁冲突与关闭释放都正常。
 */
static void test_locator_open_shares_fatfs_locks_with_path_open(void)
{
    Service_Filesystem_FileHandleTypeDef by_path = {0};
    Service_Filesystem_FileHandleTypeDef other = {0};
    Service_Filesystem_FileHandleTypeDef same_again = {0};
    Service_Filesystem_FileHandleTypeDef blocked = {0};
    static FIL fillers[_FS_LOCK];
    Service_StatusTypeDef status = SERVICE_OK;
    char path[NAME_BYTES + 8U];
    uint64_t size = 0U;
    uint8_t content[CONTENT_MAX_BYTES];

    snprintf(path, sizeof(path), "Music/%s", track_names[10U]);
    assert(Service_Filesystem_OpenFile(SERVICE_FILESYSTEM_VOLUME_SD,
                                       path,
                                       SERVICE_FILESYSTEM_FILE_MODE_READ,
                                       &by_path) == SERVICE_OK);
    assert(Service_Filesystem_OpenFileByLocator(SERVICE_FILESYSTEM_VOLUME_SD,
                                                &track_locators[20U],
                                                &other,
                                                &status) == SERVICE_FILESYSTEM_LOCATOR_OPENED);

    /* Service 的槽（SD 4 个文件）少于 FatFs 锁表（_FS_LOCK），所以经 Service 永远填不满锁表；
     * 这里把其余的锁表项用直接调用 FatFs 的文件占满，来覆盖“锁表满”这条错误路径。 */
    for (uint32_t i = 0U; i < (_FS_LOCK - 2U); i++)
    {
        open_directly_with_fatfs(&fillers[i], 100U + i);
    }

    /* 锁表已满；同一文件再按定位符打开只增加读计数，说明与按路径打开登记的是同一把锁。 */
    assert(Service_Filesystem_OpenFileByLocator(SERVICE_FILESYSTEM_VOLUME_SD,
                                                &track_locators[10U],
                                                &same_again,
                                                &status) == SERVICE_FILESYSTEM_LOCATOR_OPENED);
    assert(Service_Filesystem_GetFileSize(same_again, &size) == SERVICE_OK);
    assert(size == make_content(10U, content));

    /* 第三个不同文件没有空锁项：失败而非核对不符，且不占槽。 */
    assert(Service_Filesystem_OpenFileByLocator(SERVICE_FILESYSTEM_VOLUME_SD,
                                                &track_locators[30U],
                                                &blocked,
                                                &status) == SERVICE_FILESYSTEM_LOCATOR_FAILED);
    assert(status != SERVICE_OK);
    assert(blocked.Token == 0U);

    assert(Service_Filesystem_CloseFile(same_again) == SERVICE_OK);
    assert(Service_Filesystem_CloseFile(by_path) == SERVICE_OK);
    assert(Service_Filesystem_CloseFile(other) == SERVICE_OK);
    for (uint32_t i = 0U; i < (_FS_LOCK - 2U); i++)
    {
        assert(f_close(&fillers[i]) == FR_OK);
    }

    /* 全部关闭后锁项都已释放：第三个文件现在能打开。 */
    assert(read_track_by_locator(30U, content) == make_content(30U, content));

    /* 以写方式持有的文件不能再被按定位符只读打开。 */
    assert(Service_Filesystem_OpenFile(SERVICE_FILESYSTEM_VOLUME_SD,
                                       path,
                                       SERVICE_FILESYSTEM_FILE_MODE_READ_WRITE,
                                       &by_path) == SERVICE_OK);
    assert(Service_Filesystem_OpenFileByLocator(SERVICE_FILESYSTEM_VOLUME_SD,
                                                &track_locators[10U],
                                                &blocked,
                                                &status) == SERVICE_FILESYSTEM_LOCATOR_FAILED);
    assert(status == SERVICE_BUSY);
    assert(Service_Filesystem_CloseFile(by_path) == SERVICE_OK);
    assert(read_track_by_locator(10U, content) == make_content(10U, content));
}

/* ---------------------------------------------------------------- 入口加锁（按定位符打开） */

static SemaphoreHandle_t sd_lock;
static SemaphoreHandle_t table_lock;

/**
 * @brief 读 sd_lock 与 table_lock 此刻被持有的次数（拿减放）。
 * @param[out] sd SD 卷锁。
 * @param[out] table 表锁。
 */
static void held_counts(uint32_t *sd, uint32_t *table)
{
    const FreertosStub_SemaphoreStats sd_stats = freertos_stub_semaphore_stats(sd_lock);
    const FreertosStub_SemaphoreStats table_stats = freertos_stub_semaphore_stats(table_lock);

    *sd = sd_stats.take_successes - sd_stats.give_successes;
    *table = table_stats.take_successes - table_stats.give_successes;
}

/**
 * @brief 按“拿了哪把锁”认出 SD 卷锁和表锁：GetFileInfo 只拿卷锁；按定位符打开卷锁、表锁各拿一次。
 * @note  Service 一共三把锁（SD、Flash、表），Flash 卷在本测试里没有挂载。
 */
static void identify_locks(void)
{
    Service_Filesystem_FileInfoTypeDef info;
    Service_Filesystem_FileHandleTypeDef file = {0};
    uint32_t sd_index = 3U;
    uint32_t table_index = 3U;
    SemaphoreHandle_t handles[3];
    uint32_t before[3];

    assert(freertos_stub_created_semaphore_count() == 3U);
    for (uint32_t i = 0U; i < 3U; i++)
    {
        handles[i] = freertos_stub_created_semaphore(i);
        before[i] = freertos_stub_semaphore_stats(handles[i]).take_successes;
    }

    (void)Service_Filesystem_GetFileInfo(SERVICE_FILESYSTEM_VOLUME_SD, "Music", &info);
    for (uint32_t i = 0U; i < 3U; i++)
    {
        const uint32_t now = freertos_stub_semaphore_stats(handles[i]).take_successes;

        if (now != before[i])
        {
            assert(sd_index == 3U);
            sd_index = i;
        }
        before[i] = now;
    }
    assert(sd_index != 3U);

    assert(Service_Filesystem_OpenFileByLocator(SERVICE_FILESYSTEM_VOLUME_SD, &track_locators[1U], &file, NULL) ==
           SERVICE_FILESYSTEM_LOCATOR_OPENED);
    assert(Service_Filesystem_CloseFile(file) == SERVICE_OK);
    for (uint32_t i = 0U; i < 3U; i++)
    {
        const uint32_t now = freertos_stub_semaphore_stats(handles[i]).take_successes;

        if ((i != sd_index) && (now != before[i]))
        {
            assert(table_index == 3U);
            table_index = i;
        }
    }
    assert(table_index != 3U);
    sd_lock = handles[sd_index];
    table_lock = handles[table_index];
}

/** @brief 等表锁时核对 SD 卷锁正被调用者持有，然后替“别的任务”放表锁。 */
typedef struct
{
    uint32_t calls;
    uint32_t held_sd;
} TableWaitProbe;

/**
 * @brief 阻塞钩子：被测代码在等表锁时，记下 SD 卷锁此刻被持有的次数，然后放表锁。
 * @param[in] wait 将要进行的等待。
 * @param[in,out] context TableWaitProbe。
 */
static void probe_table_wait(const FreertosStub_Wait *wait, void *context)
{
    TableWaitProbe *probe = (TableWaitProbe *)context;

    if ((wait->kind == FREERTOS_STUB_WAIT_SEMAPHORE) && (wait->semaphore == table_lock))
    {
        uint32_t sd;
        uint32_t table;

        held_counts(&sd, &table);
        probe->calls++;
        probe->held_sd = sd;
        assert(xSemaphoreGive(table_lock) == pdTRUE);
    }
}

/**
 * @brief 按定位符打开：拿 SD 卷锁一次，其中 `f_open_dirent` 一步在表锁内（卷锁 → 表锁）；核对不符、
 *        槽用尽时按各自走到的步骤拿放，并且放锁不漏。
 */
static void test_locator_open_takes_the_volume_lock_then_the_table_lock(void)
{
    Service_Filesystem_FileHandleTypeDef file = {0};
    Service_Filesystem_FileLocatorTypeDef none;
    Service_Filesystem_FileLocatorTypeDef wrong_length = copy_locator(300U);
    TableWaitProbe probe = {0U, 0U};
    uint32_t sd_takes;
    uint32_t table_takes;
    uint32_t sd;
    uint32_t table;

    memset(&none, 0, sizeof(none));
    wrong_length.Opaque[FILESYSTEM_LOCATOR_WORD_LENGTH] += 1U;

    sd_takes = freertos_stub_semaphore_stats(sd_lock).take_successes;
    table_takes = freertos_stub_semaphore_stats(table_lock).take_successes;
    assert(Service_Filesystem_OpenFileByLocator(SERVICE_FILESYSTEM_VOLUME_SD, &track_locators[5U], &file, NULL) ==
           SERVICE_FILESYSTEM_LOCATOR_OPENED);
    assert(freertos_stub_semaphore_stats(sd_lock).take_successes - sd_takes == 1U);
    assert(freertos_stub_semaphore_stats(table_lock).take_successes - table_takes == 1U);

    /* 关闭不碰表锁（f_close 只写本卷的锁表项）。 */
    table_takes = freertos_stub_semaphore_stats(table_lock).take_successes;
    assert(Service_Filesystem_CloseFile(file) == SERVICE_OK);
    assert(freertos_stub_semaphore_stats(table_lock).take_successes == table_takes);

    /* 全零定位符：占槽后立刻发现没有定位符，还没走到表锁。 */
    sd_takes = freertos_stub_semaphore_stats(sd_lock).take_successes;
    table_takes = freertos_stub_semaphore_stats(table_lock).take_successes;
    assert_locator_mismatch(&none);
    assert(freertos_stub_semaphore_stats(sd_lock).take_successes - sd_takes == 1U);
    assert(freertos_stub_semaphore_stats(table_lock).take_successes == table_takes);

    /* 长度不符：走到了 f_open_dirent，核对失败，表锁照样放掉。 */
    sd_takes = freertos_stub_semaphore_stats(sd_lock).take_successes;
    table_takes = freertos_stub_semaphore_stats(table_lock).take_successes;
    assert_locator_mismatch(&wrong_length);
    assert(freertos_stub_semaphore_stats(sd_lock).take_successes - sd_takes == 1U);
    assert(freertos_stub_semaphore_stats(table_lock).take_successes - table_takes == 1U);

    /* 表锁被别的任务占着：调用者在已持 SD 卷锁时等表锁。 */
    assert(xSemaphoreTake(table_lock, 0U) == pdTRUE);
    freertos_stub_set_block_hook(probe_table_wait, &probe);
    assert(Service_Filesystem_OpenFileByLocator(SERVICE_FILESYSTEM_VOLUME_SD, &track_locators[6U], &file, NULL) ==
           SERVICE_FILESYSTEM_LOCATOR_OPENED);
    freertos_stub_set_block_hook(NULL, NULL);
    assert(probe.calls == 1U);
    assert(probe.held_sd == 1U);
    assert(Service_Filesystem_CloseFile(file) == SERVICE_OK);

    held_counts(&sd, &table);
    assert((sd == 0U) && (table == 0U));
}

static jmp_buf abort_jump;
static volatile sig_atomic_t abort_seen;

/**
 * @brief SIGABRT 处理：把 configASSERT 停机变成可检查的事件。
 * @param[in] signal_number 信号编号。
 */
static void on_abort(int signal_number)
{
    (void)signal_number;
    abort_seen = 1;
    longjmp(abort_jump, 1);
}

/** @brief 按定位符打开，用于在被拒绝的任务里调用。 */
static void call_open_by_locator(void)
{
    Service_Filesystem_FileHandleTypeDef file = {0};

    (void)Service_Filesystem_OpenFileByLocator(SERVICE_FILESYSTEM_VOLUME_SD, &track_locators[5U], &file, NULL);
}

/**
 * @brief 登记在拒绝名单里的任务按定位符打开：在等锁之前停机，不拿任何一把锁。
 */
static void test_denied_task_is_stopped_before_waiting_for_the_locator_open_locks(void)
{
    const TaskHandle_t allowed_task = xTaskGetCurrentTaskHandle();
    const TaskHandle_t denied_task = freertos_stub_task_create();
    void (*previous)(int);
    uint32_t sd_takes;
    uint32_t unsatisfied;

    assert(denied_task != NULL);
    Service_Filesystem_DenyCaller(denied_task);

    assert(xSemaphoreTake(sd_lock, 0U) == pdTRUE); /* 若入口先去等锁，桩会把它判成死锁。 */
    sd_takes = freertos_stub_semaphore_stats(sd_lock).take_successes;
    unsatisfied = freertos_stub_unsatisfied_infinite_waits();

    freertos_stub_set_current_task(denied_task);
    abort_seen = 0;
    previous = signal(SIGABRT, on_abort);
    assert(previous != SIG_ERR);
    if (setjmp(abort_jump) == 0)
    {
        call_open_by_locator();
    }
    (void)signal(SIGABRT, previous);
    freertos_stub_set_current_task(allowed_task);

    assert(abort_seen != 0);
    assert(freertos_stub_unsatisfied_infinite_waits() == unsatisfied);
    assert(freertos_stub_semaphore_stats(sd_lock).take_successes == sd_takes);
    assert(xSemaphoreGive(sd_lock) == pdTRUE);
}

/**
 * @brief 定位符打开的文件是正常的只读文件：大小、定位与读回都对，且拒绝写入。
 */
static void test_locator_opened_file_is_a_normal_read_only_file(void)
{
    Service_Filesystem_FileHandleTypeDef file = {0};
    uint8_t expected[CONTENT_MAX_BYTES];
    uint8_t actual[CONTENT_MAX_BYTES];
    uint32_t length = make_content(5000U, expected);
    uint32_t transferred = 0U;
    uint64_t value = 0U;

    assert(Service_Filesystem_OpenFileByLocator(SERVICE_FILESYSTEM_VOLUME_SD,
                                                &track_locators[5000U],
                                                &file,
                                                NULL) == SERVICE_FILESYSTEM_LOCATOR_OPENED);
    assert(Service_Filesystem_GetFileSize(file, &value) == SERVICE_OK);
    assert(value == length);
    assert(Service_Filesystem_GetFilePosition(file, &value) == SERVICE_OK);
    assert(value == 0U);
    assert(Service_Filesystem_SeekFile(file, 3U) == SERVICE_OK);
    assert(Service_Filesystem_ReadFile(file, actual, 4U, &transferred) == SERVICE_OK);
    assert(transferred == 4U);
    assert(memcmp(actual, &expected[3], 4U) == 0);
    assert(Service_Filesystem_WriteFile(file, expected, 1U, &transferred) != SERVICE_OK);
    assert(Service_Filesystem_CloseFile(file) == SERVICE_OK);
    assert(Service_Filesystem_CloseFile(file) == SERVICE_INVALID_HANDLE);
}

/** @brief 闸上等待时替“卷任务”开闸的钩子上下文。 */
typedef struct
{
    EventGroupHandle_t gate;
    uint32_t waits;
    uint32_t sd_takes_at_wait;
} GateProbe;

/**
 * @brief 阻塞钩子：调用者在闸上阻塞时记下此刻 SD 卷锁已被拿的次数，然后开闸。
 * @param[in] wait 将要进行的等待。
 * @param[in,out] context GateProbe。
 */
static void open_gate_when_waiting(const FreertosStub_Wait *wait, void *context)
{
    GateProbe *probe = (GateProbe *)context;

    if ((wait->kind == FREERTOS_STUB_WAIT_EVENT_GROUP) && (wait->event_group == probe->gate))
    {
        probe->waits++;
        probe->sd_takes_at_wait = freertos_stub_semaphore_stats(sd_lock).take_successes;
        Service_Filesystem_ResumeSD();
    }
}

/**
 * @brief 按定位符打开同样走进门序列：关闸时在拿锁之前等闸，开闸后打开成功；卷没挂时不等锁、不等闸，
 *        直接 FAILED 且状态为 NOT_READY；挂回后可用。
 */
static void test_locator_open_waits_at_the_gate_and_rejects_an_unmounted_volume(void)
{
    Service_Filesystem_FileHandleTypeDef file = {0};
    Service_StatusTypeDef status = SERVICE_OK;
    GateProbe probe = {freertos_stub_last_created_event_group(), 0U, 0U};
    uint32_t sd_takes;

    assert(probe.gate != NULL);
    Service_Filesystem_PauseSD();
    freertos_stub_set_block_hook(open_gate_when_waiting, &probe);
    sd_takes = freertos_stub_semaphore_stats(sd_lock).take_successes;
    assert(Service_Filesystem_OpenFileByLocator(SERVICE_FILESYSTEM_VOLUME_SD, &track_locators[2U], &file, &status) ==
           SERVICE_FILESYSTEM_LOCATOR_OPENED);
    freertos_stub_set_block_hook(NULL, NULL);
    assert(status == SERVICE_OK);
    assert(probe.waits == 1U);
    assert(probe.sd_takes_at_wait == sd_takes); /* 等闸时还没拿锁。 */
    assert(freertos_stub_semaphore_stats(sd_lock).take_successes - sd_takes == 1U);
    assert(Service_Filesystem_CloseFile(file) == SERVICE_OK);

    /* 卷没挂：SD 卷锁被占着、闸关着也立刻返回。 */
    assert(Service_Filesystem_UnmountSD() == SERVICE_OK);
    assert(xSemaphoreTake(sd_lock, 0U) == pdTRUE);
    Service_Filesystem_PauseSD();
    sd_takes = freertos_stub_semaphore_stats(sd_lock).take_successes;
    assert(Service_Filesystem_OpenFileByLocator(SERVICE_FILESYSTEM_VOLUME_SD, &track_locators[2U], &file, &status) ==
           SERVICE_FILESYSTEM_LOCATOR_FAILED);
    assert(status == SERVICE_NOT_READY);
    assert(freertos_stub_semaphore_stats(sd_lock).take_successes == sd_takes);
    assert(xSemaphoreGive(sd_lock) == pdTRUE);
    Service_Filesystem_ResumeSD();

    assert(Service_Filesystem_MountSD() == SERVICE_OK);
    assert(Service_Filesystem_OpenFileByLocator(SERVICE_FILESYSTEM_VOLUME_SD, &track_locators[2U], &file, &status) ==
           SERVICE_FILESYSTEM_LOCATOR_OPENED);
    assert(Service_Filesystem_CloseFile(file) == SERVICE_OK);
}

/**
 * @brief 测试入口：建一次镜像，按顺序跑各行为。
 * @return 全部断言通过时为 0。
 */
int main(void)
{
    /* 在第一次调用 Filesystem 之前 reset：它的锁是第一次用到时才创建的。 */
    freertos_stub_reset();
    lock_env_counter_reset();
    lock_env_log_reset();
    build_image();
    scan_music_folder();
    identify_locks();
    test_first_and_last_open_by_locator_match_path_open();
    test_every_track_has_a_working_locator();
    test_locator_open_reads_are_bounded_and_far_below_path_open();
    test_tampered_locators_are_rejected();
    test_directory_entry_locator_is_rejected();
    test_deleted_file_locator_is_rejected();
    test_failed_locator_open_does_not_leak_file_slots();
    test_locator_open_shares_fatfs_locks_with_path_open();
    test_locator_open_takes_the_volume_lock_then_the_table_lock();
    test_denied_task_is_stopped_before_waiting_for_the_locator_open_locks();
    test_locator_opened_file_is_a_normal_read_only_file();
    test_locator_open_waits_at_the_gate_and_rejects_an_unmounted_volume();
    puts("filesystem_locator: all tests passed");
    return 0;
}
