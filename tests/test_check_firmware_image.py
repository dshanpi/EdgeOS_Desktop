import contextlib
import hashlib
import io
import struct
import sys
import tempfile
import unittest
from pathlib import Path


REPOSITORY_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPOSITORY_ROOT / "tools"))

import check_firmware_image as checker  # noqa: E402


class SyntheticEdgeOsImage:
    IMAGE_SIZE = 0x500000
    PAYLOAD = b"synthetic K230 payload"
    LAYOUT = [
        ("ota_meta", 0x0F0000, 0x800, 0, 0),
        ("spl", 0x100000, 0x1000, 0, 0),
        ("uboot_env", 0x110000, 0x1000, 0, 0),
        ("uboot", 0x120000, 0x1000, 0, 0),
        ("rtt_a", 0x130000, 0x1000, 1, 3),
        ("rtapp_a", 0x140000, 0x1000, 1, 0),
        ("rtt_b", 0x150000, 0x1000, 1, 3),
        ("rtapp_b", 0x160000, 0x1000, 1, 0),
        ("bin", 0x200000, 0x100000, 0, 0),
        ("app_a", 0x300000, 0x100000, 0, 0),
        ("app_b", 0x400000, 0x100000, 0, 0),
    ]

    def __init__(self, directory: Path) -> None:
        self.path = directory / "edgeos.img"
        with self.path.open("wb") as image:
            image.truncate(self.IMAGE_SIZE)
        self._write_mbr()
        self._write_toc(self.LAYOUT)
        for name in checker.K230_PACKAGED_ENTRIES:
            self._write_package(name)

    def _entry(self, name):
        return next(item for item in self.LAYOUT if item[0] == name)

    def _write_at(self, offset, data):
        with self.path.open("r+b") as image:
            image.seek(offset)
            image.write(data)

    def _write_mbr(self):
        mbr = bytearray(checker.MBR_SIZE)
        for index, name in enumerate(checker.EDGEOS_MBR_ENTRIES):
            _, offset, size, _, _ = self._entry(name)
            entry = struct.pack(
                "<B3sB3sII",
                0,
                b"\0" * 3,
                0x0C,
                b"\0" * 3,
                offset // checker.SECTOR_SIZE,
                size // checker.SECTOR_SIZE,
            )
            start = checker.MBR_PARTITION_TABLE_OFFSET + index * 16
            mbr[start : start + 16] = entry
        mbr[510:512] = checker.MBR_SIGNATURE
        self._write_at(0, mbr)

    def _write_toc(self, layout):
        toc = bytearray(checker.K230_TOC_SIZE)
        for index, (name, offset, size, load, boot) in enumerate(layout):
            name_field = name.encode("ascii").ljust(32, b"\0")
            toc[index * 64 : (index + 1) * 64] = checker.K230_TOC_STRUCT.pack(
                name_field, offset, size, load, boot, 0
            )
        self._write_at(checker.K230_TOC_OFFSET, toc)

    def _write_package(self, name):
        _, offset, _, _, _ = self._entry(name)
        payload = self.PAYLOAD + name.encode("ascii")
        digest = hashlib.sha256(payload).digest()
        verification = digest + bytes(516 - len(digest))
        header = struct.pack(
            "<III", checker.K230_PACKAGE_MAGIC, len(payload), checker.K230_CRYPTO_NONE
        ) + verification
        self._write_at(offset, header + payload)

    def rewrite_toc_entry(self, target_name, **changes):
        layout = []
        for item in self.LAYOUT:
            current_name, offset, size, load, boot = item
            if current_name == target_name:
                current_name = changes.get("name", current_name)
                offset = changes.get("offset", offset)
                size = changes.get("size", size)
                load = changes.get("load", load)
                boot = changes.get("boot", boot)
            layout.append((current_name, offset, size, load, boot))
        self._write_toc(layout)


class FirmwareImageValidationTests(unittest.TestCase):
    def setUp(self):
        self.temporary_directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary_directory.cleanup)
        self.synthetic = SyntheticEdgeOsImage(Path(self.temporary_directory.name))

    def assert_error_contains(self, report, text):
        self.assertFalse(report.ok)
        self.assertTrue(
            any(text in error for error in report.errors),
            "expected {!r} in errors: {!r}".format(text, report.errors),
        )

    def test_valid_edgeos_image_passes(self):
        report = checker.validate_firmware_image(self.synthetic.path)

        self.assertTrue(report.ok, report.errors)
        self.assertEqual(len(report.toc_entries), len(self.synthetic.LAYOUT))
        self.assertEqual(len(report.mbr_partitions), 3)
        self.assertEqual(len(report.packages), 6)
        self.assertTrue(all(item.sha256_verified for item in report.packages))

    def test_cli_prints_pass_and_returns_zero(self):
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            result = checker.main([str(self.synthetic.path)])

        self.assertEqual(result, 0)
        self.assertIn("PASS:", output.getvalue())

    def test_cli_prints_fail_and_returns_nonzero(self):
        self.synthetic.rewrite_toc_entry("spl", size=0)
        output = io.StringIO()

        with contextlib.redirect_stdout(output):
            result = checker.main([str(self.synthetic.path)])

        self.assertEqual(result, 1)
        self.assertIn("FAIL:", output.getvalue())

    def test_zero_sized_spl_is_rejected(self):
        self.synthetic.rewrite_toc_entry("spl", size=0)

        report = checker.validate_firmware_image(self.synthetic.path)

        self.assert_error_contains(report, "'spl' has zero size")

    def test_overlapping_toc_ranges_are_rejected(self):
        self.synthetic.rewrite_toc_entry("uboot_env", offset=0x100800)

        report = checker.validate_firmware_image(self.synthetic.path)

        self.assert_error_contains(report, "TOC ranges overlap")

    def test_missing_required_entry_is_rejected(self):
        self.synthetic.rewrite_toc_entry("app_b", name="extra")

        report = checker.validate_firmware_image(self.synthetic.path)

        self.assert_error_contains(report, "required EdgeOS TOC entry 'app_b' is missing")

    def test_mbr_toc_mismatch_is_rejected(self):
        mbr_size_offset = checker.MBR_PARTITION_TABLE_OFFSET + 12
        self.synthetic._write_at(mbr_size_offset, struct.pack("<I", 1))

        report = checker.validate_firmware_image(self.synthetic.path)

        self.assert_error_contains(report, "'bin' does not match exactly one MBR")

    def test_invalid_package_magic_is_rejected(self):
        _, offset, _, _, _ = self.synthetic._entry("uboot")
        self.synthetic._write_at(offset, b"BAD!")

        report = checker.validate_firmware_image(self.synthetic.path)

        self.assert_error_contains(report, "package 'uboot' has magic")

    def test_corrupt_payload_hash_is_rejected(self):
        _, offset, _, _, _ = self.synthetic._entry("rtapp_b")
        payload_offset = offset + checker.K230_PACKAGE_HEADER_SIZE
        self.synthetic._write_at(payload_offset, b"X")

        report = checker.validate_firmware_image(self.synthetic.path)

        self.assert_error_contains(report, "package 'rtapp_b' payload SHA-256")

    def test_entry_outside_image_is_rejected(self):
        self.synthetic.rewrite_toc_entry("app_b", size=0x200000)

        report = checker.validate_firmware_image(self.synthetic.path)

        self.assert_error_contains(report, "'app_b' range")


if __name__ == "__main__":
    unittest.main()
