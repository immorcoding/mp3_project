#include <assert.h>
#include <stdio.h>

#include "Adapters/stm32_hal/ft6x36_i2c/ft6x36_i2c_stm32_hal_adapter.h"
#include "Components/ft6x36/ft6x36.h"
#include "Components/log/log.h"
#include "Platform/touch/platform_touch.h"

I2C_HandleTypeDef hi2c2;
GPIO_TypeDef test_tp_rst_gpio_port;

static FT6X36_StatusTypeDef test_bind_result;
static FT6X36_StatusTypeDef test_init_result;
static FT6X36_StatusTypeDef test_read_point_result;
static FT6X36_RawPointTypeDef test_raw_point;
static unsigned int test_read_point_call_count;

static void test_reset_fakes(void)
{
    test_bind_result = FT6X36_OK;
    test_init_result = FT6X36_OK;
    test_read_point_result = FT6X36_OK;
    test_raw_point.IsPressed = false;
    test_raw_point.X = 0u;
    test_raw_point.Y = 0u;
    test_read_point_call_count = 0u;
}

FT6X36_StatusTypeDef FT6X36_I2C_STM32HALAdapter_Bind(
    FT6X36_HandleTypeDef *hft6x36,
    FT6X36_I2C_STM32HALAdapterTypeDef *adapter)
{
    (void)hft6x36;
    (void)adapter;

    return test_bind_result;
}

FT6X36_StatusTypeDef FT6X36_Init(FT6X36_HandleTypeDef *hft6x36)
{
    (void)hft6x36;

    return test_init_result;
}

FT6X36_StatusTypeDef FT6X36_ReadID(FT6X36_HandleTypeDef *hft6x36,
                                    uint8_t *chip_id)
{
    (void)hft6x36;

    if (chip_id != NULL)
    {
        *chip_id = 0u;
    }

    return FT6X36_OK;
}

FT6X36_StatusTypeDef FT6X36_ReadRawPoint(
    FT6X36_HandleTypeDef *hft6x36,
    FT6X36_RawPointTypeDef *point)
{
    (void)hft6x36;
    test_read_point_call_count++;

    if ((test_read_point_result == FT6X36_OK) && (point != NULL))
    {
        *point = test_raw_point;
    }

    return test_read_point_result;
}

LOG_StatusTypeDef LOG_Printf(LOG_LevelTypeDef message_level,
                             const char *tag,
                             const char *format,
                             ...)
{
    (void)message_level;
    (void)tag;
    (void)format;

    return LOG_OK;
}

static void test_successful_initialization_makes_touch_available(void)
{
    test_reset_fakes();

    assert(Platform_Touch_Init() == PLATFORM_OK);
    assert(Platform_Touch_IsAvailable());
}

static void test_failed_initialization_keeps_touch_unavailable(void)
{
    Platform_Touch_RawPointTypeDef point;

    test_reset_fakes();
    test_init_result = FT6X36_ERROR;

    assert(Platform_Touch_Init() == PLATFORM_TOUCH_ERROR);
    assert(!Platform_Touch_IsAvailable());
    assert(Platform_Touch_ReadRawPoint(&point) == PLATFORM_TOUCH_ERROR);
    assert(test_read_point_call_count == 0u);
}

static void test_first_runtime_read_failure_stops_future_bus_reads(void)
{
    Platform_Touch_RawPointTypeDef point;

    test_reset_fakes();
    assert(Platform_Touch_Init() == PLATFORM_OK);

    test_read_point_result = FT6X36_ERROR;
    assert(Platform_Touch_ReadRawPoint(&point) == PLATFORM_TOUCH_ERROR);
    assert(!Platform_Touch_IsAvailable());
    assert(test_read_point_call_count == 1u);

    assert(Platform_Touch_ReadRawPoint(&point) == PLATFORM_TOUCH_ERROR);
    assert(test_read_point_call_count == 1u);
}

static void test_available_touch_publishes_raw_point(void)
{
    Platform_Touch_RawPointTypeDef point;

    test_reset_fakes();
    assert(Platform_Touch_Init() == PLATFORM_OK);

    test_raw_point.IsPressed = true;
    test_raw_point.X = 123u;
    test_raw_point.Y = 234u;

    assert(Platform_Touch_ReadRawPoint(&point) == PLATFORM_OK);
    assert(Platform_Touch_IsAvailable());
    assert(test_read_point_call_count == 1u);
    assert(point.IsPressed);
    assert(point.X == 123u);
    assert(point.Y == 234u);
}

int main(void)
{
    test_successful_initialization_makes_touch_available();
    test_failed_initialization_keeps_touch_unavailable();
    test_first_runtime_read_failure_stops_future_bus_reads();
    test_available_touch_publishes_raw_point();

    puts("Platform Touch behavior tests passed.");
    return 0;
}
