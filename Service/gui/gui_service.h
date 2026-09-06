/**
  ******************************************************************************
  * @file    gui_service.h
  * @brief   GUI Service 公共 Interface。
  ******************************************************************************
  */

#ifndef GUI_SERVICE_H
#define GUI_SERVICE_H

#include <stdint.h>
#include "Service/service.h"

#define SERVICE_GUI_QUEUE_NO_CURRENT  0xFFFFU  /* 无有效播放游标时 QueueApply 的 current_index。 */

#define SERVICE_GUI_THEME_DEFAULT   0U  /* 原外观：五色等于占位 hex，开壁纸、开 Music 毛玻璃。 */
#define SERVICE_GUI_THEME_SOLID     1U  /* 近黑纯色：关壁纸、关 Music 毛玻璃。 */
#define SERVICE_GUI_THEME_STARTUP   SERVICE_GUI_THEME_SOLID  /* 上电采用的外观。 */

Service_StatusTypeDef Service_GUI_Init(uint32_t notify_index);
Service_StatusTypeDef Service_GUI_ThemeApply(uint8_t id);
void Service_GUI_Process(void);
uint16_t Service_GUI_QueueScrollLead(void);
Service_StatusTypeDef Service_GUI_QueueConsumeSelect(uint16_t *sheet_index);
Service_StatusTypeDef Service_GUI_QueueApply(
    const char **titles,
    uint16_t length,
    uint16_t window_index,
    uint16_t current_index);

#endif /* GUI_SERVICE_H */
