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
  *            --key   T:c                   T 时触发按键 c（同窗口键盘）
  *            --out-dir DIR                 截图目录，默认当前目录
  *            --hidden                      不显示窗口
  *            --realtime                    脚本模式下仍用墙钟（默认虚拟时钟）
  *          给出任一脚本动作时，默认使用确定性虚拟时钟，最后一个动作完成后自动退出。
  ******************************************************************************
  */

#ifndef SIM_SCRIPT_H
#define SIM_SCRIPT_H

#include <stdbool.h>
#include <stdint.h>

typedef void (*sim_script_key_handler_t)(char key);

typedef struct
{
    bool hidden;          /**< 不显示窗口。 */
    bool deterministic;   /**< 使用虚拟时钟。 */
} sim_script_options_t;

/** @return 参数无法解析时为 false，并已向 stderr 打印原因。 */
bool sim_script_parse(int argc, char *argv[], sim_script_options_t *options);

/**
 * @brief 推进脚本；有脚本触点时覆盖鼠标。
 * @return 所有动作已完成、应退出时为 true。
 */
bool sim_script_step(uint32_t elapsed_ms, sim_script_key_handler_t on_key);

#endif /* SIM_SCRIPT_H */
