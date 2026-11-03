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

#include "Service/filesystem/filesystem_config.h"
#include "Service/filesystem/filesystem_directory.h"
#include "Service/filesystem/filesystem_file.h"
#include "Service/filesystem/filesystem_locator.h"
#include "Service/filesystem/filesystem_service.h"

#include "FATFS/App/fatfs.h"
#include "Middlewares/Third_Party/FatFs/src/ff.h"
#include "Middlewares/Third_Party/FatFs/src/ff_gen_drv.h"
#include "Tests/filesystem_locator/ram_disk.h"

#include <assert.h>
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
 * @param[in] notify_index 通知槽（忽略）。
 * @return 恒为 true。
 */
bool filesystem_sd_transfer_init(uint32_t notify_index)
{
    (void)notify_index;
    return true;
}

/**
 * @brief SD 传输层替身：恒已注册。
 * @return 恒为 true。
 */
bool filesystem_sd_transfer_is_registered(void)
{
    return true;
}

/**
 * @brief Flash 传输层替身：初始化恒成功；本测试不使用 Flash 卷。
 * @param[in] notify_index 通知槽（忽略）。
 * @return 恒为 true。
 */
bool filesystem_flash_transfer_init(uint32_t notify_index)
{
    (void)notify_index;
    return true;
}

/**
 * @brief Flash 传输层替身：执行器恒已注册。
 * @return 恒为 true。
 */
bool filesystem_flash_transfer_is_registered(void)
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
    assert(Service_Filesystem_InitSD(1U) == SERVICE_OK);
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
 * @brief 按定位符打开的文件和按路径打开的文件共用 FatFs 锁表：锁计数、写锁冲突与关闭释放都正常。
 */
static void test_locator_open_shares_fatfs_locks_with_path_open(void)
{
    Service_Filesystem_FileHandleTypeDef by_path = {0};
    Service_Filesystem_FileHandleTypeDef other = {0};
    Service_Filesystem_FileHandleTypeDef same_again = {0};
    Service_Filesystem_FileHandleTypeDef blocked = {0};
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

    /* 锁表只有两项且已满；同一文件再按定位符打开只增加读计数，说明与按路径打开登记的是同一把锁。 */
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

/**
 * @brief 测试入口：建一次镜像，按顺序跑各行为。
 * @return 全部断言通过时为 0。
 */
int main(void)
{
    build_image();
    scan_music_folder();
    test_first_and_last_open_by_locator_match_path_open();
    test_every_track_has_a_working_locator();
    test_locator_open_reads_are_bounded_and_far_below_path_open();
    test_tampered_locators_are_rejected();
    test_directory_entry_locator_is_rejected();
    test_deleted_file_locator_is_rejected();
    test_failed_locator_open_does_not_leak_file_slots();
    test_locator_open_shares_fatfs_locks_with_path_open();
    test_locator_opened_file_is_a_normal_read_only_file();
    puts("filesystem_locator: all tests passed");
    return 0;
}
