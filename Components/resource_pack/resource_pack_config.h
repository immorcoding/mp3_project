/**
  ******************************************************************************
  * @file    resource_pack_config.h
  * @brief   RPKC1 类型 Metadata 解码器编译开关。
  ******************************************************************************
  */

#ifndef RESOURCE_PACK_CONFIG_H
#define RESOURCE_PACK_CONFIG_H

/* resource_pack.c */
#define RESOURCE_PACK_TYPE_BINARY_ENABLE    1  /* 编译通用二进制 Metadata 解码路径。 */
#define RESOURCE_PACK_TYPE_FONT_ENABLE      0  /* 关闭字体 Metadata 解码路径。 */
#define RESOURCE_PACK_TYPE_IMAGE_ENABLE     1  /* 编译图片 Metadata 解码路径。 */
#define RESOURCE_PACK_TYPE_AUDIO_ENABLE     0  /* 关闭音频 Metadata 解码路径。 */
#define RESOURCE_PACK_TYPE_MODEL_ENABLE     0  /* 关闭模型 Metadata 解码路径。 */
#define RESOURCE_PACK_TYPE_FIRMWARE_ENABLE  0  /* 关闭固件镜像 Metadata 解码路径。 */

#endif /* RESOURCE_PACK_CONFIG_H */
