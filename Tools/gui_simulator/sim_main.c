/**
  ******************************************************************************
  * @file    sim_main.c
  * @brief   GUI 模拟器入口：SDL 事件泵 + 模拟 GUI Task 主循环。
  *
  * @details
  *          循环结构与 APP/tasks/gui/gui_task.c 一致：ConsumeInput -> 产品分区 step
  *          -> Service_GUI_Process()。真实 gui_music 依赖 Storage，这里用一个最小
  *          演示分区代替：固定曲目表、播放/暂停、上一首/下一首、假进度、选曲与
  *          唱盘旋转；C 模拟拔卡清空播放内容（清空期间命令全部忽略，再按 C 恢复）。
  *
  *          键盘：T 切换 Default/Solid 主题；C 清空播放内容；A 打印 LVGL 运行中
  *          动画数（核对唱盘动画不叠加）；Esc 退出。脚本参数见 sim_script.h。
  ******************************************************************************
  */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include <SDL.h>

#include "lvgl.h"

#include "Service/gui/gui_service.h"
#include "Service/gui/main/queue/gui_service_main_queue_config.h"
#include "fakes/resource_sim.h"
#include "sim_clock.h"
#include "sim_display.h"
#include "sim_script.h"

/** @brief 与产品 GUI_NOTIFY_LCD_TRANSFER 对应的通知槽。 */
#define SIM_GUI_NOTIFY_LCD_TRANSFER  0U

/** @brief 演示分区假进度每走 1% 的毫秒数。 */
#define SIM_DEMO_PROGRESS_STEP_MS    300U

static const char *const sim_demo_titles[] = {
    "Clair de Lune",
    "Gymnopedie No.1",
    "River Flows in You",
    "Nuvole Bianche",
    "Comptine d'un autre ete",
    "Experience",
    "Merry Christmas Mr. Lawrence",
    "Kiss the Rain",
    "Una Mattina",
    "Spring Waltz",
};

#define SIM_DEMO_TITLE_COUNT \
    ((uint16_t)(sizeof(sim_demo_titles) / sizeof(sim_demo_titles[0])))

_Static_assert(SIM_DEMO_TITLE_COUNT <= SERVICE_GUI_MAIN_QUEUE_MAX_ROWS,
               "demo playlist must fit in one Queue window");

static uint16_t sim_demo_current;
static bool sim_demo_playing;
static bool sim_demo_cleared;
static uint8_t sim_demo_progress;
static uint32_t sim_demo_last_progress_ms;
static uint8_t sim_theme = SERVICE_GUI_THEME_STARTUP;

static void sim_demo_apply_queue(void)
{
    (void)Service_GUI_QueueApply((const char **)sim_demo_titles,
                                 SIM_DEMO_TITLE_COUNT,
                                 0U,
                                 sim_demo_current);
}

/* 切歌：进度与唱盘角度归零，唱盘按当前 playing 决定是否续转。 */
static void sim_demo_select(uint16_t index)
{
    sim_demo_current = index;
    sim_demo_cleared = false;
    sim_demo_progress = 0U;
    (void)Service_GUI_ProgressApply(sim_demo_progress);
    sim_demo_apply_queue();
    (void)Service_GUI_VinylApply(sim_demo_playing, true);
    printf("[demo] track %u: %s\n", (unsigned)index, sim_demo_titles[index]);
}

/* 模拟拔卡 CLEAR：空窗、强制 paused、进度与唱盘角度归零并停转。 */
static void sim_demo_clear(void)
{
    sim_demo_cleared = true;
    sim_demo_playing = false;
    sim_demo_progress = 0U;
    (void)Service_GUI_QueueApply(NULL, 0U, 0U, SERVICE_GUI_QUEUE_NO_CURRENT);
    (void)Service_GUI_TransportApply(sim_demo_playing);
    (void)Service_GUI_ProgressApply(sim_demo_progress);
    (void)Service_GUI_VinylApply(false, true);
    printf("[demo] clear\n");
}

static void sim_demo_init(void)
{
    sim_demo_current = 0U;
    sim_demo_playing = false;
    sim_demo_cleared = false;
    sim_demo_progress = 0U;
    sim_demo_last_progress_ms = sim_clock_now();
    sim_demo_apply_queue();
    (void)Service_GUI_TransportApply(sim_demo_playing);
    (void)Service_GUI_ProgressApply(sim_demo_progress);
    (void)Service_GUI_VinylApply(false, true);
}

static void sim_demo_step(const Service_GUI_InputTypeDef *input)
{
    const uint32_t now = sim_clock_now();

    /* 与 gui_music 一致：清空后无游标，选曲/上一首/下一首/播放都不生效，再按 C 恢复。 */
    if (sim_demo_cleared && (input->command != SERVICE_GUI_INPUT_NONE))
    {
        return;
    }

    switch (input->command)
    {
    case SERVICE_GUI_INPUT_MUSIC_QUEUE_SELECT:
        if (input->param < SIM_DEMO_TITLE_COUNT)
        {
            sim_demo_select(input->param);
        }
        break;

    case SERVICE_GUI_INPUT_MUSIC_PREVIOUS:
        sim_demo_select((uint16_t)((sim_demo_current + SIM_DEMO_TITLE_COUNT - 1U) %
                                   SIM_DEMO_TITLE_COUNT));
        break;

    case SERVICE_GUI_INPUT_MUSIC_NEXT:
        sim_demo_select((uint16_t)((sim_demo_current + 1U) % SIM_DEMO_TITLE_COUNT));
        break;

    case SERVICE_GUI_INPUT_MUSIC_PLAY_PAUSE:
        sim_demo_playing = !sim_demo_playing;
        sim_demo_last_progress_ms = now;
        (void)Service_GUI_TransportApply(sim_demo_playing);
        (void)Service_GUI_VinylApply(sim_demo_playing, false);
        printf("[demo] %s\n", sim_demo_playing ? "play" : "pause");
        break;

    case SERVICE_GUI_INPUT_MUSIC_SEEK:
        sim_demo_progress = (uint8_t)input->param;
        sim_demo_last_progress_ms = now;
        printf("[demo] seek %u%%\n", (unsigned)input->param);
        break;

    case SERVICE_GUI_INPUT_NONE:
    default:
        break;
    }

    if (sim_demo_playing &&
        ((now - sim_demo_last_progress_ms) >= SIM_DEMO_PROGRESS_STEP_MS))
    {
        sim_demo_last_progress_ms = now;
        if (sim_demo_progress >= 100U)
        {
            sim_demo_select((uint16_t)((sim_demo_current + 1U) % SIM_DEMO_TITLE_COUNT));
        }
        else
        {
            sim_demo_progress++;
            (void)Service_GUI_ProgressApply(sim_demo_progress);
        }
    }
}

static void sim_handle_key(char key)
{
    if ((key == 'c') || (key == 'C'))
    {
        /* 再按一次模拟重新挂载：回到第 0 首、paused，唱盘保持归零。 */
        if (sim_demo_cleared)
        {
            sim_demo_select(0U);
            printf("[demo] remount\n");
        }
        else
        {
            sim_demo_clear();
        }
        return;
    }
    if ((key == 'a') || (key == 'A'))
    {
        printf("[anim] running %u\n", (unsigned)lv_anim_count_running());
        return;
    }
    if ((key == 't') || (key == 'T'))
    {
        const uint8_t next = (sim_theme == SERVICE_GUI_THEME_DEFAULT) ?
                                 SERVICE_GUI_THEME_SOLID : SERVICE_GUI_THEME_DEFAULT;
        const Service_StatusTypeDef status = Service_GUI_ThemeApply(next);

        if (status == SERVICE_OK)
        {
            sim_theme = next;
        }
        printf("[sim] theme -> %s (status %d)\n",
               (next == SERVICE_GUI_THEME_DEFAULT) ? "default" : "solid",
               (int)status);
    }
}

/**
 * @return false 表示用户请求退出。
 */
static bool sim_pump_events(void)
{
    SDL_Event event;
    static bool left_down;

    while (SDL_PollEvent(&event))
    {
        switch (event.type)
        {
        case SDL_QUIT:
            return false;

        case SDL_KEYDOWN:
            if (event.key.keysym.sym == SDLK_ESCAPE)
            {
                return false;
            }
            if ((event.key.keysym.sym >= 0) && (event.key.keysym.sym < 128))
            {
                sim_handle_key((char)event.key.keysym.sym);
            }
            break;

        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
            if (event.button.button == SDL_BUTTON_LEFT)
            {
                left_down = (event.type == SDL_MOUSEBUTTONDOWN);
                sim_display_set_pointer(left_down, event.button.x, event.button.y);
            }
            break;

        case SDL_MOUSEMOTION:
            sim_display_set_pointer(left_down, event.motion.x, event.motion.y);
            break;

        default:
            break;
        }
    }

    return true;
}

int main(int argc, char *argv[])
{
    Service_StatusTypeDef status;
    sim_script_options_t options;
    uint32_t start_ms;

    setvbuf(stdout, NULL, _IONBF, 0);

    if (!sim_script_parse(argc, argv, &options))
    {
        return 2;
    }
    sim_clock_set_deterministic(options.deterministic);

    if (!sim_resource_install())
    {
        fprintf(stderr, "resource install failed\n");
        return 1;
    }

    if (!sim_display_init(options.hidden))
    {
        return 1;
    }

    status = Service_GUI_Init(SIM_GUI_NOTIFY_LCD_TRANSFER);
    if (status != SERVICE_OK)
    {
        fprintf(stderr, "Service_GUI_Init failed: %d\n", (int)status);
        sim_display_deinit();
        return 1;
    }

    sim_demo_init();
    printf("[sim] running. T: toggle theme, C: clear, A: anim count, Esc: quit\n");
    start_ms = sim_clock_now();

    while (sim_pump_events())
    {
        Service_GUI_InputTypeDef input;

        if (sim_script_step(sim_clock_now() - start_ms, sim_handle_key))
        {
            break;
        }

        input.command = SERVICE_GUI_INPUT_NONE;
        input.param = 0U;
        (void)Service_GUI_ConsumeInput(&input);

        sim_demo_step(&input);

        Service_GUI_Process();

        if (!options.hidden)
        {
            sim_display_present();
        }

        /* 虚拟时钟下不等墙钟，场景以最快速度跑完且结果可复现。 */
        if (sim_clock_is_deterministic())
        {
            sim_clock_tick();
        }
        else
        {
            SDL_Delay(SIM_CLOCK_STEP_MS);
        }
    }

    sim_display_deinit();
    return 0;
}
