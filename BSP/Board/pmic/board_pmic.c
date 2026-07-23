#include "BSP/Board/board.h"
#include "BSP/Devices/pmic/pmic.h"
#include "BSP/Devices/pmic/port/pmic_i2c_port.h"
#include "System/Log/log.h"

static PMIC_HandleTypeDef hpmic;

Board_StatusTypeDef Board_Audio_SetPower(bool enabled)
{
    if (PMIC_SetALDO1Enabled(&hpmic, enabled) != PMIC_OK)
    {
        (void)LOG_Printf(LOG_LEVEL_ERROR,
                         "AUDIO",
                         "power control failed: error=%d, bus=%d, reg=0x%02X",
                         (int)hpmic.ErrorCode,
                         (int)hpmic.LastBusStatus,
                         (unsigned int)hpmic.LastFailedRegister);
        return BOARD_AUDIO_ERROR;
    }

    return BOARD_OK;
}

Board_StatusTypeDef Board_LCD_SetPower(bool enabled)
{
    if (PMIC_SetALDO2Enabled(&hpmic, enabled) != PMIC_OK)
    {
        (void)LOG_Printf(LOG_LEVEL_ERROR,
                         "LCD",
                         "power control failed: error=%d, bus=%d, reg=0x%02X",
                         (int)hpmic.ErrorCode,
                         (int)hpmic.LastBusStatus,
                         (unsigned int)hpmic.LastFailedRegister);
        return BOARD_LCD_ERROR;
    }

    return BOARD_OK;
}

Board_StatusTypeDef Board_PMIC_Init(void)
{
    /*
     * 第一步仅完成依赖注入：把 Port 提供的 Ops/Context 成对写入 hpmic。
     * Bind 本身不会产生 I2C 波形，也不会检查 AXP2101 是否在线。
     */
    if (PMIC_I2C_Port_Bind(&hpmic) != PMIC_OK)
    {
        (void)LOG_Printf(LOG_LEVEL_ERROR, "PMIC", "PMIC I2C port bind failed");
        return BOARD_PMIC_ERROR;
    }

    /*
     * 第二步由 Device 层装载默认配置、准备总线、校验芯片 ID，并按需应用
     * 启动表。错误细节始终保存在同一个 hpmic 对象中。
     */
    PMIC_StatusTypeDef pmic_status = PMIC_Init(&hpmic);
    if (pmic_status != PMIC_OK)
    {
        (void)LOG_Printf(LOG_LEVEL_ERROR,
                         "PMIC",
                         "initialization failed: error=%d, bus=%d, reg=0x%02X",
                         (int)hpmic.ErrorCode,
                         (int)hpmic.LastBusStatus,
                         (unsigned int)hpmic.LastFailedRegister);
        return BOARD_PMIC_ERROR;
    }
    return BOARD_OK;
}
