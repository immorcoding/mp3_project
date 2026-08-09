#ifndef FILESYSTEM_SERVICE_H
#define FILESYSTEM_SERVICE_H

#include "Middlewares/Third_Party/FatFs/src/ff.h"

/**
  * @brief  检查 CubeMX 是否已完成 SD DiskIO Driver 的一次性链接。
  * @retval FR_OK 驱动可供当前 Storage Task 使用。
  * @retval FR_NOT_READY Driver 未链接或链接失败。
  */
FRESULT Filesystem_Service_Init(void);

/**
  * @brief  将当前 SD 逻辑卷格式化为 FAT32。
  * @retval FatFs 原始 FRESULT。
  * @warning 调用会销毁卷中所有文件；调用者必须拥有 SD 和 FatFs 的独占权。
  */
FRESULT Filesystem_Service_MkfsSD(void);

/**
  * @brief  强制挂载当前已就绪的 SD 逻辑卷。
  * @retval FatFs 原始 FRESULT，FR_OK 表示挂载成功。
  */
FRESULT Filesystem_Service_MountSD(void);

/**
  * @brief  注销当前 SD 逻辑卷的 FatFs 卷对象。
  * @retval FatFs 原始 FRESULT，FR_OK 表示已成功注销。
  * @note   注销不会访问物理 SD 卡，可在拔卡事件已确认后调用。
  */
FRESULT Filesystem_Service_UnmountSD(void);

#endif /* FILESYSTEM_SERVICE_H */
