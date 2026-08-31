/**
 * @file test_flash_ftl.c
 * @brief 经公开接口验证 NOR 逻辑盘；Fake 仅位于 RawOps 边界。
 */

#include "Components/flash_ftl/flash_ftl.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define BLOCKS 64U
static uint8_t nor[BLOCKS * FLASH_FTL_BLOCK_BYTES];
static uint32_t map[BLOCKS];
static uint64_t versions[BLOCKS];
static uint8_t states[BLOCKS];
static uint8_t work[4096];
static uint8_t scratch[512];
static uint32_t programs, erases, reads;
static int cut_after = -1;
static uint32_t tear_bytes;
static bool injected_fault;
static uint32_t quiesce_busy;
static bool silent_partial_erase;

/**
 * @brief 消费擦写故障倒计数，并在指定操作处触发一次模拟掉电。
 * @return true 表示本次应执行撕裂操作；false 表示正常操作。
 * @note 触发后设置 injected_fault 并禁用倒计数，避免一次用例重复注入。
 */
static bool cut_now(void)
{
    if (cut_after < 0)
    {
        return false;
    }
    if (cut_after-- == 0)
    {
        injected_fault = true;
        cut_after = -1;
        return true;
    }
    return false;
}

static FlashFTL_HandleTypeDef disk;

/**
 * @brief 提供内存 Fake NOR 的固定原始几何。
 * @param[in] c 未使用，保留 RawOps Context 签名。
 * @param[out] g 有效输出，接收数组容量、4096 B 擦除块和 256 B 页。
 * @return 固定返回 FLASH_FTL_RAW_OK，不访问实际设备。
 */
static FlashFTL_RawStatusTypeDef geometry(void *c, FlashFTL_RawGeometryTypeDef *g)
{
    (void)c;
    *g = (FlashFTL_RawGeometryTypeDef){sizeof(nor), 4096, 256};
    return FLASH_FTL_RAW_OK;
}

/**
 * @brief 检查 Fake NOR 范围后复制读取数据并累计读次数。
 * @param[in] c 未使用的 RawOps Context。
 * @param[in] a Fake NOR 数组内字节地址。
 * @param[out] p 至少 n 字节的有效接收缓冲。
 * @param[in] n 读取字节数，完整范围必须在数组内。
 * @return FLASH_FTL_RAW_OK 表示已复制；范围错误通过断言终止测试。
 * @note 此 Fake 在 Start 内完成复制，生产异步生命周期仍由真实 FTL 的 Process 驱动。
 */
static FlashFTL_RawStatusTypeDef read_start(void *c, uint32_t a, uint8_t *p, uint32_t n)
{
    (void)c;
    assert(a <= sizeof(nor) && n <= sizeof(nor) - a);
    memcpy(p, nor + a, n);
    ++reads;
    return FLASH_FTL_RAW_OK;
}

/**
 * @brief 模拟 NOR 页编程，约束页边界与仅 1→0 位变化。
 * @param[in] c 未使用的 RawOps Context。
 * @param[in] a Fake NOR 内页地址，请求不得跨页。
 * @param[in] p 至少 n 字节的有效输入。
 * @param[in] n 1..256 字节。
 * @return 正常为 RAW_OK；注入故障时只编程指定前缀并返回 RAW_TIMEOUT。
 * @note 断言检查介质范围及禁止 0→1；不模拟真实编程延迟。
 */
static FlashFTL_RawStatusTypeDef program_start(void *c, uint32_t a, const uint8_t *p, uint32_t n)
{
    (void)c;
    assert(n && n <= 256 && a / 256 == (a + n - 1) / 256);
    assert(a <= sizeof(nor) && n <= sizeof(nor) - a);
    bool cut = cut_now();
    uint32_t length = cut && tear_bytes < n ? tear_bytes : n;
    for (uint32_t i = 0; i < length; i++)
    {
        assert((nor[a + i] & p[i]) == p[i]);
        nor[a + i] &= p[i];
    }
    ++programs;
    return cut ? FLASH_FTL_RAW_TIMEOUT : FLASH_FTL_RAW_OK;
}

/**
 * @brief 模拟整块擦除，并支持撕裂擦除和虚假成功注入。
 * @param[in] c 未使用的 RawOps Context。
 * @param[in] a Fake NOR 内 4096 B 对齐块首地址。
 * @return 正常或虚假成功为 RAW_OK；撕裂擦除为 RAW_TIMEOUT。
 * @note silent_partial_erase 仅擦首 256 B 却报告成功，用于验证 FTL 必须全块回读。
 */
static FlashFTL_RawStatusTypeDef erase_start(void *c, uint32_t a)
{
    (void)c;
    assert(a % 4096 == 0 && a < sizeof(nor));
    if (silent_partial_erase)
    {
        silent_partial_erase = false;
        memset(nor + a, 255, 256);
        ++erases;
        return FLASH_FTL_RAW_OK;
    }
    bool cut = cut_now();
    memset(nor + a, 255, cut && tear_bytes < 4096 ? tear_bytes : 4096);
    ++erases;
    return cut ? FLASH_FTL_RAW_TIMEOUT : FLASH_FTL_RAW_OK;
}

/**
 * @brief 提供 Fake NOR 立即完成的 Process 回调。
 * @param[in] c 未使用的 RawOps Context。
 * @return 固定返回 FLASH_FTL_RAW_OK；测试不在此模拟硬件时延。
 */
static FlashFTL_RawStatusTypeDef ready(void *c)
{
    (void)c;
    return FLASH_FTL_RAW_OK;
}

/**
 * @brief 按计数模拟安全收尾暂忙，验证 FTL 保留缓冲所有权。
 * @param[in] c 未使用的 RawOps Context。
 * @return 计数未耗尽时递减并返回 RAW_BUSY，否则返回 RAW_OK。
 */
static FlashFTL_RawStatusTypeDef quiesce(void *c)
{
    (void)c;
    if (quiesce_busy)
    {
        --quiesce_busy;
        return FLASH_FTL_RAW_BUSY;
    }
    return FLASH_FTL_RAW_OK;
}

static const FlashFTL_RawOpsTypeDef ops = {
    geometry, read_start, program_start, erase_start, ready, quiesce};

/**
 * @brief 重新绑定真实 FTL 和长期测试表，模拟重启后重新初始化。
 * @note 保留 Fake NOR 阵列，不沿用句柄就绪状态；绑定失败通过断言终止。
 */
static void bind_disk(void)
{
    FlashFTL_MemoryTypeDef m = {map, versions, states, BLOCKS, BLOCKS, work, scratch};
    assert(FlashFTL_Init(&disk, &ops, NULL, &m) == FLASH_FTL_OK);
}

/**
 * @brief 有界推进已受理的测试请求，直到 FTL 返回最终结果。
 * @return 首个非 RUNNING/WAIT 的状态；超过 200000 步断言失败。
 * @note 只在主机单线程使用，避免状态机失去终止条件时测试无限运行。
 */
static FlashFTL_StatusTypeDef finish(void)
{
    for (uint32_t i = 0; i < 200000; i++)
    {
        FlashFTL_StatusTypeDef s = FlashFTL_Process(&disk);
        if (s != FLASH_FTL_RUNNING && s != FLASH_FTL_WAIT)
        {
            return s;
        }
    }
    assert(!"operation did not terminate");
    return FLASH_FTL_IO_ERROR;
}

/**
 * @brief 验证全擦除介质打开为 UNFORMATTED，且没有隐式擦写。
 * @note 初始化为 0xFF 后检查打开结果及编程、擦除计数。 断言失败即终止用例。
 */
static void blank_media_is_not_formatted_implicitly(void)
{
    memset(nor, 255, sizeof(nor));
    bind_disk();
    assert(FlashFTL_OpenStart(&disk) == FLASH_FTL_OK);
    assert(finish() == FLASH_FTL_UNFORMATTED);
    assert(programs == 0 && erases == 0);
}

/**
 * @brief 验证显式格式化后重新打开的容量与尾扇区擦除值。
 * @note 承接已绑定空盘；重建句柄后核对预留容量及未映射读取的 0xFF。 断言失败即终止用例。
 */
static void explicit_format_survives_restart_and_reads_erased_sectors(void)
{
    uint8_t data[512];
    FlashFTL_InfoTypeDef info;
    assert(FlashFTL_FormatStart(&disk) == FLASH_FTL_OK);
    assert(finish() == FLASH_FTL_OK);
    bind_disk();
    assert(FlashFTL_OpenStart(&disk) == FLASH_FTL_OK);
    assert(finish() == FLASH_FTL_OK);
    assert(FlashFTL_GetInfo(&disk, &info) == FLASH_FTL_OK);
    assert(info.SectorCount == (BLOCKS - 2 - 7) * 7);
    memset(data, 0, sizeof(data));
    assert(FlashFTL_ReadStart(&disk, info.SectorCount - 1, data, 1) == FLASH_FTL_OK);
    assert(finish() == FLASH_FTL_OK);
    for (uint32_t i = 0; i < sizeof(data); i++)
    {
        assert(data[i] == 255);
    }
}

/**
 * @brief 验证组内单扇区覆盖不丢失相邻扇区，并能在重启后恢复。
 * @note 先写七扇区图样，再只修改中间一扇区；重开后比较完整组。 断言失败即终止用例。
 */
static void partial_group_update_survives_restart(void)
{
    uint8_t original[3584], replacement[512], actual[3584];
    for (uint32_t i = 0; i < sizeof(original); i++)
    {
        original[i] = (uint8_t)(i / 512 + 17);
    }
    memset(replacement, 0x31, sizeof(replacement));
    assert(FlashFTL_WriteStart(&disk, 7, original, 7) == FLASH_FTL_OK);
    assert(finish() == FLASH_FTL_OK);
    assert(FlashFTL_WriteStart(&disk, 10, replacement, 1) == FLASH_FTL_OK);
    assert(finish() == FLASH_FTL_OK);
    memcpy(original + 3 * 512, replacement, 512);
    bind_disk();
    assert(FlashFTL_OpenStart(&disk) == FLASH_FTL_OK);
    assert(finish() == FLASH_FTL_OK);
    assert(FlashFTL_ReadStart(&disk, 7, actual, 7) == FLASH_FTL_OK);
    assert(finish() == FLASH_FTL_OK);
    assert(!memcmp(actual, original, sizeof(actual)));
}

/**
 * @brief 验证持续覆盖写耗尽初始空闲块后仍可回收并保留最新版本。
 * @note 执行 160 次覆盖写，再重新打开并核对最后一次数据。 断言失败即终止用例。
 */
static void repeated_overwrite_reclaims_space_without_losing_data(void)
{
    uint8_t data[512], actual[512];
    for (uint32_t n = 0; n < 160; n++)
    {
        memset(data, (uint8_t)n, sizeof(data));
        assert(FlashFTL_WriteStart(&disk, 2, data, 1) == FLASH_FTL_OK);
        assert(finish() == FLASH_FTL_OK);
    }
    bind_disk();
    assert(FlashFTL_OpenStart(&disk) == FLASH_FTL_OK);
    assert(finish() == FLASH_FTL_OK);
    assert(FlashFTL_ReadStart(&disk, 2, actual, 1) == FLASH_FTL_OK);
    assert(finish() == FLASH_FTL_OK);
    assert(!memcmp(data, actual, sizeof(data)));
}

/**
 * @brief 建立独立用例所需的已格式化 Fake NOR。
 * @note 关闭擦写掉电注入和收尾暂忙，清空内存介质后执行显式格式化；不访问硬件。 断言失败即终止用例。
 */
static void format_fresh(void)
{
    cut_after = -1;
    quiesce_busy = 0;
    memset(nor, 255, sizeof(nor));
    bind_disk();
    assert(FlashFTL_FormatStart(&disk) == FLASH_FTL_OK);
    assert(finish() == FLASH_FTL_OK);
}

/**
 * @brief 遍历组记录的每个编程页与撕裂长度，验证恢复只暴露完整旧组或新组。
 * @note 在 16 个页位置分别注入 0/1/127/255/256 B 撕裂；每轮恢复同一介质快照。 断言失败即终止用例。
 */
static void power_cut_at_every_program_preserves_a_complete_group(void)
{
    static uint8_t saved[sizeof(nor)];
    uint8_t old[3584], fresh[3584], actual[3584];
    memset(old, 0x23, sizeof(old));
    memset(fresh, 0x54, sizeof(fresh));
    format_fresh();
    assert(FlashFTL_WriteStart(&disk, 0, old, 7) == FLASH_FTL_OK);
    assert(finish() == FLASH_FTL_OK);
    memcpy(saved, nor, sizeof(nor));
    const uint32_t tears[] = {0, 1, 127, 255, 256};
    for (uint32_t step = 0; step < 16; step++)
    {
        for (uint32_t b = 0; b < 5; b++)
        {
            memcpy(nor, saved, sizeof(nor));
            bind_disk();
            assert(FlashFTL_OpenStart(&disk) == FLASH_FTL_OK);
            assert(finish() == FLASH_FTL_OK);
            cut_after = (int)step;
            tear_bytes = tears[b];
            injected_fault = false;
            assert(FlashFTL_WriteStart(&disk, 0, fresh, 7) == FLASH_FTL_OK);
            assert(finish() == FLASH_FTL_IO_ERROR);
            assert(injected_fault);
            bind_disk();
            assert(FlashFTL_OpenStart(&disk) == FLASH_FTL_OK);
            assert(finish() == FLASH_FTL_OK);
            assert(FlashFTL_ReadStart(&disk, 0, actual, 7) == FLASH_FTL_OK);
            assert(finish() == FLASH_FTL_OK);
            assert(!memcmp(actual, old, sizeof(actual)) || !memcmp(actual, fresh, sizeof(actual)));
        }
    }
}

/**
 * @brief 遍历格式化擦写阶段的掉电，验证旧卷、新空卷或 INCOMPLETE 的恢复语义。
 * @note 每次从同一旧卷开始，撕裂长度为 128 B；可打开时数据不得混合旧值和擦除值。 断言失败即终止用例。
 */
static void interrupted_format_never_exposes_partly_erased_data(void)
{
    static uint8_t saved[sizeof(nor)];
    uint8_t old[512], actual[512], blank[512];
    memset(old, 0x35, 512);
    memset(blank, 255, 512);
    format_fresh();
    assert(FlashFTL_WriteStart(&disk, 0, old, 1) == FLASH_FTL_OK);
    assert(finish() == FLASH_FTL_OK);
    memcpy(saved, nor, sizeof(nor));
    for (uint32_t step = 0; step < BLOCKS + 4; step++)
    {
        memcpy(nor, saved, sizeof(nor));
        bind_disk();
        cut_after = (int)step;
        tear_bytes = 128;
        injected_fault = false;
        assert(FlashFTL_FormatStart(&disk) == FLASH_FTL_OK);
        FlashFTL_StatusTypeDef result = finish();
        assert(result == FLASH_FTL_IO_ERROR);
        assert(injected_fault);
        bind_disk();
        assert(FlashFTL_OpenStart(&disk) == FLASH_FTL_OK);
        result = finish();
        assert(result == FLASH_FTL_OK || result == FLASH_FTL_INCOMPLETE);
        if (result == FLASH_FTL_OK)
        {
            assert(FlashFTL_ReadStart(&disk, 0, actual, 1) == FLASH_FTL_OK);
            assert(finish() == FLASH_FTL_OK);
            assert(!memcmp(actual, old, 512) || !memcmp(actual, blank, 512));
        }
    }
}

/**
 * @brief 验证最新已提交版本的数据损坏必须报 CORRUPT。
 * @note 保留旧版本并破坏新版本 payload；重新打开不能静默回退旧数据。 断言失败即终止用例。
 */
static void latest_committed_payload_corruption_is_not_rolled_back(void)
{
    uint8_t old[512], fresh[512];
    memset(old, 0x28, 512);
    memset(fresh, 0x69, 512);
    format_fresh();
    assert(FlashFTL_WriteStart(&disk, 0, old, 1) == FLASH_FTL_OK);
    assert(finish() == FLASH_FTL_OK);
    assert(FlashFTL_WriteStart(&disk, 0, fresh, 1) == FLASH_FTL_OK);
    assert(finish() == FLASH_FTL_OK);
    bool found = false;
    for (uint32_t i = 8192; i < sizeof(nor); i += 4096)
    {
        if (!memcmp(nor + i + 256, fresh, 512))
        {
            nor[i + 256] ^= 1;
            found = true;
        }
    }
    assert(found);
    bind_disk();
    assert(FlashFTL_OpenStart(&disk) == FLASH_FTL_OK);
    assert(finish() == FLASH_FTL_CORRUPT);
}

/**
 * @brief 验证全 FF 数据跳过编程，以及故障收尾期间拒绝新请求。
 * @note 先核对仅编程头和提交页，再注入故障及三轮 Quiesce 暂忙，检查 WAIT/BUSY。 断言失败即终止用例。
 */
static void erased_payload_skips_programming_and_fault_retains_ownership(void)
{
    uint8_t data[3584];
    memset(data, 255, sizeof(data));
    format_fresh();
    uint32_t before = programs;
    assert(FlashFTL_WriteStart(&disk, 0, data, 7) == FLASH_FTL_OK);
    assert(finish() == FLASH_FTL_OK);
    assert(programs - before == 2);
    cut_after = 0;
    tear_bytes = 12;
    quiesce_busy = 3;
    assert(FlashFTL_WriteStart(&disk, 0, data, 7) == FLASH_FTL_OK);
    while (!injected_fault)
    {
        (void)FlashFTL_Process(&disk);
    }
    assert(FlashFTL_Process(&disk) == FLASH_FTL_WAIT);
    assert(FlashFTL_ReadStart(&disk, 0, data, 1) == FLASH_FTL_BUSY);
    assert(finish() == FLASH_FTL_IO_ERROR);
}

/**
 * @brief 验证旧版本块被 GC 擦到一半后，重启仍可读取最新提交。
 * @note 制造足够失效块，维护时注入 128 B 撕裂擦除，再重开核对最新扇区。 断言失败即终止用例。
 */
static void interrupted_gc_of_old_version_does_not_hide_latest_data(void)
{
    uint8_t data[512], actual[512];
    format_fresh();
    for (uint32_t i = 0; i < 58; i++)
    {
        memset(data, (int)i, 512);
        assert(FlashFTL_WriteStart(&disk, 0, data, 1) == FLASH_FTL_OK);
        assert(finish() == FLASH_FTL_OK);
    }
    cut_after = 0;
    tear_bytes = 128;
    assert(FlashFTL_MaintainStart(&disk) == FLASH_FTL_OK);
    assert(finish() == FLASH_FTL_IO_ERROR);
    bind_disk();
    assert(FlashFTL_OpenStart(&disk) == FLASH_FTL_OK);
    assert(finish() == FLASH_FTL_OK);
    assert(FlashFTL_ReadStart(&disk, 0, actual, 1) == FLASH_FTL_OK);
    assert(finish() == FLASH_FTL_OK);
    assert(!memcmp(data, actual, 512));
}

#ifdef FTL_TEST_FATFS
#include "ff_gen_drv.h"
#include "FATFS/Target/user_diskio.h"
#include "FATFS/Target/bsp_driver_user_diskio.h"

/* 主机后端直接绑定真实 FTL；替代硬件/任务环境，不替代 FatFs 或 USER Glue。 */
/**
 * @brief 将主机 USER 后端初始化状态映射为真实 FTL 就绪状态。
 * @param[in] lun 只允许驱动内编号 0；其他值触发断言。
 * @return FTL 就绪返回 0，否则返回 STA_NOINIT。
 * @note 仅测试替身，不启动硬件或格式化。
 */
DSTATUS BSP_USER_DISKIO_Init(BYTE lun)
{
    assert(lun == 0);
    return FlashFTL_IsReady(&disk) ? 0 : STA_NOINIT;
}

/**
 * @brief 查询主机 FTL 后端就绪状态。
 * @param[in] lun 驱动内编号，转交测试 Init 检查。
 * @return FTL 就绪返回 0，否则返回 STA_NOINIT。
 */
DSTATUS BSP_USER_DISKIO_GetStatus(BYTE lun)
{
    return BSP_USER_DISKIO_Init(lun);
}

/**
 * @brief 同步驱动真实 FTL 完成测试用扇区读取。
 * @param[in] lun 驱动内编号，必须为 0。
 * @param[out] p 至少 count * 512 B 的有效缓冲。
 * @param[in] sector 起始逻辑扇区号。
 * @param[in] count 逻辑扇区数。
 * @return Start 和最终 Process 均成功为 RES_OK，否则为 RES_ERROR。
 * @note 用于真实 FatFs 主机集成，取代 Service/RTOS 等待，不访问硬件。
 */
DRESULT BSP_USER_DISKIO_ReadBlocks(BYTE lun, BYTE *p, DWORD sector, UINT count)
{
    assert(lun == 0);
    return FlashFTL_ReadStart(&disk, sector, p, count) == FLASH_FTL_OK && finish() == FLASH_FTL_OK
               ? RES_OK
               : RES_ERROR;
}

/**
 * @brief 同步驱动真实 FTL 完成测试用扇区提交。
 * @param[in] lun 驱动内编号，必须为 0。
 * @param[in] p 至少 count * 512 B 的有效缓冲。
 * @param[in] sector 起始逻辑扇区号。
 * @param[in] count 逻辑扇区数。
 * @return Start 和最终 Process 均成功为 RES_OK，否则为 RES_ERROR。
 * @note 用于真实 FatFs 主机集成，取代 Service/RTOS 等待，不访问硬件。
 */
DRESULT BSP_USER_DISKIO_WriteBlocks(BYTE lun, const BYTE *p, DWORD sector, UINT count)
{
    assert(lun == 0);
    return FlashFTL_WriteStart(&disk, sector, p, count) == FLASH_FTL_OK && finish() == FLASH_FTL_OK
               ? RES_OK
               : RES_ERROR;
}

/**
 * @brief 为真实 FatFs 主机测试提供同步及逻辑几何命令。
 * @param[in] lun 驱动内编号，必须为 0。
 * @param[in] command CTRL_SYNC 或 GET_SECTOR_COUNT/GET_BLOCK_SIZE/GET_SECTOR_SIZE。
 * @param[out] p 几何命令的有效 DWORD/WORD 输出；CTRL_SYNC 不访问。
 * @return 命令成功返回 RES_OK，同步失败返回 RES_ERROR，未知命令返回 RES_PARERR。
 */
DRESULT BSP_USER_DISKIO_Ioctl(BYTE lun, BYTE command, void *p)
{
    FlashFTL_InfoTypeDef info;
    assert(lun == 0);
    assert(FlashFTL_GetInfo(&disk, &info) == FLASH_FTL_OK);
    switch (command)
    {
        case CTRL_SYNC:
            return FlashFTL_SyncStart(&disk) == FLASH_FTL_OK && finish() == FLASH_FTL_OK
                       ? RES_OK
                       : RES_ERROR;
        case GET_SECTOR_COUNT:
            *(DWORD *)p = info.SectorCount;
            return RES_OK;
        case GET_BLOCK_SIZE:
            *(DWORD *)p = 1;
            return RES_OK;
        case GET_SECTOR_SIZE:
            *(WORD *)p = 512;
            return RES_OK;
        default:
            return RES_PARERR;
    }
}

/**
 * @brief 验证真实 FatFs 经 USER Glue 和 FTL 写入同步后，重挂载仍保持文件内容。
 * @note 仅 FTL_TEST_FATFS 配置编译；使用测试 BSP 替代任务与硬件，测试文件为 16 KiB。 断言失败即终止用例。
 */
static void fatfs_user_diskio_file_survives_remount(void)
{
    FATFS fs;
    FIL file;
    UINT bytes;
    uint8_t input[16384], actual[16384], mkfs[4096];
    char placeholder[4], path[4];
    const TCHAR drive[] = {'1', ':', 0};
    const TCHAR filename[] = {'1', ':', '/', 't', 'e', 's', 't', '.', 'b', 'i', 'n', 0};
    format_fresh();
    assert(FATFS_LinkDriver(&USER_Driver, placeholder) == 0);
    assert(FATFS_LinkDriver(&USER_Driver, path) == 0);
    assert(path[0] == '1');
    assert(f_mkfs(drive, FM_FAT | FM_SFD, 0, mkfs, sizeof(mkfs)) == FR_OK);
    assert(f_mount(&fs, drive, 1) == FR_OK);
    for (uint32_t i = 0; i < sizeof(input); i++)
    {
        input[i] = (uint8_t)(i * 13 + i / 512);
    }
    assert(f_open(&file, filename, FA_CREATE_ALWAYS | FA_WRITE) == FR_OK);
    assert(f_write(&file, input, sizeof(input), &bytes) == FR_OK && bytes == sizeof(input));
    assert(f_sync(&file) == FR_OK);
    assert(f_close(&file) == FR_OK);
    assert(f_mount(NULL, drive, 0) == FR_OK);
    bind_disk();
    assert(FlashFTL_OpenStart(&disk) == FLASH_FTL_OK);
    assert(finish() == FLASH_FTL_OK);
    memset(&fs, 0, sizeof(fs));
    assert(f_mount(&fs, drive, 1) == FR_OK);
    assert(f_open(&file, filename, FA_READ) == FR_OK);
    assert(f_read(&file, actual, sizeof(actual), &bytes) == FR_OK && bytes == sizeof(actual));
    assert(!memcmp(input, actual, sizeof(input)));
    assert(f_close(&file) == FR_OK);
    assert(f_mount(NULL, drive, 0) == FR_OK);
    puts("FatFs -> USER DiskIO -> BSP -> FTL: remount content verified");
}
#endif

/**
 * @brief 验证溢出、越界和在飞请求被拒绝，同时跨组写入保留邻居。
 * @note 覆盖 UINT32_MAX、零长度、尾 LBA、十扇区跨组写及重启读回；非法请求不增加编程计数。 断言失败即终止用例。
 */
static void bounds_busy_and_cross_group_io_preserve_neighbors(void)
{
    uint8_t input[10 * 512], actual[14 * 512];
    FlashFTL_InfoTypeDef info;
    format_fresh();
    assert(FlashFTL_GetInfo(&disk, &info) == FLASH_FTL_OK);
    uint32_t before = programs;
    assert(FlashFTL_WriteStart(&disk, UINT32_MAX, input, 1) == FLASH_FTL_INVALID_PARAM);
    assert(FlashFTL_WriteStart(&disk, info.SectorCount - 1, input, 2) == FLASH_FTL_INVALID_PARAM);
    assert(FlashFTL_ReadStart(&disk, 0, actual, UINT32_MAX) == FLASH_FTL_INVALID_PARAM);
    assert(FlashFTL_ReadStart(&disk, 0, actual, 0) == FLASH_FTL_INVALID_PARAM);
    assert(programs == before && FlashFTL_IsReady(&disk));
    for (uint32_t i = 0; i < sizeof(input); i++)
    {
        input[i] = (uint8_t)(i * 17 + i / 512);
    }
    assert(FlashFTL_WriteStart(&disk, 3, input, 10) == FLASH_FTL_OK);
    assert(FlashFTL_SyncStart(&disk) == FLASH_FTL_BUSY);
    assert(FlashFTL_ReadStart(&disk, 0, actual, 1) == FLASH_FTL_BUSY);
    assert(finish() == FLASH_FTL_OK);
    assert(FlashFTL_WriteStart(&disk, info.SectorCount - 1, input, 1) == FLASH_FTL_OK);
    assert(finish() == FLASH_FTL_OK);
    bind_disk();
    assert(FlashFTL_OpenStart(&disk) == FLASH_FTL_OK);
    assert(finish() == FLASH_FTL_OK);
    assert(FlashFTL_ReadStart(&disk, 0, actual, 14) == FLASH_FTL_OK);
    assert(finish() == FLASH_FTL_OK);
    for (uint32_t i = 0; i < 3 * 512; i++)
    {
        assert(actual[i] == 255);
    }
    assert(!memcmp(actual + 3 * 512, input, sizeof(input)));
    for (uint32_t i = 13 * 512; i < sizeof(actual); i++)
    {
        assert(actual[i] == 255);
    }
    assert(FlashFTL_ReadStart(&disk, info.SectorCount - 1, actual, 1) == FLASH_FTL_OK);
    assert(finish() == FLASH_FTL_OK && !memcmp(actual, input, 512));
}

/**
 * @brief 验证后台维护单次最多擦一个块，并在达到高水位后停止。
 * @note 构造低空闲水位，通过擦除计数与 FreeBlocks 快照验证维护预算和滞回停止。 断言失败即终止用例。
 */
static void maintenance_is_bounded_and_stops_at_high_watermark(void)
{
    uint8_t data[512];
    format_fresh();
    for (uint32_t i = 0; i < 58; i++)
    {
        memset(data, (int)i, sizeof(data));
        assert(FlashFTL_WriteStart(&disk, 0, data, 1) == FLASH_FTL_OK);
        assert(finish() == FLASH_FTL_OK);
    }
    uint32_t before = erases;
    assert(FlashFTL_MaintainStart(&disk) == FLASH_FTL_OK);
    assert(finish() == FLASH_FTL_OK);
    assert(erases == before + 1);
    for (uint32_t i = 0; i < 20; i++)
    {
        before = erases;
        assert(FlashFTL_MaintainStart(&disk) == FLASH_FTL_OK);
        assert(finish() == FLASH_FTL_OK);
        assert(erases - before <= 1);
    }
    FlashFTL_DiagnosticsTypeDef d;
    assert(FlashFTL_GetDiagnostics(&disk, &d) == FLASH_FTL_OK);
    assert(d.FreeBlocks == 7);
    before = erases;
    assert(FlashFTL_MaintainStart(&disk) == FLASH_FTL_OK);
    assert(finish() == FLASH_FTL_OK);
    assert(erases == before);
}

/**
 * @brief 验证卷头擦除虚假成功会被全块回读发现。
 * @note 仅擦首 256 B 后谎报成功，期望 CORRUPT 且不继续其他块擦除。 断言失败即终止用例。
 */
static void volume_erase_must_be_verified_before_preparing(void)
{
    format_fresh();
    uint32_t before = erases;
    silent_partial_erase = true;
    assert(FlashFTL_FormatStart(&disk) == FLASH_FTL_OK);
    assert(finish() == FLASH_FTL_CORRUPT);
    assert(erases == before + 1);
}

/**
 * @brief 运行 FTL 行为回归，并按编译开关运行真实 FatFs 重挂载测试。
 * @return 全部断言通过返回 0；任一断言失败则终止进程。
 * @note 仅操作内存 Fake NOR，不代表真实 QSPI、任务调度或掉电验收。
 */
int main(void)
{
    blank_media_is_not_formatted_implicitly();
    explicit_format_survives_restart_and_reads_erased_sectors();
    partial_group_update_survives_restart();
    repeated_overwrite_reclaims_space_without_losing_data();
    power_cut_at_every_program_preserves_a_complete_group();
    interrupted_format_never_exposes_partly_erased_data();
    latest_committed_payload_corruption_is_not_rolled_back();
    injected_fault = false;
    erased_payload_skips_programming_and_fault_retains_ownership();
    interrupted_gc_of_old_version_does_not_hide_latest_data();
#ifdef FTL_TEST_FATFS
    fatfs_user_diskio_file_survives_remount();
#endif
    bounds_busy_and_cross_group_io_preserve_neighbors();
    maintenance_is_bounded_and_stops_at_high_watermark();
    volume_erase_must_be_verified_before_preparing();
    puts("flash_ftl: all behavior tests passed");
    return 0;
}
