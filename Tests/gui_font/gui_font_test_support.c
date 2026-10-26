/**
 ******************************************************************************
 * @file    gui_font_test_support.c
 * @brief   gui_font 主机测试的共用工具实现。
 ******************************************************************************
 */

#include "gui_font_test_support.h"

#include <assert.h>
#include <string.h>

#define GFT_HEAD_SIZE        48U
#define GFT_CMAP_TABLE_ENTRY 16U

/**
 * @brief 按小端写入 16 位整数。
 * @param[out] destination 字段首地址。
 * @param[in] value 待写数值。
 */
void gft_put_u16(uint8_t *destination, uint16_t value)
{
    destination[0] = (uint8_t)value;
    destination[1] = (uint8_t)(value >> 8U);
}

/**
 * @brief 按小端写入 32 位整数。
 * @param[out] destination 字段首地址。
 * @param[in] value 待写数值。
 */
void gft_put_u32(uint8_t *destination, uint32_t value)
{
    gft_put_u16(destination, (uint16_t)value);
    gft_put_u16(destination + 2U, (uint16_t)(value >> 16U));
}

/**
 * @brief 在已有 CRC 上继续累加 CRC-32（IEEE 802.3，反射，初值与终值异或 0xFFFFFFFF）。
 * @param[in] crc 先前的 CRC，起始为 0。
 * @param[in] data 数据首地址。
 * @param[in] length 数据长度。
 * @return 更新后的 CRC。
 */
uint32_t gft_crc32_update(uint32_t crc, const void *data, size_t length)
{
    const uint8_t *bytes = (const uint8_t *)data;

    crc = ~crc;
    for (size_t index = 0U; index < length; index++)
    {
        crc ^= bytes[index];
        for (uint32_t bit = 0U; bit < 8U; bit++)
        {
            crc = (crc >> 1U) ^ (0xEDB88320U & (0U - (crc & 1U)));
        }
    }

    return ~crc;
}

/**
 * @brief 计算 CRC-32。
 * @param[in] data 数据首地址。
 * @param[in] length 数据长度。
 * @return CRC-32。
 */
uint32_t gft_crc32(const void *data, size_t length)
{
    return gft_crc32_update(0U, data, length);
}

/**
 * @brief 向位流写入 count 位（MSB 优先）。
 * @param[in,out] buffer 位流缓冲，调用者清零过。
 * @param[in,out] position 当前位序号。
 * @param[in] value 数值，仅低 count 位有效。
 * @param[in] count 位数。
 */
static void gft_put_bits(uint8_t *buffer, uint32_t *position, uint32_t value, uint32_t count)
{
    for (uint32_t index = 0U; index < count; index++)
    {
        uint32_t bit = (value >> (count - 1U - index)) & 1U;

        buffer[*position / 8U] |= (uint8_t)(bit << (7U - (*position % 8U)));
        (*position)++;
    }
}

/**
 * @brief 计算一个字形条目的字节数。
 * @param[in] head head 参数。
 * @param[in] glyph 字形。
 * @return 条目字节数。
 */
static uint32_t gft_entry_size(const GftHead *head, const GftGlyph *glyph)
{
    uint32_t header_bits = head->AdvBits + (2U * head->XyBits) + (2U * head->WhBits);

    return (header_bits + glyph->BitmapBits + 7U) / 8U;
}

/**
 * @brief 构造 cmap 分段正文（不含 8 字节分段前缀）。
 * @param[out] out 输出缓冲。
 * @param[in] cmaps 子表数组。
 * @param[in] count 子表数。
 * @return 正文字节数。
 */
size_t gft_build_cmap_payload(uint8_t *out, const GftCmap *cmaps, uint32_t count)
{
    size_t size = 4U + (count * GFT_CMAP_TABLE_ENTRY);

    gft_put_u32(out, count);
    for (uint32_t index = 0U; index < count; index++)
    {
        const GftCmap *cmap = &cmaps[index];
        uint8_t *entry = out + 4U + (index * GFT_CMAP_TABLE_ENTRY);
        uint8_t *data = out + size;

        gft_put_u32(entry + 0U, 8U + (uint32_t)size); /* 数据偏移相对分段起点，含 8 字节前缀。 */
        gft_put_u32(entry + 4U, cmap->RangeStart);
        gft_put_u16(entry + 8U, cmap->RangeLength);
        gft_put_u16(entry + 10U, cmap->GlyphIdStart);
        gft_put_u16(entry + 12U, cmap->Count);
        entry[14U] = cmap->Type;
        entry[15U] = 0U;

        switch (cmap->Type)
        {
        case LV_FONT_FMT_TXT_CMAP_FORMAT0_FULL:
            for (uint32_t item = 0U; item < cmap->Count; item++)
            {
                data[item] = (uint8_t)cmap->Ofs[item];
            }
            size += cmap->Count;
            break;
        case LV_FONT_FMT_TXT_CMAP_SPARSE_FULL:
        case LV_FONT_FMT_TXT_CMAP_SPARSE_TINY:
            for (uint32_t item = 0U; item < cmap->Count; item++)
            {
                gft_put_u16(data + (2U * item), cmap->List[item]);
            }
            size += 2U * cmap->Count;
            if (cmap->Type == LV_FONT_FMT_TXT_CMAP_SPARSE_FULL)
            {
                for (uint32_t item = 0U; item < cmap->Count; item++)
                {
                    gft_put_u16(data + (2U * cmap->Count) + (2U * item), cmap->Ofs[item]);
                }
                size += 2U * cmap->Count;
            }
            break;
        default:
            break;
        }
    }

    return size;
}

/**
 * @brief 写一个分段的前缀（总长度含前缀、4 字节标签）。
 * @param[out] destination 分段首地址。
 * @param[in] payload_size 正文字节数。
 * @param[in] label 4 字节标签。
 */
static void gft_put_section_prefix(uint8_t *destination, size_t payload_size, const char *label)
{
    gft_put_u32(destination, (uint32_t)(payload_size + 8U));
    memcpy(destination + 4U, label, 4U);
}

/**
 * @brief 按给定参数装配一个完整 lv_font_conv bin。
 *
 * 字形 0 为 notdef，写零长度条目；其余字形按 head 的位宽写字形头，再接位图位流并补齐到字节。
 * @param[out] out 输出缓冲，调用者保证足够大。
 * @param[in] head head 参数。
 * @param[in] cmap_payload cmap 分段正文。
 * @param[in] cmap_payload_size cmap 正文字节数。
 * @param[in] glyphs 字形数组，下标即字形编号，glyphs[0] 被忽略。
 * @param[in] glyph_count 字形数，含 notdef。
 * @param[in] kern_payload kern 分段正文；NULL 表示不写 kern。
 * @param[in] kern_payload_size kern 正文字节数。
 * @return 各分段起点与总长度。
 */
GftBinLayout gft_build_bin(
    uint8_t *out,
    const GftHead *head,
    const uint8_t *cmap_payload,
    size_t cmap_payload_size,
    const GftGlyph *glyphs,
    uint32_t glyph_count,
    const uint8_t *kern_payload,
    size_t kern_payload_size)
{
    GftBinLayout layout;
    uint32_t loca_entry = (head->LocFormat == 0U) ? 2U : 4U;
    uint32_t glyf_size = 0U;
    uint8_t *loca;
    uint8_t *glyf;
    uint32_t offset = 0U;

    memset(out, 0, GFT_HEAD_SIZE);
    gft_put_section_prefix(out, GFT_HEAD_SIZE - 8U, "head");
    gft_put_u32(out + 0x08U, 1U);
    gft_put_u16(out + 0x0CU, head->TablesCount);
    gft_put_u16(out + 0x0EU, 12U);
    gft_put_u16(out + 0x10U, head->Ascent);
    gft_put_u16(out + 0x12U, (uint16_t)head->Descent);
    gft_put_u16(out + 0x1EU, head->DefaultAdvance);
    gft_put_u16(out + 0x20U, head->KernScale);
    out[0x22U] = head->LocFormat;
    out[0x23U] = head->GlyphIdFormat;
    out[0x24U] = head->AdvFormat;
    out[0x25U] = head->Bpp;
    out[0x26U] = head->XyBits;
    out[0x27U] = head->WhBits;
    out[0x28U] = head->AdvBits;
    out[0x29U] = head->Compression;
    out[0x2AU] = head->Subpx;
    gft_put_u16(out + 0x2CU, (uint16_t)-2);
    gft_put_u16(out + 0x2EU, 1U);

    layout.CmapOffset = GFT_HEAD_SIZE;
    gft_put_section_prefix(out + layout.CmapOffset, cmap_payload_size, "cmap");
    memcpy(out + layout.CmapOffset + 8U, cmap_payload, cmap_payload_size);

    layout.LocaOffset = layout.CmapOffset + 8U + (uint32_t)cmap_payload_size;
    loca = out + layout.LocaOffset;
    gft_put_section_prefix(loca, 4U + (glyph_count * loca_entry), "loca");
    gft_put_u32(loca + 8U, glyph_count);

    layout.GlyfOffset = layout.LocaOffset + 8U + 4U + (glyph_count * loca_entry);
    glyf = out + layout.GlyfOffset;

    for (uint32_t index = 0U; index < glyph_count; index++)
    {
        uint32_t size = (index == 0U) ? 0U : gft_entry_size(head, &glyphs[index]);
        uint8_t *entry_loca = loca + 12U + (index * loca_entry);

        /* loca 偏移相对 glyf 分段起点，含 8 字节分段前缀。 */
        if (head->LocFormat == 0U)
        {
            gft_put_u16(entry_loca, (uint16_t)(8U + glyf_size));
        }
        else
        {
            gft_put_u32(entry_loca, 8U + glyf_size);
        }

        if (index != 0U)
        {
            const GftGlyph *glyph = &glyphs[index];
            uint8_t *entry = glyf + 8U + glyf_size;
            uint32_t position = 0U;
            uint32_t xy_mask = (head->XyBits >= 16U) ? 0xFFFFU : ((1U << head->XyBits) - 1U);

            memset(entry, 0, size);
            if (head->AdvBits != 0U)
            {
                gft_put_bits(entry, &position, glyph->Adv, head->AdvBits);
            }
            gft_put_bits(entry, &position, (uint32_t)glyph->Ox & xy_mask, head->XyBits);
            gft_put_bits(entry, &position, (uint32_t)glyph->Oy & xy_mask, head->XyBits);
            gft_put_bits(entry, &position, glyph->W, head->WhBits);
            gft_put_bits(entry, &position, glyph->H, head->WhBits);
            for (uint32_t bit = 0U; bit < glyph->BitmapBits; bit++)
            {
                uint32_t value = (glyph->Bitmap[bit / 8U] >> (7U - (bit % 8U))) & 1U;

                gft_put_bits(entry, &position, value, 1U);
            }
        }

        glyf_size += size;
    }

    gft_put_section_prefix(glyf, glyf_size, "glyf");
    offset = layout.GlyfOffset + 8U + glyf_size;
    layout.KernOffset = offset;

    if (kern_payload != NULL)
    {
        gft_put_section_prefix(out + offset, kern_payload_size, "kern");
        memcpy(out + offset + 8U, kern_payload, kern_payload_size);
        offset += 8U + (uint32_t)kern_payload_size;
    }

    layout.Length = offset;
    return layout;
}

/**
 * @brief 复刻 LVGL v8.3.11 lv_font_fmt_txt.c 的 get_glyph_dsc_id，含 `rcp > range_length` 的上界写法。
 *
 * 不使用 LVGL 的缓存；二分查找按 unicode_list 升序。越界读取由调用者的 ASan 式布局（输出区后留保护带）发现。
 * @param[in] font 解码得到的字体。
 * @param[in] letter Unicode 码点。
 * @return 字形编号，找不到为 0。
 */
uint32_t gft_glyph_id(const lv_font_t *font, uint32_t letter)
{
    const lv_font_fmt_txt_dsc_t *dsc = (const lv_font_fmt_txt_dsc_t *)font->dsc;

    if (letter == 0U)
    {
        return 0U;
    }

    for (uint16_t index = 0U; index < dsc->cmap_num; index++)
    {
        const lv_font_fmt_txt_cmap_t *cmap = &dsc->cmaps[index];
        uint32_t rcp = letter - cmap->range_start;
        uint32_t glyph_id = 0U;

        if (rcp > cmap->range_length)
        {
            continue;
        }

        if (cmap->type == LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY)
        {
            glyph_id = cmap->glyph_id_start + rcp;
        }
        else if (cmap->type == LV_FONT_FMT_TXT_CMAP_FORMAT0_FULL)
        {
            glyph_id = cmap->glyph_id_start + ((const uint8_t *)cmap->glyph_id_ofs_list)[rcp];
        }
        else
        {
            uint32_t low = 0U;
            uint32_t high = cmap->list_length;

            while (low < high)
            {
                uint32_t middle = low + ((high - low) / 2U);

                if (cmap->unicode_list[middle] == rcp)
                {
                    glyph_id = cmap->glyph_id_start + middle;
                    if (cmap->type == LV_FONT_FMT_TXT_CMAP_SPARSE_FULL)
                    {
                        glyph_id = cmap->glyph_id_start + ((const uint16_t *)cmap->glyph_id_ofs_list)[middle];
                    }
                    break;
                }

                if (cmap->unicode_list[middle] < rcp)
                {
                    low = middle + 1U;
                }
                else
                {
                    high = middle;
                }
            }
        }

        return glyph_id;
    }

    return 0U;
}

/**
 * @brief 按 LVGL 的数据含义取字距值；字距对用线性扫描，与 LVGL 的二分查找互为独立实现。
 * @param[in] font 解码得到的字体。
 * @param[in] gid_left 左字形编号。
 * @param[in] gid_right 右字形编号。
 * @param[in] glyph_count 字形总数；类别映射须能按任一合法字形编号索引。
 * @return 字距值（未缩放），没有字距返回 0。
 */
int gft_kern_value(const lv_font_t *font, uint32_t gid_left, uint32_t gid_right, uint32_t glyph_count)
{
    const lv_font_fmt_txt_dsc_t *dsc = (const lv_font_fmt_txt_dsc_t *)font->dsc;

    if (dsc->kern_dsc == NULL)
    {
        return 0;
    }

    if (dsc->kern_classes == 0U)
    {
        const lv_font_fmt_txt_kern_pair_t *kern = (const lv_font_fmt_txt_kern_pair_t *)dsc->kern_dsc;
        const uint8_t *ids = (const uint8_t *)kern->glyph_ids;
        uint32_t width = (kern->glyph_ids_size == 0U) ? 1U : 2U;

        for (uint32_t pair = 0U; pair < kern->pair_cnt; pair++)
        {
            const uint8_t *item = ids + (pair * 2U * width);
            uint32_t left = item[0];
            uint32_t right = item[width];

            if (width == 2U)
            {
                left |= (uint32_t)item[1] << 8U;
                right |= (uint32_t)item[3] << 8U;
            }

            if ((left == gid_left) && (right == gid_right))
            {
                return kern->values[pair];
            }
        }

        return 0;
    }
    else
    {
        const lv_font_fmt_txt_kern_classes_t *kern = (const lv_font_fmt_txt_kern_classes_t *)dsc->kern_dsc;
        uint8_t left_class;
        uint8_t right_class;

        assert(gid_left < glyph_count);
        assert(gid_right < glyph_count);
        left_class = kern->left_class_mapping[gid_left];
        right_class = kern->right_class_mapping[gid_right];
        if ((left_class == 0U) || (right_class == 0U))
        {
            return 0;
        }

        return kern->class_pair_values[((left_class - 1U) * kern->right_class_cnt) + (right_class - 1U)];
    }
}
