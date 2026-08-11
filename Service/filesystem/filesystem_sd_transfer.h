#ifndef FILESYSTEM_SD_TRANSFER_H
#define FILESYSTEM_SD_TRANSFER_H

#include <stdbool.h>
#include <stdint.h>

bool filesystem_sd_transfer_init(void);
bool filesystem_sd_transfer_read_blocks(uint8_t *destination,
                                        uint32_t start_block,
                                        uint32_t block_count,
                                        uint32_t timeout_ms);
bool filesystem_sd_transfer_write_blocks(const uint8_t *source,
                                         uint32_t start_block,
                                         uint32_t block_count,
                                         uint32_t timeout_ms);

#endif /* FILESYSTEM_SD_TRANSFER_H */
