/**
  ******************************************************************************
  * @file    sim_script.h
  * @brief   模拟器命令行：定时截图、点按、拖动与按键，便于无人值守验证。
  *
  * @details
  *          时间均为自 Service_GUI_Init() 完成起的毫秒数，坐标为 LCD 像素。
  *            --shot  T:name                T 时保存 <out-dir>/name.bmp 并打印帧哈希
  *            --tap   T:x,y                 T 时按下，80 ms 后松开
  *            --drag  T:x0,y0:x1,y1:D       T 时按下，D ms 内线性移动后松开
  *            --anims T:name                T 时打印 "[anims] name N"（LVGL 运行中动画数）
  *            --key   T:c                   T 时触发按键 c（同窗口键盘）
  *            --name-fail  T:N              T 时切换假 Storage 第 N 首（播放列表位置）的名字是否取不到
  *            --name-delay T:K              T 起假 Storage 名称槽应答多等 K 圈（0 恢复为下一圈应答）
  *            --window-delay T:K            T 起假 Storage 窗口槽应答多等 K 圈（0 恢复为下一圈应答）
  *            --out-dir DIR                 截图目录，默认当前目录
  *            --hidden                      不显示窗口
  *            --realtime                    脚本模式下仍用墙钟（默认虚拟时钟）
  *            --font-unavailable            启动时不安装字体资源，曲名回退 Montserrat 12
  *          给出任一脚本动作时，默认使用确定性虚拟时钟，最后一个动作完成后自动退出。
  ******************************************************************************
  */

#ifndef SIM_SCRIPT_H
#define SIM_SCRIPT_H

#include <stdbool.h>
#include <stdint.h>

typedef void (*sim_script_key_handler_t)(char key);

/** @brief 假 Storage 名称槽与窗口槽的脚本开关。 */
typedef enum
{
    SIM_SCRIPT_NAME_FAIL = 0, /**< 切换某首名字取不到；值为播放列表位置。 */
    SIM_SCRIPT_NAME_DELAY,    /**< 设置名称槽应答额外等待的圈数。 */
    SIM_SCRIPT_WINDOW_DELAY   /**< 设置窗口槽应答额外等待的圈数。 */
} sim_script_slot_control_t;

typedef void (*sim_script_slot_handler_t)(sim_script_slot_control_t control, unsigned value);

/** @brief 脚本动作的处理函数（交互键盘与名称槽开关）。 */
typedef struct
{
    sim_script_key_handler_t on_key;   /**< --key。 */
    sim_script_slot_handler_t on_slot; /**< --name-fail / --name-delay / --window-delay。 */
} sim_script_handlers_t;

typedef struct
{
    bool hidden;          /**< 不显示窗口。 */
    bool deterministic;   /**< 使用虚拟时钟。 */
    bool font_unavailable;/**< 不安装字体资源（对应板上资源包 v2 或 FONT 损坏）。 */
} sim_script_options_t;

/** @return 参数无法解析时为 false，并已向 stderr 打印原因。 */
bool sim_script_parse(int argc, char *argv[], sim_script_options_t *options);

/**
 * @brief 推进脚本；有脚本触点时覆盖鼠标。
 * @return 所有动作已完成、应退出时为 true。
 */
bool sim_script_step(uint32_t elapsed_ms, const sim_script_handlers_t *handlers);

#endif /* SIM_SCRIPT_H */
