/**
 * @file storage_audio_block.h
 * @brief 音频块与块池：SDRAM 静态块池加空闲、就绪两个只传指针的静态队列。
 * @note  块只由 CPU 读写，不是 DMA 缓冲（SD DMA → bounce buffer → memcpy 进块），不适用 ARC-18。
 *        块池在 SDRAM NOLOAD 段（STORAGE_AUDIO_POOL_SECTION），块内容入队才有效，不依赖清零。
 *        队列静态放 DTCM（.bss）。本头只含块与队列操作，不含唤醒；唤醒见 storage_audio_stream.h。
 */

#ifndef STORAGE_AUDIO_BLOCK_H
#define STORAGE_AUDIO_BLOCK_H

#include <stdbool.h>
#include <stdint.h>

#include "APP/tasks/storage/stream/storage_audio_config.h"

/**
 * @brief 音频块：块头只含 {流序号, 有效字节数}，其后是音频帧流字节。
 * @note  有效字节数 1..STORAGE_AUDIO_BLOCK_BYTES；任何块都可以是短块，没有 kind、末块标志或块序号。
 */
typedef struct
{
    uint32_t StreamSeq;                          /**< 所属音频流的序号，由 Storage 盖章。 */
    uint32_t ValidBytes;                         /**< Data 中有效字节数。 */
    uint8_t Data[STORAGE_AUDIO_BLOCK_BYTES];     /**< 音频区间内的原始字节。 */
} Storage_AudioBlockTypeDef;

void storage_audio_block_init(void);
Storage_AudioBlockTypeDef *storage_audio_block_acquire(void);
bool storage_audio_block_publish(Storage_AudioBlockTypeDef *block);
Storage_AudioBlockTypeDef *storage_audio_block_peek_ready(void);
Storage_AudioBlockTypeDef *storage_audio_block_take_ready(void);
bool storage_audio_block_release(Storage_AudioBlockTypeDef *block);
uint32_t storage_audio_block_free_count(void);

#endif /* STORAGE_AUDIO_BLOCK_H */
