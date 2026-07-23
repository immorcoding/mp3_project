#include "audio_port.h"
#include "BSP/Devices/audio/audio.h"
#include "i2s.h"
#include "stm32h7xx_hal_gpio.h"
#include <stdbool.h>
#include <stdint.h>

static Audio_BusStatusTypeDef audio_port_result(int32_t native_status)
{
/*******自定义修改状态码*******/
    HAL_StatusTypeDef status = (HAL_StatusTypeDef)native_status;

    switch (status)
    {
        case HAL_OK:
            return AUDIO_BUS_OK; // Handle specific error case
        case HAL_ERROR:
            return AUDIO_BUS_ERROR; // Handle specific error case
        case HAL_BUSY:
            return AUDIO_BUS_BUSY; // Handle specific error case
        case HAL_TIMEOUT:
            return AUDIO_BUS_TIMEOUT; // Handle specific error case
        default:
            // Handle other errors
            return AUDIO_BUS_ERROR; //unknown error
    
    }
/*****************************/
}

static Audio_BusStatusTypeDef audio_prepare(void *AudioContext)
{
    // Implement the actual preparation of the audio bus (e.g., configure GPIOs, I2C/SPI, etc.)
    // This function should be implemented according to the specific hardware.

    if (AudioContext == NULL)
    {
        return AUDIO_BUS_ERROR; // Handle the error appropriately
    }

/**************** 自定义的音频模块初始化函数 ****************/
    if (HAL_I2S_GetState((I2S_HandleTypeDef *)AudioContext) != HAL_I2S_STATE_READY) //检查i2s句柄是否初始化
    {
        return AUDIO_BUS_ERROR;
    }
    return AUDIO_BUS_OK; //参照return audio_fail();
/****************************************************/
}

static Audio_BusStatusTypeDef audio_transmit(void *AudioContext, const uint16_t *data, uint16_t size)
{
    // Implement the actual data transmission over the audio bus (e.g., I2C/SPI).
    // This function should be implemented according to the specific hardware.

    if (AudioContext == NULL || data == NULL || size == 0)
    {
        return AUDIO_BUS_ERROR; // Handle the error appropriately
    }

/**************** 自定义的音频发送函数 ****************/
    HAL_StatusTypeDef status = HAL_I2S_Transmit((I2S_HandleTypeDef *)AudioContext,
                                                data,
                                                size,
                                                1000);
    return audio_port_result((int32_t)status); // Return the appropriate state after transmission
/****************************************************/
}

static Audio_StatusTypeDef audio_mute(void *MuteContext, bool mute)
{
    (void) MuteContext;

/**************** 自定义的静音函数 ****************/
        HAL_GPIO_WritePin(PCM_XSMT_GPIO_Port, PCM_XSMT_Pin, mute ? GPIO_PIN_RESET : GPIO_PIN_SET);
/****************************************************/
    return AUDIO_OK;
}

static const Audio_BusOpsTypeDef audio_bus_ops = {
    .Transmit = audio_transmit,
    .Prepare = audio_prepare
};

Audio_StatusTypeDef Audio_Port_Bind(Audio_HandleTypeDef *haudio)
{
    // Initialize the audio port (e.g., configure GPIOs, I2C/SPI, etc.)
    // This function should be implemented according to the specific hardware.

    if (haudio == NULL)
    {
        return AUDIO_ERROR; // Handle the error appropriately
    }
    haudio->BusOps = &audio_bus_ops; // Assign the appropriate bus operations for the audio hardware
    haudio->Mute = &audio_mute;
/**************** 句柄绑定，由用户改动 ****************/
    haudio->BusContext = &hi2s2;
    haudio->MuteContext = NULL;

    return AUDIO_OK;
}
