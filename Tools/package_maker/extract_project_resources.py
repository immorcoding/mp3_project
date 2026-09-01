"""从当前 MP3 工程源码提取需要放入外部 NOR Flash 的资源。"""

from pathlib import Path
import re
import struct


EXPECTED_CP_TABLE_BYTES = 87172
EXPECTED_WALLPAPER_BYTES = 240 * 320 * 3

CP936_SOURCE_PATH = (
    Path("Middlewares")
    / "Third_Party"
    / "FatFs"
    / "src"
    / "option"
    / "cc936.c"
)

WALLPAPER_SOURCE_PATH = (
    Path("GUI")
    / "images"
    / "ui_img_wallpaper_indigo_mist_soft_dark_png.c"
)

WALLPAPER_ARRAY_NAME = "ui_img_wallpaper_indigo_mist_soft_dark_png_data"


def remove_comments(text: str) -> str:
    """移除 C 源码中的块注释和行注释，避免把注释数字当成数组元素。"""

    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//.*?$", "", text, flags=re.M)


def extract_array_body(source: str, declaration_pattern: str) -> str:
    """依据声明正则表达式返回 C 数组初始化体。"""

    match = re.search(
        declaration_pattern + r"\s*=\s*\{(.*?)\};",
        source,
        flags=re.S,
    )

    if match is None:
        raise RuntimeError(f"没有找到数组：{declaration_pattern}")

    return remove_comments(match.group(1))


def extract_wchar_array(source: str, name: str) -> bytes:
    """提取 WCHAR 数组并以显式小端 uint16 格式编码。"""

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
    """提取以十六进制字面量初始化的 uint8_t 数组。"""

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


def extract_project_resources(
    project_root: Path,
    output_directory: Path,
) -> dict[str, Path]:
    """提取 CP936 转换表和默认壁纸，并返回各输出文件路径。"""

    cp936_source = (project_root / CP936_SOURCE_PATH).read_text(encoding="utf-8")
    wallpaper_source = (project_root / WALLPAPER_SOURCE_PATH).read_text(encoding="utf-8")

    resources = {
        "uni2oem": extract_wchar_array(cp936_source, "uni2oem"),
        "oem2uni": extract_wchar_array(cp936_source, "oem2uni"),
        "wallpaper": extract_byte_array(wallpaper_source, WALLPAPER_ARRAY_NAME),
    }

    if len(resources["wallpaper"]) != EXPECTED_WALLPAPER_BYTES:
        raise RuntimeError(
            f"壁纸长度错误：{len(resources['wallpaper'])}，"
            f"预期 {EXPECTED_WALLPAPER_BYTES}"
        )

    output_directory.mkdir(parents=True, exist_ok=True)
    output_paths: dict[str, Path] = {}

    for resource_name, data in resources.items():
        output_path = output_directory / f"{resource_name}.bin"
        output_path.write_bytes(data)
        output_paths[resource_name] = output_path

    return output_paths
