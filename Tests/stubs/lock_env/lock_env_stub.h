/**
 * @file lock_env_stub.h
 * @brief Filesystem 持锁统计在主机上依赖的两个外部接口的替身：Cortex-M7 周期计数器与 Service Log。
 *
 * @details
 *          持锁统计（Debug 构建才有）用周期计数器量时间、用 `Service_Log_Post` 出汇总日志。主机测试
 *          链接 Filesystem Service 时需要这两个符号，本目录分开提供，因为有的测试工程自己定义了
 *          `Service_Log_Post`（只链接周期计数器那一半）：
 *          - `lock_env_cycle_counter.c`：脚本化的时间，只在测试调用 `lock_env_advance_us` 时前进，
 *            永远不依赖真实时钟。
 *          - `lock_env_log.c`：记录被投递的日志行，供测试读取。
 */
#ifndef HOST_STUB_LOCK_ENV_STUB_H
#define HOST_STUB_LOCK_ENV_STUB_H

#include <stdbool.h>
#include <stdint.h>

/** @brief 日志投递钩子：每条 `Service_Log_Post` 进来时先调用一次，用来核对“投递时是否持锁”。 */
typedef void (*LockEnv_PostHook)(const char *tag, const char *text);

/**
 * @brief 周期计数器归零，频率恢复为 480 MHz。
 */
void lock_env_counter_reset(void);

/**
 * @brief 让假周期计数器前进给定微秒（32 位计数器会回绕，与硬件一致）。
 * @param[in] microseconds 前进的微秒数。
 */
void lock_env_advance_us(uint32_t microseconds);

/**
 * @brief 让假周期计数器的频率报告为 0（模拟计数器未启动），用来走 tick 兜底。
 * @param[in] unavailable true 表示频率为 0。
 */
void lock_env_set_counter_unavailable(bool unavailable);

/**
 * @brief 清空日志记录与投递钩子。
 */
void lock_env_log_reset(void);

/**
 * @brief 安装日志投递钩子；传 NULL 取消。
 * @param[in] hook 钩子。
 */
void lock_env_set_post_hook(LockEnv_PostHook hook);

/**
 * @brief 已记录的日志条数（自 reset 起；最多记 64 条）。
 * @return 条数。
 */
uint32_t lock_env_log_count(void);

/**
 * @brief 第 index 条日志的标签。
 * @param[in] index 从 0 起的序号。
 * @return 标签；序号越界为 NULL。
 */
const char *lock_env_log_tag(uint32_t index);

/**
 * @brief 第 index 条日志的正文。
 * @param[in] index 从 0 起的序号。
 * @return 正文；序号越界为 NULL。
 */
const char *lock_env_log_text(uint32_t index);

/**
 * @brief 第 index 条日志的等级（`Service_Log_LevelTypeDef` 的数值）。
 * @param[in] index 从 0 起的序号。
 * @return 等级数值；序号越界为 0。
 */
uint32_t lock_env_log_level(uint32_t index);

/**
 * @brief 最长的一条日志正文的字符数（不含结尾 '\0'）。
 * @return 字符数，没有日志为 0。
 */
uint32_t lock_env_log_longest_text(void);

#endif
