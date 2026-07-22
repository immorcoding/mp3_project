/**
  ******************************************************************************
  * @file    sd_port.h
  * @brief   本板 SDMMC1 Adapter 的绑定接口。
  ******************************************************************************
  */

#ifndef SD_PORT_H
#define SD_PORT_H

#include "BSP/Devices/sd/sd.h"

/**
  * @brief  将本板的 SDMMC1、hsd1 和低有效 SD_CD 安装到 Device 句柄。
  * @param  hsdcard 待绑定的 SD Card Device 句柄。
  * @retval SDCARD_OK    绑定成功；本函数不产生 SDMMC 总线波形。
  * @retval SDCARD_ERROR 句柄为空。
  */
SDCard_StatusTypeDef SDCard_Port_Bind(SDCard_HandleTypeDef *hsdcard);

#endif /* SD_PORT_H */
