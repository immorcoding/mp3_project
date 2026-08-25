/*
 * 可删除终端原型。
 * 问题：多张 Settings 卡片是否能共享一次全屏模糊计算，同时保持卡片外壁纸清晰？
 */

#include <stdio.h>

#include "backdrop_model.h"

#define PROTOTYPE_FULL_ALPHA_FRAME_BYTES (240U * 320U * 3U)

static void prototype_render_state(const Prototype_BackdropStateTypeDef *state)
{
    printf("\033[2J\033[H");
    printf("\033[1mSettings backdrop prototype\033[0m\n\n");
    printf("  Active screen:              %s\n",
           Prototype_BackdropScreenName(state->ActiveScreen));
    printf("  Wallpaper version:          %lu\n",
           (unsigned long)state->WallpaperVersion);
    printf("  Effect workspace busy:      %s\n",
           state->EffectWorkspaceBusy ? "yes" : "no");
    printf("  Full blur generations:      %lu\n",
           (unsigned long)state->BlurGeneration);
    printf("  Settings composite valid:   %s\n",
           state->SettingsCompositeValid ? "yes" : "no");
    printf("  Composite generations:      %lu\n",
           (unsigned long)state->CompositeGeneration);
    printf("  Visible Settings cards:     %u\n\n",
           (unsigned int)state->SettingsCardCount);

    printf("  SDRAM peak for Alpha prototype:\n");
    printf("    effect workspace:         %lu B\n",
           (unsigned long)PROTOTYPE_FULL_ALPHA_FRAME_BYTES);
    printf("    settings composite:       %lu B\n",
           (unsigned long)PROTOTYPE_FULL_ALPHA_FRAME_BYTES);
    printf("    total:                    %lu B\n\n",
           (unsigned long)(2U * PROTOTYPE_FULL_ALPHA_FRAME_BYTES));

    printf("\033[1m[b]\033[0m Boot  \033[1m[l]\033[0m Lock  ");
    printf("\033[1m[s]\033[0m Settings  \033[1m[w]\033[0m Change wallpaper\n");
    printf("\033[1m[r]\033[0m Recompose cards  ");
    printf("\033[1m[x]\033[0m Request other effect  \033[1m[q]\033[0m Quit\n");
}

static Prototype_ActionTypeDef prototype_action_from_key(int key, int *quit)
{
    *quit = 0;

    switch (key)
    {
        case 'b':
            return PROTOTYPE_ACTION_ENTER_BOOT;
        case 'l':
            return PROTOTYPE_ACTION_ENTER_LOCK;
        case 's':
            return PROTOTYPE_ACTION_ENTER_SETTINGS;
        case 'w':
            return PROTOTYPE_ACTION_CHANGE_WALLPAPER;
        case 'r':
            return PROTOTYPE_ACTION_RECOMPOSE_CARDS;
        case 'x':
            return PROTOTYPE_ACTION_REQUEST_OTHER_EFFECT;
        case 'q':
            *quit = 1;
            return PROTOTYPE_ACTION_ENTER_LOCK;
        default:
            return PROTOTYPE_ACTION_RECOMPOSE_CARDS;
    }
}

int main(void)
{
    Prototype_BackdropStateTypeDef state;
    Prototype_ResultTypeDef result;
    int key;
    int quit;

    Prototype_BackdropStateInit(&state);

    while (1)
    {
        prototype_render_state(&state);
        key = getchar();

        if (key == '\n' || key == '\r')
        {
            continue;
        }

        result = Prototype_BackdropApply(
            &state,
            prototype_action_from_key(key, &quit));

        if (quit != 0)
        {
            break;
        }

        printf("\nAction result: %s\n", Prototype_BackdropResultName(result));
        (void)getchar();
    }

    return 0;
}
