/**
  ******************************************************************************
  * @file    sim_main.c
  * @brief   GUI 模拟器入口：SDL 事件泵 + 模拟 GUI Task 主循环。
  *
  * @details
  *          循环结构与 APP/tasks/gui/gui_task.c 一致：ConsumeInput -> 遍历真实的
  *          APP/apps/app_table.c 调用各应用的 gui_step -> Service_GUI_Process()；启动时同样先调各应用的
  *          boot_init（格子等内存准备），再在 Service_GUI_Init() 之后调 gui_init。
  *          应用是真实的 APP 音乐分区、真实的 ui_cell 与格子定义；只有音乐前台流程由
  *          fakes/music_sim.c 代替（注册表里的 foreground_step 与 boot_init），播放合同由 fakes/playback_sim.c 代替，
  *          两者每圈先于各应用的 gui_step 推进一次：前台替身 -> 假播放 -> GUI step。
  *
  *          键盘：T 切换 Default/Solid 主题；C 拔卡/重新挂载（代次 +1，拔卡时假
  *          后端按拔卡停止，当圈发代次 0 的空窗）；G 模拟重扫（代次 +1，不重发窗口，
  *          已显示窗口保留到下一次要窗）；E 重扫得到空库/恢复；S 挂起/恢复假后端的命令处理；B 让假后端之后一次开启为坏
  *          文件；R 让假后端以下一个非 NONE 停止原因停止；A 打印 LVGL 运行中动画数
  *          （核对唱盘与状态行闪烁不叠加）；L 淡出回到 Lock（核对 Lock 呼吸随屏幕
  *          重启）；Esc 退出。脚本参数见 sim_script.h；曲名与窗口应答的开关（某首名字取不到、
  *          应答延迟若干圈）只有脚本参数 --name-fail / --name-delay / --window-delay。
  *          模拟器测不到“新需求覆盖旧需求”（假后端保持一次答一个的节奏），那由 Tests/music_foreground 和上板负责。
  ******************************************************************************
  */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include <SDL.h>

#include "lvgl.h"

#include "APP/apps/app_table.h"
#include "Service/gui/gui_service.h"
#include "Service/gui/view/gui_service_view.h"
#include "fakes/playback_sim.h"
#include "fakes/resource_sim.h"
#include "fakes/music_sim.h"
#include "sim_clock.h"
#include "sim_display.h"
#include "sim_script.h"

/** @brief 与产品 GUI_NOTIFY_LCD_TRANSFER 对应的通知槽。 */
#define SIM_GUI_NOTIFY_LCD_TRANSFER  0U

static uint8_t sim_theme = SERVICE_GUI_THEME_STARTUP;

/**
 * @brief 处理一个按键（交互键盘与脚本 --key 共用）。
 * @param[in] key ASCII 键值，大小写等价。
 */
static void sim_handle_key(char key)
{
    if ((key == 'c') || (key == 'C'))
    {
        /* 拔卡/重新挂载：假前台当圈发代次 0 的空窗，真实音乐分区当圈 CLEAR，重新挂载后隔一圈按最新需求发窗；
           拔卡时假后端同时按 K5 停止，三态、唱盘与时间由快照驱动。 */
        if (!music_sim_toggle_mount())
        {
            playback_sim_card_removed();
        }
        return;
    }
    if ((key == 's') || (key == 'S'))
    {
        playback_sim_toggle_suspend();
        return;
    }
    if ((key == 'g') || (key == 'G'))
    {
        music_sim_rescan();
        return;
    }
    if ((key == 'e') || (key == 'E'))
    {
        /* 空库：模拟重扫得到 0 首，代次 +1；再按一次恢复固定曲库。 */
        music_sim_toggle_empty();
        return;
    }
    if ((key == 'b') || (key == 'B'))
    {
        playback_sim_mark_bad();
        return;
    }
    if ((key == 'r') || (key == 'R'))
    {
        playback_sim_stop_next_reason();
        return;
    }
    if ((key == 'a') || (key == 'A'))
    {
        printf("[anim] running %u\n", (unsigned)lv_anim_count_running());
        return;
    }
    if ((key == 'l') || (key == 'L'))
    {
        /* 回到 Lock：产品暂无重新锁屏入口，模拟器用它验证 Lock 动画随屏幕重启。 */
        lv_scr_load_anim(service_gui_view_get()->lock, LV_SCR_LOAD_ANIM_FADE_OUT, 300U, 0U, false);
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
 * @brief 处理脚本的替身开关：曲名与窗口应答转给假音乐前台流程，跳转回显延迟转给假播放后端。
 * @param[in] control 开关种类。
 * @param[in] value   --name-fail 为播放列表位置，--name-delay / --window-delay 为额外等待圈数，
 *                    --seek-echo-delay 为毫秒数。
 */
static void sim_handle_slot(sim_script_slot_control_t control, unsigned value)
{
    switch (control)
    {
        case SIM_SCRIPT_NAME_FAIL:
            music_sim_toggle_name_fail((uint16_t)value);
            break;

        case SIM_SCRIPT_NAME_DELAY:
            music_sim_set_name_delay((uint32_t)value);
            break;

        case SIM_SCRIPT_WINDOW_DELAY:
            music_sim_set_window_delay((uint32_t)value);
            break;

        case SIM_SCRIPT_SEEK_ECHO_DELAY:
            playback_sim_set_seek_echo_delay((uint32_t)value);
            break;

        default:
            break;
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

/**
 * @brief 模拟器入口：解析脚本参数，初始化显示与 GUI，运行主循环直到退出。
 * @param[in] argc 参数个数。
 * @param[in] argv 参数数组，格式见 sim_script.h。
 * @retval 0 正常退出。
 * @retval 1 资源、显示或 GUI 初始化失败。
 * @retval 2 脚本参数无法解析。
 */
int main(int argc, char *argv[])
{
    Service_StatusTypeDef status;
    sim_script_options_t options;
    const sim_script_handlers_t handlers = {sim_handle_key, sim_handle_slot};
    uint32_t start_ms;

    setvbuf(stdout, NULL, _IONBF, 0);

    if (!sim_script_parse(argc, argv, &options))
    {
        return 2;
    }
    sim_clock_set_deterministic(options.deterministic);

    if (!sim_resource_install(!options.font_unavailable))
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

    app_table_boot_init();
    playback_sim_init();
    for (size_t i = 0U; i < app_table_count; i++)
    {
        if (app_table[i].gui_init != NULL)
        {
            app_table[i].gui_init();
        }
    }
    printf("[sim] running. T: toggle theme, C: card remove/mount, G: rescan, "
           "S: suspend/resume playback, B: next open bad, R: stop with next reason, "
           "A: anim count, L: back to Lock, Esc: quit\n");
    start_ms = sim_clock_now();

    while (sim_pump_events())
    {
        Service_GUI_InputTypeDef input;

        if (sim_script_step(sim_clock_now() - start_ms, &handlers))
        {
            break;
        }

        input.command = SERVICE_GUI_INPUT_NONE;
        input.param = 0U;
        (void)Service_GUI_ConsumeInput(&input);

        for (size_t i = 0U; i < app_table_count; i++)
        {
            if (app_table[i].foreground_step != NULL)
            {
                (void)app_table[i].foreground_step();
            }
        }
        playback_sim_step(sim_clock_now());
        for (size_t i = 0U; i < app_table_count; i++)
        {
            if (app_table[i].gui_step != NULL)
            {
                app_table[i].gui_step(&input);
            }
        }

        /* 返回值是板上 GUI Task 每圈该睡的毫秒数；模拟器每圈固定推进 SIM_CLOCK_STEP_MS，忽略它。 */
        (void)Service_GUI_Process();

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
