#include "Platform/log/platform_log.h"

#include "Adapters/log_usb_cdc/log_usb_cdc_adapter.h"
#include "Components/log/log_adapter.h"

static LOG_UsbCDCAdapterTypeDef hplatform_log_usb_cdc;

/**
  * @brief 为本产品装配 USB CDC 输出与 HAL 时间源并初始化 Log Component。
  * @retval PLATFORM_OK 日志装配和初始化成功。
  * @retval PLATFORM_LOG_ERROR Adapter 绑定或日志初始化失败。
  */
Platform_StatusTypeDef Platform_Log_Init(void)
{
    LOG_OutputTypeDef output;
    LOG_TimeSourceTypeDef time_source;

    if (LOG_UsbCDCAdapter_Bind(&hplatform_log_usb_cdc,
                               &output,
                               &time_source) != LOG_OK)
    {
        return PLATFORM_LOG_ERROR;
    }

    return (LOG_Init(&output, &time_source) == LOG_OK)
        ? PLATFORM_OK
        : PLATFORM_LOG_ERROR;
}
