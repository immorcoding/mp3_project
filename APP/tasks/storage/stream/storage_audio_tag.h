/**
 * @file storage_audio_tag.h
 * @brief 越过标签纯函数：由文件头尾字节算出音频区间 [Start, End)。
 * @note  不依赖 FreeRTOS、FatFs 或 HAL，可在主机上直接测试。读文件由 Storage 流主循环负责。
 */

#ifndef STORAGE_AUDIO_TAG_H
#define STORAGE_AUDIO_TAG_H

#include <stdbool.h>
#include <stdint.h>

#define STORAGE_AUDIO_TAG_HEAD_BYTES 10U   /* ID3v2 头长度；调用方读文件前 min(此值, 文件长度) 字节。 */
#define STORAGE_AUDIO_TAG_TAIL_BYTES 160U  /* ID3v1(128) + APEv2 脚(32)；调用方读文件末 min(此值, 文件长度) 字节。 */

/** @brief 音频区间，字节偏移，半开区间 [Start, End)。 */
typedef struct
{
    uint32_t Start; /**< 第一个音频字节的偏移。 */
    uint32_t End;   /**< 最后一个音频字节之后的偏移。 */
} Storage_AudioRangeTypeDef;

bool storage_audio_tag_locate(const uint8_t *head,
                              uint32_t head_bytes,
                              const uint8_t *tail,
                              uint32_t tail_bytes,
                              uint32_t file_size,
                              Storage_AudioRangeTypeDef *range);

#endif /* STORAGE_AUDIO_TAG_H */
