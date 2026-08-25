/* 可删除原型的纯状态机；不包含终端、LVGL 或硬件接口。 */

#include "backdrop_model.h"

static void prototype_generate_settings_composite(
    Prototype_BackdropStateTypeDef *state)
{
    /*
     * 模拟：工作区先生成全屏模糊壁纸，再按所有卡片圆角区域裁剪进持久合成背景。
     * 卡片数量只影响裁剪次数，不新增长期卡片像素缓冲。
     */
    state->EffectWorkspaceBusy = true;
    state->BlurGeneration++;
    state->CompositeGeneration++;
    state->SettingsCompositeValid = true;
    state->EffectWorkspaceBusy = false;
}

void Prototype_BackdropStateInit(Prototype_BackdropStateTypeDef *state)
{
    state->ActiveScreen = PROTOTYPE_SCREEN_BOOT;
    state->EffectWorkspaceBusy = false;
    state->SettingsCompositeValid = false;
    state->WallpaperVersion = 1U;
    state->BlurGeneration = 0U;
    state->CompositeGeneration = 0U;
    state->SettingsCardCount = 4U;
}

Prototype_ResultTypeDef Prototype_BackdropApply(
    Prototype_BackdropStateTypeDef *state,
    Prototype_ActionTypeDef action)
{
    switch (action)
    {
        case PROTOTYPE_ACTION_ENTER_BOOT:
            if (state->EffectWorkspaceBusy)
            {
                return PROTOTYPE_RESULT_BUSY;
            }

            state->ActiveScreen = PROTOTYPE_SCREEN_BOOT;
            state->EffectWorkspaceBusy = true;
            state->BlurGeneration++;
            return PROTOTYPE_RESULT_OK;

        case PROTOTYPE_ACTION_ENTER_LOCK:
            state->ActiveScreen = PROTOTYPE_SCREEN_LOCK;
            /* Boot 已切出；工作区可被后续视觉效果重新申请。 */
            state->EffectWorkspaceBusy = false;
            return PROTOTYPE_RESULT_OK;

        case PROTOTYPE_ACTION_ENTER_SETTINGS:
            if (state->EffectWorkspaceBusy)
            {
                return PROTOTYPE_RESULT_BUSY;
            }

            state->ActiveScreen = PROTOTYPE_SCREEN_SETTINGS;
            prototype_generate_settings_composite(state);
            return PROTOTYPE_RESULT_OK;

        case PROTOTYPE_ACTION_CHANGE_WALLPAPER:
            state->WallpaperVersion++;
            state->SettingsCompositeValid = false;

            if (state->ActiveScreen == PROTOTYPE_SCREEN_SETTINGS)
            {
                if (state->EffectWorkspaceBusy)
                {
                    return PROTOTYPE_RESULT_BUSY;
                }

                prototype_generate_settings_composite(state);
            }

            return PROTOTYPE_RESULT_OK;

        case PROTOTYPE_ACTION_RECOMPOSE_CARDS:
            if (state->ActiveScreen != PROTOTYPE_SCREEN_SETTINGS)
            {
                return PROTOTYPE_RESULT_NO_CHANGE;
            }

            if (state->EffectWorkspaceBusy)
            {
                return PROTOTYPE_RESULT_BUSY;
            }

            prototype_generate_settings_composite(state);
            return PROTOTYPE_RESULT_OK;

        case PROTOTYPE_ACTION_REQUEST_OTHER_EFFECT:
            return state->EffectWorkspaceBusy
                ? PROTOTYPE_RESULT_BUSY
                : PROTOTYPE_RESULT_OK;

        default:
            return PROTOTYPE_RESULT_NO_CHANGE;
    }
}

const char *Prototype_BackdropScreenName(Prototype_ScreenTypeDef screen)
{
    switch (screen)
    {
        case PROTOTYPE_SCREEN_BOOT:
            return "Boot";
        case PROTOTYPE_SCREEN_LOCK:
            return "Lock";
        case PROTOTYPE_SCREEN_SETTINGS:
            return "Settings";
        default:
            return "Unknown";
    }
}

const char *Prototype_BackdropResultName(Prototype_ResultTypeDef result)
{
    switch (result)
    {
        case PROTOTYPE_RESULT_OK:
            return "OK";
        case PROTOTYPE_RESULT_BUSY:
            return "BUSY";
        case PROTOTYPE_RESULT_NO_CHANGE:
            return "NO_CHANGE";
        default:
            return "Unknown";
    }
}
