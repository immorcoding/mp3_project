/**
  ******************************************************************************
  * @file    sim_script.c
  * @brief   模拟器命令行脚本实现。
  ******************************************************************************
  */

#include "sim_script.h"

#include <stdio.h>
#include <string.h>

#include "lvgl.h"

#include "sim_display.h"

#define SIM_SCRIPT_MAX_ACTIONS  64
#define SIM_SCRIPT_TAP_MS       80U

typedef enum
{
    SIM_ACTION_SHOT = 0,
    SIM_ACTION_DRAG,
    SIM_ACTION_KEY,
    SIM_ACTION_ANIMS,
    SIM_ACTION_SLOT
} sim_action_kind_t;

typedef struct
{
    sim_action_kind_t kind;
    uint32_t at_ms;
    uint32_t duration_ms;
    int x0;
    int y0;
    int x1;
    int y1;
    char key;
    sim_script_slot_control_t slot_control;
    unsigned slot_value;
    const char *path;
    bool started;
    bool done;
} sim_action_t;

static const char *sim_out_dir = ".";
static sim_action_t sim_actions[SIM_SCRIPT_MAX_ACTIONS];
static int sim_action_count;

static bool sim_script_add(const sim_action_t *action)
{
    if (sim_action_count >= SIM_SCRIPT_MAX_ACTIONS)
    {
        fprintf(stderr, "too many script actions\n");
        return false;
    }

    sim_actions[sim_action_count++] = *action;
    return true;
}

/** @brief 假 Storage 槽位开关的命令行名与对应开关。 */
static const struct
{
    const char *option;
    sim_script_slot_control_t control;
} sim_script_slot_options[] = {
    {"--name-fail", SIM_SCRIPT_NAME_FAIL},
    {"--name-delay", SIM_SCRIPT_NAME_DELAY},
    {"--window-delay", SIM_SCRIPT_WINDOW_DELAY},
};

/**
 * @brief 按命令行名查假 Storage 槽位开关。
 * @param[in]  option  命令行参数名，如 `--window-delay`。
 * @param[out] control 查到时写入对应开关。
 * @return 是槽位开关时为 true。
 */
static bool sim_script_find_slot_option(const char *option, sim_script_slot_control_t *control)
{
    size_t i;

    for (i = 0U; i < sizeof(sim_script_slot_options) / sizeof(sim_script_slot_options[0]); i++)
    {
        if (strcmp(option, sim_script_slot_options[i].option) == 0)
        {
            *control = sim_script_slot_options[i].control;
            return true;
        }
    }

    return false;
}

/**
 * @brief 解析命令行脚本参数，登记定时动作。
 * @param[in]  argc    参数个数。
 * @param[in]  argv    参数数组。
 * @param[out] options 解析出的窗口与时钟选项。
 * @retval true  解析成功。
 * @retval false 参数无法解析，已向 stderr 打印原因。
 */
bool sim_script_parse(int argc, char *argv[], sim_script_options_t *options)
{
    int i;
    bool realtime = false;

    options->hidden = false;
    options->deterministic = false;
    options->font_unavailable = false;

    for (i = 1; i < argc; i++)
    {
        sim_action_t action;
        const char *value;
        unsigned at;
        int consumed = 0;

        memset(&action, 0, sizeof(action));

        if (strcmp(argv[i], "--hidden") == 0)
        {
            options->hidden = true;
            continue;
        }
        if (strcmp(argv[i], "--font-unavailable") == 0)
        {
            options->font_unavailable = true;
            continue;
        }
        if (strcmp(argv[i], "--realtime") == 0)
        {
            realtime = true;
            continue;
        }

        if (i + 1 >= argc)
        {
            fprintf(stderr, "missing value for %s\n", argv[i]);
            return false;
        }
        value = argv[i + 1];

        if (strcmp(argv[i], "--out-dir") == 0)
        {
            sim_out_dir = value;
            i++;
            continue;
        }

        if (strcmp(argv[i], "--shot") == 0)
        {
            if ((sscanf(value, "%u:%n", &at, &consumed) != 1) || (consumed == 0) || (value[consumed] == '\0'))
            {
                fprintf(stderr, "bad --shot %s\n", value);
                return false;
            }
            action.kind = SIM_ACTION_SHOT;
            action.path = value + consumed;
        }
        else if (strcmp(argv[i], "--tap") == 0)
        {
            if (sscanf(value, "%u:%d,%d", &at, &action.x0, &action.y0) != 3)
            {
                fprintf(stderr, "bad --tap %s\n", value);
                return false;
            }
            action.kind = SIM_ACTION_DRAG;
            action.x1 = action.x0;
            action.y1 = action.y0;
            action.duration_ms = SIM_SCRIPT_TAP_MS;
        }
        else if (strcmp(argv[i], "--drag") == 0)
        {
            unsigned duration;

            if (sscanf(value, "%u:%d,%d:%d,%d:%u", &at,
                       &action.x0, &action.y0, &action.x1, &action.y1,
                       &duration) != 6)
            {
                fprintf(stderr, "bad --drag %s\n", value);
                return false;
            }
            action.kind = SIM_ACTION_DRAG;
            action.duration_ms = (duration == 0U) ? 1U : duration;
        }
        else if (strcmp(argv[i], "--anims") == 0)
        {
            if ((sscanf(value, "%u:%n", &at, &consumed) != 1) || (consumed == 0) || (value[consumed] == '\0'))
            {
                fprintf(stderr, "bad --anims %s\n", value);
                return false;
            }
            action.kind = SIM_ACTION_ANIMS;
            action.path = value + consumed;
        }
        else if (strcmp(argv[i], "--key") == 0)
        {
            if (sscanf(value, "%u:%c", &at, &action.key) != 2)
            {
                fprintf(stderr, "bad --key %s\n", value);
                return false;
            }
            action.kind = SIM_ACTION_KEY;
        }
        else if (sim_script_find_slot_option(argv[i], &action.slot_control))
        {
            if (sscanf(value, "%u:%u", &at, &action.slot_value) != 2)
            {
                fprintf(stderr, "bad %s %s\n", argv[i], value);
                return false;
            }
            action.kind = SIM_ACTION_SLOT;
        }
        else
        {
            fprintf(stderr, "unknown option %s\n", argv[i]);
            return false;
        }

        action.at_ms = at;
        if (!sim_script_add(&action))
        {
            return false;
        }
        i++;
    }

    options->deterministic = (sim_action_count > 0) && !realtime;
    return true;
}

/**
 * @brief 推进脚本：执行到期的截图、动画计数、按键、名称槽开关与触点动作。
 * @param[in] elapsed_ms 自 GUI 初始化完成起的毫秒数。
 * @param[in] handlers   按键与名称槽开关的处理函数。
 * @retval true  所有动作已完成，应退出。
 * @retval false 无脚本或仍有未完成动作。
 */
bool sim_script_step(uint32_t elapsed_ms, const sim_script_handlers_t *handlers)
{
    int i;
    bool all_done = true;

    if (sim_action_count == 0)
    {
        return false;
    }

    for (i = 0; i < sim_action_count; i++)
    {
        sim_action_t *action = &sim_actions[i];

        if (action->done)
        {
            continue;
        }

        if (elapsed_ms < action->at_ms)
        {
            all_done = false;
            continue;
        }

        switch (action->kind)
        {
        case SIM_ACTION_SHOT:
        {
            char path[512];

            (void)snprintf(path, sizeof(path), "%s/%s.bmp", sim_out_dir, action->path);
            printf("[shot] %s %08lx%s\n",
                   action->path,
                   (unsigned long)sim_display_hash(),
                   sim_display_save_bmp(path) ? "" : " (save failed)");
            action->done = true;
        }
            break;

        case SIM_ACTION_ANIMS:
            printf("[anims] %s %u\n", action->path, (unsigned)lv_anim_count_running());
            action->done = true;
            break;

        case SIM_ACTION_KEY:
            handlers->on_key(action->key);
            action->done = true;
            break;

        case SIM_ACTION_SLOT:
            handlers->on_slot(action->slot_control, action->slot_value);
            action->done = true;
            break;

        case SIM_ACTION_DRAG:
        default:
        {
            const uint32_t t = elapsed_ms - action->at_ms;

            if (t >= action->duration_ms)
            {
                sim_display_set_pointer_lcd(false, action->x1, action->y1);
                action->done = true;
            }
            else
            {
                const int x = action->x0 +
                              (int)((action->x1 - action->x0) * (int)t / (int)action->duration_ms);
                const int y = action->y0 +
                              (int)((action->y1 - action->y0) * (int)t / (int)action->duration_ms);

                sim_display_set_pointer_lcd(true, x, y);
                action->started = true;
                all_done = false;
            }
            break;
        }
        }
    }

    return all_done;
}
