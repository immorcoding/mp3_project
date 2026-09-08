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

            self.assertEqual(len(package), 0x77000)
            self.assertEqual(package[:4], b"RPKC")
            self.assertEqual(struct.unpack_from("<H", package, 0x14)[0], 4)
            self.assertEqual(manifest["package_version"], 2)

            first_entry = struct.unpack_from("<IHHQIIIIII", package, 0x40)
            second_entry = struct.unpack_from("<IHHQIIIIII", package, 0x68)
            third_entry = struct.unpack_from("<IHHQIIIIII", package, 0x90)
            fourth_entry = struct.unpack_from("<IHHQIIIIII", package, 0xB8)

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

            self.assertEqual(manifest["deployment"]["mapped_address"], "0x90401000")
            self.assertEqual(
                [resource["id"] for resource in manifest["resources"]],
                [1, 2, 3, 4],
            )
            self.assertEqual(
                [resource["name"] for resource in manifest["resources"]],
                [
                    "cp936_uni2oem",
                    "cp936_oem2uni",
                    "ui_img_wallpaper_indigo_mist_soft_dark_png",
                    "vinyl_original_144px",
                ],
            )


if __name__ == "__main__":
    unittest.main()
