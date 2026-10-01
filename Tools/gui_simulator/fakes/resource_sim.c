/**
  ******************************************************************************
  * @file    resource_sim.c
  * @brief   外部资源区替身：定义链接脚本资源符号，并模拟 Resource Service 安装。
  *
  * @details
  *          固件中 vinyl 区由链接脚本在 SDRAM 预留 0xF300 字节，启动时
  *          Service/resource 从资源包拷入。模拟器用 asm 定义同名起止符号，
  *          再把 Resources/imgs 的打包源数组拷入，效果等同安装完成。
  ******************************************************************************
  */

#include "fakes/resource_sim.h"

#include <stdint.h>
#include <string.h>

#include "lvgl.h"

/* 打包源本身不含任何 include（资源包工具只取数组），在此提供 LVGL 类型后直接编入。 */
#include "Resources/imgs/vinyl_original_144px.c"

/** @brief 与 stm32h743zgtx_flash.ld 中的 vinyl 区大小一致。 */
#define SIM_RESOURCE_VINYL_SIZE  0xF300

__asm__(
    ".data\n"
    ".balign 32\n"
    ".globl __external_resource_vinyl_start__\n"
    "__external_resource_vinyl_start__:\n"
    ".space 0xF300\n"
    ".globl __external_resource_vinyl_end__\n"
    "__external_resource_vinyl_end__:\n"
    ".text\n");

extern uint8_t __external_resource_vinyl_start__[];
extern uint8_t __external_resource_vinyl_end__[];

bool sim_resource_install(void)
{
    const uint32_t region = (uint32_t)(__external_resource_vinyl_end__ -
                                       __external_resource_vinyl_start__);

    if ((region != SIM_RESOURCE_VINYL_SIZE) ||
        (vinyl_original_144px.data_size != region))
    {
        return false;
    }

    memcpy(__external_resource_vinyl_start__, vinyl_original_144px.data, region);
    return true;
}
