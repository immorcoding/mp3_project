/**
 ******************************************************************************
 * @file    test_w25qxx.c
 * @brief   W25Qxx Component 的主机行为测试。
 ******************************************************************************
 */

#include "Components/w25qxx/w25qxx.h"
#include "Adapters/bridge/flash_ftl_w25qxx/flash_ftl_w25qxx_bridge.h"
#include "Components/w25qxx/w25qxx_config.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#define W25QXX_FAKE_STATUS_SEQUENCE_LENGTH  4u

typedef struct
{
    uint8_t ExpectedInstruction;
    W25Qxx_JedecIDTypeDef Response;
    bool WasCalled;
    uint8_t ExpectedStatusRegister1Instruction;
    uint8_t StatusRegister1Response;
    bool StatusRegister1WasCalled;
    uint8_t StatusRegister1ResponseSequence[W25QXX_FAKE_STATUS_SEQUENCE_LENGTH];
    uint32_t StatusRegister1ResponseCount;
    uint32_t StatusRegister1ResponseIndex;
    uint8_t ExpectedStatusRegister2Instruction;
    uint8_t StatusRegister2Response;
    bool StatusRegister2WasCalled;
    uint8_t StatusRegister2ResponseSequence[W25QXX_FAKE_STATUS_SEQUENCE_LENGTH];
    uint32_t StatusRegister2ResponseCount;
    uint32_t StatusRegister2ResponseIndex;
    uint8_t ExpectedExecuteInstruction;
    bool ExecuteWasCalled;
    uint8_t ExpectedAddressedExecuteInstruction;
    uint32_t ExpectedAddressedExecuteAddress;
    W25Qxx_BusAddressedTransferConfigTypeDef ExpectedAddressedExecuteTransferConfig;
    bool AddressedExecuteWasCalled;
    uint8_t ExpectedWriteInstruction;
    uint8_t ExpectedWriteData;
    bool WriteWasCalled;
    uint32_t TickMs;
    uint8_t ExpectedAddressedInstruction;
    uint32_t ExpectedAddress;
    W25Qxx_BusAddressedTransferConfigTypeDef ExpectedAddressedTransferConfig;
    uint8_t AddressedResponse[4];
    bool AddressedWasCalled;
    uint8_t ExpectedAsyncAddressedInstruction;
    uint32_t ExpectedAsyncAddress;
    W25Qxx_BusAddressedTransferConfigTypeDef ExpectedAsyncAddressedTransferConfig;
    uint32_t ExpectedAsyncAddressedDataLength;
    bool AsyncAddressedWasCalled;
    W25Qxx_BusStatusTypeDef AsyncAddressedStatus;
    uint8_t ExpectedStatusPollingInstruction;
    uint8_t ExpectedStatusPollingMatch;
    uint8_t ExpectedStatusPollingMask;
    bool StatusPollingWasCalled;
    bool StatusPollingAbortWasCalled;
    W25Qxx_BusStatusTypeDef StatusPollingStatus;
    W25Qxx_BusStatusTypeDef StatusPollingAbortStatus;
    uint8_t ExpectedAddressedWriteInstruction;
    uint32_t ExpectedAddressedWriteAddress;
    W25Qxx_BusAddressedTransferConfigTypeDef ExpectedAddressedWriteTransferConfig;
    uint8_t ExpectedAddressedWriteData[W25QXX_PAGE_PROGRAM_MAX_SIZE_BYTES];
    uint32_t ExpectedAddressedWriteDataLength;
    bool AddressedWriteWasCalled;
} W25Qxx_FakeBusTypeDef;

/* 当前板级 W25Q256 只约束厂商和容量；MemoryType 的 0x40 / 0x70 均可接受。 */
static const W25Qxx_ExpectedJedecIDTypeDef test_w25q256_expected_id = {
    .ManufacturerID = W25QXX_MANUFACTURER_ID_WINBOND,
    .CapacityID = W25QXX_CAPACITY_ID_256MBIT
};

/**
 * @brief 模拟 JEDEC ID 和状态寄存器读取，按配置提供响应序列。
 * @param[in,out] context 用例配置的 Fake Bus Context；不访问实际 QSPI。
 * @param[in] instruction 待核对的指令字节。
 * @param[out] data 至少 data_length 字节的接收缓冲。
 * @param[in] data_length 数据字节数，须匹配用例期望。
 * @return 匹配指令及长度返回 BUS_OK，否则返回 BUS_ERROR。
 * @note 状态响应序列耗尽后回落到固定值，支持 WIP/QE 不同阶段测试。
 */
static W25Qxx_BusStatusTypeDef test_w25qxx_read_command(
    void *context,
    uint8_t instruction,
    uint8_t *data,
    uint32_t data_length)
{
    W25Qxx_FakeBusTypeDef *fake_bus = (W25Qxx_FakeBusTypeDef *)context;

    if ((fake_bus == NULL) || (data == NULL))
    {
        return W25QXX_BUS_ERROR;
    }

    if ((instruction == fake_bus->ExpectedInstruction) &&
        (data_length == sizeof(fake_bus->Response)))
    {
        fake_bus->WasCalled = true;
        (void)memcpy(data, &fake_bus->Response, sizeof(fake_bus->Response));
        return W25QXX_BUS_OK;
    }

    if ((instruction == fake_bus->ExpectedStatusRegister1Instruction) &&
        (data_length == sizeof(fake_bus->StatusRegister1Response)))
    {
        fake_bus->StatusRegister1WasCalled = true;
        if (fake_bus->StatusRegister1ResponseIndex <
            fake_bus->StatusRegister1ResponseCount)
        {
            *data = fake_bus->StatusRegister1ResponseSequence[
                fake_bus->StatusRegister1ResponseIndex++];
        }
        else
        {
            *data = fake_bus->StatusRegister1Response;
        }

        return W25QXX_BUS_OK;
    }

    if ((instruction == fake_bus->ExpectedStatusRegister2Instruction) &&
        (data_length == sizeof(fake_bus->StatusRegister2Response)))
    {
        fake_bus->StatusRegister2WasCalled = true;
        if (fake_bus->StatusRegister2ResponseIndex <
            fake_bus->StatusRegister2ResponseCount)
        {
            *data = fake_bus->StatusRegister2ResponseSequence[
                fake_bus->StatusRegister2ResponseIndex++];
        }
        else
        {
            *data = fake_bus->StatusRegister2Response;
        }

        return W25QXX_BUS_OK;
    }

    return W25QXX_BUS_ERROR;
}

/**
 * @brief 核对无地址命令并记录调用，用于写使能验证。
 * @param[in,out] context 用例配置的 Fake Bus Context；不访问实际 QSPI。
 * @param[in] instruction 待核对的指令字节。
 * @return Context 和指令匹配返回 BUS_OK，否则返回 BUS_ERROR。
 */
static W25Qxx_BusStatusTypeDef test_w25qxx_execute_command(
    void *context,
    uint8_t instruction)
{
    W25Qxx_FakeBusTypeDef *fake_bus = (W25Qxx_FakeBusTypeDef *)context;

    if ((fake_bus == NULL) ||
        (instruction != fake_bus->ExpectedExecuteInstruction))
    {
        return W25QXX_BUS_ERROR;
    }

    fake_bus->ExecuteWasCalled = true;
    return W25QXX_BUS_OK;
}

/**
 * @brief 核对带地址无数据命令及协议阶段，用于扇区擦除测试。
 * @param[in,out] context 用例配置的 Fake Bus Context；不访问实际 QSPI。
 * @param[in] instruction 待核对的指令字节。
 * @param[in] address 待核对的芯片字节地址。
 * @param[in] transfer_config 地址长度、线宽、模式字节及 dummy cycle 配置，须匹配期望。
 * @return 全部匹配返回 BUS_OK，否则返回 BUS_ERROR；成功时登记已调用。
 */
static W25Qxx_BusStatusTypeDef test_w25qxx_execute_addressed_command(
    void *context,
    uint8_t instruction,
    uint32_t address,
    const W25Qxx_BusAddressedTransferConfigTypeDef *transfer_config)
{
    W25Qxx_FakeBusTypeDef *fake_bus = (W25Qxx_FakeBusTypeDef *)context;

    if ((fake_bus == NULL) ||
        (transfer_config == NULL) ||
        (instruction != fake_bus->ExpectedAddressedExecuteInstruction) ||
        (address != fake_bus->ExpectedAddressedExecuteAddress) ||
        (transfer_config->AddressLength !=
         fake_bus->ExpectedAddressedExecuteTransferConfig.AddressLength) ||
        (transfer_config->AddressLineMode !=
         fake_bus->ExpectedAddressedExecuteTransferConfig.AddressLineMode) ||
        (transfer_config->DataLineMode !=
         fake_bus->ExpectedAddressedExecuteTransferConfig.DataLineMode) ||
        (transfer_config->HasAlternateByte !=
         fake_bus->ExpectedAddressedExecuteTransferConfig.HasAlternateByte) ||
        (transfer_config->AlternateByte !=
         fake_bus->ExpectedAddressedExecuteTransferConfig.AlternateByte) ||
        (transfer_config->AlternateByteLineMode !=
         fake_bus->ExpectedAddressedExecuteTransferConfig.AlternateByteLineMode) ||
        (transfer_config->DummyCycles !=
         fake_bus->ExpectedAddressedExecuteTransferConfig.DummyCycles))
    {
        return W25QXX_BUS_ERROR;
    }

    fake_bus->AddressedExecuteWasCalled = true;
    return W25QXX_BUS_OK;
}

/**
 * @brief 核对单字节寄存器写入，验证 QE 配置命令。
 * @param[in,out] context 用例配置的 Fake Bus Context；不访问实际 QSPI。
 * @param[in] instruction 待核对的指令字节。
 * @param[in] data 有效输入，期望一个寄存器值字节。
 * @param[in] data_length 数据字节数，须匹配用例期望。
 * @return 指令、长度和值匹配返回 BUS_OK，否则返回 BUS_ERROR。
 */
static W25Qxx_BusStatusTypeDef test_w25qxx_write_command(
    void *context,
    uint8_t instruction,
    const uint8_t *data,
    uint32_t data_length)
{
    W25Qxx_FakeBusTypeDef *fake_bus = (W25Qxx_FakeBusTypeDef *)context;

    if ((fake_bus == NULL) ||
        (data == NULL) ||
        (instruction != fake_bus->ExpectedWriteInstruction) ||
        (data_length != 1u) ||
        (*data != fake_bus->ExpectedWriteData))
    {
        return W25QXX_BUS_ERROR;
    }

    fake_bus->WriteWasCalled = true;
    return W25QXX_BUS_OK;
}

/**
 * @brief 提供由测试用例控制的毫秒时钟。
 * @param[in] context Fake Bus Context，可为 NULL。
 * @return 返回 TickMs；Context 为空时返回 0。
 * @note 不主动推进时间，用例通过修改 TickMs 精确触发超时。
 */
static uint32_t test_w25qxx_get_tick_ms(void *context)
{
    const W25Qxx_FakeBusTypeDef *fake_bus = context;

    return (fake_bus != NULL) ? fake_bus->TickMs : 0u;
}

/**
 * @brief 核对同步带地址读取的所有协议阶段并复制预设响应。
 * @param[in,out] context 用例配置的 Fake Bus Context；不访问实际 QSPI。
 * @param[in] instruction 待核对的指令字节。
 * @param[in] address 待核对的芯片字节地址。
 * @param[in] transfer_config 地址长度、线宽、模式字节及 dummy cycle 配置，须匹配期望。
 * @param[out] data 有效接收缓冲，容量至少 data_length。
 * @param[in] data_length 数据字节数，须匹配用例期望。
 * @return 全部匹配并复制成功为 BUS_OK，否则为 BUS_ERROR。
 */
static W25Qxx_BusStatusTypeDef test_w25qxx_read_addressed_command(
    void *context,
    uint8_t instruction,
    uint32_t address,
    const W25Qxx_BusAddressedTransferConfigTypeDef *transfer_config,
    uint8_t *data,
    uint32_t data_length)
{
    W25Qxx_FakeBusTypeDef *fake_bus = (W25Qxx_FakeBusTypeDef *)context;

    if ((fake_bus == NULL) ||
        (transfer_config == NULL) ||
        (data == NULL) ||
        (instruction != fake_bus->ExpectedAddressedInstruction) ||
        (address != fake_bus->ExpectedAddress) ||
        (transfer_config->AddressLength !=
         fake_bus->ExpectedAddressedTransferConfig.AddressLength) ||
        (transfer_config->AddressLineMode !=
         fake_bus->ExpectedAddressedTransferConfig.AddressLineMode) ||
        (transfer_config->DataLineMode !=
         fake_bus->ExpectedAddressedTransferConfig.DataLineMode) ||
        (transfer_config->HasAlternateByte !=
         fake_bus->ExpectedAddressedTransferConfig.HasAlternateByte) ||
        (transfer_config->AlternateByte !=
         fake_bus->ExpectedAddressedTransferConfig.AlternateByte) ||
        (transfer_config->AlternateByteLineMode !=
         fake_bus->ExpectedAddressedTransferConfig.AlternateByteLineMode) ||
        (transfer_config->DummyCycles !=
         fake_bus->ExpectedAddressedTransferConfig.DummyCycles) ||
        (data_length != sizeof(fake_bus->AddressedResponse)))
    {
        return W25QXX_BUS_ERROR;
    }

    fake_bus->AddressedWasCalled = true;
    (void)memcpy(data,
                 fake_bus->AddressedResponse,
                 sizeof(fake_bus->AddressedResponse));
    return W25QXX_BUS_OK;
}

/**
 * @brief 核对异步带地址读取请求，记录受理但不填充数据。
 * @param[in,out] context 用例配置的 Fake Bus Context；不访问实际 QSPI。
 * @param[in] instruction 待核对的指令字节。
 * @param[in] address 待核对的芯片字节地址。
 * @param[in] transfer_config 地址长度、线宽、模式字节及 dummy cycle 配置，须匹配期望。
 * @param[out] data 非空接收缓冲；此 Fake 不实际写入数据。
 * @param[in] data_length 数据字节数，须匹配用例期望。
 * @return 请求匹配为 BUS_OK，否则为 BUS_ERROR。
 * @note 最终状态由 AsyncAddressedStatus 注入，不能把 Start 成功当作完成。
 */
static W25Qxx_BusStatusTypeDef test_w25qxx_start_read_addressed_command(
    void *context,
    uint8_t instruction,
    uint32_t address,
    const W25Qxx_BusAddressedTransferConfigTypeDef *transfer_config,
    uint8_t *data,
    uint32_t data_length)
{
    W25Qxx_FakeBusTypeDef *fake_bus = (W25Qxx_FakeBusTypeDef *)context;

    if ((fake_bus == NULL) ||
        (transfer_config == NULL) ||
        (data == NULL) ||
        (instruction != fake_bus->ExpectedAsyncAddressedInstruction) ||
        (address != fake_bus->ExpectedAsyncAddress) ||
        (transfer_config->AddressLength !=
         fake_bus->ExpectedAsyncAddressedTransferConfig.AddressLength) ||
        (transfer_config->AddressLineMode !=
         fake_bus->ExpectedAsyncAddressedTransferConfig.AddressLineMode) ||
        (transfer_config->DataLineMode !=
         fake_bus->ExpectedAsyncAddressedTransferConfig.DataLineMode) ||
        (transfer_config->HasAlternateByte !=
         fake_bus->ExpectedAsyncAddressedTransferConfig.HasAlternateByte) ||
        (transfer_config->AlternateByte !=
         fake_bus->ExpectedAsyncAddressedTransferConfig.AlternateByte) ||
        (transfer_config->AlternateByteLineMode !=
         fake_bus->ExpectedAsyncAddressedTransferConfig.AlternateByteLineMode) ||
        (transfer_config->DummyCycles !=
         fake_bus->ExpectedAsyncAddressedTransferConfig.DummyCycles) ||
        (data_length != fake_bus->ExpectedAsyncAddressedDataLength))
    {
        return W25QXX_BUS_ERROR;
    }

    fake_bus->AsyncAddressedWasCalled = true;
    return W25QXX_BUS_OK;
}

/**
 * @brief 返回用例控制的异步读取进度。
 * @param[in] context Fake Bus Context。
 * @return 返回 AsyncAddressedStatus；Context 为空返回 BUS_ERROR。
 */
static W25Qxx_BusStatusTypeDef test_w25qxx_get_read_addressed_command_status(
    void *context)
{
    const W25Qxx_FakeBusTypeDef *fake_bus = context;

    return (fake_bus != NULL) ? fake_bus->AsyncAddressedStatus : W25QXX_BUS_ERROR;
}

/**
 * @brief 核对状态自动轮询指令、匹配值和掩码并记录受理。
 * @param[in,out] context 用例配置的 Fake Bus Context；不访问实际 QSPI。
 * @param[in] instruction 待核对的指令字节。
 * @param[in] match 状态匹配目标值。
 * @param[in] mask 参与比较的位掩码。
 * @return 期望匹配返回 BUS_OK，否则返回 BUS_ERROR。
 */
static W25Qxx_BusStatusTypeDef test_w25qxx_start_status_match_polling(
    void *context,
    uint8_t instruction,
    uint8_t match,
    uint8_t mask)
{
    W25Qxx_FakeBusTypeDef *fake_bus = (W25Qxx_FakeBusTypeDef *)context;

    if ((fake_bus == NULL) ||
        (instruction != fake_bus->ExpectedStatusPollingInstruction) ||
        (match != fake_bus->ExpectedStatusPollingMatch) ||
        (mask != fake_bus->ExpectedStatusPollingMask))
    {
        return W25QXX_BUS_ERROR;
    }

    fake_bus->StatusPollingWasCalled = true;
    return W25QXX_BUS_OK;
}

/**
 * @brief 返回用例控制的状态匹配轮询结果。
 * @param[in] context Fake Bus Context。
 * @return 返回 StatusPollingStatus；Context 为空返回 BUS_ERROR。
 */
static W25Qxx_BusStatusTypeDef test_w25qxx_get_status_match_polling_status(
    void *context)
{
    const W25Qxx_FakeBusTypeDef *fake_bus = context;

    return (fake_bus != NULL) ? fake_bus->StatusPollingStatus : W25QXX_BUS_ERROR;
}

/**
 * @brief 记录轮询中止请求并返回注入的中止结果。
 * @param[in,out] context 用例配置的 Fake Bus Context；不访问实际 QSPI。
 * @return 返回 StatusPollingAbortStatus；Context 为空返回 BUS_ERROR。
 * @note 仅模拟控制器中止，不模拟 NOR 内部操作取消。
 */
static W25Qxx_BusStatusTypeDef test_w25qxx_abort_status_match_polling(
    void *context)
{
    W25Qxx_FakeBusTypeDef *fake_bus = (W25Qxx_FakeBusTypeDef *)context;

    if (fake_bus == NULL)
    {
        return W25QXX_BUS_ERROR;
    }

    fake_bus->StatusPollingAbortWasCalled = true;
    return fake_bus->StatusPollingAbortStatus;
}

/**
 * @brief 核对页编程的地址、协议阶段及完整 payload。
 * @param[in,out] context 用例配置的 Fake Bus Context；不访问实际 QSPI。
 * @param[in] instruction 待核对的指令字节。
 * @param[in] address 待核对的芯片字节地址。
 * @param[in] transfer_config 地址长度、线宽、模式字节及 dummy cycle 配置，须匹配期望。
 * @param[in] data 有效编程输入，内容须匹配用例预设数组。
 * @param[in] data_length 数据字节数，须匹配用例期望。
 * @return 全部匹配返回 BUS_OK 并登记调用，否则返回 BUS_ERROR。
 */
static W25Qxx_BusStatusTypeDef test_w25qxx_write_addressed_command(
    void *context,
    uint8_t instruction,
    uint32_t address,
    const W25Qxx_BusAddressedTransferConfigTypeDef *transfer_config,
    const uint8_t *data,
    uint32_t data_length)
{
    W25Qxx_FakeBusTypeDef *fake_bus = (W25Qxx_FakeBusTypeDef *)context;

    if ((fake_bus == NULL) ||
        (transfer_config == NULL) ||
        (data == NULL) ||
        (instruction != fake_bus->ExpectedAddressedWriteInstruction) ||
        (address != fake_bus->ExpectedAddressedWriteAddress) ||
        (transfer_config->AddressLength !=
         fake_bus->ExpectedAddressedWriteTransferConfig.AddressLength) ||
        (transfer_config->AddressLineMode !=
         fake_bus->ExpectedAddressedWriteTransferConfig.AddressLineMode) ||
        (transfer_config->DataLineMode !=
         fake_bus->ExpectedAddressedWriteTransferConfig.DataLineMode) ||
        (transfer_config->HasAlternateByte !=
         fake_bus->ExpectedAddressedWriteTransferConfig.HasAlternateByte) ||
        (transfer_config->AlternateByte !=
         fake_bus->ExpectedAddressedWriteTransferConfig.AlternateByte) ||
        (transfer_config->AlternateByteLineMode !=
         fake_bus->ExpectedAddressedWriteTransferConfig.AlternateByteLineMode) ||
        (transfer_config->DummyCycles !=
         fake_bus->ExpectedAddressedWriteTransferConfig.DummyCycles) ||
        (data_length != fake_bus->ExpectedAddressedWriteDataLength) ||
        (memcmp(data, fake_bus->ExpectedAddressedWriteData, data_length) != 0))
    {
        return W25QXX_BUS_ERROR;
    }

    fake_bus->AddressedWriteWasCalled = true;
    return W25QXX_BUS_OK;
}

static const W25Qxx_BusOpsTypeDef test_w25qxx_fake_bus_ops = {
    .ReadCommand = test_w25qxx_read_command,
    .ReadAddressedCommand = test_w25qxx_read_addressed_command,
    .StartReadAddressedCommand = test_w25qxx_start_read_addressed_command,
    .GetReadAddressedCommandStatus =
        test_w25qxx_get_read_addressed_command_status,
    .StartStatusMatchPolling = test_w25qxx_start_status_match_polling,
    .GetStatusMatchPollingStatus =
        test_w25qxx_get_status_match_polling_status,
    .AbortStatusMatchPolling = test_w25qxx_abort_status_match_polling,
    .ExecuteCommand = test_w25qxx_execute_command,
    .ExecuteAddressedCommand = test_w25qxx_execute_addressed_command,
    .WriteCommand = test_w25qxx_write_command,
    .WriteAddressedCommand = test_w25qxx_write_addressed_command,
    .GetTickMs = test_w25qxx_get_tick_ms
};

/**
 * @brief 验证期望厂商和容量的 W25Q256 可初始化，并保留实际 JEDEC 信息。
 * @note 通过 Fake Bus 提供输入并以断言核对结果；不访问硬件，断言失败即终止测试。
 */
static void test_w25qxx_init_accepts_expected_w25q256(void)
{
    W25Qxx_FakeBusTypeDef fake_bus = {
        .ExpectedInstruction = 0x9Fu,
        .Response = {
            .ManufacturerID = W25QXX_MANUFACTURER_ID_WINBOND,
            .MemoryType = 0x70u,
            .CapacityID = W25QXX_CAPACITY_ID_256MBIT
        }
    };
    W25Qxx_HandleTypeDef hflash = {
        .BusOps = &test_w25qxx_fake_bus_ops,
        .BusContext = &fake_bus,
        .ExpectedJedecID = &test_w25q256_expected_id
    };
    W25Qxx_JedecIDTypeDef jedec_id;

    assert(W25Qxx_Init(&hflash) == W25QXX_OK);
    assert(fake_bus.WasCalled);

    assert(W25Qxx_GetJedecID(&hflash, &jedec_id) == W25QXX_OK);
    assert(jedec_id.ManufacturerID == W25QXX_MANUFACTURER_ID_WINBOND);
    assert(jedec_id.MemoryType == 0x70u);
    assert(jedec_id.CapacityID == W25QXX_CAPACITY_ID_256MBIT);
}

/**
 * @brief 验证错误厂商 ID 会使初始化失败。
 * @note 通过 Fake Bus 提供输入并以断言核对结果；不访问硬件，断言失败即终止测试。
 */
static void test_w25qxx_init_rejects_unexpected_manufacturer(void)
{
    W25Qxx_FakeBusTypeDef fake_bus = {
        .ExpectedInstruction = 0x9Fu,
        .Response = {
            .ManufacturerID = 0xC8u,
            .MemoryType = 0x40u,
            .CapacityID = W25QXX_CAPACITY_ID_256MBIT
        }
    };
    W25Qxx_HandleTypeDef hflash = {
        .BusOps = &test_w25qxx_fake_bus_ops,
        .BusContext = &fake_bus,
        .ExpectedJedecID = &test_w25q256_expected_id
    };

    assert(W25Qxx_Init(&hflash) == W25QXX_ERROR);
    assert(fake_bus.WasCalled);
    assert(hflash.State == W25QXX_STATE_ERROR);
    assert(hflash.ErrorCode == W25QXX_ERROR_CHIP_MISMATCH);
    assert(hflash.LastBusStatus == W25QXX_BUS_OK);
}

/**
 * @brief 验证不匹配的容量 ID 会使初始化失败。
 * @note 通过 Fake Bus 提供输入并以断言核对结果；不访问硬件，断言失败即终止测试。
 */
static void test_w25qxx_init_rejects_unexpected_capacity(void)
{
    W25Qxx_FakeBusTypeDef fake_bus = {
        .ExpectedInstruction = 0x9Fu,
        .Response = {
            .ManufacturerID = W25QXX_MANUFACTURER_ID_WINBOND,
            .MemoryType = 0x40u,
            .CapacityID = W25QXX_CAPACITY_ID_16MBIT
        }
    };
    W25Qxx_HandleTypeDef hflash = {
        .BusOps = &test_w25qxx_fake_bus_ops,
        .BusContext = &fake_bus,
        .ExpectedJedecID = &test_w25q256_expected_id
    };

    assert(W25Qxx_Init(&hflash) == W25QXX_ERROR);
    assert(fake_bus.WasCalled);
    assert(hflash.State == W25QXX_STATE_ERROR);
    assert(hflash.ErrorCode == W25QXX_ERROR_CHIP_MISMATCH);
    assert(hflash.LastBusStatus == W25QXX_BUS_OK);
}

/**
 * @brief 验证 W25Q256 对外报告固定四字节 Quad I/O 读取协议。
 * @note 通过 Fake Bus 提供输入并以断言核对结果；不访问硬件，断言失败即终止测试。
 */
static void test_w25qxx_get_array_read_protocol_describes_w25q256_quad_i_o(void)
{
    W25Qxx_FakeBusTypeDef fake_bus = {
        .ExpectedInstruction = 0x9Fu,
        .Response = {
            .ManufacturerID = W25QXX_MANUFACTURER_ID_WINBOND,
            .MemoryType = 0x40u,
            .CapacityID = W25QXX_CAPACITY_ID_256MBIT
        }
    };
    W25Qxx_HandleTypeDef hflash = {
        .BusOps = &test_w25qxx_fake_bus_ops,
        .BusContext = &fake_bus,
        .ExpectedJedecID = &test_w25q256_expected_id
    };
    W25Qxx_ArrayReadProtocolTypeDef protocol;

    assert(W25Qxx_Init(&hflash) == W25QXX_OK);
    assert(W25Qxx_GetArrayReadProtocol(&hflash, &protocol) == W25QXX_OK);
    assert(protocol.Instruction == 0xECu);
    assert(protocol.TransferConfig.AddressLength == 4u);
    assert(protocol.TransferConfig.AddressLineMode == W25QXX_BUS_LINES_4);
    assert(protocol.TransferConfig.DataLineMode == W25QXX_BUS_LINES_4);
    assert(protocol.TransferConfig.HasAlternateByte);
    assert(protocol.TransferConfig.AlternateByte == 0xFFu);
    assert(protocol.TransferConfig.AlternateByteLineMode == W25QXX_BUS_LINES_4);
    assert(protocol.TransferConfig.DummyCycles == 4u);
}

/**
 * @brief 验证 SFDP 正确签名经带地址读取后被接受。
 * @note 通过 Fake Bus 提供输入并以断言核对结果；不访问硬件，断言失败即终止测试。
 */
static void test_w25qxx_probe_sfdp_accepts_valid_signature(void)
{
    W25Qxx_FakeBusTypeDef fake_bus = {
        .ExpectedInstruction = 0x9Fu,
        .Response = {
            .ManufacturerID = W25QXX_MANUFACTURER_ID_WINBOND,
            .MemoryType = 0x40u,
            .CapacityID = W25QXX_CAPACITY_ID_256MBIT
        },
        .ExpectedAddressedInstruction = 0x5Au,
        .ExpectedAddress = 0u,
        .ExpectedAddressedTransferConfig = {
            .AddressLength = 3u,
            .AddressLineMode = W25QXX_BUS_LINES_1,
            .DataLineMode = W25QXX_BUS_LINES_1,
            .HasAlternateByte = false,
            .AlternateByte = 0u,
            .AlternateByteLineMode = W25QXX_BUS_LINES_1,
            .DummyCycles = 8u
        },
        .AddressedResponse = {'S', 'F', 'D', 'P'}
    };
    W25Qxx_HandleTypeDef hflash = {
        .BusOps = &test_w25qxx_fake_bus_ops,
        .BusContext = &fake_bus,
        .ExpectedJedecID = &test_w25q256_expected_id
    };

    assert(W25Qxx_Init(&hflash) == W25QXX_OK);
    assert(W25Qxx_ProbeSFDP(&hflash) == W25QXX_OK);
    assert(fake_bus.AddressedWasCalled);
    assert(hflash.State == W25QXX_STATE_READY);
}

/**
 * @brief 验证损坏的 SFDP 签名被拒绝。
 * @note 通过 Fake Bus 提供输入并以断言核对结果；不访问硬件，断言失败即终止测试。
 */
static void test_w25qxx_probe_sfdp_rejects_invalid_signature(void)
{
    W25Qxx_FakeBusTypeDef fake_bus = {
        .ExpectedInstruction = 0x9Fu,
        .Response = {
            .ManufacturerID = W25QXX_MANUFACTURER_ID_WINBOND,
            .MemoryType = 0x40u,
            .CapacityID = W25QXX_CAPACITY_ID_256MBIT
        },
        .ExpectedAddressedInstruction = 0x5Au,
        .ExpectedAddress = 0u,
        .ExpectedAddressedTransferConfig = {
            .AddressLength = 3u,
            .AddressLineMode = W25QXX_BUS_LINES_1,
            .DataLineMode = W25QXX_BUS_LINES_1,
            .HasAlternateByte = false,
            .AlternateByte = 0u,
            .AlternateByteLineMode = W25QXX_BUS_LINES_1,
            .DummyCycles = 8u
        },
        .AddressedResponse = {0u, 0u, 0u, 0u}
    };
    W25Qxx_HandleTypeDef hflash = {
        .BusOps = &test_w25qxx_fake_bus_ops,
        .BusContext = &fake_bus,
        .ExpectedJedecID = &test_w25q256_expected_id
    };

    assert(W25Qxx_Init(&hflash) == W25QXX_OK);
    assert(W25Qxx_ProbeSFDP(&hflash) == W25QXX_ERROR);
    assert(fake_bus.AddressedWasCalled);
    assert(hflash.State == W25QXX_STATE_ERROR);
    assert(hflash.ErrorCode == W25QXX_ERROR_INVALID_SFDP_SIGNATURE);
    assert(hflash.LastBusStatus == W25QXX_BUS_OK);
}

/**
 * @brief 验证两个状态寄存器中的 WIP、WEL、QE 位被正确解析。
 * @note 通过 Fake Bus 提供输入并以断言核对结果；不访问硬件，断言失败即终止测试。
 */
static void test_w25qxx_read_status_registers_parses_wip_wel_and_qe(void)
{
    W25Qxx_FakeBusTypeDef fake_bus = {
        .ExpectedInstruction = 0x9Fu,
        .Response = {
            .ManufacturerID = W25QXX_MANUFACTURER_ID_WINBOND,
            .MemoryType = 0x40u,
            .CapacityID = W25QXX_CAPACITY_ID_256MBIT
        },
        .ExpectedStatusRegister1Instruction = 0x05u,
        .StatusRegister1Response = 0x03u,
        .ExpectedStatusRegister2Instruction = 0x35u,
        .StatusRegister2Response = 0x02u
    };
    W25Qxx_HandleTypeDef hflash = {
        .BusOps = &test_w25qxx_fake_bus_ops,
        .BusContext = &fake_bus,
        .ExpectedJedecID = &test_w25q256_expected_id
    };
    W25Qxx_StatusRegistersTypeDef status_registers;

    assert(W25Qxx_Init(&hflash) == W25QXX_OK);
    assert(W25Qxx_ReadStatusRegisters(&hflash, &status_registers) == W25QXX_OK);
    assert(fake_bus.StatusRegister1WasCalled);
    assert(fake_bus.StatusRegister2WasCalled);
    assert(status_registers.StatusRegister1 == 0x03u);
    assert(status_registers.StatusRegister2 == 0x02u);
    assert(status_registers.IsWriteInProgress);
    assert(status_registers.IsWriteEnabled);
    assert(status_registers.IsQuadEnabled);
    assert(hflash.State == W25QXX_STATE_READY);
}

/**
 * @brief 验证 SR2 总线读取失败不会产生有效状态快照。
 * @note 通过 Fake Bus 提供输入并以断言核对结果；不访问硬件，断言失败即终止测试。
 */
static void test_w25qxx_read_status_registers_rejects_status_register_2_bus_error(void)
{
    W25Qxx_FakeBusTypeDef fake_bus = {
        .ExpectedInstruction = 0x9Fu,
        .Response = {
            .ManufacturerID = W25QXX_MANUFACTURER_ID_WINBOND,
            .MemoryType = 0x40u,
            .CapacityID = W25QXX_CAPACITY_ID_256MBIT
        },
        .ExpectedStatusRegister1Instruction = 0x05u,
        .StatusRegister1Response = 0x00u,
        .ExpectedStatusRegister2Instruction = 0x00u
    };
    W25Qxx_HandleTypeDef hflash = {
        .BusOps = &test_w25qxx_fake_bus_ops,
        .BusContext = &fake_bus,
        .ExpectedJedecID = &test_w25q256_expected_id
    };
    W25Qxx_StatusRegistersTypeDef status_registers;

    assert(W25Qxx_Init(&hflash) == W25QXX_OK);
    assert(W25Qxx_ReadStatusRegisters(&hflash, &status_registers) == W25QXX_ERROR);
    assert(fake_bus.StatusRegister1WasCalled);
    assert(!fake_bus.StatusRegister2WasCalled);
    assert(hflash.State == W25QXX_STATE_ERROR);
    assert(hflash.ErrorCode == W25QXX_ERROR_READ_STATUS_REGISTER_2);
    assert(hflash.LastBusStatus == W25QXX_BUS_ERROR);
}

/**
 * @brief 验证 QE 已置位时跳过不必要的寄存器写入。
 * @note 通过 Fake Bus 提供输入并以断言核对结果；不访问硬件，断言失败即终止测试。
 */
static void test_w25qxx_ensure_quad_enabled_skips_write_when_qe_is_set(void)
{
    W25Qxx_FakeBusTypeDef fake_bus = {
        .ExpectedInstruction = 0x9Fu,
        .Response = {
            .ManufacturerID = W25QXX_MANUFACTURER_ID_WINBOND,
            .MemoryType = 0x40u,
            .CapacityID = W25QXX_CAPACITY_ID_256MBIT
        },
        .ExpectedStatusRegister1Instruction = 0x05u,
        .StatusRegister1Response = 0x00u,
        .ExpectedStatusRegister2Instruction = 0x35u,
        .StatusRegister2Response = 0x02u
    };
    W25Qxx_HandleTypeDef hflash = {
        .BusOps = &test_w25qxx_fake_bus_ops,
        .BusContext = &fake_bus,
        .ExpectedJedecID = &test_w25q256_expected_id
    };

    assert(W25Qxx_Init(&hflash) == W25QXX_OK);
    assert(W25Qxx_EnsureQuadEnabled(&hflash) == W25QXX_OK);
    assert(fake_bus.StatusRegister1WasCalled);
    assert(fake_bus.StatusRegister2WasCalled);
    assert(!fake_bus.ExecuteWasCalled);
    assert(!fake_bus.WriteWasCalled);
    assert(hflash.State == W25QXX_STATE_READY);
    assert(hflash.ErrorCode == W25QXX_ERROR_NONE);
}

/**
 * @brief 验证 QE 未开启时执行写使能、写寄存器及回读确认。
 * @note 通过 Fake Bus 提供输入并以断言核对结果；不访问硬件，断言失败即终止测试。
 */
static void test_w25qxx_ensure_quad_enabled_sets_qe_and_verifies_it(void)
{
    W25Qxx_FakeBusTypeDef fake_bus = {
        .ExpectedInstruction = 0x9Fu,
        .Response = {
            .ManufacturerID = W25QXX_MANUFACTURER_ID_WINBOND,
            .MemoryType = 0x40u,
            .CapacityID = W25QXX_CAPACITY_ID_256MBIT
        },
        .ExpectedStatusRegister1Instruction = 0x05u,
        .StatusRegister1ResponseSequence = {0x00u, 0x02u, 0x01u, 0x00u},
        .StatusRegister1ResponseCount = 4u,
        .ExpectedStatusRegister2Instruction = 0x35u,
        .StatusRegister2ResponseSequence = {0x48u, 0x48u, 0x48u, 0x4Au},
        .StatusRegister2ResponseCount = 4u,
        .ExpectedExecuteInstruction = 0x06u,
        .ExpectedWriteInstruction = 0x31u,
        .ExpectedWriteData = 0x4Au
    };
    W25Qxx_HandleTypeDef hflash = {
        .BusOps = &test_w25qxx_fake_bus_ops,
        .BusContext = &fake_bus,
        .ExpectedJedecID = &test_w25q256_expected_id
    };

    assert(W25Qxx_Init(&hflash) == W25QXX_OK);
    assert(W25Qxx_EnsureQuadEnabled(&hflash) == W25QXX_OK);
    assert(fake_bus.ExecuteWasCalled);
    assert(fake_bus.WriteWasCalled);
    assert(fake_bus.StatusRegister1ResponseIndex == 4u);
    assert(fake_bus.StatusRegister2ResponseIndex == 4u);
    assert(hflash.State == W25QXX_STATE_READY);
    assert(hflash.ErrorCode == W25QXX_ERROR_NONE);
}

/**
 * @brief 验证 NOR 正忙时拒绝修改 QE，且不发送写命令。
 * @note 通过 Fake Bus 提供输入并以断言核对结果；不访问硬件，断言失败即终止测试。
 */
static void test_w25qxx_ensure_quad_enabled_rejects_busy_flash_without_writing(void)
{
    W25Qxx_FakeBusTypeDef fake_bus = {
        .ExpectedInstruction = 0x9Fu,
        .Response = {
            .ManufacturerID = W25QXX_MANUFACTURER_ID_WINBOND,
            .MemoryType = 0x40u,
            .CapacityID = W25QXX_CAPACITY_ID_256MBIT
        },
        .ExpectedStatusRegister1Instruction = 0x05u,
        .StatusRegister1Response = 0x01u,
        .ExpectedStatusRegister2Instruction = 0x35u,
        .StatusRegister2Response = 0x00u
    };
    W25Qxx_HandleTypeDef hflash = {
        .BusOps = &test_w25qxx_fake_bus_ops,
        .BusContext = &fake_bus,
        .ExpectedJedecID = &test_w25q256_expected_id
    };

    assert(W25Qxx_Init(&hflash) == W25QXX_OK);
    assert(W25Qxx_EnsureQuadEnabled(&hflash) == W25QXX_ERROR);
    assert(fake_bus.StatusRegister1WasCalled);
    assert(fake_bus.StatusRegister2WasCalled);
    assert(!fake_bus.ExecuteWasCalled);
    assert(!fake_bus.WriteWasCalled);
    assert(hflash.State == W25QXX_STATE_READY);
    assert(hflash.ErrorCode == W25QXX_ERROR_FLASH_BUSY);
}

/**
 * @brief 验证同步数组读取使用 0xEC 四字节地址 Quad I/O 事务。
 * @note 通过 Fake Bus 提供输入并以断言核对结果；不访问硬件，断言失败即终止测试。
 */
static void test_w25qxx_read_uses_quad_i_o_4byte_address_command(void)
{
    W25Qxx_FakeBusTypeDef fake_bus = {
        .ExpectedInstruction = 0x9Fu,
        .Response = {
            .ManufacturerID = W25QXX_MANUFACTURER_ID_WINBOND,
            .MemoryType = 0x40u,
            .CapacityID = W25QXX_CAPACITY_ID_256MBIT
        },
        .ExpectedAddressedInstruction = 0xECu,
        .ExpectedAddress = 0x00010200u,
        .ExpectedAddressedTransferConfig = {
            .AddressLength = 4u,
            .AddressLineMode = W25QXX_BUS_LINES_4,
            .DataLineMode = W25QXX_BUS_LINES_4,
            .HasAlternateByte = true,
            .AlternateByte = 0xFFu,
            .AlternateByteLineMode = W25QXX_BUS_LINES_4,
            .DummyCycles = 4u
        },
        .AddressedResponse = {0x10u, 0x20u, 0x30u, 0x40u}
    };
    W25Qxx_HandleTypeDef hflash = {
        .BusOps = &test_w25qxx_fake_bus_ops,
        .BusContext = &fake_bus,
        .ExpectedJedecID = &test_w25q256_expected_id
    };
    uint8_t read_buffer[4] = {0u};

    assert(W25Qxx_Init(&hflash) == W25QXX_OK);
    assert(W25Qxx_Read(&hflash,
                        fake_bus.ExpectedAddress,
                        read_buffer,
                        sizeof(read_buffer)) == W25QXX_OK);
    assert(fake_bus.AddressedWasCalled);
    assert(memcmp(read_buffer,
                  fake_bus.AddressedResponse,
                  sizeof(read_buffer)) == 0);
    assert(hflash.State == W25QXX_STATE_READY);
}

/**
 * @brief 验证同步 Quad I/O 读取拒绝未按要求对齐的地址。
 * @note 通过 Fake Bus 提供输入并以断言核对结果；不访问硬件，断言失败即终止测试。
 */
static void test_w25qxx_read_rejects_unaligned_quad_i_o_address(void)
{
    W25Qxx_FakeBusTypeDef fake_bus = {
        .ExpectedInstruction = 0x9Fu,
        .Response = {
            .ManufacturerID = W25QXX_MANUFACTURER_ID_WINBOND,
            .MemoryType = 0x40u,
            .CapacityID = W25QXX_CAPACITY_ID_256MBIT
        }
    };
    W25Qxx_HandleTypeDef hflash = {
        .BusOps = &test_w25qxx_fake_bus_ops,
        .BusContext = &fake_bus,
        .ExpectedJedecID = &test_w25q256_expected_id
    };
    uint8_t read_buffer[4] = {0u};

    assert(W25Qxx_Init(&hflash) == W25QXX_OK);
    assert(W25Qxx_Read(&hflash,
                        0x00010201u,
                        read_buffer,
                        sizeof(read_buffer)) == W25QXX_ERROR);
    assert(!fake_bus.AddressedWasCalled);
    assert(hflash.State == W25QXX_STATE_READY);
    assert(hflash.ErrorCode == W25QXX_ERROR_INVALID_READ_ADDRESS_ALIGNMENT);
}

/**
 * @brief 验证非 W25Q256 器件不能使用当前固定四字节数组读取实现。
 * @note 通过 Fake Bus 提供输入并以断言核对结果；不访问硬件，断言失败即终止测试。
 */
static void test_w25qxx_read_rejects_non_w25q256_fixed_4byte_operation(void)
{
    W25Qxx_FakeBusTypeDef fake_bus = {
        .ExpectedInstruction = 0x9Fu,
        .Response = {
            .ManufacturerID = W25QXX_MANUFACTURER_ID_WINBOND,
            .MemoryType = 0x40u,
            .CapacityID = W25QXX_CAPACITY_ID_16MBIT
        }
    };
    const W25Qxx_ExpectedJedecIDTypeDef expected_id = {
        .ManufacturerID = W25QXX_MANUFACTURER_ID_WINBOND,
        .CapacityID = W25QXX_CAPACITY_ID_16MBIT
    };
    W25Qxx_HandleTypeDef hflash = {
        .BusOps = &test_w25qxx_fake_bus_ops,
        .BusContext = &fake_bus,
        .ExpectedJedecID = &expected_id
    };
    uint8_t read_buffer[4] = {0u};

    assert(W25Qxx_Init(&hflash) == W25QXX_OK);
    assert(W25Qxx_Read(&hflash,
                        0x00000000u,
                        read_buffer,
                        sizeof(read_buffer)) == W25QXX_ERROR);
    assert(!fake_bus.AddressedWasCalled);
    assert(hflash.State == W25QXX_STATE_READY);
    assert(hflash.ErrorCode == W25QXX_ERROR_UNSUPPORTED_ARRAY_OPERATION);
}

/**
 * @brief 验证异步读取按四字节 Quad I/O 协议受理并经 Process 完成。
 * @note 通过 Fake Bus 提供输入并以断言核对结果；不访问硬件，断言失败即终止测试。
 */
static void test_w25qxx_start_read_uses_quad_i_o_4byte_address_command(void)
{
    W25Qxx_FakeBusTypeDef fake_bus = {
        .ExpectedInstruction = 0x9Fu,
        .Response = {
            .ManufacturerID = W25QXX_MANUFACTURER_ID_WINBOND,
            .MemoryType = 0x40u,
            .CapacityID = W25QXX_CAPACITY_ID_256MBIT
        },
        .ExpectedAsyncAddressedInstruction = 0xECu,
        .ExpectedAsyncAddress = 0x00010200u,
        .ExpectedAsyncAddressedTransferConfig = {
            .AddressLength = 4u,
            .AddressLineMode = W25QXX_BUS_LINES_4,
            .DataLineMode = W25QXX_BUS_LINES_4,
            .HasAlternateByte = true,
            .AlternateByte = 0xFFu,
            .AlternateByteLineMode = W25QXX_BUS_LINES_4,
            .DummyCycles = 4u
        },
        .ExpectedAsyncAddressedDataLength = 4u,
        .AsyncAddressedStatus = W25QXX_BUS_BUSY
    };
    W25Qxx_HandleTypeDef hflash = {
        .BusOps = &test_w25qxx_fake_bus_ops,
        .BusContext = &fake_bus,
        .ExpectedJedecID = &test_w25q256_expected_id
    };
    uint8_t read_buffer[4] = {0u};

    assert(W25Qxx_Init(&hflash) == W25QXX_OK);
    assert(W25Qxx_StartRead(&hflash,
                             fake_bus.ExpectedAsyncAddress,
                             read_buffer,
                             sizeof(read_buffer)) == W25QXX_OK);
    assert(fake_bus.AsyncAddressedWasCalled);
    assert(hflash.State == W25QXX_STATE_BUSY);
    assert(hflash.ActiveOperation == W25QXX_OPERATION_ARRAY_READ);
    assert(W25Qxx_Process(&hflash) == W25QXX_BUSY);

    fake_bus.AsyncAddressedStatus = W25QXX_BUS_OK;
    assert(W25Qxx_Process(&hflash) == W25QXX_OK);
    assert(hflash.State == W25QXX_STATE_READY);
    assert(hflash.ActiveOperation == W25QXX_OPERATION_NONE);
}

/**
 * @brief 验证异步读取持续忙时，时间预算到期会记录读取超时。
 * @note 通过 Fake Bus 提供输入并以断言核对结果；不访问硬件，断言失败即终止测试。
 */
static void test_w25qxx_process_times_out_stalled_async_read(void)
{
    W25Qxx_FakeBusTypeDef fake_bus = {
        .ExpectedInstruction = 0x9Fu,
        .Response = {
            .ManufacturerID = W25QXX_MANUFACTURER_ID_WINBOND,
            .MemoryType = 0x40u,
            .CapacityID = W25QXX_CAPACITY_ID_256MBIT
        },
        .ExpectedAsyncAddressedInstruction = 0xECu,
        .ExpectedAsyncAddress = 0x00010200u,
        .ExpectedAsyncAddressedTransferConfig = {
            .AddressLength = 4u,
            .AddressLineMode = W25QXX_BUS_LINES_4,
            .DataLineMode = W25QXX_BUS_LINES_4,
            .HasAlternateByte = true,
            .AlternateByte = 0xFFu,
            .AlternateByteLineMode = W25QXX_BUS_LINES_4,
            .DummyCycles = 4u
        },
        .ExpectedAsyncAddressedDataLength = 4u,
        .AsyncAddressedStatus = W25QXX_BUS_BUSY
    };
    W25Qxx_HandleTypeDef hflash = {
        .BusOps = &test_w25qxx_fake_bus_ops,
        .BusContext = &fake_bus,
        .ExpectedJedecID = &test_w25q256_expected_id
    };
    uint8_t read_buffer[4] = {0u};

    assert(W25Qxx_Init(&hflash) == W25QXX_OK);
    assert(W25Qxx_StartRead(&hflash,
                             fake_bus.ExpectedAsyncAddress,
                             read_buffer,
                             sizeof(read_buffer)) == W25QXX_OK);

    fake_bus.TickMs = W25QXX_ARRAY_READ_TIMEOUT_MS;
    assert(W25Qxx_Process(&hflash) == W25QXX_ERROR);
    assert(hflash.State == W25QXX_STATE_ERROR);
    assert(hflash.ActiveOperation == W25QXX_OPERATION_NONE);
    assert(hflash.ErrorCode == W25QXX_ERROR_READ_ARRAY_TIMEOUT);
    assert(hflash.LastBusStatus == W25QXX_BUS_TIMEOUT);
}

/**
 * @brief 验证页编程按四字节 Quad 指令启动并等待状态匹配完成。
 * @note 通过 Fake Bus 提供输入并以断言核对结果；不访问硬件，断言失败即终止测试。
 */
static void test_w25qxx_program_page_starts_quad_4byte_operation(void)
{
    static const uint8_t expected_data[] = {0xAAu, 0x55u, 0x33u, 0xCCu};
    W25Qxx_FakeBusTypeDef fake_bus = {
        .ExpectedInstruction = 0x9Fu,
        .Response = {
            .ManufacturerID = W25QXX_MANUFACTURER_ID_WINBOND,
            .MemoryType = 0x40u,
            .CapacityID = W25QXX_CAPACITY_ID_256MBIT
        },
        .ExpectedStatusRegister1Instruction = 0x05u,
        .StatusRegister1ResponseSequence = {0x00u, 0x02u},
        .StatusRegister1ResponseCount = 2u,
        .ExpectedStatusRegister2Instruction = 0x35u,
        .StatusRegister2ResponseSequence = {0x02u, 0x02u},
        .StatusRegister2ResponseCount = 2u,
        .ExpectedExecuteInstruction = 0x06u,
        .ExpectedAddressedWriteInstruction = 0x34u,
        .ExpectedAddressedWriteAddress = 0x00012340u,
        .ExpectedAddressedWriteTransferConfig = {
            .AddressLength = 4u,
            .AddressLineMode = W25QXX_BUS_LINES_1,
            .DataLineMode = W25QXX_BUS_LINES_4,
            .HasAlternateByte = false,
            .AlternateByte = 0u,
            .AlternateByteLineMode = W25QXX_BUS_LINES_1,
            .DummyCycles = 0u
        },
        .ExpectedAddressedWriteData = {0xAAu, 0x55u, 0x33u, 0xCCu},
        .ExpectedAddressedWriteDataLength = sizeof(expected_data),
        .ExpectedStatusPollingInstruction = 0x05u,
        .ExpectedStatusPollingMatch = 0x00u,
        .ExpectedStatusPollingMask = 0x01u,
        .StatusPollingStatus = W25QXX_BUS_BUSY,
        .StatusPollingAbortStatus = W25QXX_BUS_OK
    };
    W25Qxx_HandleTypeDef hflash = {
        .BusOps = &test_w25qxx_fake_bus_ops,
        .BusContext = &fake_bus,
        .ExpectedJedecID = &test_w25q256_expected_id
    };

    assert(W25Qxx_Init(&hflash) == W25QXX_OK);
    assert(W25Qxx_ProgramPageStart(&hflash,
                                    fake_bus.ExpectedAddressedWriteAddress,
                                    expected_data,
                                    sizeof(expected_data)) == W25QXX_OK);
    assert(fake_bus.ExecuteWasCalled);
    assert(fake_bus.AddressedWriteWasCalled);
    assert(fake_bus.StatusPollingWasCalled);
    assert(hflash.State == W25QXX_STATE_BUSY);
    assert(hflash.ActiveOperation == W25QXX_OPERATION_PAGE_PROGRAM);
    assert(W25Qxx_Process(&hflash) == W25QXX_BUSY);
    assert(hflash.State == W25QXX_STATE_BUSY);
    fake_bus.StatusPollingStatus = W25QXX_BUS_OK;
    assert(W25Qxx_Process(&hflash) == W25QXX_OK);
    assert(hflash.State == W25QXX_STATE_READY);
    assert(hflash.ActiveOperation == W25QXX_OPERATION_NONE);
}

/**
 * @brief 验证跨越物理页边界的编程请求在提交总线前被拒绝。
 * @note 通过 Fake Bus 提供输入并以断言核对结果；不访问硬件，断言失败即终止测试。
 */
static void test_w25qxx_program_page_rejects_page_crossing_request(void)
{
    static const uint8_t data[] = {0x00u, 0x00u, 0x00u};
    W25Qxx_FakeBusTypeDef fake_bus = {
        .ExpectedInstruction = 0x9Fu,
        .Response = {
            .ManufacturerID = W25QXX_MANUFACTURER_ID_WINBOND,
            .MemoryType = 0x40u,
            .CapacityID = W25QXX_CAPACITY_ID_256MBIT
        }
    };
    W25Qxx_HandleTypeDef hflash = {
        .BusOps = &test_w25qxx_fake_bus_ops,
        .BusContext = &fake_bus,
        .ExpectedJedecID = &test_w25q256_expected_id
    };

    assert(W25Qxx_Init(&hflash) == W25QXX_OK);
    assert(W25Qxx_ProgramPageStart(&hflash,
                                    0x000123FEu,
                                    data,
                                    sizeof(data)) == W25QXX_ERROR);
    assert(!fake_bus.ExecuteWasCalled);
    assert(!fake_bus.AddressedWriteWasCalled);
    assert(hflash.State == W25QXX_STATE_READY);
    assert(hflash.ErrorCode == W25QXX_ERROR_PAGE_BOUNDARY);
}

/**
 * @brief 验证 QE 未开启时拒绝 Quad 页编程。
 * @note 通过 Fake Bus 提供输入并以断言核对结果；不访问硬件，断言失败即终止测试。
 */
static void test_w25qxx_program_page_rejects_quad_disabled_flash(void)
{
    static const uint8_t data[] = {0xA5u};
    W25Qxx_FakeBusTypeDef fake_bus = {
        .ExpectedInstruction = 0x9Fu,
        .Response = {
            .ManufacturerID = W25QXX_MANUFACTURER_ID_WINBOND,
            .MemoryType = 0x40u,
            .CapacityID = W25QXX_CAPACITY_ID_256MBIT
        },
        .ExpectedStatusRegister1Instruction = 0x05u,
        .StatusRegister1Response = 0x00u,
        .ExpectedStatusRegister2Instruction = 0x35u,
        .StatusRegister2Response = 0x00u
    };
    W25Qxx_HandleTypeDef hflash = {
        .BusOps = &test_w25qxx_fake_bus_ops,
        .BusContext = &fake_bus,
        .ExpectedJedecID = &test_w25q256_expected_id
    };

    assert(W25Qxx_Init(&hflash) == W25QXX_OK);
    assert(W25Qxx_ProgramPageStart(&hflash,
                                    0x00012340u,
                                    data,
                                    sizeof(data)) == W25QXX_ERROR);
    assert(!fake_bus.ExecuteWasCalled);
    assert(!fake_bus.AddressedWriteWasCalled);
    assert(hflash.State == W25QXX_STATE_READY);
    assert(hflash.ErrorCode == W25QXX_ERROR_QUAD_NOT_ENABLED);
}

/**
 * @brief 验证页编程超时会记录专用错误并中止状态轮询。
 * @note 通过 Fake Bus 提供输入并以断言核对结果；不访问硬件，断言失败即终止测试。
 */
static void test_w25qxx_process_times_out_stalled_page_program(void)
{
    static const uint8_t data[] = {0x5Au};
    W25Qxx_FakeBusTypeDef fake_bus = {
        .ExpectedInstruction = 0x9Fu,
        .Response = {
            .ManufacturerID = W25QXX_MANUFACTURER_ID_WINBOND,
            .MemoryType = 0x40u,
            .CapacityID = W25QXX_CAPACITY_ID_256MBIT
        },
        .ExpectedStatusRegister1Instruction = 0x05u,
        .StatusRegister1ResponseSequence = {0x00u, 0x02u},
        .StatusRegister1ResponseCount = 2u,
        .ExpectedStatusRegister2Instruction = 0x35u,
        .StatusRegister2ResponseSequence = {0x02u, 0x02u},
        .StatusRegister2ResponseCount = 2u,
        .ExpectedExecuteInstruction = 0x06u,
        .ExpectedAddressedWriteInstruction = 0x34u,
        .ExpectedAddressedWriteAddress = 0x00012340u,
        .ExpectedAddressedWriteTransferConfig = {
            .AddressLength = 4u,
            .AddressLineMode = W25QXX_BUS_LINES_1,
            .DataLineMode = W25QXX_BUS_LINES_4,
            .HasAlternateByte = false,
            .AlternateByte = 0u,
            .AlternateByteLineMode = W25QXX_BUS_LINES_1,
            .DummyCycles = 0u
        },
        .ExpectedAddressedWriteData = {0x5Au},
        .ExpectedAddressedWriteDataLength = sizeof(data),
        .ExpectedStatusPollingInstruction = 0x05u,
        .ExpectedStatusPollingMatch = 0x00u,
        .ExpectedStatusPollingMask = 0x01u,
        .StatusPollingStatus = W25QXX_BUS_BUSY,
        .StatusPollingAbortStatus = W25QXX_BUS_OK
    };
    W25Qxx_HandleTypeDef hflash = {
        .BusOps = &test_w25qxx_fake_bus_ops,
        .BusContext = &fake_bus,
        .ExpectedJedecID = &test_w25q256_expected_id
    };

    assert(W25Qxx_Init(&hflash) == W25QXX_OK);
    assert(W25Qxx_ProgramPageStart(&hflash,
                                    fake_bus.ExpectedAddressedWriteAddress,
                                    data,
                                    sizeof(data)) == W25QXX_OK);
    fake_bus.TickMs = 5u;
    assert(W25Qxx_Process(&hflash) == W25QXX_ERROR);
    assert(hflash.State == W25QXX_STATE_ERROR);
    assert(hflash.ActiveOperation == W25QXX_OPERATION_NONE);
    assert(hflash.ErrorCode == W25QXX_ERROR_PAGE_PROGRAM_TIMEOUT);
    assert(hflash.LastBusStatus == W25QXX_BUS_TIMEOUT);
    assert(fake_bus.StatusPollingAbortWasCalled);
}

/**
 * @brief 验证对齐扇区擦除的命令、状态轮询及完成状态。
 * @note 通过 Fake Bus 提供输入并以断言核对结果；不访问硬件，断言失败即终止测试。
 */
static void test_w25qxx_sector_erase_starts_and_completes(void)
{
    W25Qxx_FakeBusTypeDef fake_bus = {
        .ExpectedInstruction = 0x9Fu,
        .Response = {
            .ManufacturerID = W25QXX_MANUFACTURER_ID_WINBOND,
            .MemoryType = 0x40u,
            .CapacityID = W25QXX_CAPACITY_ID_256MBIT
        },
        .ExpectedStatusRegister1Instruction = 0x05u,
        .StatusRegister1ResponseSequence = {0x00u, 0x02u},
        .StatusRegister1ResponseCount = 2u,
        .ExpectedStatusRegister2Instruction = 0x35u,
        .StatusRegister2ResponseSequence = {0x02u, 0x02u},
        .StatusRegister2ResponseCount = 2u,
        .ExpectedExecuteInstruction = 0x06u,
        .ExpectedAddressedExecuteInstruction = 0x21u,
        .ExpectedAddressedExecuteAddress = 0x01FFF000u,
        .ExpectedAddressedExecuteTransferConfig = {
            .AddressLength = 4u,
            .AddressLineMode = W25QXX_BUS_LINES_1,
            .DataLineMode = W25QXX_BUS_LINES_1,
            .HasAlternateByte = false,
            .AlternateByte = 0u,
            .AlternateByteLineMode = W25QXX_BUS_LINES_1,
            .DummyCycles = 0u
        },
        .ExpectedStatusPollingInstruction = 0x05u,
        .ExpectedStatusPollingMatch = 0x00u,
        .ExpectedStatusPollingMask = 0x01u,
        .StatusPollingStatus = W25QXX_BUS_BUSY,
        .StatusPollingAbortStatus = W25QXX_BUS_OK
    };
    W25Qxx_HandleTypeDef hflash = {
        .BusOps = &test_w25qxx_fake_bus_ops,
        .BusContext = &fake_bus,
        .ExpectedJedecID = &test_w25q256_expected_id
    };

    assert(W25Qxx_Init(&hflash) == W25QXX_OK);
    assert(W25Qxx_SectorEraseStart(&hflash,
                                    fake_bus.ExpectedAddressedExecuteAddress) == W25QXX_OK);
    assert(fake_bus.ExecuteWasCalled);
    assert(fake_bus.AddressedExecuteWasCalled);
    assert(fake_bus.StatusPollingWasCalled);
    assert(hflash.State == W25QXX_STATE_BUSY);
    assert(hflash.ActiveOperation == W25QXX_OPERATION_SECTOR_ERASE);
    assert(W25Qxx_Process(&hflash) == W25QXX_BUSY);
    fake_bus.StatusPollingStatus = W25QXX_BUS_OK;
    assert(W25Qxx_Process(&hflash) == W25QXX_OK);
    assert(hflash.State == W25QXX_STATE_READY);
    assert(hflash.ActiveOperation == W25QXX_OPERATION_NONE);
}

/**
 * @brief 验证未按 4 KiB 对齐的扇区擦除请求被拒绝。
 * @note 通过 Fake Bus 提供输入并以断言核对结果；不访问硬件，断言失败即终止测试。
 */
static void test_w25qxx_sector_erase_rejects_unaligned_address(void)
{
    W25Qxx_FakeBusTypeDef fake_bus = {
        .ExpectedInstruction = 0x9Fu,
        .Response = {
            .ManufacturerID = W25QXX_MANUFACTURER_ID_WINBOND,
            .MemoryType = 0x40u,
            .CapacityID = W25QXX_CAPACITY_ID_256MBIT
        }
    };
    W25Qxx_HandleTypeDef hflash = {
        .BusOps = &test_w25qxx_fake_bus_ops,
        .BusContext = &fake_bus,
        .ExpectedJedecID = &test_w25q256_expected_id
    };

    assert(W25Qxx_Init(&hflash) == W25QXX_OK);
    assert(W25Qxx_SectorEraseStart(&hflash, 0x00000001u) == W25QXX_ERROR);
    assert(!fake_bus.ExecuteWasCalled);
    assert(!fake_bus.AddressedExecuteWasCalled);
    assert(hflash.State == W25QXX_STATE_READY);
    assert(hflash.ErrorCode == W25QXX_ERROR_SECTOR_BOUNDARY);
}

/**
 * @brief 验证扇区擦除超时会记录专用错误并中止状态轮询。
 * @note 通过 Fake Bus 提供输入并以断言核对结果；不访问硬件，断言失败即终止测试。
 */
static void test_w25qxx_process_times_out_stalled_sector_erase(void)
{
    W25Qxx_FakeBusTypeDef fake_bus = {
        .ExpectedInstruction = 0x9Fu,
        .Response = {
            .ManufacturerID = W25QXX_MANUFACTURER_ID_WINBOND,
            .MemoryType = 0x40u,
            .CapacityID = W25QXX_CAPACITY_ID_256MBIT
        },
        .ExpectedStatusRegister1Instruction = 0x05u,
        .StatusRegister1ResponseSequence = {0x00u, 0x02u},
        .StatusRegister1ResponseCount = 2u,
        .ExpectedStatusRegister2Instruction = 0x35u,
        .StatusRegister2ResponseSequence = {0x02u, 0x02u},
        .StatusRegister2ResponseCount = 2u,
        .ExpectedExecuteInstruction = 0x06u,
        .ExpectedAddressedExecuteInstruction = 0x21u,
        .ExpectedAddressedExecuteAddress = 0x00000000u,
        .ExpectedAddressedExecuteTransferConfig = {
            .AddressLength = 4u,
            .AddressLineMode = W25QXX_BUS_LINES_1,
            .DataLineMode = W25QXX_BUS_LINES_1,
            .HasAlternateByte = false,
            .AlternateByte = 0u,
            .AlternateByteLineMode = W25QXX_BUS_LINES_1,
            .DummyCycles = 0u
        },
        .ExpectedStatusPollingInstruction = 0x05u,
        .ExpectedStatusPollingMatch = 0x00u,
        .ExpectedStatusPollingMask = 0x01u,
        .StatusPollingStatus = W25QXX_BUS_BUSY,
        .StatusPollingAbortStatus = W25QXX_BUS_OK
    };
    W25Qxx_HandleTypeDef hflash = {
        .BusOps = &test_w25qxx_fake_bus_ops,
        .BusContext = &fake_bus,
        .ExpectedJedecID = &test_w25q256_expected_id
    };

    assert(W25Qxx_Init(&hflash) == W25QXX_OK);
    assert(W25Qxx_SectorEraseStart(&hflash,
                                    fake_bus.ExpectedAddressedExecuteAddress) == W25QXX_OK);
    fake_bus.TickMs = 500u;
    assert(W25Qxx_Process(&hflash) == W25QXX_ERROR);
    assert(hflash.State == W25QXX_STATE_ERROR);
    assert(hflash.ActiveOperation == W25QXX_OPERATION_NONE);
    assert(hflash.ErrorCode == W25QXX_ERROR_SECTOR_ERASE_TIMEOUT);
    assert(hflash.LastBusStatus == W25QXX_BUS_TIMEOUT);
    assert(fake_bus.StatusPollingAbortWasCalled);
}

static unsigned quiesce_calls;

/**
 * @brief 累计安全收尾调用次数并模拟控制器已静止。
 * @param[in] context 未使用，保留 BusOps 签名。
 * @return 固定返回 W25QXX_BUS_OK。
 * @note 不修改 Fake NOR 的 WIP，用于区分控制器收尾和芯片内部完成。
 */
static W25Qxx_BusStatusTypeDef test_quiesce(void *context)
{
    (void)context;
    ++quiesce_calls;
    return W25QXX_BUS_OK;
}

/**
 * @brief 验证 Quiesce 成功仍保留 ERROR，直到 WIP 清零后显式恢复 READY。
 * @note 通过 Fake Bus 提供输入并以断言核对结果；不访问硬件，断言失败即终止测试。
 */
static void test_quiesce_is_not_nor_operation_cancellation(void)
{
    W25Qxx_FakeBusTypeDef bus = {0};
    W25Qxx_BusOpsTypeDef ops = test_w25qxx_fake_bus_ops;
    ops.Quiesce = test_quiesce;
    bus.ExpectedStatusRegister1Instruction = 0x05;
    bus.ExpectedStatusRegister2Instruction = 0x35;
    bus.StatusRegister1Response = 1;
    bus.StatusRegister2Response = 2;
    W25Qxx_HandleTypeDef h = {.BusOps = &ops,
                              .BusContext = &bus,
                              .ExpectedJedecID = &test_w25q256_expected_id,
                              .State = W25QXX_STATE_ERROR};
    assert(W25Qxx_Quiesce(&h) == W25QXX_OK);
    assert(quiesce_calls == 1 && h.State == W25QXX_STATE_ERROR);
    assert(W25Qxx_Recover(&h) == W25QXX_BUSY);
    assert(h.State == W25QXX_STATE_ERROR);
    bus.StatusRegister1Response = 0;
    assert(W25Qxx_Recover(&h) == W25QXX_OK);
    assert(h.State == W25QXX_STATE_READY);
}

/**
 * @brief 验证真实 FTL Bridge 的分区边界、地址转换及异步推进。
 * @note 通过 Fake Bus 提供输入并以断言核对结果；不访问硬件，断言失败即终止测试。
 */
static void test_ftl_bridge_bounds_and_async_progress(void)
{
    static uint32_t map[32];
    static uint64_t versions[32];
    static uint8_t states[32], work[4096], scratch[512];
    FlashFTL_MemoryTypeDef memory = {map, versions, states, 32, 32, work, scratch};
    FlashFTL_HandleTypeDef ftl;
    FlashFTL_W25QxxBridgeTypeDef bridge;
    W25Qxx_BusOpsTypeDef ops = test_w25qxx_fake_bus_ops;
    ops.Quiesce = test_quiesce;
    W25Qxx_FakeBusTypeDef bus = {
        .ExpectedInstruction = 0x9f,
        .Response = {W25QXX_MANUFACTURER_ID_WINBOND, 0x40, W25QXX_CAPACITY_ID_256MBIT},
        .ExpectedAsyncAddressedInstruction = 0xec,
        .ExpectedAsyncAddress = 0x201000 - 256,
        .ExpectedAsyncAddressedDataLength = 256,
        .ExpectedAsyncAddressedTransferConfig = {.AddressLength = 4,
                                                 .AddressLineMode = W25QXX_BUS_LINES_4,
                                                 .DataLineMode = W25QXX_BUS_LINES_4,
                                                 .HasAlternateByte = true,
                                                 .AlternateByte = 0xff,
                                                 .AlternateByteLineMode = W25QXX_BUS_LINES_4,
                                                 .DummyCycles = 4},
        .AsyncAddressedStatus = W25QXX_BUS_BUSY};
    W25Qxx_HandleTypeDef device = {
        .BusOps = &ops, .BusContext = &bus, .ExpectedJedecID = &test_w25q256_expected_id};
    assert(W25Qxx_Init(&device) == W25QXX_OK);
    assert(FlashFTL_W25QxxBridge_Bind(&ftl, &bridge, &device, 1, 0x20000, &memory) ==
           FLASH_FTL_INVALID_PARAM);
    assert(FlashFTL_W25QxxBridge_Bind(&ftl, &bridge, &device, 0x1fff000, 0x20000, &memory) ==
           FLASH_FTL_INVALID_PARAM);
    assert(FlashFTL_W25QxxBridge_Bind(&ftl, &bridge, &device, 0x1e1000, 0x20000, &memory) ==
           FLASH_FTL_OK);
    assert(ftl.Ops->ReadStart(ftl.Context, 0x20000, scratch, 1) == FLASH_FTL_RAW_ERROR);
    assert(ftl.Ops->ReadStart(ftl.Context, 0x1ffff, scratch, 2) == FLASH_FTL_RAW_ERROR);
    assert(ftl.Ops->ReadStart(ftl.Context, UINT32_MAX, scratch, 2) == FLASH_FTL_RAW_ERROR);
    assert(ftl.Ops->ProgramStart(ftl.Context, 128, scratch, 256) == FLASH_FTL_RAW_ERROR);
    assert(ftl.Ops->EraseStart(ftl.Context, 1) == FLASH_FTL_RAW_ERROR);
    assert(ftl.Ops->EraseStart(ftl.Context, 0x20000) == FLASH_FTL_RAW_ERROR);
    assert(!bus.AsyncAddressedWasCalled && !bus.ExecuteWasCalled);
    assert(ftl.Ops->ReadStart(ftl.Context, 0x20000 - 256, scratch, 256) == FLASH_FTL_RAW_OK);
    assert(bus.AsyncAddressedWasCalled);
    assert(ftl.Ops->Process(ftl.Context) == FLASH_FTL_RAW_BUSY);
    bus.AsyncAddressedStatus = W25QXX_BUS_OK;
    assert(ftl.Ops->Process(ftl.Context) == FLASH_FTL_RAW_OK);
}

/**
 * @brief 运行 W25Qxx 识别、Quad 配置、数组操作、安全收尾及真实 FTL Bridge 回归。
 * @return 全部断言通过返回 0，失败由断言终止进程。
 * @note 仅主机测试，不覆盖 HAL、DMA/Cache 或实际 NOR 时序。
 */
int main(void)
{
    test_quiesce_is_not_nor_operation_cancellation();
    test_ftl_bridge_bounds_and_async_progress();

    test_w25qxx_init_accepts_expected_w25q256();
    test_w25qxx_init_rejects_unexpected_manufacturer();
    test_w25qxx_init_rejects_unexpected_capacity();
    test_w25qxx_get_array_read_protocol_describes_w25q256_quad_i_o();
    test_w25qxx_probe_sfdp_accepts_valid_signature();
    test_w25qxx_probe_sfdp_rejects_invalid_signature();
    test_w25qxx_read_status_registers_parses_wip_wel_and_qe();
    test_w25qxx_read_status_registers_rejects_status_register_2_bus_error();
    test_w25qxx_ensure_quad_enabled_skips_write_when_qe_is_set();
    test_w25qxx_ensure_quad_enabled_sets_qe_and_verifies_it();
    test_w25qxx_ensure_quad_enabled_rejects_busy_flash_without_writing();
    test_w25qxx_read_uses_quad_i_o_4byte_address_command();
    test_w25qxx_read_rejects_unaligned_quad_i_o_address();
    test_w25qxx_read_rejects_non_w25q256_fixed_4byte_operation();
    test_w25qxx_start_read_uses_quad_i_o_4byte_address_command();
    test_w25qxx_process_times_out_stalled_async_read();
    test_w25qxx_program_page_starts_quad_4byte_operation();
    test_w25qxx_program_page_rejects_page_crossing_request();
    test_w25qxx_program_page_rejects_quad_disabled_flash();
    test_w25qxx_process_times_out_stalled_page_program();
    test_w25qxx_sector_erase_starts_and_completes();
    test_w25qxx_sector_erase_rejects_unaligned_address();
    test_w25qxx_process_times_out_stalled_sector_erase();

    return 0;
}
