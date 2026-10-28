/**
 * @file filesystem_text.c
 * @brief 复用 FatFs 的 CP936 表把 GBK 双字节码转 Unicode，FatFs 类型不出本 Module。
 */

#include "Service/filesystem/filesystem_text.h"

#include "Middlewares/Third_Party/FatFs/src/ff.h"

/**
 * @brief 把 GBK 双字节码转为 Unicode 码点。
 * @param[in] gbk_code 首字节在高 8 位的 GBK 双字节码。
 * @return Unicode 码点；0 表示无映射。
 * @note  内部调用 FatFs `ff_convert(code, 1)`，只读 CP936 表；表由 Resource Service 在任务启动前就绪，
 *        运行期不修改，故可在任意任务上下文调用，不阻塞、不访问存储介质。
 */
uint16_t Service_Filesystem_GbkToUnicode(uint16_t gbk_code)
{
    return (uint16_t)ff_convert((WCHAR)gbk_code, 1U);
}
