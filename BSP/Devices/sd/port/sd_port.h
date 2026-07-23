/**
  ******************************************************************************
  * @file    sd_port.h
  * @brief   本板 SDMMC1 Adapter 的绑定接口。
  *
  * @details
  *          Bind 只把 SDMMC1 Ops 与对应的 hsd1 Context 成对安装到 Device
  *          Handle，不初始化控制器、不枚举 SD 卡，也不产生总线波形。
  ******************************************************************************
  */

#ifndef SD_PORT_H
#define SD_PORT_H

#include "BSP/Devices/sd/sd.h"

SDCard_StatusTypeDef SDCard_Port_Bind(SDCard_HandleTypeDef *hsdcard);

#endif /* SD_PORT_H */
