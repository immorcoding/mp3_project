/**
 * @file lock_env_log.c
 * @brief Service Log 的主机替身：记录被投递的日志；行为见 lock_env_stub.h。
 */

#include "lock_env_stub.h"

#include "Service/log/log_service.h"

#include <string.h>

#define LOCK_ENV_LOG_MAX 64U
#define LOCK_ENV_TEXT_MAX 128U
#define LOCK_ENV_TAG_MAX 16U

static LockEnv_PostHook post_hook;
static uint32_t log_count;
static char log_tags[LOCK_ENV_LOG_MAX][LOCK_ENV_TAG_MAX];
static char log_texts[LOCK_ENV_LOG_MAX][LOCK_ENV_TEXT_MAX];
static uint32_t longest_text;

/**
 * @brief 清空日志记录与钩子。
 */
void lock_env_log_reset(void)
{
    post_hook = NULL;
    log_count = 0U;
    longest_text = 0U;
    memset(log_tags, 0, sizeof(log_tags));
    memset(log_texts, 0, sizeof(log_texts));
}

/**
 * @brief 安装投递钩子。
 * @param[in] hook 钩子或 NULL。
 */
void lock_env_set_post_hook(LockEnv_PostHook hook)
{
    post_hook = hook;
}

/**
 * @brief 日志条数。
 * @return 条数。
 */
uint32_t lock_env_log_count(void)
{
    return log_count;
}

/**
 * @brief 第 index 条的标签。
 * @param[in] index 序号。
 * @return 标签或 NULL。
 */
const char *lock_env_log_tag(uint32_t index)
{
    return (index < log_count) ? log_tags[index] : NULL;
}

/**
 * @brief 第 index 条的正文。
 * @param[in] index 序号。
 * @return 正文或 NULL。
 */
const char *lock_env_log_text(uint32_t index)
{
    return (index < log_count) ? log_texts[index] : NULL;
}

/**
 * @brief 最长一条正文的字符数。
 * @return 字符数。
 */
uint32_t lock_env_log_longest_text(void)
{
    return longest_text;
}

/**
 * @brief Service Log 替身：先调钩子，再把标签和正文记下来；与真实实现一样拒绝 128 字节及以上的正文。
 * @param[in] level 等级，不记录。
 * @param[in] tag 标签。
 * @param[in] text 正文。
 * @retval SERVICE_OK 已记录。
 * @retval SERVICE_INVALID_PARAM 参数空，或正文太长（真实实现会丢弃并计入 ServiceDropped）。
 */
Service_StatusTypeDef Service_Log_Post(Service_Log_LevelTypeDef level, const char *tag, const char *text)
{
    size_t length;

    (void)level;
    if ((tag == NULL) || (text == NULL))
    {
        return SERVICE_INVALID_PARAM;
    }
    if (post_hook != NULL)
    {
        post_hook(tag, text);
    }
    length = strlen(text);
    if (length >= LOCK_ENV_TEXT_MAX)
    {
        return SERVICE_INVALID_PARAM;
    }
    if (length > longest_text)
    {
        longest_text = (uint32_t)length;
    }
    if (log_count < LOCK_ENV_LOG_MAX)
    {
        strncpy(log_tags[log_count], tag, LOCK_ENV_TAG_MAX - 1U);
        memcpy(log_texts[log_count], text, length + 1U);
        log_count++;
    }
    return SERVICE_OK;
}
