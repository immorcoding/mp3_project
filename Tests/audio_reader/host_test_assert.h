/**
 ******************************************************************************
 * @file    host_test_assert.h
 * @brief   主机测试的断言：失败时打印位置并以非零码退出，不走 abort()。
 *
 * @details
 *          标准库的 assert 失败会调用 abort()，Windows 上的运行库会弹出 “Debug Error! abort() has been called”
 *          模态对话框，进程一直挂着等人点，CTest 也被卡住（红灯的时候最容易遇到）。本头把 assert 换成
 *          “打印文件、行号和条件，然后 exit(1)”：CTest 看到非零退出码就判失败，不弹窗，不依赖平台函数。
 *          只给主机测试用，不碰产品代码；用法与标准 assert 相同，测试用 `#include "host_test_assert.h"`
 *          代替 `#include <assert.h>`。
 ******************************************************************************
 */

#ifndef HOST_TEST_ASSERT_H
#define HOST_TEST_ASSERT_H

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

/**
 * @brief 断言失败：打印位置和条件文本，刷新输出，以退出码 1 结束（不调用 abort()）。
 * @param[in] condition 失败的条件文本。
 * @param[in] file 源文件名。
 * @param[in] line 行号。
 */
static inline void host_test_assert_fail(const char *condition, const char *file, int line)
{
    (void)fprintf(stderr, "%s:%d: assertion failed: %s\n", file, line, condition);
    (void)fflush(stderr);
    exit(1);
}

#undef assert
#define assert(condition) ((condition) ? (void)0 : host_test_assert_fail(#condition, __FILE__, __LINE__))

#endif /* HOST_TEST_ASSERT_H */
