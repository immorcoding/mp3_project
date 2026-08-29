/**
  ******************************************************************************
  * @file    test_w25qxx.c
  * @brief   W25Qxx Component 的主机行为测试。
  ******************************************************************************
  */

#include "Components/w25qxx/w25qxx.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

typedef struct
{
    uint8_t ExpectedInstruction;
    W25Qxx_JedecIDTypeDef Response;
    bool WasCalled;
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

    if ((fake_bus == NULL) ||
        (data == NULL) ||
        (data_length != sizeof(fake_bus->Response)) ||
        (instruction != fake_bus->ExpectedInstruction))
    {
        return W25QXX_BUS_ERROR;
    }

    fake_bus->WasCalled = true;
    (void)memcpy(data, &fake_bus->Response, sizeof(fake_bus->Response));
    return W25QXX_BUS_OK;
}

static const W25Qxx_BusOpsTypeDef test_w25qxx_fake_bus_ops = {
    .ReadCommand = test_w25qxx_read_command
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

int main(void)
{
    test_w25qxx_init_accepts_expected_w25q256();
    test_w25qxx_init_rejects_unexpected_manufacturer();
    test_w25qxx_init_rejects_unexpected_capacity();

    return 0;
}
