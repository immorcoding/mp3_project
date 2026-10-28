/**
 * @file storage_namebuffer.h
 * @brief 曲目显示名单槽：GUI 音乐分区 request，Storage load。与 Queue 窗口槽互不争用。
 */

#ifndef STORAGE_NAMEBUFFER_H
#define STORAGE_NAMEBUFFER_H

#include <stdbool.h>
#include <stdint.h>

#include "APP/tasks/storage/storage_task.h"
#include "APP/tasks/storage/catalog/storage_catalog_name.h"

#define STORAGE_NAMEBUFFER_IDLE    0U  /* 可接受下一次 request。 */
#define STORAGE_NAMEBUFFER_PENDING 1U  /* GUI 已提交，等待 Storage load。 */
#define STORAGE_NAMEBUFFER_READY   2U  /* 应答已填好或已作废为空名，待 GUI 读完后写回 IDLE。 */

typedef struct
{
    volatile uint32_t Status;     /* IDLE → PENDING → READY；GUI 读完必须写回 IDLE。 */
    uint32_t Generation;          /* request 时为请求代次（0 = 当前）；READY 后为实际应答代次，0 表示取不到。 */
    uint16_t Index;               /* 请求的播放列表位置。 */
    char Title[STORAGE_CATALOG_NAME_BYTES];  /* READY 且代次非 0 时为曲目显示名（可用标题，否则文件主名），可直接显示的 UTF-8；否则空串。 */
    char Artist[STORAGE_CATALOG_NAME_BYTES]; /* READY 且代次非 0 时为歌手（取不到为空串）；否则空串。 */
} Storage_NameBufferTypeDef;

extern Storage_NameBufferTypeDef storage_namebuffer;

void storage_namebuffer_bind(void *storage_task);
Storage_StatusTypeDef storage_namebuffer_request(uint16_t index, uint32_t generation);
void storage_namebuffer_load(bool sd_ready);
void storage_namebuffer_complete_unavailable(void);

#endif /* STORAGE_NAMEBUFFER_H */
