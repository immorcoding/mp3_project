from pathlib import Path
import binascii
import json
import re
import struct


PROJECT = Path(r"E:\Projects\project_mp3\version0.4.0")

CP936_SOURCE = (
    PROJECT
    / "Middlewares"
    / "Third_Party"
    / "FatFs"
    / "src"
    / "option"
    / "cc936.c"
)

WALLPAPER_SOURCE = (
    PROJECT
    / "GUI"
    / "images"
    / "ui_img_wallpaper_indigo_mist_soft_dark_png.c"
)

OUTPUT_DIRECTORY = PROJECT / "build" / "external-resources"
OUTPUT_BINARY = OUTPUT_DIRECTORY / "resource_pack.bin"
OUTPUT_MANIFEST = OUTPUT_DIRECTORY / "resource_pack.json"

HEADER_SIZE = 0x1000
UNI2OEM_OFFSET = 0x01000
OEM2UNI_OFFSET = 0x16484
WALLPAPER_OFFSET = 0x2C000
PACK_SIZE = 0x65000

EXPECTED_CP_TABLE_BYTES = 87172
EXPECTED_WALLPAPER_BYTES = 240 * 320 * 3

RESOURCE_MAGIC = b"RPK1"
RESOURCE_FORMAT_VERSION = 1

# 自定义像素格式编号：
# 当前 SquareLine 数组是 LVGL 8.3.11、240×320 的 TRUE_COLOR_ALPHA 数据。
PIXEL_FORMAT_LVGL_TRUE_COLOR_ALPHA = 1


def remove_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    text = re.sub(r"//.*?$", "", text, flags=re.M)
    return text


def extract_array_body(source: str, declaration_pattern: str) -> str:
    match = re.search(
        declaration_pattern + r"\s*=\s*\{(.*?)\};",
        source,
        flags=re.S,
    )

    if match is None:
        raise RuntimeError(f"没有找到数组：{declaration_pattern}")

    return remove_comments(match.group(1))


def extract_wchar_array(source: str, name: str) -> bytes:
    body = extract_array_body(
        source,
        rf"(?:static\s*)?const\s+WCHAR\s+{re.escape(name)}\s*\[\s*\]",
    )

    values = [
        int(value, 0)
        for value in re.findall(
            r"\b(?:0[xX][0-9A-Fa-f]+|[0-9]+)\b",
            body,
        )
    ]

    for value in values:
        if value > 0xFFFF:
            raise RuntimeError(f"{name} 中存在超出 WCHAR 的数值：{value:#x}")

    data = struct.pack(f"<{len(values)}H", *values)

    if len(data) != EXPECTED_CP_TABLE_BYTES:
        raise RuntimeError(
            f"{name} 长度错误：{len(data)}，"
            f"预期 {EXPECTED_CP_TABLE_BYTES}"
        )

    return data


def extract_byte_array(source: str, name: str) -> bytes:
    body = extract_array_body(
        source,
        rf"const.*?uint8_t\s+{re.escape(name)}\s*\[\s*\]",
    )

    values = [
        int(value, 16)
        for value in re.findall(r"0x[0-9A-Fa-f]+", body)
    ]

    for value in values:
        if value > 0xFF:
            raise RuntimeError(f"{name} 中存在超出 uint8_t 的数值")

    return bytes(values)


def crc32(data: bytes) -> int:
    return binascii.crc32(data) & 0xFFFFFFFF


def main() -> None:
    cp936_source = CP936_SOURCE.read_text(encoding="utf-8")
    wallpaper_source = WALLPAPER_SOURCE.read_text(encoding="utf-8")

    uni2oem = extract_wchar_array(cp936_source, "uni2oem")
    oem2uni = extract_wchar_array(cp936_source, "oem2uni")

    wallpaper = extract_byte_array(
        wallpaper_source,
        "ui_img_wallpaper_indigo_mist_soft_dark_png_data",
    )

    if len(wallpaper) != EXPECTED_WALLPAPER_BYTES:
        raise RuntimeError(
            f"壁纸长度错误：{len(wallpaper)}，"
            f"预期 {EXPECTED_WALLPAPER_BYTES}"
        )

    assert UNI2OEM_OFFSET + len(uni2oem) == OEM2UNI_OFFSET
    assert OEM2UNI_OFFSET + len(oem2uni) <= WALLPAPER_OFFSET
    assert WALLPAPER_OFFSET + len(wallpaper) <= PACK_SIZE

    image = bytearray([0xFF]) * PACK_SIZE

    image[UNI2OEM_OFFSET:UNI2OEM_OFFSET + len(uni2oem)] = uni2oem
    image[OEM2UNI_OFFSET:OEM2UNI_OFFSET + len(oem2uni)] = oem2uni
    image[WALLPAPER_OFFSET:WALLPAPER_OFFSET + len(wallpaper)] = wallpaper

    header = bytearray([0xFF]) * HEADER_SIZE

    # 资源包头采用小端编码。
    struct.pack_into(
        "<4s15I",
        header,
        0,
        RESOURCE_MAGIC,
        RESOURCE_FORMAT_VERSION,
        HEADER_SIZE,
        PACK_SIZE,
        UNI2OEM_OFFSET,
        len(uni2oem),
        crc32(uni2oem),
        OEM2UNI_OFFSET,
        len(oem2uni),
        crc32(oem2uni),
        WALLPAPER_OFFSET,
        len(wallpaper),
        crc32(wallpaper),
        240,
        320,
        PIXEL_FORMAT_LVGL_TRUE_COLOR_ALPHA,
    )

    # 与当前 FTL 元数据风格一致：CRC 位于头页 252～255，
    # 覆盖头页前 252 字节。
    struct.pack_into("<I", header, 252, crc32(header[:252]))

    image[:HEADER_SIZE] = header

    OUTPUT_DIRECTORY.mkdir(parents=True, exist_ok=True)
    OUTPUT_BINARY.write_bytes(image)

    manifest = {
        "magic": RESOURCE_MAGIC.decode("ascii"),
        "format_version": RESOURCE_FORMAT_VERSION,
        "nor_chip_offset": "0x00401000",
        "mapped_address_if_base_is_0x90000000": "0x90401000",
        "pack_size": PACK_SIZE,
        "pack_size_hex": f"0x{PACK_SIZE:X}",
        "header_crc32": f"0x{crc32(header[:252]):08X}",
        "resources": {
            "uni2oem": {
                "offset": f"0x{UNI2OEM_OFFSET:X}",
                "length": len(uni2oem),
                "crc32": f"0x{crc32(uni2oem):08X}",
            },
            "oem2uni": {
                "offset": f"0x{OEM2UNI_OFFSET:X}",
                "length": len(oem2uni),
                "crc32": f"0x{crc32(oem2uni):08X}",
            },
            "wallpaper": {
                "offset": f"0x{WALLPAPER_OFFSET:X}",
                "length": len(wallpaper),
                "width": 240,
                "height": 320,
                "bytes_per_pixel": 3,
                "crc32": f"0x{crc32(wallpaper):08X}",
            },
        },
    }

    OUTPUT_MANIFEST.write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2),
        encoding="utf-8",
    )

    print(f"已生成：{OUTPUT_BINARY}")
    print(f"资源包长度：{len(image)} B / 0x{len(image):X}")
    print(f"uni2oem CRC32：0x{crc32(uni2oem):08X}")
    print(f"oem2uni CRC32：0x{crc32(oem2uni):08X}")
    print(f"wallpaper CRC32：0x{crc32(wallpaper):08X}")


if __name__ == "__main__":
    main()