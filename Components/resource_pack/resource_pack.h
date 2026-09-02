/**
 ******************************************************************************
 * @file    resource_pack.h
 * @brief   与硬件和业务无关的 RPKC1 只读资源包 Component Interface。
 ******************************************************************************
 */

#ifndef RESOURCE_PACK_H
#define RESOURCE_PACK_H

#include <stdint.h>

/** @brief RPKC1 公共操作状态。 */
typedef enum
{
    RESOURCE_PACK_OK = 0U,              /**< 本次解析或读取成功。 */
    RESOURCE_PACK_INVALID_PARAM,        /**< 句柄、缓冲区或范围参数非法。 */
    RESOURCE_PACK_NOT_OPEN,             /**< 资源包尚未成功打开。 */
    RESOURCE_PACK_NOT_FOUND,            /**< 指定资源 ID 不存在。 */
    RESOURCE_PACK_FORMAT_ERROR,         /**< 包头、目录或元数据布局非法。 */
    RESOURCE_PACK_CRC_ERROR,            /**< CRC 校验失败。 */
    RESOURCE_PACK_UNSUPPORTED_TYPE,     /**< 资源类型编号不被当前实现支持。 */
    RESOURCE_PACK_UNSUPPORTED_VERSION,  /**< 包或元数据版本不被当前实现支持。 */
    RESOURCE_PACK_UNSUPPORTED_FORMAT    /**< 元素、像素或其他格式编号不被支持。 */
} ResourcePack_StatusTypeDef;

/** @brief RPKC1 标准顶层资源类型编号。 */
typedef enum
{
    RESOURCE_PACK_TYPE_INVALID = 0U,  /**< 无效或未指定类型。 */
    RESOURCE_PACK_TYPE_BINARY = 1U,   /**< 通用二进制。 */
    RESOURCE_PACK_TYPE_FONT = 2U,     /**< 字体。 */
    RESOURCE_PACK_TYPE_IMAGE = 3U,    /**< 图片。 */
    RESOURCE_PACK_TYPE_AUDIO = 4U,    /**< 音频。 */
    RESOURCE_PACK_TYPE_MODEL = 5U,    /**< 模型。 */
    RESOURCE_PACK_TYPE_FIRMWARE = 6U  /**< 固件镜像。 */
} ResourcePack_TypeTypeDef;

/** @brief BINARY Metadata V1 的标准元素格式编号。 */
typedef enum
{
    RESOURCE_PACK_ELEMENT_FORMAT_INVALID = 0U, /**< 无效元素格式。 */
    RESOURCE_PACK_ELEMENT_FORMAT_OPAQUE = 1U,  /**< 不解释内部布局的不透明字节。 */
    RESOURCE_PACK_ELEMENT_FORMAT_UINT8 = 2U,   /**< 无符号 8 位。 */
    RESOURCE_PACK_ELEMENT_FORMAT_UINT16 = 3U,  /**< 无符号 16 位。 */
    RESOURCE_PACK_ELEMENT_FORMAT_UINT32 = 4U,  /**< 无符号 32 位。 */
    RESOURCE_PACK_ELEMENT_FORMAT_INT8 = 5U,    /**< 有符号 8 位。 */
    RESOURCE_PACK_ELEMENT_FORMAT_INT16 = 6U,   /**< 有符号 16 位。 */
    RESOURCE_PACK_ELEMENT_FORMAT_INT32 = 7U    /**< 有符号 32 位。 */
} ResourcePack_ElementFormatTypeDef;

/** @brief BINARY Metadata V1 的字节序编号。 */
typedef enum
{
    RESOURCE_PACK_BYTE_ORDER_UNKNOWN = 0U,        /**< 未声明字节序。 */
    RESOURCE_PACK_BYTE_ORDER_LITTLE_ENDIAN = 1U,  /**< 小端。 */
    RESOURCE_PACK_BYTE_ORDER_BIG_ENDIAN = 2U      /**< 大端。 */
} ResourcePack_ByteOrderTypeDef;

/** @brief IMAGE Metadata V1 的图片载荷格式编号。 */
typedef enum
{
    RESOURCE_PACK_IMAGE_FORMAT_UNKNOWN = 0U,     /**< 未声明图片载荷格式。 */
    RESOURCE_PACK_IMAGE_FORMAT_LVGL_NATIVE = 1U  /**< LVGL 原生像素缓冲。 */
} ResourcePack_ImageFormatTypeDef;

/** @brief IMAGE Metadata V1 的像素格式编号。 */
typedef enum
{
    RESOURCE_PACK_PIXEL_FORMAT_UNKNOWN = 0U,           /**< 未声明像素格式。 */
    RESOURCE_PACK_PIXEL_FORMAT_TRUE_COLOR_ALPHA = 1U   /**< 真彩加 Alpha。 */
} ResourcePack_PixelFormatTypeDef;

/** @brief IMAGE Metadata V1 的色彩空间编号。 */
typedef enum
{
    RESOURCE_PACK_COLOR_SPACE_UNKNOWN = 0U, /**< 未声明色彩空间。 */
    RESOURCE_PACK_COLOR_SPACE_SRGB = 1U     /**< sRGB。 */
} ResourcePack_ColorSpaceTypeDef;

/** @brief IMAGE Metadata V1 的 Alpha 表达方式编号。 */
typedef enum
{
    RESOURCE_PACK_ALPHA_MODE_UNKNOWN = 0U,        /**< 未声明 Alpha 方式。 */
    RESOURCE_PACK_ALPHA_MODE_STRAIGHT = 1U,       /**< 非预乘 Alpha。 */
    RESOURCE_PACK_ALPHA_MODE_PREMULTIPLIED = 2U,  /**< 预乘 Alpha。 */
    RESOURCE_PACK_ALPHA_MODE_OPAQUE = 3U          /**< 不透明，忽略 Alpha。 */
} ResourcePack_AlphaModeTypeDef;

/** @brief 已打开资源包的公共头信息。 */
typedef struct
{
    uint32_t FormatVersion;
    uint32_t HeaderSize;
    uint32_t PackSize;
    uint32_t EntryOffset;
    uint16_t EntryCount;
    uint16_t EntrySize;
    uint32_t DataAlignment;
    uint32_t MetadataAlignment;
    uint32_t VendorID;
    uint32_t ProductID;
    uint64_t PackageVersion;
} ResourcePack_InfoTypeDef;

/** @brief 显式解码后的单个 40 字节 RPKC1 Entry。 */
typedef struct
{
    uint32_t ResourceID;
    uint16_t ResourceType;
    uint16_t Flags;
    uint64_t ResourceVersion;
    uint32_t DataOffset;
    uint32_t DataLength;
    uint32_t DataCRC32;
    uint32_t MetadataOffset;
    uint32_t MetadataLength;
    uint32_t MetadataCRC32;
} ResourcePack_EntryTypeDef;

/** @brief 指向包内不可变 Metadata 和 Data 的临时只读 View。 */
typedef struct
{
    const uint8_t *Metadata;
    uint32_t MetadataLength;
    const uint8_t *Data;
    uint32_t DataLength;
} ResourcePack_EntryViewTypeDef;

/** @brief BINARY Metadata V1 的已解码字段。 */
typedef struct
{
    uint32_t MetadataVersion;
    uint32_t MetadataSize;
    uint32_t ElementFormat;
    uint32_t ByteOrder;
    uint32_t ElementSize;
    uint32_t ElementCount;
} ResourcePack_BinaryMetadataTypeDef;

/** @brief IMAGE Metadata V1 的已解码字段。 */
typedef struct
{
    uint32_t MetadataVersion;
    uint32_t MetadataSize;
    uint32_t ImageFormat;
    uint32_t PixelFormat;
    uint32_t Width;
    uint32_t Height;
    uint32_t StrideBytes;
    uint32_t FrameCount;
    uint32_t ColorSpace;
    uint32_t AlphaMode;
} ResourcePack_ImageMetadataTypeDef;

/** @brief 零动态内存的已打开资源包句柄。 */
typedef struct
{
    const uint8_t *Pack;
    uint32_t AvailableSize;
    ResourcePack_InfoTypeDef Info;
    uint8_t IsOpen;
} ResourcePack_HandleTypeDef;

ResourcePack_StatusTypeDef ResourcePack_Open(
    const uint8_t *pack,
    uint32_t available_size,
    ResourcePack_HandleTypeDef *handle);
ResourcePack_StatusTypeDef ResourcePack_GetInfo(
    const ResourcePack_HandleTypeDef *handle,
    ResourcePack_InfoTypeDef *info);
ResourcePack_StatusTypeDef ResourcePack_GetEntryCount(
    const ResourcePack_HandleTypeDef *handle,
    uint16_t *entry_count);
ResourcePack_StatusTypeDef ResourcePack_GetEntry(
    const ResourcePack_HandleTypeDef *handle,
    uint16_t index,
    ResourcePack_EntryTypeDef *entry);
ResourcePack_StatusTypeDef ResourcePack_FindEntry(
    const ResourcePack_HandleTypeDef *handle,
    uint32_t resource_id,
    ResourcePack_EntryTypeDef *entry);
ResourcePack_StatusTypeDef ResourcePack_GetEntryView(
    const ResourcePack_HandleTypeDef *handle,
    const ResourcePack_EntryTypeDef *entry,
    ResourcePack_EntryViewTypeDef *view);
ResourcePack_StatusTypeDef ResourcePack_VerifyMetadata(
    const ResourcePack_EntryTypeDef *entry,
    const ResourcePack_EntryViewTypeDef *view);
ResourcePack_StatusTypeDef ResourcePack_VerifyBuffer(
    const void *buffer,
    uint32_t length,
    uint32_t expected_crc32);
ResourcePack_StatusTypeDef ResourcePack_DecodeBinaryMetadata(
    const ResourcePack_EntryTypeDef *entry,
    const ResourcePack_EntryViewTypeDef *view,
    ResourcePack_BinaryMetadataTypeDef *metadata);
ResourcePack_StatusTypeDef ResourcePack_DecodeImageMetadata(
    const ResourcePack_EntryTypeDef *entry,
    const ResourcePack_EntryViewTypeDef *view,
    ResourcePack_ImageMetadataTypeDef *metadata);

#endif /* RESOURCE_PACK_H */
