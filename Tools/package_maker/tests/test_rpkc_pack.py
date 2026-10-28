import binascii
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest


PACKAGE_MAKER_DIRECTORY = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PACKAGE_MAKER_DIRECTORY))

from rpkc_pack import build_package
from extract_project_resources import extract_byte_array, extract_project_resources
from main import generate_project_package


FONT_DIRECTORY = Path(__file__).resolve().parents[3] / "Resources" / "fonts"
sys.path.insert(0, str(FONT_DIRECTORY))

import generate_font

FONT_BIN_PATH = (
    Path(__file__).resolve().parents[3]
    / "Resources"
    / "fonts"
    / "noto_sans_cjk_sc_12_4bpp.bin"
)
MEBIBYTE = 1 << 20


def put_bits(buffer: bytearray, bit_position: int, value: int, count: int) -> None:
    """按 MSB 优先把 value 的低 count 位写入位流。"""

    for index in range(count):
        if (value >> (count - 1 - index)) & 1:
            position = bit_position + index
            buffer[position // 8] |= 0x80 >> (position % 8)


def build_synthetic_font_bin(bitmap_bytes: list[int], loca_format: int = 1) -> bytes:
    """构造合成的 lv_font_conv bin：字形 0 为零长度 notdef，其后各字形按位图字节数排列。

    位图为 0 的字形是空盒子（如空格），只含 4 字节的字形头位流。
    """

    adv_bits, xy_bits, wh_bits = 9, 5, 4
    glyph_count = len(bitmap_bytes) + 1
    glyphs = [b""]

    for bitmap in bitmap_bytes:
        # 27 位字形头的最后几位落在第 4 字节，位图紧随其后共用该字节，与 lv_font_conv 的排布一致。
        glyph = bytearray(3 + bitmap if bitmap else 4)
        put_bits(glyph, 0, 5, adv_bits)
        box = 1 if bitmap else 0
        put_bits(glyph, adv_bits + 2 * xy_bits, box, wh_bits)
        put_bits(glyph, adv_bits + 2 * xy_bits + wh_bits, box, wh_bits)
        glyphs.append(bytes(glyph))

    offsets = []
    cursor = 8
    for glyph in glyphs:
        offsets.append(cursor)
        cursor += len(glyph)

    head = bytearray(48)
    struct.pack_into("<I4s", head, 0, 48, b"head")
    struct.pack_into("<IHHHh", head, 8, 1, 4, 12, 13, -3)
    head[0x22:0x29] = bytes([loca_format, 1, 1, 4, xy_bits, wh_bits, adv_bits])

    cmap = struct.pack("<I4sI", 12, b"cmap", 0)
    entry_format = "<I" if loca_format else "<H"
    loca_body = b"".join(struct.pack(entry_format, offset) for offset in offsets)
    loca = struct.pack("<I4sI", 12 + len(loca_body), b"loca", glyph_count) + loca_body
    glyf = struct.pack("<I4s", 8 + sum(len(g) for g in glyphs), b"glyf") + b"".join(glyphs)

    return bytes(head) + cmap + loca + glyf


class BuildPackageTests(unittest.TestCase):

    def test_builds_minimal_binary_resource_package(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            working_directory = Path(temporary_directory)
            data_path = working_directory / "sample.bin"
            package_path = working_directory / "package.bin"
            manifest_path = working_directory / "manifest.json"

            data_path.write_bytes(b"\x12\x34\x56\x78")

            configuration = {
                "format_version": 1,
                "header_size": 0x1000,
                "data_alignment": 0x1000,
                "metadata_alignment": 4,
                "vendor_id": 1,
                "product_id": 1,
                "package_version": 1,
                "resources": [
                    {
                        "id": 7,
                        "name": "sample",
                        "type": "BINARY",
                        "version": 3,
                        "data": "sample.bin",
                    }
                ],
            }

            result = build_package(
                configuration=configuration,
                base_directory=working_directory,
                output_binary=package_path,
                output_manifest=manifest_path,
            )

            package = package_path.read_bytes()
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))

            self.assertEqual(package[0:4], b"RPKC")
            self.assertEqual(struct.unpack_from("<I", package, 4)[0], 1)
            self.assertEqual(struct.unpack_from("<I", package, 8)[0], 0x1000)
            self.assertEqual(struct.unpack_from("<H", package, 0x14)[0], 1)
            self.assertEqual(struct.unpack_from("<H", package, 0x16)[0], 40)

            resource_entry = struct.unpack_from("<IHHQIIIIII", package, 0x40)
            self.assertEqual(resource_entry[0], 7)
            self.assertEqual(resource_entry[1], 1)
            self.assertEqual(resource_entry[2], 0)
            self.assertEqual(resource_entry[3], 3)
            self.assertEqual(resource_entry[4], 0x1000)
            self.assertEqual(resource_entry[5], 4)
            self.assertEqual(resource_entry[6], binascii.crc32(b"\x12\x34\x56\x78"))
            self.assertEqual(resource_entry[7:], (0, 0, 0))

            expected_header_crc = binascii.crc32(package[:0x0FFC]) & 0xFFFFFFFF
            self.assertEqual(struct.unpack_from("<I", package, 0x0FFC)[0], expected_header_crc)
            self.assertEqual(package[0x1000:0x1004], b"\x12\x34\x56\x78")
            self.assertTrue(all(value == 0xFF for value in package[0x1004:]))

            self.assertEqual(result.pack_size, 0x2000)
            self.assertEqual(manifest["magic"], "RPKC")
            self.assertEqual(manifest["resources"][0]["name"], "sample")
            self.assertEqual(manifest["resources"][0]["source"], "sample.bin")

            repeated_package_path = working_directory / "repeated.bin"
            repeated_manifest_path = working_directory / "repeated.json"
            build_package(
                configuration=configuration,
                base_directory=working_directory,
                output_binary=repeated_package_path,
                output_manifest=repeated_manifest_path,
            )

            self.assertEqual(package, repeated_package_path.read_bytes())

    def test_sorts_entries_and_rejects_duplicate_resource_ids(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            working_directory = Path(temporary_directory)
            (working_directory / "first.bin").write_bytes(b"first")
            (working_directory / "second.bin").write_bytes(b"second")

            configuration = {
                "format_version": 1,
                "header_size": 0x1000,
                "data_alignment": 0x1000,
                "metadata_alignment": 4,
                "vendor_id": 1,
                "product_id": 1,
                "package_version": 1,
                "resources": [
                    {
                        "id": 2,
                        "name": "second",
                        "type": "BINARY",
                        "data": "second.bin",
                    },
                    {
                        "id": 1,
                        "name": "first",
                        "type": "BINARY",
                        "data": "first.bin",
                    },
                ],
            }

            package_path = working_directory / "sorted.bin"
            manifest_path = working_directory / "sorted.json"
            build_package(
                configuration,
                working_directory,
                package_path,
                manifest_path,
            )

            package = package_path.read_bytes()
            self.assertEqual(struct.unpack_from("<I", package, 0x40)[0], 1)
            self.assertEqual(struct.unpack_from("<I", package, 0x68)[0], 2)

            configuration["resources"][1]["id"] = 2

            with self.assertRaisesRegex(ValueError, "ResourceID 必须唯一"):
                build_package(
                    configuration,
                    working_directory,
                    package_path,
                    manifest_path,
                )

    def test_rejects_metadata_that_disagrees_with_data_length(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            working_directory = Path(temporary_directory)
            (working_directory / "table.bin").write_bytes(b"\x00\x01\x02\x03")

            configuration = {
                "format_version": 1,
                "header_size": 0x1000,
                "data_alignment": 0x1000,
                "metadata_alignment": 4,
                "vendor_id": 1,
                "product_id": 1,
                "package_version": 1,
                "resources": [
                    {
                        "id": 1,
                        "name": "table",
                        "type": "BINARY",
                        "data": "table.bin",
                        "metadata": {
                            "element_format": "UINT16",
                            "byte_order": "LITTLE_ENDIAN",
                            "element_size": 2,
                            "element_count": 3,
                        },
                    }
                ],
            }

            with self.assertRaisesRegex(ValueError, "metadata 与数据长度不一致"):
                build_package(
                    configuration,
                    working_directory,
                    working_directory / "invalid.bin",
                    working_directory / "invalid.json",
                )


class FontResourceTests(unittest.TestCase):

    def font_configuration(self, font_format: str = "LV_FONT_CONV_BIN") -> dict:
        return {
            "format_version": 1,
            "header_size": 0x1000,
            "data_alignment": 0x1000,
            "metadata_alignment": 4,
            "vendor_id": 1,
            "product_id": 1,
            "package_version": 3,
            "resources": [
                {
                    "id": 5,
                    "name": "font",
                    "type": "FONT",
                    "version": 1,
                    "data": "font.bin",
                    "metadata": {
                        "version": 1,
                        "font_format": font_format,
                        "codepoint_encoding": "UNICODE",
                    },
                }
            ],
        }

    def build_font_package(
        self,
        font_bin: bytes,
        font_format: str = "LV_FONT_CONV_BIN",
    ) -> tuple[bytes, dict]:
        with tempfile.TemporaryDirectory() as temporary_directory:
            working_directory = Path(temporary_directory)
            (working_directory / "font.bin").write_bytes(font_bin)
            build_package(
                self.font_configuration(font_format),
                working_directory,
                working_directory / "package.bin",
                working_directory / "manifest.json",
            )
            manifest = json.loads(
                (working_directory / "manifest.json").read_text(encoding="utf-8")
            )
            return (working_directory / "package.bin").read_bytes(), manifest

    def test_packs_font_entry_with_32_byte_metadata_derived_from_the_bin(self) -> None:
        font_bin = build_synthetic_font_bin([9, 0, 7])
        package, manifest = self.build_font_package(font_bin)

        entry = struct.unpack_from("<IHHQIIIIII", package, 0x40)
        self.assertEqual(entry[0], 5)
        self.assertEqual(entry[1], 2)
        self.assertEqual(entry[3], 1)
        self.assertEqual(entry[5], len(font_bin))
        self.assertEqual(entry[6], binascii.crc32(font_bin))
        self.assertEqual(entry[8], 32)

        metadata = package[entry[7]:entry[7] + 32]
        self.assertEqual(
            struct.unpack("<8I", metadata),
            (1, 32, 1, 1, 4, 12, 13, 0xFFFFFFFD),
        )
        self.assertEqual(package[entry[4]:entry[4] + entry[5]], font_bin)
        self.assertEqual(manifest["resources"][0]["metadata"]["glyph_count"], 4)

    def test_font_metadata_handles_16_bit_loca(self) -> None:
        package, _ = self.build_font_package(build_synthetic_font_bin([9, 7], loca_format=0))

        entry = struct.unpack_from("<IHHQIIIIII", package, 0x40)
        self.assertEqual(struct.unpack_from("<I", package, entry[7] + 0x10)[0], 3)

    def test_font_bitmap_total_must_stay_below_one_mebibyte(self) -> None:
        self.build_font_package(build_synthetic_font_bin([MEBIBYTE // 2, MEBIBYTE // 2 - 1]))

        with self.assertRaisesRegex(ValueError, "位图总量"):
            self.build_font_package(build_synthetic_font_bin([MEBIBYTE // 2, MEBIBYTE // 2]))

        # 空盒子字形的头字节不计入位图总量。
        self.build_font_package(build_synthetic_font_bin([MEBIBYTE - 1, 0, 0]))

    def test_rejects_truncated_or_unknown_font_bin(self) -> None:
        font_bin = build_synthetic_font_bin([9, 7])

        with self.assertRaisesRegex(ValueError, "字体 bin"):
            self.build_font_package(font_bin[:-3])

        with self.assertRaisesRegex(ValueError, "字体 bin"):
            self.build_font_package(b"\x00" * 64)

    def test_rejects_unknown_font_format_names(self) -> None:
        with self.assertRaisesRegex(ValueError, "font_format"):
            self.build_font_package(build_synthetic_font_bin([9]), font_format="TTF")

    def test_charset_rule_yields_the_documented_gb2312_counts(self) -> None:
        symbols, hanzi = generate_font.gb2312_characters()

        self.assertEqual(len(symbols), 682)
        self.assertEqual(len(hanzi), 6763)
        # 区位 1-1 为全角空格；16-1 为"啊"，87-94 为"齄"。
        self.assertEqual(symbols[0], "　")
        self.assertEqual(hanzi[0], "啊")
        self.assertEqual(hanzi[-1], "齄")
        self.assertEqual(len(set(symbols + hanzi)), 682 + 6763)

    def test_committed_font_bin_has_expected_glyph_set(self) -> None:
        package, manifest = self.build_font_package(FONT_BIN_PATH.read_bytes())

        entry = struct.unpack_from("<IHHQIIIIII", package, 0x40)
        metadata = struct.unpack_from("<8I", package, entry[7])
        # 独立解析 bin 得到：7619 个字符（并集去重后）+ 索引 0 的 notdef；生成脚本的字集规则给出同一个数。
        self.assertEqual(metadata[4], 7620)
        self.assertEqual(metadata[4], len(generate_font.charset_codepoints()) + 1)
        self.assertEqual(metadata[5], 12)
        self.assertEqual(metadata[6], 13)
        self.assertEqual(metadata[7], 0xFFFFFFFD)
        self.assertEqual(manifest["resources"][0]["metadata"]["bitmap_bytes"], 520360)
        self.assertLess(manifest["resources"][0]["metadata"]["bitmap_bytes"], MEBIBYTE)

    def test_charset_rule_adds_latin1_and_two_punctuation_marks_with_deduplication(self) -> None:
        symbols, hanzi = generate_font.gb2312_characters()
        codepoints = generate_font.charset_codepoints()
        expected = (
            set(range(0x20, 0x7F))
            | set(range(0xA0, 0x100))
            | {0x2013, 0x2122}
            | {ord(character) for character in symbols + hanzi}
        )

        self.assertEqual(codepoints, sorted(expected))
        self.assertEqual(len(codepoints), len(set(codepoints)))
        # 去重后的字数：GB2312 已含 19 个 Latin-1 字符，Latin-1 加两个标点净增 79 个。
        self.assertEqual(len(codepoints), 95 + 682 + 6763 + 79)
        for absent in (0x7F, 0x9F, 0x100, 0x2012, 0x2121):
            self.assertNotIn(absent, codepoints)


class ExtractProjectResourcesTests(unittest.TestCase):

    def test_vinyl_pack_source_keeps_only_true_color_alpha_16bit(self) -> None:
        project_root = Path(__file__).resolve().parents[3]
        source_path = (
            project_root / "Resources" / "imgs" / "vinyl_original_144px.c"
        )
        source = source_path.read_text(encoding="utf-8")

        self.assertNotIn("LV_COLOR_DEPTH", source)
        self.assertNotIn("LV_COLOR_16_SWAP", source)
        self.assertIn("TRUE_COLOR_ALPHA", source)

        data = extract_byte_array(source, "vinyl_original_144px_map")
        self.assertEqual(len(data), 144 * 144 * 3)

    def test_extracts_cp936_tables_and_sorted_imgs_c_arrays(self) -> None:
        project_root = Path(__file__).resolve().parents[3]

        with tempfile.TemporaryDirectory() as temporary_directory:
            extracted = extract_project_resources(
                project_root=project_root,
                output_directory=Path(temporary_directory),
            )

            uni2oem = extracted.binaries["uni2oem"].read_bytes()
            oem2uni = extracted.binaries["oem2uni"].read_bytes()
            image_names = [image.name for image in extracted.images]
            wallpaper = extracted.images[0].output_path.read_bytes()
            vinyl = extracted.images[1].output_path.read_bytes()

            self.assertEqual(len(uni2oem), 87172)
            self.assertEqual(len(oem2uni), 87172)
            self.assertEqual(
                image_names,
                [
                    "ui_img_wallpaper_indigo_mist_soft_dark_png",
                    "vinyl_original_144px",
                ],
            )
            self.assertEqual(extracted.images[0].width, 240)
            self.assertEqual(extracted.images[0].height, 320)
            self.assertEqual(extracted.images[1].width, 144)
            self.assertEqual(extracted.images[1].height, 144)
            self.assertEqual(len(wallpaper), 240 * 320 * 3)
            self.assertEqual(len(vinyl), 144 * 144 * 3)
            self.assertEqual(binascii.crc32(uni2oem) & 0xFFFFFFFF, 0xFBAAB4D2)
            self.assertEqual(binascii.crc32(oem2uni) & 0xFFFFFFFF, 0x60F7F8F0)
            self.assertEqual(binascii.crc32(wallpaper) & 0xFFFFFFFF, 0xBBB21D5D)

    def test_generates_current_project_rpkc1_package(self) -> None:
        project_root = Path(__file__).resolve().parents[3]
        configuration_path = PACKAGE_MAKER_DIRECTORY / "resource_pack.json"

        with tempfile.TemporaryDirectory() as temporary_directory:
            output_directory = Path(temporary_directory)
            generate_project_package(
                project_root=project_root,
                configuration_path=configuration_path,
                output_directory=output_directory,
            )

            package = (output_directory / "resource_pack.bin").read_bytes()
            manifest = json.loads(
                (output_directory / "resource_pack_manifest.json").read_text(
                    encoding="utf-8"
                )
            )

            # 字体 bin 623520 B 位于 0x77000，向上对齐到 0x1000 后总长 0x110000。
            self.assertEqual(len(package), 0x110000)
            self.assertEqual(package[:4], b"RPKC")
            self.assertEqual(struct.unpack_from("<H", package, 0x14)[0], 5)
            self.assertEqual(manifest["package_version"], 4)

            first_entry = struct.unpack_from("<IHHQIIIIII", package, 0x40)
            second_entry = struct.unpack_from("<IHHQIIIIII", package, 0x68)
            third_entry = struct.unpack_from("<IHHQIIIIII", package, 0x90)
            fourth_entry = struct.unpack_from("<IHHQIIIIII", package, 0xB8)
            fifth_entry = struct.unpack_from("<IHHQIIIIII", package, 0xE0)

            self.assertEqual(first_entry[0], 1)
            self.assertEqual(first_entry[4], 0x2000)
            self.assertEqual(first_entry[7], 0x1000)
            self.assertEqual(first_entry[8], 24)
            self.assertEqual(second_entry[0], 2)
            self.assertEqual(second_entry[4], 0x18000)
            self.assertEqual(second_entry[7], 0x17484)
            self.assertEqual(third_entry[0], 3)
            self.assertEqual(third_entry[4], 0x2E000)
            self.assertEqual(third_entry[7], 0x2D484)
            self.assertEqual(third_entry[8], 40)
            self.assertEqual(fourth_entry[0], 4)
            self.assertEqual(fourth_entry[4], 0x67000)
            self.assertEqual(fourth_entry[5], 144 * 144 * 3)
            self.assertEqual(fourth_entry[7], 0x66400)
            self.assertEqual(fourth_entry[8], 40)

            self.assertEqual(fifth_entry[0], 5)
            self.assertEqual(fifth_entry[1], 2)
            self.assertEqual(fifth_entry[3], 2)
            self.assertEqual(fifth_entry[4], 0x77000)
            self.assertEqual(fifth_entry[5], 623520)
            self.assertEqual(fifth_entry[7], 0x76300)
            self.assertEqual(fifth_entry[8], 32)
            font_bin = FONT_BIN_PATH.read_bytes()
            self.assertEqual(package[0x77000:0x77000 + len(font_bin)], font_bin)

            self.assertEqual(manifest["deployment"]["mapped_address"], "0x90401000")
            self.assertEqual(
                [resource["id"] for resource in manifest["resources"]],
                [1, 2, 3, 4, 5],
            )
            self.assertEqual(
                [resource["name"] for resource in manifest["resources"]],
                [
                    "cp936_uni2oem",
                    "cp936_oem2uni",
                    "ui_img_wallpaper_indigo_mist_soft_dark_png",
                    "vinyl_original_144px",
                    "noto_sans_cjk_sc_12_4bpp",
                ],
            )
            self.assertEqual(
                [resource["version"] for resource in manifest["resources"]],
                [1, 1, 1, 1, 2],
            )

    def test_project_package_is_deterministic_and_fits_the_resource_area(self) -> None:
        project_root = Path(__file__).resolve().parents[3]
        configuration_path = PACKAGE_MAKER_DIRECTORY / "resource_pack.json"
        packages = []

        for _ in range(2):
            with tempfile.TemporaryDirectory() as temporary_directory:
                output_directory = Path(temporary_directory)
                generate_project_package(
                    project_root=project_root,
                    configuration_path=configuration_path,
                    output_directory=output_directory,
                )
                packages.append((output_directory / "resource_pack.bin").read_bytes())

        self.assertEqual(packages[0], packages[1])
        # 资源区起点 0x00401000，上限 0x003FE000；v4 实际 PackSize 0x110000 远小于上限。
        self.assertLessEqual(len(packages[0]), 0x003FE000)


if __name__ == "__main__":
    unittest.main()
