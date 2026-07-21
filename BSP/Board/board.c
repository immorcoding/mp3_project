#include "board.h"
#include "System/Log/log.h"

#include "BSP/Devices/audio/audio.h"
#include "BSP/Devices/audio/port/audio_port.h"

#include "BSP/Devices/pmic/pmic.h"
#include "BSP/Devices/pmic/port/pmic_i2c_port.h"

static PMIC_HandleTypeDef hpmic;
static Audio_HandleTypeDef haudio;

Audio_StatusTypeDef Board_Audio_Init(void)
{
    if (Audio_Port_Bind(&haudio) != AUDIO_OK)
    {
        (void)LOG_Printf(LOG_LEVEL_ERROR, "AUDIO", "Audio port bind failed");
        return AUDIO_ERROR;
    }
    Audio_StatusTypeDef audio_status = Audio_Init(&haudio);
    if (audio_status != AUDIO_OK)
    {
        (void)LOG_Printf(LOG_LEVEL_ERROR, "AUDIO", "PMIC initialization failed");
    }

    return Audio_Init(&haudio);
}

PMIC_StatusTypeDef Board_PMIC_Init(void)
{
    /*
     * 第一步仅完成依赖注入：把 Port 提供的 Ops/Context 成对写入 hpmic。
     * Bind 本身不会产生 I2C 波形，也不会检查 AXP2101 是否在线。
     */
    if (PMIC_I2C_Port_Bind(&hpmic) != PMIC_OK)
    {
        (void)LOG_Printf(LOG_LEVEL_ERROR, "PMIC", "PMIC I2C port bind failed");
        return PMIC_ERROR;
    }

    /*
     * 第二步由 Device 层装载默认配置、准备总线、校验芯片 ID，并按需应用
     * 启动表。错误细节始终保存在同一个 hpmic 对象中。
     */
    PMIC_StatusTypeDef pmic_status = PMIC_Init(&hpmic);
    if (pmic_status != PMIC_OK)
    {
        (void)LOG_Printf(LOG_LEVEL_ERROR, "PMIC", "PMIC initialization failed with error code: %d", hpmic.ErrorCode);
        return PMIC_ERROR;
    }
    return PMIC_OK;
}

Board_StatusTypeDef Board_Init(void)
{
    /* 初始化 PMIC */
    if (Board_PMIC_Init() != PMIC_OK)
    {
        // Handle PMIC initialization error
        return BOARD_PMIC_ERROR;
    }

    /* 初始化 Audio */
    if (Board_Audio_Init() != AUDIO_OK)
    {
        // Handle Audio initialization error
        return BOARD_AUDIO_ERROR;
    }

    return BOARD_OK;
}