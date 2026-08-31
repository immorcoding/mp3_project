/**
  ******************************************************************************
  * @file    test_w25qxx.c
  * @brief   W25Qxx Component 的主机行为测试。
  ******************************************************************************
  */

#include "Components/w25qxx/w25qxx.h"
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

static uint32_t test_w25qxx_get_tick_ms(void *context)
{
    const W25Qxx_FakeBusTypeDef *fake_bus = context;

    return (fake_bus != NULL) ? fake_bus->TickMs : 0u;
}

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

static W25Qxx_BusStatusTypeDef test_w25qxx_get_read_addressed_command_status(
    void *context)
{
    const W25Qxx_FakeBusTypeDef *fake_bus = context;

    return (fake_bus != NULL) ? fake_bus->AsyncAddressedStatus : W25QXX_BUS_ERROR;
}

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

static W25Qxx_BusStatusTypeDef test_w25qxx_get_status_match_polling_status(
    void *context)
{
    const W25Qxx_FakeBusTypeDef *fake_bus = context;

    return (fake_bus != NULL) ? fake_bus->StatusPollingStatus : W25QXX_BUS_ERROR;
}

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

int main(void)
{
    test_w25qxx_init_accepts_expected_w25q256();
    test_w25qxx_init_rejects_unexpected_manufacturer();
    test_w25qxx_init_rejects_unexpected_capacity();
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
