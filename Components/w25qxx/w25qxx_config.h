/**
  ******************************************************************************
  * @file    w25qxx_config.h
  * @brief   W25Qxx Device 的私有协议与时序参数。
  ******************************************************************************
  */

#ifndef W25QXX_CONFIG_H
#define W25QXX_CONFIG_H

/* JEDEC ID 命令返回制造商、存储器类型和容量代码三个字节。 */
#define W25QXX_COMMAND_READ_JEDEC_ID          0x9Fu
#define W25QXX_JEDEC_ID_LENGTH                3u

#endif /* W25QXX_CONFIG_H */
