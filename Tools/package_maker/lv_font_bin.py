"""lv_font_conv 1.5.3 未压缩 bin 的结构核对，供 FONT Metadata V1 取值与上限检查。

统计口径与 LVGL 8.3 的 ``lv_font_load`` 及 ``Components/resource_pack`` 的
``ResourcePack_DecodeFontMetadata`` 一致：字形 0（notdef）不计，盒子宽或高为 0 的字形不计，
其余字形的位图字节数为条目长度减去整字节的字形头。cmap 与 kern 的内容由消费者解析，这里只跳过。
"""

from dataclasses import dataclass
import struct


SECTION_PREFIX_SIZE = 8
HEAD_SIZE = 48
LOCA_FIXED_SIZE = 12
MAX_HEADER_BITS = 128
MAX_BOX_BITS = 16

# LV_FONT_FMT_TXT_LARGE = 0 时字形位图索引只有 20 位，位图总量必须小于 1 MiB。
BITMAP_LIMIT_BYTES = 1 << 20


@dataclass(frozen=True)
class LvFontBinInfo:
    """从 bin 核对出的字体事实。"""

    glyph_count: int
    pixel_height: int
    ascent: int
    descent: int
    bitmap_bytes: int


def find_section(data: bytes, offset: int, label: bytes) -> tuple[int, int]:
    """校验并返回 (起点, 长度)；标签或范围不符时抛出 ValueError。"""

    if offset + SECTION_PREFIX_SIZE > len(data) or data[offset + 4:offset + 8] != label:
        raise ValueError(f"字体 bin 缺少 {label.decode('ascii')} 分段")

    length = struct.unpack_from("<I", data, offset)[0]

    if length < SECTION_PREFIX_SIZE or offset + length > len(data):
        raise ValueError(f"字体 bin 的 {label.decode('ascii')} 分段长度非法")

    return offset, length


def read_bits(data: bytes, byte_offset: int, bit_position: int, count: int) -> int:
    """按 MSB 优先从 byte_offset 起的位流读取 count 位。"""

    value = 0

    for index in range(count):
        position = bit_position + index
        bit = (data[byte_offset + position // 8] >> (7 - position % 8)) & 1
        value = (value << 1) | bit

    return value


def parse_lv_font_bin(data: bytes) -> LvFontBinInfo:
    """解析 bin 的 head、cmap、loca、glyf 分段，返回字形数、字号、行高与位图总量。

    Raises:
        ValueError: bin 结构不完整或不一致，或位图总量不小于 1 MiB。
    """

    head_offset, head_length = find_section(data, 0, b"head")

    if head_length < HEAD_SIZE:
        raise ValueError("字体 bin 的 head 分段过短")

    pixel_height, ascent, descent = struct.unpack_from("<HHh", data, 0x0E)
    index_to_loc_format = data[0x22]
    xy_bits = data[0x26]
    wh_bits = data[0x27]
    advance_bits = data[0x28]
    header_bits = advance_bits + 2 * xy_bits + 2 * wh_bits

    if (
        index_to_loc_format not in (0, 1)
        or wh_bits > MAX_BOX_BITS
        or header_bits > MAX_HEADER_BITS
    ):
        raise ValueError("字体 bin 的 head 参数非法")

    header_bytes = (header_bits + 7) // 8
    box_bit_position = advance_bits + 2 * xy_bits
    loca_entry_size = 2 if index_to_loc_format == 0 else 4

    cmap_offset, cmap_length = find_section(data, head_length, b"cmap")
    loca_offset, loca_length = find_section(data, cmap_offset + cmap_length, b"loca")

    if loca_length < LOCA_FIXED_SIZE:
        raise ValueError("字体 bin 的 loca 分段过短")

    glyf_offset, glyf_length = find_section(data, loca_offset + loca_length, b"glyf")
    glyph_count = struct.unpack_from("<I", data, loca_offset + 8)[0]

    if glyph_count == 0 or LOCA_FIXED_SIZE + glyph_count * loca_entry_size > loca_length:
        raise ValueError("字体 bin 的 loca 条目数非法")

    loca_format = "<H" if loca_entry_size == 2 else "<I"
    offsets = [
        struct.unpack_from(loca_format, data, loca_offset + LOCA_FIXED_SIZE + index * loca_entry_size)[0]
        for index in range(glyph_count)
    ]
    bitmap_bytes = 0

    for index in range(glyph_count):
        offset = offsets[index]
        next_offset = offsets[index + 1] if index + 1 < glyph_count else glyf_length

        if offset > next_offset or next_offset > glyf_length:
            raise ValueError("字体 bin 的 loca 偏移非法")

        # 字形 0（notdef）被 LVGL 置空，lv_font_conv 为它写入零长度条目，不要求有字形头。
        if index == 0:
            continue

        if next_offset - offset < header_bytes:
            raise ValueError("字体 bin 的字形条目短于字形头")

        glyph = glyf_offset + offset
        box_width = read_bits(data, glyph, box_bit_position, wh_bits)
        box_height = read_bits(data, glyph, box_bit_position + wh_bits, wh_bits)

        if box_width * box_height:
            bitmap_bytes += next_offset - offset - header_bits // 8

            if bitmap_bytes >= BITMAP_LIMIT_BYTES:
                raise ValueError(
                    f"字体位图总量不得达到 {BITMAP_LIMIT_BYTES} B（1 MiB）：LV_FONT_FMT_TXT_LARGE = 0 时位图索引只有 20 位"
                )

    return LvFontBinInfo(
        glyph_count=glyph_count,
        pixel_height=pixel_height,
        ascent=ascent,
        descent=descent,
        bitmap_bytes=bitmap_bytes,
    )
