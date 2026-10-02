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
 * @brief 把打包源拷入资源区，须在 Service_GUI_Init() 之前调用。
 * @return 资源区或源数据大小不符时为 false。
 */
bool sim_resource_install(void);

#endif /* RESOURCE_SIM_H */
