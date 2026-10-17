/**
 * @file storage_audio_block.c
 * @brief 音频块池与空闲/就绪队列。
 */

#include "storage_audio_block.h"

#include "Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/queue.h"

_Static_assert((STORAGE_AUDIO_BLOCK_BYTES % 512U) == 0U, "block length must be a multiple of 512 B");
_Static_assert(STORAGE_AUDIO_BLOCK_BYTES <= (32U * 1024U), "block length must not exceed the SD bounce buffer");
_Static_assert(STORAGE_AUDIO_BLOCK_COUNT >= 3U, "block pool needs at least 3 blocks");

/** @brief 块池；SDRAM NOLOAD，不清零，块内容在入队前由 Storage 写满头与有效字节。 */
static Storage_AudioBlockTypeDef storage_audio_block_pool[STORAGE_AUDIO_BLOCK_COUNT]
    __attribute__((section(STORAGE_AUDIO_POOL_SECTION), aligned(32)));

/** @brief 队列元素是块指针；存储与控制块静态放 DTCM。 */
static uint8_t storage_audio_free_storage[STORAGE_AUDIO_BLOCK_COUNT * sizeof(Storage_AudioBlockTypeDef *)];
static uint8_t storage_audio_ready_storage[STORAGE_AUDIO_BLOCK_COUNT * sizeof(Storage_AudioBlockTypeDef *)];
static StaticQueue_t storage_audio_free_control;
static StaticQueue_t storage_audio_ready_control;
static QueueHandle_t storage_audio_free_queue;
static QueueHandle_t storage_audio_ready_queue;

/**
 * @brief 创建两个静态队列，并把全部块放入空闲队列。
 * @note  仅 Storage Task 在 SDRAM 全容量诊断（若启用）之后、首次 acquire 之前调用一次；
 *        不触碰块内容（NOLOAD 段此时内容无效）。
 */
void storage_audio_block_init(void)
{
    uint32_t i;

    storage_audio_free_queue = xQueueCreateStatic(STORAGE_AUDIO_BLOCK_COUNT,
                                                  sizeof(Storage_AudioBlockTypeDef *),
                                                  storage_audio_free_storage,
                                                  &storage_audio_free_control);
    storage_audio_ready_queue = xQueueCreateStatic(STORAGE_AUDIO_BLOCK_COUNT,
                                                   sizeof(Storage_AudioBlockTypeDef *),
                                                   storage_audio_ready_storage,
                                                   &storage_audio_ready_control);

    for (i = 0U; i < STORAGE_AUDIO_BLOCK_COUNT; i++)
    {
        Storage_AudioBlockTypeDef *block = &storage_audio_block_pool[i];

        (void)xQueueSend(storage_audio_free_queue, &block, 0U);
    }
}

/**
 * @brief 从空闲队列取一块，不阻塞。
 * @return 块指针；无空闲块为 NULL。
 * @note  Storage Task 使用；块内容无效，须自行盖章并写满有效字节。
 */
Storage_AudioBlockTypeDef *storage_audio_block_acquire(void)
{
    Storage_AudioBlockTypeDef *block = NULL;

    if (xQueueReceive(storage_audio_free_queue, &block, 0U) != pdTRUE)
    {
        return NULL;
    }
    return block;
}

/**
 * @brief 把读满的块放入就绪队列，不阻塞。
 * @param[in] block 已盖章、有效字节数在 1..块容量的块。
 * @return true 已入队。
 * @note  Storage Task 使用。队列容量等于块数，正常不会失败。
 */
bool storage_audio_block_publish(Storage_AudioBlockTypeDef *block)
{
    return xQueueSend(storage_audio_ready_queue, &block, 0U) == pdTRUE;
}

/**
 * @brief 查看就绪队列队首而不取出。
 * @return 队首块；队列空为 NULL。
 * @note  只有播放任务消费就绪队列，队首在其取走前不会变。
 */
Storage_AudioBlockTypeDef *storage_audio_block_peek_ready(void)
{
    Storage_AudioBlockTypeDef *block = NULL;

    if (xQueuePeek(storage_audio_ready_queue, &block, 0U) != pdTRUE)
    {
        return NULL;
    }
    return block;
}

/**
 * @brief 取出就绪队列队首，不阻塞。
 * @return 块指针；队列空为 NULL。
 * @note  播放任务使用；用完或判定为旧流后须 storage_audio_block_release。
 */
Storage_AudioBlockTypeDef *storage_audio_block_take_ready(void)
{
    Storage_AudioBlockTypeDef *block = NULL;

    if (xQueueReceive(storage_audio_ready_queue, &block, 0U) != pdTRUE)
    {
        return NULL;
    }
    return block;
}

/**
 * @brief 把块放回空闲队列，不阻塞。
 * @param[in] block 不再使用的块。
 * @return true 已归还。
 * @note  播放任务与 Storage Task 都可调用。播放任务归还后须经
 *        storage_audio_stream_return_block() 归还（含置 Storage 的“块已归还”FLAG）。
 */
bool storage_audio_block_release(Storage_AudioBlockTypeDef *block)
{
    return xQueueSend(storage_audio_free_queue, &block, 0U) == pdTRUE;
}

/**
 * @brief 当前空闲块数。
 * @return 空闲队列中的块数。
 */
uint32_t storage_audio_block_free_count(void)
{
    return (uint32_t)uxQueueMessagesWaiting(storage_audio_free_queue);
}
