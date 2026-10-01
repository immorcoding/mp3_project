/**
  ******************************************************************************
  * @file    sim_lv_conf.h
  * @brief   模拟器 LVGL 配置：包装产品 lv_conf.h，只覆盖 host 不可用的选项。
  *
  * @details
  *          产品配置仍是唯一事实源；此处只关闭 STM32 DMA2D 后端、去掉指向
  *          固件链接脚本的段属性，并关闭性能/内存浮层。其余参数（颜色深度、
  *          内存池、字体等）与板上一致。
  ******************************************************************************
  */

#ifndef SIM_LV_CONF_H
#define SIM_LV_CONF_H

#include "Middlewares/Third_Party/LVGL/lv_conf.h"

#undef LV_USE_GPU_STM32_DMA2D
#define LV_USE_GPU_STM32_DMA2D 0

#undef LV_ATTRIBUTE_LARGE_RAM_ARRAY
#define LV_ATTRIBUTE_LARGE_RAM_ARRAY

/* PC 上的 FPS/CPU 与内存占用不代表板上表现，且会让基线截图随实现细节漂移。 */
#undef LV_USE_PERF_MONITOR
#define LV_USE_PERF_MONITOR 0
#undef LV_USE_MEM_MONITOR
#define LV_USE_MEM_MONITOR 0

#endif /* SIM_LV_CONF_H */
