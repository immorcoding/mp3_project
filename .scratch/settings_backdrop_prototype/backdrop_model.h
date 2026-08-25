/*
 * 可删除原型：多卡片局部毛玻璃的缓冲区所有权模型。
 * 不属于正式固件源码，原型结论确认后应删除本目录。
 */

#ifndef BACKDROP_MODEL_H
#define BACKDROP_MODEL_H

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    PROTOTYPE_SCREEN_BOOT = 0,
    PROTOTYPE_SCREEN_LOCK,
    PROTOTYPE_SCREEN_SETTINGS
} Prototype_ScreenTypeDef;

typedef enum
{
    PROTOTYPE_RESULT_OK = 0,
    PROTOTYPE_RESULT_BUSY,
    PROTOTYPE_RESULT_NO_CHANGE
} Prototype_ResultTypeDef;

typedef enum
{
    PROTOTYPE_ACTION_ENTER_BOOT = 0,
    PROTOTYPE_ACTION_ENTER_LOCK,
    PROTOTYPE_ACTION_ENTER_SETTINGS,
    PROTOTYPE_ACTION_CHANGE_WALLPAPER,
    PROTOTYPE_ACTION_RECOMPOSE_CARDS,
    PROTOTYPE_ACTION_REQUEST_OTHER_EFFECT
} Prototype_ActionTypeDef;

typedef struct
{
    Prototype_ScreenTypeDef ActiveScreen;
    bool EffectWorkspaceBusy;
    bool SettingsCompositeValid;
    uint32_t WallpaperVersion;
    uint32_t BlurGeneration;
    uint32_t CompositeGeneration;
    uint8_t SettingsCardCount;
} Prototype_BackdropStateTypeDef;

void Prototype_BackdropStateInit(Prototype_BackdropStateTypeDef *state);
Prototype_ResultTypeDef Prototype_BackdropApply(
    Prototype_BackdropStateTypeDef *state,
    Prototype_ActionTypeDef action);
const char *Prototype_BackdropScreenName(Prototype_ScreenTypeDef screen);
const char *Prototype_BackdropResultName(Prototype_ResultTypeDef result);

#endif /* BACKDROP_MODEL_H */
