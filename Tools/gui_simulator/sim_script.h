/**
  ******************************************************************************
  * @file    sim_script.h
  * @brief   模拟器命令行脚本：定时截图、点按、拖动与按键，便于无人值守验证。
  *
  * @details
  *          时间均为自 Service_GUI_Init() 完成起的毫秒数，坐标为 LCD 像素。
  *            --shot  T:file.bmp            T 时保存帧缓冲
  *            --tap   T:x,y                 T 时按下，80 ms 后松开
  *            --drag  T:x0,y0:x1,y1:D       T 时按下，D ms 内线性移动后松开
  *            --key   T:c                   T 时触发按键 c（同窗口键盘）
  *          给出任一脚本动作时，最后一个动作完成后程序自动退出。
  ******************************************************************************
  */

#ifndef SIM_SCRIPT_H
#define SIM_SCRIPT_H

#include <stdbool.h>
#include <stdint.h>

typedef void (*sim_script_key_handler_t)(char key);

/** @return 参数无法解析时为 false，并已向 stderr 打印原因。 */
bool sim_script_parse(int argc, char *argv[]);

/**
 * @brief 推进脚本；有脚本触点时覆盖鼠标。
 * @return 所有动作已完成、应退出时为 true。
 */
bool sim_script_step(uint32_t elapsed_ms, sim_script_key_handler_t on_key);

#endif /* SIM_SCRIPT_H */
