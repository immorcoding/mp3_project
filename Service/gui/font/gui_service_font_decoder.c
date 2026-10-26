/**
  ******************************************************************************
  * @file    gui_service_font_decoder.c
  * @brief   把 lv_font_conv 1.5.3 的未压缩 bin 解码为 LVGL 8.3 的 fmt_txt 字体，写入调用者给定的输出区。
  *
  * 输入 bin 只读；输出区内依次放字体描述头、cmap 子表、码点列表、字形描述、kern 与重排后的位图，
  * 不使用 LVGL 堆，不分配任何内存。分段框架（标签、长度、范围）与 head 参数由
  * ResourcePack_LocateFontBin() 给出，本文件只解析 cmap、loca、glyf 与 kern 的内容。
  *
  * 与 lv_font_load 的两处刻意差别：
  *  - 所有 FORMAT0 子表都输出为 SPARSE 子表。LVGL 8.3 的码点查找用 `rcp > range_length` 判断范围，
  *    码点恰为范围末尾的下一个时，FORMAT0 会读到列表外一项或返回邻近字形；稀疏表二分查找不受影响，
  *    缺字因此稳定落到占位框而不是错字。
  *  - 先完整校验再写输出区；任何不一致都在写入前以明确的状态返回。
  ******************************************************************************
  */

#include "Service/gui/font/gui_service_font_decoder.h"

#include <stddef.h>
#include <string.h>

#include "Components/resource_pack/resource_pack.h"

#define GUI_FONT_SECTION_PREFIX      8U
#define GUI_FONT_CMAP_FIXED_SIZE     12U
#define GUI_FONT_CMAP_ENTRY_SIZE     16U
#define GUI_FONT_KERN_FIXED_SIZE     16U
#define GUI_FONT_UNICODE_MAX         0x10FFFFU
#define GUI_FONT_GLYPH_COUNT_MAX     0x10000U
#define GUI_FONT_CMAP_COUNT_MAX      511U
#define GUI_FONT_KERN_PAIR_MAX       0x3FFFFFFFU
#define GUI_FONT_ADV_W_MAX           0xFFFU
#define GUI_FONT_FIELD_BITS_MAX      16U
#define GUI_FONT_TABLES_WITH_KERN    4U
#define GUI_FONT_KERN_FORMAT_PAIRS   0U
#define GUI_FONT_KERN_FORMAT_CLASSES 3U

/** @brief kern 的存在与种类。 */
typedef enum
{
    GUI_FONT_KERN_NONE = 0U,
    GUI_FONT_KERN_PAIRS,
    GUI_FONT_KERN_CLASSES
} GuiFont_KernKindTypeDef;

/** @brief 一次校验得到的全部事实与输出区内各块的偏移。 */
typedef struct
{
    const uint8_t *Bin;
    uint32_t BinSize;
    ResourcePack_FontBinLayoutTypeDef Layout;
    uint32_t HeaderBits;
    uint32_t LocaEntrySize;
    uint32_t ListBytes;
    uint32_t BitmapBytes;
    GuiFont_KernKindTypeDef KernKind;
    uint32_t KernOffset;      /**< kern 分段在 bin 内的起点。 */
    uint32_t KernPairs;
    uint32_t KernIdBytes;
    uint32_t KernMappingLength;
    uint32_t KernMappingBytes;/**< 输出的每张类别映射表字节数：不小于字形数。 */
    uint32_t KernRows;
    uint32_t KernCols;
    uint32_t OffDsc;
    uint32_t OffCmaps;
    uint32_t OffLists;
    uint32_t OffGlyphs;
    uint32_t OffKernDsc;
    uint32_t OffKernA;        /**< pairs：ids；classes：左映射。 */
    uint32_t OffKernB;        /**< pairs：values；classes：右映射。 */
    uint32_t OffKernC;        /**< classes：值表。 */
    uint32_t OffBitmap;
    uint32_t Total;
} GuiFont_PlanTypeDef;

/**
 * @brief 向上取整到输出区对齐。
 * @param[in] value 字节数，64 位以便调用者累加后统一判断是否超出 32 位。
 * @return 取整后的字节数。
 */
static uint64_t gui_font_align_up(uint64_t value)
{
    return (value + (SERVICE_GUI_FONT_DECODE_REGION_ALIGN - 1U)) & ~((uint64_t)SERVICE_GUI_FONT_DECODE_REGION_ALIGN - 1U);
}

/**
 * @brief 按小端读 16 位。
 * @param[in] data 字段首地址。
 * @return 数值。
 */
static uint16_t gui_font_read_u16(const uint8_t *data)
{
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8U));
}

/**
 * @brief 按小端读 32 位。
 * @param[in] data 字段首地址。
 * @return 数值。
 */
static uint32_t gui_font_read_u32(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8U) | ((uint32_t)data[2] << 16U) | ((uint32_t)data[3] << 24U);
}

/**
 * @brief 从位流按 MSB 优先读取最多 32 位。
 * @param[in] data 位流首地址，调用者保证覆盖 bit_position + count 位。
 * @param[in] bit_position 起始位序号。
 * @param[in] count 位数。
 * @return 读到的数值。
 */
static uint32_t gui_font_read_bits(const uint8_t *data, uint32_t bit_position, uint32_t count)
{
    uint32_t value = 0U;

    for (uint32_t index = 0U; index < count; index++)
    {
        uint32_t position = bit_position + index;

        value = (value << 1U) | (((uint32_t)data[position / 8U] >> (7U - (position % 8U))) & 1U);
    }

    return value;
}

/**
 * @brief 读 count 位并按二进制补码扩展符号。
 * @param[in] data 位流首地址。
 * @param[in] bit_position 起始位序号。
 * @param[in] count 位数，1..16。
 * @return 有符号数值。
 */
static int32_t gui_font_read_signed_bits(const uint8_t *data, uint32_t bit_position, uint32_t count)
{
    int32_t value = (int32_t)gui_font_read_bits(data, bit_position, count);

    if ((value & (1 << (count - 1U))) != 0)
    {
        value -= (int32_t)(1UL << count);
    }

    return value;
}

/**
 * @brief 读 loca 第 index 项（相对 glyf 分段起点，含 8 字节分段前缀）。
 * @param[in] plan 校验计划。
 * @param[in] index 项序号，调用者保证小于字形数。
 * @return 偏移。
 */
static uint32_t gui_font_read_loca(const GuiFont_PlanTypeDef *plan, uint32_t index)
{
    const uint8_t *entry = plan->Bin + plan->Layout.Loca.Offset + 12U + (index * plan->LocaEntrySize);

    return (plan->LocaEntrySize == 2U) ? (uint32_t)gui_font_read_u16(entry) : gui_font_read_u32(entry);
}

/**
 * @brief 校验 head 参数中本解码器支持的组合。
 * @param[in,out] plan 校验计划，写入 HeaderBits 与 LocaEntrySize。
 * @return 通过返回 OK。
 */
static Service_GUI_FontDecodeStatusTypeDef gui_font_check_head(GuiFont_PlanTypeDef *plan)
{
    const ResourcePack_FontBinLayoutTypeDef *layout = &plan->Layout;
    int32_t line_height = (int32_t)layout->Ascent - (int32_t)layout->Descent;

    if ((layout->CompressionId != 0U) || (layout->SubpixelsMode != 0U))
    {
        return SERVICE_GUI_FONT_DECODE_UNSUPPORTED;
    }

    if ((layout->BitsPerPixel != 1U) && (layout->BitsPerPixel != 2U) && (layout->BitsPerPixel != 3U) &&
        (layout->BitsPerPixel != 4U) && (layout->BitsPerPixel != 8U))
    {
        return SERVICE_GUI_FONT_DECODE_UNSUPPORTED;
    }

    if ((layout->AdvanceWidthFormat > 1U) || (layout->GlyphIdFormat > 1U) ||
        (layout->XyBits == 0U) || (layout->XyBits > GUI_FONT_FIELD_BITS_MAX) ||
        (layout->WhBits == 0U) || (layout->WhBits > GUI_FONT_FIELD_BITS_MAX) ||
        (layout->AdvanceWidthBits > GUI_FONT_FIELD_BITS_MAX) ||
        (line_height <= 0) || (line_height > 0x7FFF) ||
        (layout->UnderlinePosition < -128) || (layout->UnderlinePosition > 127) ||
        (layout->UnderlineThickness > 127U))
    {
        return SERVICE_GUI_FONT_DECODE_FORMAT_ERROR;
    }

    if (layout->GlyphCount > GUI_FONT_GLYPH_COUNT_MAX)
    {
        return SERVICE_GUI_FONT_DECODE_FORMAT_ERROR;
    }

    plan->HeaderBits = (uint32_t)layout->AdvanceWidthBits + (2U * layout->XyBits) + (2U * layout->WhBits);
    plan->LocaEntrySize = (layout->IndexToLocFormat == 0U) ? 2U : 4U;
    return SERVICE_GUI_FONT_DECODE_OK;
}

/**
 * @brief 校验 cmap 并（给定输出块时）写出子表与列表。
 *
 * 校验：子表表头与数据落在 cmap 分段内；码点范围不超出 Unicode；稀疏列表严格升序且不超出范围；
 * 所有映射到的字形编号小于字形数。FORMAT0 子表展开为 SPARSE 子表（见文件头）。
 * @param[in,out] plan 校验计划；校验时累加 ListBytes。
 * @param[in] region 输出区首地址；NULL 表示只校验。
 * @return 通过返回 OK。
 */
static Service_GUI_FontDecodeStatusTypeDef gui_font_process_cmaps(GuiFont_PlanTypeDef *plan, uint8_t *region)
{
    const uint8_t *cmap = plan->Bin + plan->Layout.Cmap.Offset;
    uint32_t cmap_length = plan->Layout.Cmap.Length;
    uint32_t glyph_count = plan->Layout.GlyphCount;
    uint32_t count;
    uint64_t table_end;
    uint64_t list_bytes = 0U;
    lv_font_fmt_txt_cmap_t *cmaps = (region != NULL) ? (lv_font_fmt_txt_cmap_t *)(void *)(region + plan->OffCmaps) : NULL;
    uint8_t *lists = (region != NULL) ? (region + plan->OffLists) : NULL;

    if (cmap_length < GUI_FONT_CMAP_FIXED_SIZE)
    {
        return SERVICE_GUI_FONT_DECODE_FORMAT_ERROR;
    }

    count = gui_font_read_u32(cmap + GUI_FONT_SECTION_PREFIX);
    table_end = GUI_FONT_CMAP_FIXED_SIZE + ((uint64_t)count * GUI_FONT_CMAP_ENTRY_SIZE);
    if ((count == 0U) || (count > GUI_FONT_CMAP_COUNT_MAX) || (table_end > cmap_length))
    {
        return SERVICE_GUI_FONT_DECODE_FORMAT_ERROR;
    }

    for (uint32_t index = 0U; index < count; index++)
    {
        const uint8_t *entry = cmap + GUI_FONT_CMAP_FIXED_SIZE + (index * GUI_FONT_CMAP_ENTRY_SIZE);
        uint32_t data_offset = gui_font_read_u32(entry);
        uint32_t range_start = gui_font_read_u32(entry + 4U);
        uint32_t range_length = gui_font_read_u16(entry + 8U);
        uint32_t glyph_id_start = gui_font_read_u16(entry + 10U);
        uint32_t entries = gui_font_read_u16(entry + 12U);
        uint8_t type = entry[14U];
        const uint8_t *data;
        uint32_t list_count;
        uint16_t *unicode_list = NULL;
        uint16_t *offset_list = NULL;
        uint8_t has_offsets;

        if ((range_length == 0U) || (data_offset < table_end) || (data_offset > cmap_length))
        {
            return SERVICE_GUI_FONT_DECODE_FORMAT_ERROR;
        }

        if ((range_start > GUI_FONT_UNICODE_MAX) || ((range_start + range_length - 1U) > GUI_FONT_UNICODE_MAX))
        {
            return SERVICE_GUI_FONT_DECODE_CODEPOINT_RANGE;
        }

        data = cmap + data_offset;

        switch (type)
        {
        case LV_FONT_FMT_TXT_CMAP_FORMAT0_FULL:
            if ((entries != range_length) || (range_length > (cmap_length - data_offset)))
            {
                return SERVICE_GUI_FONT_DECODE_FORMAT_ERROR;
            }
            list_count = range_length;
            has_offsets = 1U;
            break;
        case LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY:
            list_count = range_length;
            has_offsets = 0U;
            break;
        case LV_FONT_FMT_TXT_CMAP_SPARSE_TINY:
        case LV_FONT_FMT_TXT_CMAP_SPARSE_FULL:
            list_count = entries;
            has_offsets = (type == LV_FONT_FMT_TXT_CMAP_SPARSE_FULL) ? 1U : 0U;
            if (((uint64_t)list_count * (has_offsets ? 4U : 2U)) > (cmap_length - data_offset))
            {
                return SERVICE_GUI_FONT_DECODE_FORMAT_ERROR;
            }
            break;
        default:
            return SERVICE_GUI_FONT_DECODE_UNSUPPORTED;
        }

        if (lists != NULL)
        {
            unicode_list = (uint16_t *)(void *)(lists + list_bytes);
            offset_list = has_offsets ? (uint16_t *)(void *)(lists + list_bytes + (2U * list_count)) : NULL;
        }

        for (uint32_t item = 0U; item < list_count; item++)
        {
            uint32_t relative = item;
            uint32_t offset = item;

            if (type == LV_FONT_FMT_TXT_CMAP_FORMAT0_FULL)
            {
                offset = data[item];
            }
            else if ((type == LV_FONT_FMT_TXT_CMAP_SPARSE_TINY) || (type == LV_FONT_FMT_TXT_CMAP_SPARSE_FULL))
            {
                relative = gui_font_read_u16(data + (2U * item));
                offset = has_offsets ? gui_font_read_u16(data + (2U * list_count) + (2U * item)) : item;

                if (relative >= range_length)
                {
                    return SERVICE_GUI_FONT_DECODE_CODEPOINT_RANGE;
                }

                if ((item != 0U) && (relative <= gui_font_read_u16(data + (2U * (item - 1U)))))
                {
                    return SERVICE_GUI_FONT_DECODE_FORMAT_ERROR;
                }
            }

            if (has_offsets && ((glyph_id_start + offset) >= glyph_count))
            {
                return SERVICE_GUI_FONT_DECODE_FORMAT_ERROR;
            }

            if (!has_offsets && ((glyph_id_start + item) >= glyph_count))
            {
                return SERVICE_GUI_FONT_DECODE_FORMAT_ERROR;
            }

            if (unicode_list != NULL)
            {
                unicode_list[item] = (uint16_t)relative;
                if (offset_list != NULL)
                {
                    offset_list[item] = (uint16_t)offset;
                }
            }
        }

        if (cmaps != NULL)
        {
            cmaps[index].range_start = range_start;
            cmaps[index].range_length = (uint16_t)range_length;
            cmaps[index].glyph_id_start = (uint16_t)glyph_id_start;
            cmaps[index].unicode_list = unicode_list;
            cmaps[index].glyph_id_ofs_list = offset_list;
            cmaps[index].list_length = (uint16_t)list_count;
            cmaps[index].type = has_offsets ? (lv_font_fmt_txt_cmap_type_t)LV_FONT_FMT_TXT_CMAP_SPARSE_FULL
                                            : (lv_font_fmt_txt_cmap_type_t)LV_FONT_FMT_TXT_CMAP_SPARSE_TINY;
        }

        list_bytes += (uint64_t)list_count * (has_offsets ? 4U : 2U);
    }

    if (list_bytes > 0x7FFFFFFFU)
    {
        return SERVICE_GUI_FONT_DECODE_FORMAT_ERROR;
    }

    plan->ListBytes = (uint32_t)list_bytes;
    return SERVICE_GUI_FONT_DECODE_OK;
}

/**
 * @brief 校验 loca 与每个字形条目，并（给定输出块时）写出字形描述与重排后的位图。
 *
 * 位图总量达到 1 MiB 即失败。重排与 lv_font_load 的结果逐字节一致：条目中字形头占 HeaderBits 位，
 * 位图自第 HeaderBits 位起，非整字节时整体左移 HeaderBits % 8 位。
 * @param[in,out] plan 校验计划；校验时写 BitmapBytes。
 * @param[in] region 输出区首地址；NULL 表示只校验。
 * @return 通过返回 OK。
 */
static Service_GUI_FontDecodeStatusTypeDef gui_font_process_glyphs(GuiFont_PlanTypeDef *plan, uint8_t *region)
{
    const ResourcePack_FontBinLayoutTypeDef *layout = &plan->Layout;
    const uint8_t *glyf = plan->Bin + layout->Glyf.Offset;
    uint32_t header_bytes = plan->HeaderBits / 8U;
    uint32_t header_shift = plan->HeaderBits % 8U;
    uint32_t box_position = (uint32_t)layout->AdvanceWidthBits + (2U * layout->XyBits);
    uint64_t bitmap_bytes = 0U;
    lv_font_fmt_txt_glyph_dsc_t *glyph_dsc =
        (region != NULL) ? (lv_font_fmt_txt_glyph_dsc_t *)(void *)(region + plan->OffGlyphs) : NULL;
    uint8_t *bitmap = (region != NULL) ? (region + plan->OffBitmap) : NULL;

    for (uint32_t index = 0U; index < layout->GlyphCount; index++)
    {
        uint32_t offset = gui_font_read_loca(plan, index);
        uint32_t next = (index + 1U < layout->GlyphCount) ? gui_font_read_loca(plan, index + 1U) : layout->Glyf.Length;
        const uint8_t *entry;
        uint32_t entry_length;
        uint32_t position = 0U;
        uint32_t advance;
        int32_t offset_x;
        int32_t offset_y;
        uint32_t box_width;
        uint32_t box_height;

        if ((offset > next) || (next > layout->Glyf.Length))
        {
            return SERVICE_GUI_FONT_DECODE_FORMAT_ERROR;
        }

        if (index == 0U)
        {
            /* notdef：lv_font_conv 写零长度条目，LVGL 一律置空。 */
            if (glyph_dsc != NULL)
            {
                memset(&glyph_dsc[0], 0, sizeof(glyph_dsc[0]));
            }
            continue;
        }

        entry_length = next - offset;
        if ((offset < GUI_FONT_SECTION_PREFIX) || ((uint64_t)entry_length * 8U < plan->HeaderBits))
        {
            return SERVICE_GUI_FONT_DECODE_FORMAT_ERROR;
        }

        entry = glyf + offset;

        advance = (layout->AdvanceWidthBits != 0U) ? gui_font_read_bits(entry, position, layout->AdvanceWidthBits)
                                                  : layout->DefaultAdvanceWidth;
        position += layout->AdvanceWidthBits;
        if (layout->AdvanceWidthFormat == 0U)
        {
            advance *= 16U;
        }

        offset_x = gui_font_read_signed_bits(entry, position, layout->XyBits);
        offset_y = gui_font_read_signed_bits(entry, position + layout->XyBits, layout->XyBits);
        box_width = gui_font_read_bits(entry, box_position, layout->WhBits);
        box_height = gui_font_read_bits(entry, box_position + layout->WhBits, layout->WhBits);

        if ((advance > GUI_FONT_ADV_W_MAX) || (offset_x < -128) || (offset_x > 127) ||
            (offset_y < -128) || (offset_y > 127) || (box_width > 255U) || (box_height > 255U))
        {
            return SERVICE_GUI_FONT_DECODE_FORMAT_ERROR;
        }

        if (glyph_dsc != NULL)
        {
            glyph_dsc[index].bitmap_index = (uint32_t)bitmap_bytes;
            glyph_dsc[index].adv_w = advance;
            glyph_dsc[index].box_w = (uint8_t)box_width;
            glyph_dsc[index].box_h = (uint8_t)box_height;
            glyph_dsc[index].ofs_x = (int8_t)offset_x;
            glyph_dsc[index].ofs_y = (int8_t)offset_y;
        }

        if ((box_width * box_height) != 0U)
        {
            uint32_t stored = entry_length - header_bytes;
            uint32_t needed = ((box_width * box_height * layout->BitsPerPixel) + 7U) / 8U;

            if (stored < needed)
            {
                return SERVICE_GUI_FONT_DECODE_FORMAT_ERROR;
            }

            if ((bitmap_bytes + stored) >= RESOURCE_PACK_FONT_BITMAP_LIMIT_BYTES)
            {
                return SERVICE_GUI_FONT_DECODE_BITMAP_TOO_LARGE;
            }

            if (bitmap != NULL)
            {
                const uint8_t *source = entry + header_bytes;

                for (uint32_t k = 0U; k < stored; k++)
                {
                    uint32_t high = (uint32_t)source[k] << header_shift;
                    uint32_t low = ((header_shift != 0U) && ((k + 1U) < stored)) ? ((uint32_t)source[k + 1U] >> (8U - header_shift)) : 0U;

                    bitmap[bitmap_bytes + k] = (uint8_t)((high | low) & 0xFFU);
                }
            }

            bitmap_bytes += stored;
        }
    }

    plan->BitmapBytes = (uint32_t)bitmap_bytes;
    return SERVICE_GUI_FONT_DECODE_OK;
}

/**
 * @brief 定位并校验 kern 分段，并（给定输出块时）写出字距描述与数组。
 *
 * TablesCount 不小于 4 时 glyf 之后必须有 kern；支持格式 0（有序字距对，8 或 16 位字形编号）与
 * 格式 3（类别表）。类别映射在输出区补零到字形数，使 LVGL 以任意合法字形编号索引都不越界。
 * @param[in,out] plan 校验计划；校验时写 Kern* 字段。
 * @param[in] region 输出区首地址；NULL 表示只校验。
 * @return 通过返回 OK。
 */
static Service_GUI_FontDecodeStatusTypeDef gui_font_process_kern(GuiFont_PlanTypeDef *plan, uint8_t *region)
{
    const ResourcePack_FontBinLayoutTypeDef *layout = &plan->Layout;
    const uint8_t *kern;
    uint32_t kern_length;
    uint32_t format;

    if (layout->TablesCount < GUI_FONT_TABLES_WITH_KERN)
    {
        plan->KernKind = GUI_FONT_KERN_NONE;
        return SERVICE_GUI_FONT_DECODE_OK;
    }

    plan->KernOffset = layout->Glyf.Offset + layout->Glyf.Length;
    if (((uint64_t)plan->KernOffset + GUI_FONT_KERN_FIXED_SIZE) > plan->BinSize)
    {
        return SERVICE_GUI_FONT_DECODE_FORMAT_ERROR;
    }

    kern = plan->Bin + plan->KernOffset;
    kern_length = gui_font_read_u32(kern);
    if ((memcmp(kern + 4U, "kern", 4U) != 0) || (kern_length < GUI_FONT_KERN_FIXED_SIZE) ||
        (((uint64_t)plan->KernOffset + kern_length) > plan->BinSize))
    {
        return SERVICE_GUI_FONT_DECODE_FORMAT_ERROR;
    }

    format = kern[GUI_FONT_SECTION_PREFIX];
    if (format == GUI_FONT_KERN_FORMAT_PAIRS)
    {
        uint32_t pairs = gui_font_read_u32(kern + GUI_FONT_SECTION_PREFIX + 4U);
        uint32_t id_width = (layout->GlyphIdFormat == 0U) ? 2U : 4U;

        if ((pairs > GUI_FONT_KERN_PAIR_MAX) ||
            (((uint64_t)GUI_FONT_KERN_FIXED_SIZE + ((uint64_t)pairs * (id_width + 1U))) > kern_length))
        {
            return SERVICE_GUI_FONT_DECODE_FORMAT_ERROR;
        }

        plan->KernKind = GUI_FONT_KERN_PAIRS;
        plan->KernPairs = pairs;
        plan->KernIdBytes = pairs * id_width;

        if (region != NULL)
        {
            lv_font_fmt_txt_kern_pair_t *dsc = (lv_font_fmt_txt_kern_pair_t *)(void *)(region + plan->OffKernDsc);
            uint8_t *ids = region + plan->OffKernA;
            int8_t *values = (int8_t *)(void *)(region + plan->OffKernB);

            memcpy(ids, kern + GUI_FONT_KERN_FIXED_SIZE, plan->KernIdBytes);
            memcpy(values, kern + GUI_FONT_KERN_FIXED_SIZE + plan->KernIdBytes, pairs);
            dsc->glyph_ids = ids;
            dsc->values = values;
            dsc->pair_cnt = pairs;
            dsc->glyph_ids_size = layout->GlyphIdFormat;
        }
    }
    else if (format == GUI_FONT_KERN_FORMAT_CLASSES)
    {
        uint32_t mapping_length = gui_font_read_u16(kern + GUI_FONT_SECTION_PREFIX + 4U);
        uint32_t rows = kern[GUI_FONT_SECTION_PREFIX + 6U];
        uint32_t cols = kern[GUI_FONT_SECTION_PREFIX + 7U];
        const uint8_t *left = kern + GUI_FONT_KERN_FIXED_SIZE;
        const uint8_t *right = left + mapping_length;
        const uint8_t *values = right + mapping_length;

        if (((uint64_t)GUI_FONT_KERN_FIXED_SIZE + (2ULL * mapping_length) + ((uint64_t)rows * cols)) > kern_length)
        {
            return SERVICE_GUI_FONT_DECODE_FORMAT_ERROR;
        }

        for (uint32_t index = 0U; index < mapping_length; index++)
        {
            if ((left[index] > rows) || (right[index] > cols))
            {
                return SERVICE_GUI_FONT_DECODE_FORMAT_ERROR;
            }
        }

        plan->KernKind = GUI_FONT_KERN_CLASSES;
        plan->KernMappingLength = mapping_length;
        plan->KernMappingBytes = (mapping_length > layout->GlyphCount) ? mapping_length : layout->GlyphCount;
        plan->KernRows = rows;
        plan->KernCols = cols;

        if (region != NULL)
        {
            lv_font_fmt_txt_kern_classes_t *dsc =
                (lv_font_fmt_txt_kern_classes_t *)(void *)(region + plan->OffKernDsc);
            uint8_t *left_out = region + plan->OffKernA;
            uint8_t *right_out = region + plan->OffKernB;
            int8_t *values_out = (int8_t *)(void *)(region + plan->OffKernC);

            memset(left_out, 0, plan->KernMappingBytes);
            memset(right_out, 0, plan->KernMappingBytes);
            memcpy(left_out, left, mapping_length);
            memcpy(right_out, right, mapping_length);
            memcpy(values_out, values, rows * cols);
            dsc->class_pair_values = values_out;
            dsc->left_class_mapping = left_out;
            dsc->right_class_mapping = right_out;
            dsc->left_class_cnt = (uint8_t)rows;
            dsc->right_class_cnt = (uint8_t)cols;
        }
    }
    else
    {
        return SERVICE_GUI_FONT_DECODE_UNSUPPORTED;
    }

    return SERVICE_GUI_FONT_DECODE_OK;
}

/**
 * @brief 在校验结果上排出输出区内各块的偏移与总大小。
 * @param[in,out] plan 校验计划。
 * @return 总大小在 32 位内返回 OK。
 */
static Service_GUI_FontDecodeStatusTypeDef gui_font_layout_region(GuiFont_PlanTypeDef *plan)
{
    uint64_t cursor = 0U;
    uint32_t cmap_count = gui_font_read_u32(plan->Bin + plan->Layout.Cmap.Offset + GUI_FONT_SECTION_PREFIX);

    plan->OffDsc = (uint32_t)cursor;
    cursor += gui_font_align_up(sizeof(lv_font_fmt_txt_dsc_t));
    plan->OffCmaps = (uint32_t)cursor;
    cursor += gui_font_align_up((uint64_t)cmap_count * sizeof(lv_font_fmt_txt_cmap_t));
    plan->OffLists = (uint32_t)cursor;
    cursor += gui_font_align_up(plan->ListBytes);
    plan->OffGlyphs = (uint32_t)cursor;
    cursor += gui_font_align_up((uint64_t)plan->Layout.GlyphCount * sizeof(lv_font_fmt_txt_glyph_dsc_t));

    if (plan->KernKind == GUI_FONT_KERN_PAIRS)
    {
        plan->OffKernDsc = (uint32_t)cursor;
        cursor += gui_font_align_up(sizeof(lv_font_fmt_txt_kern_pair_t));
        plan->OffKernA = (uint32_t)cursor;
        cursor += gui_font_align_up(plan->KernIdBytes);
        plan->OffKernB = (uint32_t)cursor;
        cursor += gui_font_align_up(plan->KernPairs);
    }
    else if (plan->KernKind == GUI_FONT_KERN_CLASSES)
    {
        plan->OffKernDsc = (uint32_t)cursor;
        cursor += gui_font_align_up(sizeof(lv_font_fmt_txt_kern_classes_t));
        plan->OffKernA = (uint32_t)cursor;
        cursor += gui_font_align_up(plan->KernMappingBytes);
        plan->OffKernB = (uint32_t)cursor;
        cursor += gui_font_align_up(plan->KernMappingBytes);
        plan->OffKernC = (uint32_t)cursor;
        cursor += gui_font_align_up((uint64_t)plan->KernRows * plan->KernCols);
    }

    plan->OffBitmap = (uint32_t)cursor;
    cursor += gui_font_align_up(plan->BitmapBytes);

    if (cursor > 0x7FFFFFFFU)
    {
        return SERVICE_GUI_FONT_DECODE_FORMAT_ERROR;
    }

    plan->Total = (uint32_t)cursor;
    return SERVICE_GUI_FONT_DECODE_OK;
}

/**
 * @brief 完整校验输入并排出输出区布局，不写输出区。
 * @param[in] bin bin 首地址。
 * @param[in] bin_size bin 长度。
 * @param[out] plan 接收校验计划。
 * @return 输入合法返回 OK。
 */
static Service_GUI_FontDecodeStatusTypeDef gui_font_plan(const uint8_t *bin, uint32_t bin_size, GuiFont_PlanTypeDef *plan)
{
    Service_GUI_FontDecodeStatusTypeDef status;
    ResourcePack_StatusTypeDef located;

    if ((bin == NULL) || (bin_size == 0U) || (plan == NULL))
    {
        return SERVICE_GUI_FONT_DECODE_INVALID_PARAM;
    }

    memset(plan, 0, sizeof(*plan));
    plan->Bin = bin;
    plan->BinSize = bin_size;

    located = ResourcePack_LocateFontBin(bin, bin_size, &plan->Layout);
    if (located != RESOURCE_PACK_OK)
    {
        return (located == RESOURCE_PACK_INVALID_PARAM) ? SERVICE_GUI_FONT_DECODE_INVALID_PARAM
                                                       : SERVICE_GUI_FONT_DECODE_FORMAT_ERROR;
    }

    status = gui_font_check_head(plan);
    if (status == SERVICE_GUI_FONT_DECODE_OK)
    {
        status = gui_font_process_cmaps(plan, NULL);
    }

    if (status == SERVICE_GUI_FONT_DECODE_OK)
    {
        status = gui_font_process_glyphs(plan, NULL);
    }

    if (status == SERVICE_GUI_FONT_DECODE_OK)
    {
        status = gui_font_process_kern(plan, NULL);
    }

    if (status == SERVICE_GUI_FONT_DECODE_OK)
    {
        status = gui_font_layout_region(plan);
    }

    return status;
}

/**
 * @brief 把校验计划的规模摘要写给调用者。
 * @param[in] plan 校验计划。
 * @param[out] info 接收摘要；可为 NULL。
 */
static void gui_font_fill_info(const GuiFont_PlanTypeDef *plan, Service_GUI_FontDecodeInfoTypeDef *info)
{
    if (info != NULL)
    {
        info->RegionSize = plan->Total;
        info->GlyphCount = plan->Layout.GlyphCount;
        info->CmapCount = gui_font_read_u32(plan->Bin + plan->Layout.Cmap.Offset + GUI_FONT_SECTION_PREFIX);
        info->BitmapBytes = plan->BitmapBytes;
        info->KernPairCount = (plan->KernKind == GUI_FONT_KERN_PAIRS) ? plan->KernPairs : 0U;
    }
}

/**
 * @brief 校验 bin 并给出解码所需的输出区大小与字体规模，不写任何输出。
 * @param[in] bin bin 首地址，调用者保证可读 bin_size 字节；解码器只读。
 * @param[in] bin_size bin 长度。
 * @param[out] info 接收规模摘要，RegionSize 为 Build 所需的输出区字节数。
 * @return 输入合法返回 SERVICE_GUI_FONT_DECODE_OK；否则返回失败原因，info 内容未定义。
 */
Service_GUI_FontDecodeStatusTypeDef service_gui_font_decode_measure(
    const uint8_t *bin,
    uint32_t bin_size,
    Service_GUI_FontDecodeInfoTypeDef *info)
{
    GuiFont_PlanTypeDef plan;
    Service_GUI_FontDecodeStatusTypeDef status;

    if (info == NULL)
    {
        return SERVICE_GUI_FONT_DECODE_INVALID_PARAM;
    }

    status = gui_font_plan(bin, bin_size, &plan);
    if (status == SERVICE_GUI_FONT_DECODE_OK)
    {
        gui_font_fill_info(&plan, info);
    }

    return status;
}

/**
 * @brief 解码 bin：在 region 内建立字形描述、cmap、kern 与重排位图，并填写 lv_font_t。
 *
 * 先完整校验，再检查容量，之后才写 region；因此任何失败都发生在写入之前，region 与 font 保持原样。
 * 成功后 region 必须在字体使用期间保持有效且不被改写；bin 在解码完成后可以释放。
 * 只可在 GUI Task 调用；不使用 LVGL 堆，不访问硬件，不记日志。
 * @param[in] bin bin 首地址，调用者保证可读 bin_size 字节；解码器只读。
 * @param[in] bin_size bin 长度。
 * @param[out] region 输出区首地址，须按 SERVICE_GUI_FONT_DECODE_REGION_ALIGN 对齐。
 * @param[in] region_size 输出区容量，不小于 Measure 给出的 RegionSize。
 * @param[out] font 接收字体对象，须由调用者持有；其 dsc 指向 region 内。
 * @param[out] info 接收规模摘要；可为 NULL。
 * @return 成功返回 SERVICE_GUI_FONT_DECODE_OK；输出区不足返回 OUTPUT_TOO_SMALL；其余为输入问题。
 */
Service_GUI_FontDecodeStatusTypeDef service_gui_font_decode_build(
    const uint8_t *bin,
    uint32_t bin_size,
    void *region,
    uint32_t region_size,
    lv_font_t *font,
    Service_GUI_FontDecodeInfoTypeDef *info)
{
    GuiFont_PlanTypeDef plan;
    Service_GUI_FontDecodeStatusTypeDef status;
    uint8_t *base = (uint8_t *)region;
    lv_font_fmt_txt_dsc_t *dsc;

    if ((region == NULL) || (font == NULL) ||
        (((uintptr_t)region % SERVICE_GUI_FONT_DECODE_REGION_ALIGN) != 0U))
    {
        return SERVICE_GUI_FONT_DECODE_INVALID_PARAM;
    }

    status = gui_font_plan(bin, bin_size, &plan);
    if (status != SERVICE_GUI_FONT_DECODE_OK)
    {
        return status;
    }

    if (region_size < plan.Total)
    {
        return SERVICE_GUI_FONT_DECODE_OUTPUT_TOO_SMALL;
    }

    status = gui_font_process_cmaps(&plan, base);
    if (status == SERVICE_GUI_FONT_DECODE_OK)
    {
        status = gui_font_process_glyphs(&plan, base);
    }

    if (status == SERVICE_GUI_FONT_DECODE_OK)
    {
        status = gui_font_process_kern(&plan, base);
    }

    if (status != SERVICE_GUI_FONT_DECODE_OK)
    {
        return status;
    }

    dsc = (lv_font_fmt_txt_dsc_t *)(void *)(base + plan.OffDsc);
    memset(dsc, 0, sizeof(*dsc));
    dsc->glyph_bitmap = base + plan.OffBitmap;
    dsc->glyph_dsc = (const lv_font_fmt_txt_glyph_dsc_t *)(const void *)(base + plan.OffGlyphs);
    dsc->cmaps = (const lv_font_fmt_txt_cmap_t *)(const void *)(base + plan.OffCmaps);
    dsc->cmap_num = (uint16_t)gui_font_read_u32(plan.Bin + plan.Layout.Cmap.Offset + GUI_FONT_SECTION_PREFIX);
    dsc->bpp = plan.Layout.BitsPerPixel;
    dsc->bitmap_format = LV_FONT_FMT_TXT_PLAIN;
    dsc->cache = NULL;

    if (plan.KernKind != GUI_FONT_KERN_NONE)
    {
        dsc->kern_dsc = base + plan.OffKernDsc;
        dsc->kern_scale = plan.Layout.KerningScale;
        dsc->kern_classes = (plan.KernKind == GUI_FONT_KERN_CLASSES) ? 1U : 0U;
    }

    memset(font, 0, sizeof(*font));
    font->get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt;
    font->get_glyph_bitmap = lv_font_get_bitmap_fmt_txt;
    font->line_height = (lv_coord_t)((int32_t)plan.Layout.Ascent - (int32_t)plan.Layout.Descent);
    font->base_line = (lv_coord_t)(-(int32_t)plan.Layout.Descent);
    font->subpx = LV_FONT_SUBPX_NONE;
    font->underline_position = (int8_t)plan.Layout.UnderlinePosition;
    font->underline_thickness = (int8_t)plan.Layout.UnderlineThickness;
    font->dsc = dsc;
    font->fallback = NULL;

    gui_font_fill_info(&plan, info);
    return SERVICE_GUI_FONT_DECODE_OK;
}

/**
 * @brief 把解码结果转成简短英文原因，供启动日志使用。
 * @param[in] status 解码结果。
 * @return 静态字符串，不为 NULL。
 */
const char *service_gui_font_decode_status_text(Service_GUI_FontDecodeStatusTypeDef status)
{
    switch (status)
    {
    case SERVICE_GUI_FONT_DECODE_OK:
        return "ok";
    case SERVICE_GUI_FONT_DECODE_INVALID_PARAM:
        return "invalid param";
    case SERVICE_GUI_FONT_DECODE_FORMAT_ERROR:
        return "bad format";
    case SERVICE_GUI_FONT_DECODE_UNSUPPORTED:
        return "unsupported";
    case SERVICE_GUI_FONT_DECODE_CODEPOINT_RANGE:
        return "codepoint range";
    case SERVICE_GUI_FONT_DECODE_BITMAP_TOO_LARGE:
        return "bitmap too large";
    case SERVICE_GUI_FONT_DECODE_OUTPUT_TOO_SMALL:
        return "output too small";
    default:
        return "unknown";
    }
}
