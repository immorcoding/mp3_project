/**
  ******************************************************************************
  * @file    gui_service_font_decoder.h
  * @brief   GUI Service 私有的 lv_font_conv bin 解码器 Interface。
  ******************************************************************************
  */

#ifndef GUI_SERVICE_FONT_DECODER_H
#define GUI_SERVICE_FONT_DECODER_H

#include <stdint.h>

#include "lvgl.h"

/** @brief 解码器输出区的首地址须满足的对齐字节数。 */
#define SERVICE_GUI_FONT_DECODE_REGION_ALIGN  8U

/** @brief 字体解码结果。 */
typedef enum
{
    SERVICE_GUI_FONT_DECODE_OK = 0U,         /**< 输入合法，查询或建表成功。 */
    SERVICE_GUI_FONT_DECODE_INVALID_PARAM,   /**< 指针为空、输出区未按要求对齐或字体长度为 0。 */
    SERVICE_GUI_FONT_DECODE_FORMAT_ERROR,    /**< 输入被截断，或表长、偏移、字形头等相互矛盾。 */
    SERVICE_GUI_FONT_DECODE_UNSUPPORTED,     /**< 合法 bin 但超出本解码器范围：压缩位图、子像素、非常用 bpp 或 kern 格式。 */
    SERVICE_GUI_FONT_DECODE_CODEPOINT_RANGE, /**< cmap 的码点范围超出 Unicode，或列表项超出其范围。 */
    SERVICE_GUI_FONT_DECODE_BITMAP_TOO_LARGE,/**< 位图总量不小于 1 MiB，LV_FONT_FMT_TXT_LARGE = 0 时无法索引。 */
    SERVICE_GUI_FONT_DECODE_OUTPUT_TOO_SMALL /**< 输出区容量不足以容纳字形表、cmap、kern 与重排位图。 */
} Service_GUI_FontDecodeStatusTypeDef;

/** @brief 一次校验得到的字体规模；Measure 与 Build 填同一组值。 */
typedef struct
{
    uint32_t RegionSize;   /**< 输出区需要的字节数，已按 SERVICE_GUI_FONT_DECODE_REGION_ALIGN 取整。 */
    uint32_t GlyphCount;   /**< 字形描述数，含索引 0 的 notdef。 */
    uint32_t CmapCount;    /**< 输出的 cmap 子表数，与 bin 中的子表一一对应。 */
    uint32_t BitmapBytes;  /**< 重排后的位图总字节数。 */
    uint32_t KernPairCount;/**< kern 字距对数；kern 为类别表或不存在时为 0。 */
} Service_GUI_FontDecodeInfoTypeDef;

Service_GUI_FontDecodeStatusTypeDef service_gui_font_decode_measure(
    const uint8_t *bin,
    uint32_t bin_size,
    Service_GUI_FontDecodeInfoTypeDef *info);
Service_GUI_FontDecodeStatusTypeDef service_gui_font_decode_build(
    const uint8_t *bin,
    uint32_t bin_size,
    void *region,
    uint32_t region_size,
    lv_font_t *font,
    Service_GUI_FontDecodeInfoTypeDef *info);
const char *service_gui_font_decode_status_text(Service_GUI_FontDecodeStatusTypeDef status);

#endif /* GUI_SERVICE_FONT_DECODER_H */
