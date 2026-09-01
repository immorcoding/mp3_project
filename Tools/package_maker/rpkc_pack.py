"""RPKC1 通用资源包生成器。"""

from dataclasses import dataclass
import binascii
import json
from pathlib import Path
import struct
from typing import Any


MAGIC = b"RPKC"
CURRENT_FORMAT_VERSION = 1
HEADER_PREFIX_SIZE = 0x40
ENTRY_SIZE = 0x28
ERASED_BYTE = 0xFF

RESOURCE_TYPES = {
    "BINARY": 1,
    "FONT": 2,
    "IMAGE": 3,
    "AUDIO": 4,
    "MODEL": 5,
    "FIRMWARE": 6,
}

ELEMENT_FORMATS = {
    "OPAQUE": 1,
    "UINT8": 2,
    "UINT16": 3,
    "UINT32": 4,
    "INT8": 5,
    "INT16": 6,
    "INT32": 7,
}

BYTE_ORDERS = {
    "UNKNOWN": 0,
    "LITTLE_ENDIAN": 1,
    "BIG_ENDIAN": 2,
}

IMAGE_FORMATS = {
    "LVGL_NATIVE": 1,
}

PIXEL_FORMATS = {
    "TRUE_COLOR_ALPHA": 1,
}

COLOR_SPACES = {
    "UNKNOWN": 0,
    "SRGB": 1,
}

ALPHA_MODES = {
    "UNKNOWN": 0,
    "STRAIGHT": 1,
    "PREMULTIPLIED": 2,
    "OPAQUE": 3,
}


@dataclass(frozen=True)
class PackageBuildResult:
    """描述一次资源包生成的关键结果。"""

    pack_size: int
    header_crc32: int
    resource_count: int


@dataclass(frozen=True)
class PreparedResource:
    """保存已读取、校验并完成布局的单个资源。"""

    resource_id: int
    name: str
    source: str
    resource_type: int
    resource_type_name: str
    flags: int
    version: int
    data: bytes
    data_offset: int
    data_crc32: int
    metadata: bytes
    metadata_offset: int
    metadata_crc32: int
    metadata_description: dict[str, Any] | None


def crc32(data: bytes) -> int:
    """计算 RPKC1 使用的 CRC-32/ISO-HDLC 校验值。"""

    return binascii.crc32(data) & 0xFFFFFFFF


def parse_integer(value: Any, field_name: str) -> int:
    """将 JSON 数字或带进制前缀的字符串转换为整数。"""

    if isinstance(value, bool):
        raise ValueError(f"{field_name} 不能使用布尔值")

    if isinstance(value, int):
        return value

    if isinstance(value, str):
        try:
            return int(value, 0)
        except ValueError as error:
            raise ValueError(f"{field_name} 不是有效整数：{value}") from error

    raise ValueError(f"{field_name} 必须是整数或整数文本")


def align_up(value: int, alignment: int) -> int:
    """将数值向上对齐到指定的二次幂边界。"""

    return (value + alignment - 1) & ~(alignment - 1)


def validate_alignment(value: int, field_name: str) -> None:
    """确认对齐值是有效的正二次幂。"""

    if value <= 0 or value & (value - 1):
        raise ValueError(f"{field_name} 必须是正二次幂，当前值为 {value}")


def enum_value(mapping: dict[str, int], value: Any, field_name: str) -> int:
    """解析具有稳定文本名称的枚举配置。"""

    if isinstance(value, str) and value in mapping:
        return mapping[value]

    numeric_value = parse_integer(value, field_name)

    if numeric_value < 0 or numeric_value > 0xFFFFFFFF:
        raise ValueError(f"{field_name} 超出 uint32 范围")

    return numeric_value


def build_binary_metadata(metadata: dict[str, Any], data_length: int) -> tuple[bytes, dict[str, Any]]:
    """编码 BINARY Metadata V1，并校验元素数量与数据长度。"""

    version = parse_integer(metadata.get("version", 1), "metadata.version")

    if version != 1:
        raise ValueError(f"BINARY metadata 只支持版本 1，当前为 {version}")

    element_format = enum_value(
        ELEMENT_FORMATS,
        metadata["element_format"],
        "metadata.element_format",
    )
    byte_order = enum_value(
        BYTE_ORDERS,
        metadata["byte_order"],
        "metadata.byte_order",
    )
    element_size = parse_integer(metadata["element_size"], "metadata.element_size")
    element_count = parse_integer(metadata["element_count"], "metadata.element_count")

    if element_size <= 0 or element_count <= 0:
        raise ValueError("BINARY metadata 的 element_size 和 element_count 必须大于 0")

    if element_size * element_count != data_length:
        raise ValueError(
            "BINARY metadata 与数据长度不一致："
            f"{element_size} * {element_count} != {data_length}"
        )

    encoded = struct.pack(
        "<6I",
        version,
        24,
        element_format,
        byte_order,
        element_size,
        element_count,
    )
    description = {
        "version": version,
        "size": len(encoded),
        "element_format": metadata["element_format"],
        "byte_order": metadata["byte_order"],
        "element_size": element_size,
        "element_count": element_count,
    }

    return encoded, description


def build_image_metadata(metadata: dict[str, Any], data_length: int) -> tuple[bytes, dict[str, Any]]:
    """编码 IMAGE Metadata V1，并校验当前单帧像素数据长度。"""

    version = parse_integer(metadata.get("version", 1), "metadata.version")

    if version != 1:
        raise ValueError(f"IMAGE metadata 只支持版本 1，当前为 {version}")

    image_format = enum_value(
        IMAGE_FORMATS,
        metadata["image_format"],
        "metadata.image_format",
    )
    pixel_format = enum_value(
        PIXEL_FORMATS,
        metadata["pixel_format"],
        "metadata.pixel_format",
    )
    width = parse_integer(metadata["width"], "metadata.width")
    height = parse_integer(metadata["height"], "metadata.height")
    stride_bytes = parse_integer(metadata["stride_bytes"], "metadata.stride_bytes")
    frame_count = parse_integer(metadata.get("frame_count", 1), "metadata.frame_count")
    color_space = enum_value(
        COLOR_SPACES,
        metadata.get("color_space", "UNKNOWN"),
        "metadata.color_space",
    )
    alpha_mode = enum_value(
        ALPHA_MODES,
        metadata.get("alpha_mode", "UNKNOWN"),
        "metadata.alpha_mode",
    )

    if min(width, height, stride_bytes, frame_count) <= 0:
        raise ValueError("IMAGE metadata 的尺寸、步长和帧数必须大于 0")

    if stride_bytes * height * frame_count != data_length:
        raise ValueError(
            "IMAGE metadata 与数据长度不一致："
            f"{stride_bytes} * {height} * {frame_count} != {data_length}"
        )

    encoded = struct.pack(
        "<10I",
        version,
        40,
        image_format,
        pixel_format,
        width,
        height,
        stride_bytes,
        frame_count,
        color_space,
        alpha_mode,
    )
    description = {
        "version": version,
        "size": len(encoded),
        "image_format": metadata["image_format"],
        "pixel_format": metadata["pixel_format"],
        "width": width,
        "height": height,
        "stride_bytes": stride_bytes,
        "frame_count": frame_count,
        "color_space": metadata.get("color_space", "UNKNOWN"),
        "alpha_mode": metadata.get("alpha_mode", "UNKNOWN"),
    }

    return encoded, description


def build_metadata(
    resource_type_name: str,
    metadata: dict[str, Any] | None,
    data_length: int,
) -> tuple[bytes, dict[str, Any] | None]:
    """依据资源类型生成可选 Metadata；未配置时返回空 Metadata。"""

    if metadata is None:
        return b"", None

    if resource_type_name == "BINARY":
        return build_binary_metadata(metadata, data_length)

    if resource_type_name == "IMAGE":
        return build_image_metadata(metadata, data_length)

    raise ValueError(f"暂不支持为 {resource_type_name} 生成 metadata")


def resolve_resource_type(value: Any) -> tuple[int, str]:
    """解析标准资源类型名称或自定义 uint16 类型编号。"""

    if isinstance(value, str) and value in RESOURCE_TYPES:
        return RESOURCE_TYPES[value], value

    numeric_value = parse_integer(value, "resource.type")

    if numeric_value <= 0 or numeric_value >= 0xFFFF:
        raise ValueError("resource.type 必须位于 1..0xFFFE")

    return numeric_value, f"CUSTOM_0x{numeric_value:04X}"


def prepare_resources(
    resource_configurations: list[dict[str, Any]],
    base_directory: Path,
    header_size: int,
    data_alignment: int,
    metadata_alignment: int,
) -> tuple[list[PreparedResource], int]:
    """读取资源、生成 Metadata，并依 ResourceID 顺序完成物理布局。"""

    if not resource_configurations:
        raise ValueError("资源列表不能为空")

    if len(resource_configurations) > 0xFFFF:
        raise ValueError("资源数量超出 EntryCount 的 uint16 范围")

    sorted_configurations = sorted(
        resource_configurations,
        key=lambda item: parse_integer(item["id"], "resource.id"),
    )
    resource_ids = [
        parse_integer(item["id"], "resource.id")
        for item in sorted_configurations
    ]

    if any(resource_id <= 0 or resource_id > 0xFFFFFFFF for resource_id in resource_ids):
        raise ValueError("ResourceID 必须位于 1..0xFFFFFFFF")

    if len(set(resource_ids)) != len(resource_ids):
        raise ValueError("ResourceID 必须唯一")

    cursor = header_size
    prepared_resources: list[PreparedResource] = []

    for resource_configuration, resource_id in zip(sorted_configurations, resource_ids):
        name = str(resource_configuration["name"])
        resource_type, resource_type_name = resolve_resource_type(resource_configuration["type"])
        flags = parse_integer(resource_configuration.get("flags", 0), f"{name}.flags")
        version = parse_integer(resource_configuration.get("version", 1), f"{name}.version")

        if flags != 0:
            raise ValueError(f"{name}.flags 在 RPKC1 中必须为 0")

        if version < 0 or version > 0xFFFFFFFFFFFFFFFF:
            raise ValueError(f"{name}.version 超出 uint64 范围")

        data_path = (base_directory / resource_configuration["data"]).resolve()
        source = str(resource_configuration["data"]).replace("\\", "/")
        data = data_path.read_bytes()

        if not data:
            raise ValueError(f"资源 {name} 的数据不能为空")

        metadata, metadata_description = build_metadata(
            resource_type_name,
            resource_configuration.get("metadata"),
            len(data),
        )

        if metadata:
            cursor = align_up(cursor, metadata_alignment)
            metadata_offset = cursor
            cursor += len(metadata)
            metadata_crc32 = crc32(metadata)
        else:
            metadata_offset = 0
            metadata_crc32 = 0

        cursor = align_up(cursor, data_alignment)
        data_offset = cursor
        cursor += len(data)

        prepared_resources.append(
            PreparedResource(
                resource_id=resource_id,
                name=name,
                source=source,
                resource_type=resource_type,
                resource_type_name=resource_type_name,
                flags=flags,
                version=version,
                data=data,
                data_offset=data_offset,
                data_crc32=crc32(data),
                metadata=metadata,
                metadata_offset=metadata_offset,
                metadata_crc32=metadata_crc32,
                metadata_description=metadata_description,
            )
        )

        cursor = align_up(cursor, metadata_alignment)

    return prepared_resources, align_up(cursor, data_alignment)


def build_manifest(
    configuration: dict[str, Any],
    resources: list[PreparedResource],
    pack_size: int,
    header_crc32: int,
) -> dict[str, Any]:
    """生成供烧录和联调使用的人类可读 JSON 清单。"""

    deployment = configuration.get("deployment", {})
    nor_chip_offset = parse_integer(
        deployment.get("nor_chip_offset", 0),
        "deployment.nor_chip_offset",
    )
    mapped_base = parse_integer(
        deployment.get("mapped_base", 0),
        "deployment.mapped_base",
    )

    return {
        "magic": MAGIC.decode("ascii"),
        "format_version": CURRENT_FORMAT_VERSION,
        "vendor_id": parse_integer(configuration["vendor_id"], "vendor_id"),
        "product_id": parse_integer(configuration["product_id"], "product_id"),
        "package_version": parse_integer(configuration["package_version"], "package_version"),
        "pack_size": pack_size,
        "pack_size_hex": f"0x{pack_size:X}",
        "header_crc32": f"0x{header_crc32:08X}",
        "deployment": {
            "nor_chip_offset": f"0x{nor_chip_offset:08X}",
            "mapped_base": f"0x{mapped_base:08X}",
            "mapped_address": f"0x{mapped_base + nor_chip_offset:08X}",
        },
        "resources": [
            {
                "id": resource.resource_id,
                "name": resource.name,
                "source": resource.source,
                "type": resource.resource_type_name,
                "type_value": resource.resource_type,
                "version": resource.version,
                "data_offset": f"0x{resource.data_offset:X}",
                "data_length": len(resource.data),
                "data_crc32": f"0x{resource.data_crc32:08X}",
                "metadata_offset": (
                    f"0x{resource.metadata_offset:X}"
                    if resource.metadata_offset
                    else "0x0"
                ),
                "metadata_length": len(resource.metadata),
                "metadata_crc32": f"0x{resource.metadata_crc32:08X}",
                "metadata": resource.metadata_description,
            }
            for resource in resources
        ],
    }


def build_package(
    configuration: dict[str, Any],
    base_directory: Path,
    output_binary: Path,
    output_manifest: Path,
) -> PackageBuildResult:
    """校验配置并生成确定性的 RPKC1 二进制包与 JSON 清单。"""

    format_version = parse_integer(configuration.get("format_version", 1), "format_version")
    header_size = parse_integer(configuration["header_size"], "header_size")
    data_alignment = parse_integer(configuration["data_alignment"], "data_alignment")
    metadata_alignment = parse_integer(
        configuration["metadata_alignment"],
        "metadata_alignment",
    )
    vendor_id = parse_integer(configuration["vendor_id"], "vendor_id")
    product_id = parse_integer(configuration["product_id"], "product_id")
    package_version = parse_integer(configuration["package_version"], "package_version")

    if format_version != CURRENT_FORMAT_VERSION:
        raise ValueError(f"只支持 RPKC1，当前 format_version 为 {format_version}")

    validate_alignment(data_alignment, "data_alignment")
    validate_alignment(metadata_alignment, "metadata_alignment")

    if data_alignment < 4:
        raise ValueError("data_alignment 必须至少为 4")

    if data_alignment > 0xFFFFFFFF or metadata_alignment > 0xFFFFFFFF:
        raise ValueError("对齐值超出 uint32 范围")

    if header_size > 0xFFFFFFFF:
        raise ValueError("header_size 超出 uint32 范围")

    if header_size < HEADER_PREFIX_SIZE + ENTRY_SIZE + 4:
        raise ValueError("header_size 太小，无法容纳固定头、资源表和 HeaderCRC32")

    if header_size % metadata_alignment:
        raise ValueError("header_size 必须按 metadata_alignment 对齐")

    if not 0 <= vendor_id <= 0xFFFFFFFF:
        raise ValueError("vendor_id 超出 uint32 范围")

    if not 0 <= product_id <= 0xFFFFFFFF:
        raise ValueError("product_id 超出 uint32 范围")

    if not 0 <= package_version <= 0xFFFFFFFFFFFFFFFF:
        raise ValueError("package_version 超出 uint64 范围")

    resources, pack_size = prepare_resources(
        configuration["resources"],
        base_directory,
        header_size,
        data_alignment,
        metadata_alignment,
    )

    entry_table_end = HEADER_PREFIX_SIZE + len(resources) * ENTRY_SIZE

    if entry_table_end > header_size - 4:
        raise ValueError("资源表超出 HeaderCRC32 之前的头部空间")

    maximum_pack_size = parse_integer(
        configuration.get("maximum_pack_size", 0xFFFFFFFF),
        "maximum_pack_size",
    )

    if pack_size > maximum_pack_size:
        raise ValueError(
            f"资源包大小 0x{pack_size:X} 超出上限 0x{maximum_pack_size:X}"
        )

    if pack_size > 0xFFFFFFFF:
        raise ValueError("资源包大小超出 RPKC1 uint32 地址范围")

    package = bytearray([ERASED_BYTE]) * pack_size

    struct.pack_into(
        "<4sIIIIHHIIIIQ16s",
        package,
        0,
        MAGIC,
        format_version,
        header_size,
        pack_size,
        HEADER_PREFIX_SIZE,
        len(resources),
        ENTRY_SIZE,
        data_alignment,
        metadata_alignment,
        vendor_id,
        product_id,
        package_version,
        bytes([ERASED_BYTE]) * 16,
    )

    for index, resource in enumerate(resources):
        entry_offset = HEADER_PREFIX_SIZE + index * ENTRY_SIZE

        struct.pack_into(
            "<IHHQIIIIII",
            package,
            entry_offset,
            resource.resource_id,
            resource.resource_type,
            resource.flags,
            resource.version,
            resource.data_offset,
            len(resource.data),
            resource.data_crc32,
            resource.metadata_offset,
            len(resource.metadata),
            resource.metadata_crc32,
        )

        if resource.metadata:
            metadata_end = resource.metadata_offset + len(resource.metadata)
            package[resource.metadata_offset:metadata_end] = resource.metadata

        data_end = resource.data_offset + len(resource.data)
        package[resource.data_offset:data_end] = resource.data

    header_crc32 = crc32(package[:header_size - 4])
    struct.pack_into("<I", package, header_size - 4, header_crc32)

    manifest = build_manifest(
        configuration,
        resources,
        pack_size,
        header_crc32,
    )

    output_binary.parent.mkdir(parents=True, exist_ok=True)
    output_manifest.parent.mkdir(parents=True, exist_ok=True)
    output_binary.write_bytes(package)
    output_manifest.write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )

    return PackageBuildResult(
        pack_size=pack_size,
        header_crc32=header_crc32,
        resource_count=len(resources),
    )


def build_package_from_file(
    configuration_path: Path,
    output_binary: Path,
    output_manifest: Path,
) -> PackageBuildResult:
    """从 JSON 配置文件加载参数并生成 RPKC1 资源包。"""

    configuration = json.loads(configuration_path.read_text(encoding="utf-8"))

    return build_package(
        configuration=configuration,
        base_directory=configuration_path.parent,
        output_binary=output_binary,
        output_manifest=output_manifest,
    )
