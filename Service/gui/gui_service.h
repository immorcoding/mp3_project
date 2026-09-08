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
#include "Platform/platform.h"
#include "Platform/lcd/platform_lcd.h"

#define SERVICE_GUI_QUEUE_NO_CURRENT  0xFFFFU  /* 无有效播放游标时 QueueApply 的 current_index。 */

#define SERVICE_GUI_THEME_DEFAULT   0U  /* 原外观：五色等于占位 hex，开壁纸、开 Music 毛玻璃。 */
#define SERVICE_GUI_THEME_SOLID     1U  /* 近黑纯色：关壁纸、关 Music 毛玻璃。 */
#define SERVICE_GUI_THEME_STARTUP   SERVICE_GUI_THEME_SOLID  /* 上电采用的外观。 */

#define SERVICE_GUI_WIDTH           PLATFORM_LCD_WIDTH  /* 当前产品 LCD 可见宽度，单位为像素。 */
#define SERVICE_GUI_HEIGHT          PLATFORM_LCD_HEIGHT  /* 当前产品 LCD 可见高度，单位为像素。 */

#define SERVICE_GUI_MUSIC_VINYL_DIAMETER   144U  /* 须与 MusicPlayerVinylImage 宽高一致，单位为像素。 */

typedef enum
{
    SERVICE_GUI_INPUT_NONE = 0U,                 /**< 本圈没有待处理点击。 */
    SERVICE_GUI_INPUT_MUSIC_QUEUE_SELECT,        /**< Queue 行点按；param 有效。 */
    SERVICE_GUI_INPUT_MUSIC_PREVIOUS,            /**< Now Playing 上一首。 */
    SERVICE_GUI_INPUT_MUSIC_PLAY_PAUSE,          /**< Now Playing 播放/暂停。 */
    SERVICE_GUI_INPUT_MUSIC_NEXT,            /**< Now Playing 下一首。 */
    SERVICE_GUI_INPUT_MUSIC_SEEK             /**< 进度条松手；param 为 0..100 百分比。 */
} Service_GUI_InputCommandTypeDef;

typedef struct
{
    Service_GUI_InputCommandTypeDef command; /**< 本圈命令；无点击为 NONE。 */
    uint16_t param;                    /**< QUEUE_SELECT：播放列表下标。SEEK：0..100 百分比。 */
} Service_GUI_InputTypeDef;

Service_StatusTypeDef Service_GUI_Init(uint32_t notify_index);
Service_StatusTypeDef Service_GUI_ThemeApply(uint8_t id);
void Service_GUI_Process(void);
uint16_t Service_GUI_QueueScrollLead(void);
Service_StatusTypeDef Service_GUI_ConsumeInput(Service_GUI_InputTypeDef *input);
Service_StatusTypeDef Service_GUI_TransportApply(bool playing);
Service_StatusTypeDef Service_GUI_ProgressApply(uint8_t percent);
Service_StatusTypeDef Service_GUI_QueueApply(
    const char **titles,
    uint16_t length,
    uint16_t window_index,
    uint16_t current_index);

#endif /* GUI_SERVICE_H */
