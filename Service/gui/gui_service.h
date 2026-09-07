/**
  ******************************************************************************
  * @file    gui_service.h
  * @brief   GUI Service 公共 Interface。
  ******************************************************************************
  */

#ifndef GUI_SERVICE_H
#define GUI_SERVICE_H

#include <stdbool.h>
#include <stdint.h>
#include "Service/service.h"

#define SERVICE_GUI_QUEUE_NO_CURRENT  0xFFFFU  /* 无有效播放游标时 QueueApply 的 current_index。 */

#define SERVICE_GUI_THEME_DEFAULT   0U  /* 原外观：五色等于占位 hex，开壁纸、开 Music 毛玻璃。 */
#define SERVICE_GUI_THEME_SOLID     1U  /* 近黑纯色：关壁纸、关 Music 毛玻璃。 */
#define SERVICE_GUI_THEME_STARTUP   SERVICE_GUI_THEME_SOLID  /* 上电采用的外观。 */

typedef enum
{
    SERVICE_GUI_INPUT_NONE = 0U,                 /**< 本圈没有待处理点击。 */
    SERVICE_GUI_INPUT_MUSIC_QUEUE_SELECT,        /**< Queue 行点按；sheet_index 有效。 */
    SERVICE_GUI_INPUT_MUSIC_PREVIOUS,            /**< Now Playing 上一首。 */
    SERVICE_GUI_INPUT_MUSIC_PLAY_PAUSE,          /**< Now Playing 播放/暂停。 */
    SERVICE_GUI_INPUT_MUSIC_NEXT                 /**< Now Playing 下一首。 */
} Service_GUI_InputCommandTypeDef;

typedef struct
{
    Service_GUI_InputCommandTypeDef command; /**< 本圈命令；无点击为 NONE。 */
    uint16_t sheet_index;                    /**< 仅 QUEUE_SELECT 时为播放列表下标。 */
} Service_GUI_InputTypeDef;

Service_StatusTypeDef Service_GUI_Init(uint32_t notify_index);
Service_StatusTypeDef Service_GUI_ThemeApply(uint8_t id);
void Service_GUI_Process(void);
uint16_t Service_GUI_QueueScrollLead(void);
Service_StatusTypeDef Service_GUI_ConsumeInput(Service_GUI_InputTypeDef *input);
Service_StatusTypeDef Service_GUI_TransportApply(bool playing);
Service_StatusTypeDef Service_GUI_QueueApply(
    const char **titles,
    uint16_t length,
    uint16_t window_index,
    uint16_t current_index);

#endif /* GUI_SERVICE_H */
