/**
  ******************************************************************************
  * @file    sim_script.c
  * @brief   模拟器命令行脚本实现。
  ******************************************************************************
  */

#include "sim_script.h"

#include <stdio.h>
#include <string.h>

#include "sim_display.h"

#define SIM_SCRIPT_MAX_ACTIONS  64
#define SIM_SCRIPT_TAP_MS       80U

typedef enum
{
    SIM_ACTION_SHOT = 0,
    SIM_ACTION_DRAG,
    SIM_ACTION_KEY
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

bool sim_script_parse(int argc, char *argv[], sim_script_options_t *options)
{
    int i;
    bool realtime = false;

    options->hidden = false;
    options->deterministic = false;

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
            if ((sscanf(value, "%u:%n", &at, &consumed) != 1) || (consumed == 0))
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
        else if (strcmp(argv[i], "--key") == 0)
        {
            if (sscanf(value, "%u:%c", &at, &action.key) != 2)
            {
                fprintf(stderr, "bad --key %s\n", value);
                return false;
            }
            action.kind = SIM_ACTION_KEY;
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

bool sim_script_step(uint32_t elapsed_ms, sim_script_key_handler_t on_key)
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

        case SIM_ACTION_KEY:
            on_key(action->key);
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
