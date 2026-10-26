/**
  ******************************************************************************
  * @file    resource_sim.h
  * @brief   外部资源区替身。
  ******************************************************************************
  */

#ifndef RESOURCE_SIM_H
#define RESOURCE_SIM_H

#include <stdbool.h>

/**
 * @brief 把打包源与字体 bin 拷入资源区，须在 Service_GUI_Init() 之前调用。
 * @param[in] font_available 为假时不安装字体，Service_Resource_GetFont() 报告不可用
 *            （对应板上仍是资源包 v2 或 FONT 条目损坏）。
 * @return 资源区或源数据大小不符时为 false。
 */
bool sim_resource_install(bool font_available);

#endif /* RESOURCE_SIM_H */
