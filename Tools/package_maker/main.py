"""提取工程资源并生成可烧录的 RPKC1 资源包。"""

import argparse
import json
from pathlib import Path

from extract_project_resources import extract_project_resources
from rpkc_pack import build_package


PACKAGE_MAKER_DIRECTORY = Path(__file__).resolve().parent
DEFAULT_PROJECT_ROOT = PACKAGE_MAKER_DIRECTORY.parents[1]
DEFAULT_CONFIGURATION_PATH = PACKAGE_MAKER_DIRECTORY / "resource_pack.json"
DEFAULT_OUTPUT_DIRECTORY = DEFAULT_PROJECT_ROOT / "build" / "external-resources"


def parse_arguments() -> argparse.Namespace:
    """解析命令行参数，并提供适用于当前工程目录结构的默认值。"""

    parser = argparse.ArgumentParser(description="生成 MP3 工程的 RPKC1 外部资源包")
    parser.add_argument(
        "--project-root",
        type=Path,
        default=DEFAULT_PROJECT_ROOT,
        help="工程根目录",
    )
    parser.add_argument(
        "--config",
        type=Path,
        default=DEFAULT_CONFIGURATION_PATH,
        help="RPKC1 JSON 配置文件",
    )
    parser.add_argument(
        "--output-directory",
        type=Path,
        default=DEFAULT_OUTPUT_DIRECTORY,
        help="资源包、清单和中间资源的输出目录",
    )

    return parser.parse_args()


def generate_project_package(
    project_root: Path,
    configuration_path: Path,
    output_directory: Path,
) -> None:
    """依次提取工程资源、组装 RPKC1，并输出便于核对的结果摘要。"""

    generated_directory = output_directory / "generated"
    extracted_paths = extract_project_resources(
        project_root=project_root,
        output_directory=generated_directory,
    )

    configuration = json.loads(configuration_path.read_text(encoding="utf-8"))
    output_binary = output_directory / "resource_pack.bin"
    output_manifest = output_directory / "resource_pack_manifest.json"

    result = build_package(
        configuration=configuration,
        base_directory=output_directory,
        output_binary=output_binary,
        output_manifest=output_manifest,
    )

    print(f"已生成：{output_binary}")
    print(f"资源包长度：{result.pack_size} B / 0x{result.pack_size:X}")
    print(f"Header CRC32：0x{result.header_crc32:08X}")

    for resource_name, resource_path in extracted_paths.items():
        print(f"已提取 {resource_name}：{resource_path}")


def main() -> None:
    """执行命令行资源包生成流程。"""

    arguments = parse_arguments()
    generate_project_package(
        project_root=arguments.project_root.resolve(),
        configuration_path=arguments.config.resolve(),
        output_directory=arguments.output_directory.resolve(),
    )


if __name__ == "__main__":
    main()
