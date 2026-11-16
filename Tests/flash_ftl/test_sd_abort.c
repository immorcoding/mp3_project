/**
 * @file test_sd_abort.c
 * @brief SD Card Component 的显式中止（`SDCard_AbortTransfer`）：状态转换与 Port 契约。
 *
 * 真实的 `Components/sd/sd.c`，Port 层用测试内的假实现（可脚本化中止结果、在位与否）。接缝是 Component 的
 * 公开 Interface：断言 Device 的持续状态、返回值、诊断快照与对 Port 的调用，不看 Component 内部字段。
 * 主机测试证明状态机的分支走对；不证明 HAL 寄存器序列与真实 DMA（那是 Adapter 的范围，靠板级验收）。
 */

#include "Components/sd/sd.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define FAKE_BLOCK_COUNT 1024U
#define FAKE_BLOCK_SIZE 512U

/** @brief 假 Port 的脚本与观察值。 */
typedef struct
{
    bool present;                          /**< 卡检测。 */
    SDCard_PortStatusTypeDef abort_status; /**< AbortTransfer 的返回值。 */
    bool abort_op_installed;               /**< false 时不安装 AbortTransfer（Port 不支持）。 */
    uint32_t abort_calls;                  /**< AbortTransfer 次数。 */
    uint32_t last_abort_timeout_ms;        /**< 最近一次 AbortTransfer 收到的上界。 */
    uint32_t init_calls;                   /**< Init 次数。 */
    uint32_t deinit_calls;                 /**< DeInit 次数。 */
    uint32_t start_read_calls;             /**< StartReadBlocks 次数。 */
    uint32_t start_write_calls;            /**< StartWriteBlocks 次数。 */
    uint32_t sync_calls;                   /**< Sync 次数。 */
} FakePort;

static FakePort fake;
static SDCard_PortOpsTypeDef ops;
static SDCard_HandleTypeDef device;

/**
 * @brief 假 Port：卡检测。
 * @param[in] context 与 PortOps 成对绑定的对象（未使用）。
 * @return 脚本里的在位与否。
 */
static bool fake_is_present(const void *context)
{
    (void)context;
    return fake.present;
}

/**
 * @brief 假 Port：初始化。
 * @param[in,out] context 与 PortOps 成对绑定的对象（未使用）。
 * @retval SDCARD_PORT_OK 卡在位。
 * @retval SDCARD_PORT_NOT_PRESENT 卡不在位。
 */
static SDCard_PortStatusTypeDef fake_init(void *context)
{
    (void)context;
    fake.init_calls++;
    return fake.present ? SDCARD_PORT_OK : SDCARD_PORT_NOT_PRESENT;
}

/**
 * @brief 假 Port：反初始化，只计数。
 * @param[in,out] context 与 PortOps 成对绑定的对象（未使用）。
 * @return 固定 SDCARD_PORT_OK。
 */
static SDCard_PortStatusTypeDef fake_deinit(void *context)
{
    (void)context;
    fake.deinit_calls++;
    return SDCARD_PORT_OK;
}

/**
 * @brief 假 Port：给出固定容量。
 * @param[in,out] context 与 PortOps 成对绑定的对象（未使用）。
 * @param[out] info 接收介质信息。
 * @return 固定 SDCARD_PORT_OK。
 */
static SDCard_PortStatusTypeDef fake_get_info(void *context, SDCard_InfoTypeDef *info)
{
    (void)context;
    info->BlockCount = FAKE_BLOCK_COUNT;
    info->BlockSize = FAKE_BLOCK_SIZE;
    info->CapacityBytes = (uint64_t)FAKE_BLOCK_COUNT * FAKE_BLOCK_SIZE;
    return SDCARD_PORT_OK;
}

/**
 * @brief 假 Port：阻塞读，本测试不使用，只满足基础绑定条件。
 * @param[in,out] context 与 PortOps 成对绑定的对象（未使用）。
 * @param[out] data 接收缓冲（未使用）。
 * @param[in] start 起始块（未使用）。
 * @param[in] count 块数（未使用）。
 * @param[in] timeout_ms 上界（未使用）。
 * @return 固定 SDCARD_PORT_OK。
 */
static SDCard_PortStatusTypeDef fake_read(void *context, uint8_t *data, uint32_t start, uint32_t count,
                                          uint32_t timeout_ms)
{
    (void)context;
    (void)data;
    (void)start;
    (void)count;
    (void)timeout_ms;
    return SDCARD_PORT_OK;
}

/**
 * @brief 假 Port：阻塞写，本测试不使用，只满足基础绑定条件。
 * @param[in,out] context 与 PortOps 成对绑定的对象（未使用）。
 * @param[in] data 数据（未使用）。
 * @param[in] start 起始块（未使用）。
 * @param[in] count 块数（未使用）。
 * @param[in] timeout_ms 上界（未使用）。
 * @return 固定 SDCARD_PORT_OK。
 */
static SDCard_PortStatusTypeDef fake_write(void *context, const uint8_t *data, uint32_t start, uint32_t count,
                                           uint32_t timeout_ms)
{
    (void)context;
    (void)data;
    (void)start;
    (void)count;
    (void)timeout_ms;
    return SDCARD_PORT_OK;
}

/**
 * @brief 假 Port：启动 DMA 读，只计数。
 * @param[in,out] context 与 PortOps 成对绑定的对象（未使用）。
 * @param[out] data 接收缓冲（未使用）。
 * @param[in] start 起始块（未使用）。
 * @param[in] count 块数（未使用）。
 * @return 固定 SDCARD_PORT_OK。
 */
static SDCard_PortStatusTypeDef fake_start_read(void *context, uint8_t *data, uint32_t start, uint32_t count)
{
    (void)context;
    (void)data;
    (void)start;
    (void)count;
    fake.start_read_calls++;
    return SDCARD_PORT_OK;
}

/**
 * @brief 假 Port：启动 DMA 写，只计数。
 * @param[in,out] context 与 PortOps 成对绑定的对象（未使用）。
 * @param[in] data 数据（未使用）。
 * @param[in] start 起始块（未使用）。
 * @param[in] count 块数（未使用）。
 * @return 固定 SDCARD_PORT_OK。
 */
static SDCard_PortStatusTypeDef fake_start_write(void *context, const uint8_t *data, uint32_t start, uint32_t count)
{
    (void)context;
    (void)data;
    (void)start;
    (void)count;
    fake.start_write_calls++;
    return SDCARD_PORT_OK;
}

/**
 * @brief 假 Port：等待卡回到 TRANSFER，只计数。
 * @param[in,out] context 与 PortOps 成对绑定的对象（未使用）。
 * @param[in] timeout_ms 上界（未使用）。
 * @return 固定 SDCARD_PORT_OK。
 */
static SDCard_PortStatusTypeDef fake_sync(void *context, uint32_t timeout_ms)
{
    (void)context;
    (void)timeout_ms;
    fake.sync_calls++;
    return SDCARD_PORT_OK;
}

/**
 * @brief 假 Port：显式中止，记录调用与 Component 传下来的上界，返回脚本里的结果。
 * @param[in,out] context 与 PortOps 成对绑定的对象（未使用）。
 * @param[in] timeout_ms Component 传给 Port 的中止上界。
 * @return 脚本里的 abort_status。
 */
static SDCard_PortStatusTypeDef fake_abort(void *context, uint32_t timeout_ms)
{
    (void)context;
    fake.abort_calls++;
    fake.last_abort_timeout_ms = timeout_ms;
    return fake.abort_status;
}

static uint8_t dma_buffer[FAKE_BLOCK_SIZE * 64U];

/**
 * @brief 每个用例开头：重建假 Port 与 Device，Init 到 READY。
 */
static void begin_test(void)
{
    memset(&fake, 0, sizeof(fake));
    fake.present = true;
    fake.abort_status = SDCARD_PORT_OK;
    fake.abort_op_installed = true;

    memset(&ops, 0, sizeof(ops));
    ops.IsPresent = fake_is_present;
    ops.Init = fake_init;
    ops.DeInit = fake_deinit;
    ops.GetInfo = fake_get_info;
    ops.ReadBlocks = fake_read;
    ops.WriteBlocks = fake_write;
    ops.StartReadBlocks = fake_start_read;
    ops.StartWriteBlocks = fake_start_write;
    ops.Sync = fake_sync;
    ops.AbortTransfer = fake_abort;

    memset(&device, 0, sizeof(device));
    device.PortOps = &ops;
    device.PortContext = &fake;
    assert(SDCard_Init(&device) == SDCARD_OK);
    assert(SDCard_GetState(&device) == SDCARD_STATE_READY);
}

/**
 * @brief 开一笔 DMA 读，让 Device 进入 BUSY（模拟“DMA 在飞、等待超时”）。
 */
static void start_read_in_flight(void)
{
    assert(SDCard_StartReadBlocks(&device, dma_buffer, 100U, 64U) == SDCARD_OK);
    assert(SDCard_GetState(&device) == SDCARD_STATE_BUSY);
}

/**
 * @brief 在飞的传输被中止并停住：Device 回到 READY，紧接着可以再启动一笔，Port 只收到一次中止且带有限上界。
 */
static void test_abort_in_flight_transfer_returns_device_to_ready(void)
{
    begin_test();
    start_read_in_flight();

    assert(SDCard_AbortTransfer(&device) == SDCARD_OK);

    assert(SDCard_GetState(&device) == SDCARD_STATE_READY);
    assert(fake.abort_calls == 1U);
    assert(fake.last_abort_timeout_ms > 0U);
    assert(fake.last_abort_timeout_ms <= 1000U);
    /* 紧接着再读（R2.2 的“超时后紧接着再读”）：不需要重新初始化。 */
    assert(SDCard_StartReadBlocks(&device, dma_buffer, 200U, 64U) == SDCARD_OK);
    assert(fake.start_read_calls == 2U);
    assert(fake.init_calls == 1U);
}

/**
 * @brief 没有在飞的传输（Device 不在 BUSY）：拒绝，Port 不被调用，状态不变；空句柄安全。
 */
static void test_abort_without_transfer_in_flight_is_rejected(void)
{
    begin_test();

    assert(SDCard_AbortTransfer(&device) == SDCARD_ERROR);

    assert(fake.abort_calls == 0U);
    assert(SDCard_GetState(&device) == SDCARD_STATE_READY);
    assert(device.ErrorCode == SDCARD_ERROR_NOT_READY);
    assert(SDCard_AbortTransfer(NULL) == SDCARD_ERROR);
}

/**
 * @brief 中止时发现卡已拔出：Device 进入 NOT_PRESENT、缓存的介质信息失效；Port 已自行反初始化，
 *        Device 之后不再重复反初始化，重新插卡才初始化。
 */
static void test_abort_with_card_removed_reports_no_media(void)
{
    SDCard_InfoTypeDef info;

    begin_test();
    start_read_in_flight();
    fake.present = false;
    fake.abort_status = SDCARD_PORT_NOT_PRESENT;

    assert(SDCard_AbortTransfer(&device) == SDCARD_ERROR);

    assert(SDCard_GetState(&device) == SDCARD_STATE_NOT_PRESENT);
    assert(device.ErrorCode == SDCARD_ERROR_NOT_PRESENT);
    assert(SDCard_GetInfo(&device, &info) == SDCARD_ERROR);

    /* Port 已经反初始化：卡还不在时刷新不会再对它反初始化一次。 */
    assert(SDCard_Refresh(&device) == SDCARD_OK);
    assert(fake.deinit_calls == 0U);
    assert(SDCard_GetState(&device) == SDCARD_STATE_NOT_PRESENT);

    /* 重新插卡：走正常初始化，回到 READY。 */
    fake.present = true;
    assert(SDCard_Refresh(&device) == SDCARD_OK);
    assert(SDCard_GetState(&device) == SDCARD_STATE_READY);
    assert(fake.init_calls == 2U);
}

/**
 * @brief 到上界还没停住：卷保持 ERROR，不自动重试，也不接受新的传输；Port 已反初始化，Device 不再重复反初始化。
 *        恢复路径有两条：显式再 Init，或拔卡后重新插卡。
 */
static void test_abort_that_does_not_stop_keeps_the_device_in_error(void)
{
    begin_test();
    start_read_in_flight();
    fake.abort_status = SDCARD_PORT_TIMEOUT;

    assert(SDCard_AbortTransfer(&device) == SDCARD_ERROR);

    assert(SDCard_GetState(&device) == SDCARD_STATE_ERROR);
    assert(device.ErrorCode == SDCARD_ERROR_PORT_ABORT);
    assert(device.LastPortStatus == SDCARD_PORT_TIMEOUT);
    /* 新的传输被拒绝，刷新不会自动重试。 */
    assert(SDCard_StartReadBlocks(&device, dma_buffer, 0U, 1U) == SDCARD_ERROR);
    assert(fake.start_read_calls == 1U);
    assert(SDCard_Refresh(&device) == SDCARD_ERROR);
    assert(SDCard_GetState(&device) == SDCARD_STATE_ERROR);
    assert(fake.init_calls == 1U);
    assert(fake.deinit_calls == 0U);

    /* 恢复路径一：显式再 Init，直接初始化 Port（它已经反初始化）。 */
    assert(SDCard_Init(&device) == SDCARD_OK);
    assert(SDCard_GetState(&device) == SDCARD_STATE_READY);
    assert(fake.init_calls == 2U);
    assert(fake.deinit_calls == 0U);
}

/**
 * @brief 恢复路径二：停不住之后拔卡、再插卡，回到 READY，Port 不被重复反初始化。
 */
static void test_device_left_in_error_by_abort_recovers_after_reinsertion(void)
{
    begin_test();
    start_read_in_flight();
    fake.abort_status = SDCARD_PORT_TIMEOUT;
    assert(SDCard_AbortTransfer(&device) == SDCARD_ERROR);

    fake.present = false;
    assert(SDCard_Refresh(&device) == SDCARD_OK);
    assert(SDCard_GetState(&device) == SDCARD_STATE_NOT_PRESENT);
    assert(fake.deinit_calls == 0U);

    fake.present = true;
    assert(SDCard_Refresh(&device) == SDCARD_OK);
    assert(SDCard_GetState(&device) == SDCARD_STATE_READY);
    assert(fake.init_calls == 2U);
}

/**
 * @brief Port 不支持中止：不能让 Device 永远停在 BUSY，进入 ERROR 并记 PORT_NOT_BOUND，不调用空指针。
 */
static void test_abort_on_port_without_abort_operation_fails_into_error(void)
{
    begin_test();
    start_read_in_flight();
    ops.AbortTransfer = NULL;

    assert(SDCard_AbortTransfer(&device) == SDCARD_ERROR);

    assert(SDCard_GetState(&device) == SDCARD_STATE_ERROR);
    assert(device.ErrorCode == SDCARD_ERROR_PORT_NOT_BOUND);
    assert(fake.abort_calls == 0U);
}

/**
 * @brief 依次运行全部用例。
 * @return 全部通过时为 0，断言失败时进程中止。
 */
int main(void)
{
    test_abort_in_flight_transfer_returns_device_to_ready();
    test_abort_without_transfer_in_flight_is_rejected();
    test_abort_with_card_removed_reports_no_media();
    test_abort_that_does_not_stop_keeps_the_device_in_error();
    test_device_left_in_error_by_abort_recovers_after_reinsertion();
    test_abort_on_port_without_abort_operation_fails_into_error();
    printf("sd_component_abort: all passed\n");
    return 0;
}
