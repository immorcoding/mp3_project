"""从当前 MP3 工程源码提取需要放入外部 NOR Flash 的资源。"""

from dataclasses import dataclass
from pathlib import Path
import re
import struct


EXPECTED_CP_TABLE_BYTES = 87172
TRUE_COLOR_ALPHA_BYTES_PER_PIXEL = 3

CP936_SOURCE_PATH = (
    Path("Middlewares")
    / "Third_Party"
    / "FatFs"
    / "src"
    / "option"
    / "cc936.c"
)

IMAGE_SOURCE_DIRECTORY = Path("Resources") / "imgs"
UINT8_ARRAY_DECLARATION = re.compile(r"const.*?uint8_t\s+(\w+)\s*\[\s*\]", re.S)


@dataclass(frozen=True)
class ExtractedImage:
    """一次扫描得到的 LVGL TRUE_COLOR_ALPHA 图片。"""

    name: str
    source_path: Path
    output_path: Path
    width: int
    height: int


@dataclass(frozen=True)
class ExtractedProjectResources:
    """CP936 表与按文件名排序的图片扫描结果。"""

    binaries: dict[str, Path]
    images: list[ExtractedImage]


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


def extract_first_byte_array(source: str) -> tuple[str, bytes]:
    """提取文件中第一个 uint8_t 像素数组。"""

    match = UINT8_ARRAY_DECLARATION.search(source)
    if match is None:
        raise RuntimeError("没有找到 uint8_t 数组")

    name = match.group(1)
    return name, extract_byte_array(source, name)


def parse_true_color_alpha_size(source: str, source_name: str) -> tuple[int, int]:
    """从 lv_img_dsc_t 读取宽高，并要求 cf 为 TRUE_COLOR_ALPHA。"""

    width_match = re.search(r"\.header\.w\s*=\s*(\d+)", source)
    height_match = re.search(r"\.header\.h\s*=\s*(\d+)", source)
    cf_match = re.search(r"\.header\.cf\s*=\s*(\w+)", source)

    if width_match is None or height_match is None:
        raise RuntimeError(f"{source_name} 缺少宽高描述")

    if cf_match is None or cf_match.group(1) != "LV_IMG_CF_TRUE_COLOR_ALPHA":
        raise RuntimeError(f"{source_name} 不是 TRUE_COLOR_ALPHA")

    return int(width_match.group(1)), int(height_match.group(1))


def list_image_sources(project_root: Path) -> list[Path]:
    """按文件名排序列出 Resources/imgs 下的 .c；忽略 PNG。"""

    directory = project_root / IMAGE_SOURCE_DIRECTORY
    if not directory.is_dir():
        raise RuntimeError(f"图片源目录不存在：{directory}")

    return sorted(path for path in directory.glob("*.c") if path.is_file())


def extract_project_resources(
    project_root: Path,
    output_directory: Path,
) -> ExtractedProjectResources:
    """提取 CP936 转换表，并扫描 Resources/imgs 中的 C 数组。"""

    cp936_source = (project_root / CP936_SOURCE_PATH).read_text(encoding="utf-8")
    binaries_data = {
        "uni2oem": extract_wchar_array(cp936_source, "uni2oem"),
        "oem2uni": extract_wchar_array(cp936_source, "oem2uni"),
    }

    output_directory.mkdir(parents=True, exist_ok=True)
    binaries: dict[str, Path] = {}

    for resource_name, data in binaries_data.items():
        output_path = output_directory / f"{resource_name}.bin"
        output_path.write_bytes(data)
        binaries[resource_name] = output_path

    images: list[ExtractedImage] = []

    for source_path in list_image_sources(project_root):
        source = source_path.read_text(encoding="utf-8")
        _array_name, data = extract_first_byte_array(source)
        width, height = parse_true_color_alpha_size(source, source_path.name)
        expected_bytes = width * height * TRUE_COLOR_ALPHA_BYTES_PER_PIXEL

        if len(data) != expected_bytes:
            raise RuntimeError(
                f"{source_path.name} 长度错误：{len(data)}，"
                f"预期 {expected_bytes}"
            )

        output_path = output_directory / f"{source_path.stem}.bin"
        output_path.write_bytes(data)
        images.append(
            ExtractedImage(
                name=source_path.stem,
                source_path=source_path,
                output_path=output_path,
                width=width,
                height=height,
            )
        )

    return ExtractedProjectResources(binaries=binaries, images=images)
