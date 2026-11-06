/**
 * @file audio_reader_block.h
 * @brief 音频块与块池：SDRAM 静态块池加空闲、就绪两个只传指针的静态队列。
 * @note  块只由 CPU 读写，不是 DMA 缓冲（SD DMA → bounce buffer → memcpy 进块），不适用 ARC-18。
 *        块池在 SDRAM NOLOAD 段（AUDIO_READER_POOL_SECTION），块内容入队才有效，不依赖清零。
 *        队列静态放 DTCM（.bss）。本头只含块与队列操作，不含唤醒；唤醒见 audio_reader_stream.h。
 *        块池与队列由 Boot 任务（app_tasks.c）调用 audio_reader_block_init() 初始化，做完才创建用到它们的任务。
 */

#ifndef AUDIO_READER_BLOCK_H
#define AUDIO_READER_BLOCK_H

#include <stdbool.h>
#include <stdint.h>

#include "APP/tasks/audio_reader/audio_reader_config.h"

/**
 * @brief 音频块：块头只含 {流序号, 有效字节数}，其后是音频帧流字节。
 * @note  有效字节数 1..AUDIO_READER_BLOCK_BYTES；任何块都可以是短块，没有 kind、末块标志或块序号。
 */
typedef struct
{
    uint32_t StreamSeq;                          /**< 所属音频流的序号，由音频读取任务盖章。 */
    uint32_t ValidBytes;                         /**< Data 中有效字节数。 */
    uint8_t Data[AUDIO_READER_BLOCK_BYTES];     /**< 音频区间内的原始字节。 */
} AudioReader_BlockTypeDef;

void audio_reader_block_init(void);
AudioReader_BlockTypeDef *audio_reader_block_acquire(void);
bool audio_reader_block_publish(AudioReader_BlockTypeDef *block);
AudioReader_BlockTypeDef *audio_reader_block_peek_ready(void);
AudioReader_BlockTypeDef *audio_reader_block_take_ready(void);
bool audio_reader_block_release(AudioReader_BlockTypeDef *block);
uint32_t audio_reader_block_free_count(void);
uint32_t audio_reader_block_ready_count(void);

#endif /* AUDIO_READER_BLOCK_H */
