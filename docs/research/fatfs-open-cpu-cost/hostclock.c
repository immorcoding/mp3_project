/* 主机计时（仅此文件包含 windows.h）。 */
#include <windows.h>
#include "hostclock.h"

/**
 * @brief 单调时钟
 * @return 微秒
 */
double hostclock_us(void)
{
    static LARGE_INTEGER f;
    LARGE_INTEGER c;
    if (!f.QuadPart) QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart * 1e6 / (double)f.QuadPart;
}

/**
 * @brief 固定核心并提高优先级
 */
void hostclock_pin(void)
{
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    SetThreadAffinityMask(GetCurrentThread(), 1u << 2);
}
