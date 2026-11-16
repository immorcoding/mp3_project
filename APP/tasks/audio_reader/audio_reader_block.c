/**
 * @file audio_reader_block.c
 * @brief 音频块池与空闲/就绪队列。
 */

#include "audio_reader_block.h"

#include "Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/queue.h"

_Static_assert((AUDIO_READER_BLOCK_BYTES % 512U) == 0U, "block length must be a multiple of 512 B");
_Static_assert(AUDIO_READER_BLOCK_BYTES <= (32U * 1024U), "block length must not exceed the SD bounce buffer");
_Static_assert(AUDIO_READER_BLOCK_COUNT >= 3U, "block pool needs at least 3 blocks");

/** @brief 块池；SDRAM NOLOAD，不清零，块内容在入队前由音频读取任务写满头与有效字节。 */
static AudioReader_BlockTypeDef audio_reader_block_pool[AUDIO_READER_BLOCK_COUNT]
    __attribute__((section(AUDIO_READER_POOL_SECTION), aligned(32)));

/** @brief 队列元素是块指针；存储与控制块静态放 DTCM。 */
static uint8_t audio_reader_free_storage[AUDIO_READER_BLOCK_COUNT * sizeof(AudioReader_BlockTypeDef *)];
static uint8_t audio_reader_ready_storage[AUDIO_READER_BLOCK_COUNT * sizeof(AudioReader_BlockTypeDef *)];
static StaticQueue_t audio_reader_free_control;
static StaticQueue_t audio_reader_ready_control;
static QueueHandle_t audio_reader_free_queue;
static QueueHandle_t audio_reader_ready_queue;

/**
 * @brief 创建两个静态队列，并把全部块放入空闲队列。
 * @note  仅 Boot 任务（app_tasks.c）在 SDRAM 全容量诊断（若启用）之后、创建音频读取任务和播放任务之前调用一次；
 *        不触碰块内容（NOLOAD 段此时内容无效）。Boot 栈 192 word（#220 从 128 提上来）：本函数最深一条是 xQueueSend 的调用链，
 *        核算见 APP/tasks/audio_reader/README.md。
 */
void audio_reader_block_init(void)
{
    uint32_t i;

    audio_reader_free_queue = xQueueCreateStatic(AUDIO_READER_BLOCK_COUNT,
                                                  sizeof(AudioReader_BlockTypeDef *),
                                                  audio_reader_free_storage,
                                                  &audio_reader_free_control);
    audio_reader_ready_queue = xQueueCreateStatic(AUDIO_READER_BLOCK_COUNT,
                                                   sizeof(AudioReader_BlockTypeDef *),
                                                   audio_reader_ready_storage,
                                                   &audio_reader_ready_control);

    for (i = 0U; i < AUDIO_READER_BLOCK_COUNT; i++)
    {
        AudioReader_BlockTypeDef *block = &audio_reader_block_pool[i];

        (void)xQueueSend(audio_reader_free_queue, &block, 0U);
    }
}

/**
 * @brief 从空闲队列取一块，不阻塞。
 * @return 块指针；无空闲块为 NULL。
 * @note  音频读取任务使用；块内容无效，须自行盖章并写满有效字节。
 */
AudioReader_BlockTypeDef *audio_reader_block_acquire(void)
{
    AudioReader_BlockTypeDef *block = NULL;

    if (xQueueReceive(audio_reader_free_queue, &block, 0U) != pdTRUE)
    {
        return NULL;
    }
    return block;
}

/**
 * @brief 把读满的块放入就绪队列，不阻塞。
 * @param[in] block 已盖章、有效字节数在 1..块容量的块。
 * @return true 已入队。
 * @note  音频读取任务使用。队列容量等于块数，正常不会失败。
 */
bool audio_reader_block_publish(AudioReader_BlockTypeDef *block)
{
    return xQueueSend(audio_reader_ready_queue, &block, 0U) == pdTRUE;
}

/**
 * @brief 查看就绪队列队首而不取出。
 * @return 队首块；队列空为 NULL。
 * @note  只有播放任务消费就绪队列，队首在其取走前不会变。
 */
AudioReader_BlockTypeDef *audio_reader_block_peek_ready(void)
{
    AudioReader_BlockTypeDef *block = NULL;

    if (xQueuePeek(audio_reader_ready_queue, &block, 0U) != pdTRUE)
    {
        return NULL;
    }
    return block;
}

/**
 * @brief 取出就绪队列队首，不阻塞。
 * @return 块指针；队列空为 NULL。
 * @note  播放任务使用；用完或判定为旧流后须 audio_reader_block_release。
 */
AudioReader_BlockTypeDef *audio_reader_block_take_ready(void)
{
    AudioReader_BlockTypeDef *block = NULL;

    if (xQueueReceive(audio_reader_ready_queue, &block, 0U) != pdTRUE)
    {
        return NULL;
    }
    return block;
}

/**
 * @brief 把块放回空闲队列，不阻塞。
 * @param[in] block 不再使用的块。
 * @return true 已归还。
 * @note  播放任务与音频读取任务都可调用。播放任务归还后须经
 *        audio_reader_stream_return_block() 归还（含置音频读取任务的“块已归还”FLAG）。
 */
bool audio_reader_block_release(AudioReader_BlockTypeDef *block)
{
    return xQueueSend(audio_reader_free_queue, &block, 0U) == pdTRUE;
}

/**
 * @brief 当前空闲块数。
 * @return 空闲队列中的块数。
 */
uint32_t audio_reader_block_free_count(void)
{
    return (uint32_t)uxQueueMessagesWaiting(audio_reader_free_queue);
}

/**
 * @brief 当前就绪块数（就绪队列里的块数）。
 * @return 就绪队列中的块数；不含播放任务手里那块（它已从就绪队列取走）。
 * @note  安全水位按这个数算（#194 第 4 条）。
 */
uint32_t audio_reader_block_ready_count(void)
{
    return (uint32_t)uxQueueMessagesWaiting(audio_reader_ready_queue);
}
