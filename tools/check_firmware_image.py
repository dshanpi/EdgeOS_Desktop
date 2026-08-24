#!/usr/bin/env python3
"""Validate a complete EdgeOS K230 disk image without modifying it."""

import argparse
import hashlib
import os
import re
import struct
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import BinaryIO, Dict, List, Optional, Sequence, Tuple


SECTOR_SIZE = 512
MBR_SIZE = SECTOR_SIZE
MBR_PARTITION_TABLE_OFFSET = 446
MBR_PARTITION_ENTRY_SIZE = 16
MBR_PARTITION_COUNT = 4
MBR_SIGNATURE = b"\x55\xaa"

K230_TOC_OFFSET = 0xE0000
K230_TOC_ENTRY_SIZE = 64
K230_TOC_MAX_ENTRIES = 16
K230_TOC_SIZE = K230_TOC_ENTRY_SIZE * K230_TOC_MAX_ENTRIES
K230_TOC_STRUCT = struct.Struct("<32sQQBB10xI")

K230_PACKAGE_MAGIC = 0x3033324B
K230_PACKAGE_HEADER_SIZE = 528
K230_PACKAGE_FIXED_HEADER = struct.Struct("<III")
K230_CRYPTO_NONE = 0
K230_CRYPTO_SM4 = 1
K230_CRYPTO_AES = 2
K230_CRYPTO_NAMES = {
    K230_CRYPTO_NONE: "none",
    K230_CRYPTO_SM4: "SM4/SM2",
    K230_CRYPTO_AES: "AES/RSA",
}

EDGEOS_REQUIRED_ENTRIES = (
    "ota_meta",
    "spl",
    "uboot_env",
    "uboot",
    "rtt_a",
    "rtapp_a",
    "rtt_b",
    "rtapp_b",
    "bin",
    "app_a",
    "app_b",
)
EDGEOS_MBR_ENTRIES = ("bin", "app_a", "app_b")
K230_PACKAGED_ENTRIES = (
    "spl",
    "uboot",
    "rtt_a",
    "rtapp_a",
    "rtt_b",
    "rtapp_b",
)
TOC_NAME_PATTERN = re.compile(r"[A-Za-z0-9_.-]+\Z")
HASH_CHUNK_SIZE = 1024 * 1024


@dataclass(frozen=True)
class TocEntry:
    index: int
    name: str
    offset: int
    size: int
    load: int
    boot: int
    load_addr: int

    @property
    def end(self) -> int:
        return self.offset + self.size


@dataclass(frozen=True)
class MbrPartition:
    index: int
    bootable: bool
    partition_type: int
    offset: int
    size: int

    @property
    def end(self) -> int:
        return self.offset + self.size


@dataclass(frozen=True)
class PackageCheck:
    name: str
    payload_length: int
    crypto_type: int
    sha256_verified: Optional[bool]


@dataclass
class ValidationReport:
    image_path: Path
    image_size: int = 0
    toc_entries: List[TocEntry] = field(default_factory=list)
    mbr_partitions: List[MbrPartition] = field(default_factory=list)
    packages: List[PackageCheck] = field(default_factory=list)
    errors: List[str] = field(default_factory=list)
    notices: List[str] = field(default_factory=list)

    @property
    def ok(self) -> bool:
        return not self.errors


def _read_exact(image: BinaryIO, offset: int, size: int) -> bytes:
    image.seek(offset)
    data = image.read(size)
    if len(data) != size:
        raise ValueError(
            "short read at 0x{:x}: expected {} bytes, got {}".format(
                offset, size, len(data)
            )
        )
    return data


def _parse_mbr(raw: bytes, report: ValidationReport) -> List[MbrPartition]:
    if raw[510:512] != MBR_SIGNATURE:
        report.errors.append("MBR signature 0x55aa is missing")

    partitions: List[MbrPartition] = []
    for index in range(MBR_PARTITION_COUNT):
        start = MBR_PARTITION_TABLE_OFFSET + index * MBR_PARTITION_ENTRY_SIZE
        entry = raw[start : start + MBR_PARTITION_ENTRY_SIZE]
        boot_flag = entry[0]
        partition_type = entry[4]
        start_lba, sector_count = struct.unpack_from("<II", entry, 8)

        if entry == bytes(MBR_PARTITION_ENTRY_SIZE):
            continue
        if partition_type == 0 or sector_count == 0:
            report.errors.append(
                "MBR partition {} is partially populated".format(index + 1)
            )
            continue
        if boot_flag not in (0x00, 0x80):
            report.errors.append(
                "MBR partition {} has invalid boot flag 0x{:02x}".format(
                    index + 1, boot_flag
                )
            )

        partitions.append(
            MbrPartition(
                index=index + 1,
                bootable=boot_flag == 0x80,
                partition_type=partition_type,
                offset=start_lba * SECTOR_SIZE,
                size=sector_count * SECTOR_SIZE,
            )
        )

    return partitions


def _is_empty_toc_slot(raw: bytes) -> bool:
    return raw == bytes(K230_TOC_ENTRY_SIZE) or raw == b"\xff" * K230_TOC_ENTRY_SIZE


def _parse_toc(raw: bytes, report: ValidationReport) -> List[TocEntry]:
    entries: List[TocEntry] = []
    terminated = False

    for index in range(K230_TOC_MAX_ENTRIES):
        item = raw[index * K230_TOC_ENTRY_SIZE : (index + 1) * K230_TOC_ENTRY_SIZE]
        if _is_empty_toc_slot(item):
            terminated = True
            continue

        if terminated:
            report.errors.append(
                "TOC entry {} contains data after an empty terminator".format(index)
            )

        name_raw, offset, size, load, boot, load_addr = K230_TOC_STRUCT.unpack(item)
        nul = name_raw.find(b"\x00")
        if nul < 0:
            report.errors.append("TOC entry {} name is not NUL-terminated".format(index))
            continue
        if any(name_raw[nul + 1 :]):
            report.errors.append(
                "TOC entry {} name has non-zero bytes after its terminator".format(index)
            )

        try:
            name = name_raw[:nul].decode("ascii")
        except UnicodeDecodeError:
            report.errors.append("TOC entry {} name is not ASCII".format(index))
            continue

        if not name or not TOC_NAME_PATTERN.fullmatch(name):
            report.errors.append(
                "TOC entry {} has invalid name {!r}".format(index, name)
            )
            continue

        entries.append(
            TocEntry(
                index=index,
                name=name,
                offset=offset,
                size=size,
                load=load,
                boot=boot,
                load_addr=load_addr,
            )
        )

    if not entries:
        report.errors.append("K230 TOC contains no entries")
    return entries


def _validate_toc_entries(report: ValidationReport) -> Dict[str, TocEntry]:
    entries_by_name: Dict[str, TocEntry] = {}
    toc_end = K230_TOC_OFFSET + K230_TOC_SIZE

    for entry in report.toc_entries:
        if entry.name in entries_by_name:
            report.errors.append("duplicate TOC entry {!r}".format(entry.name))
        else:
            entries_by_name[entry.name] = entry

        if entry.offset == 0:
            report.errors.append("TOC entry {!r} has zero offset".format(entry.name))
        if entry.size == 0:
            report.errors.append("TOC entry {!r} has zero size".format(entry.name))
            continue
        if entry.end > report.image_size:
            report.errors.append(
                "TOC entry {!r} range [0x{:x}, 0x{:x}) exceeds image size 0x{:x}".format(
                    entry.name, entry.offset, entry.end, report.image_size
                )
            )
        if entry.offset < toc_end and K230_TOC_OFFSET < entry.end:
            report.errors.append("TOC entry {!r} overlaps the TOC".format(entry.name))

    ranged_entries = sorted(
        (entry for entry in report.toc_entries if entry.size),
        key=lambda entry: (entry.offset, entry.end, entry.name),
    )
    for previous, current in zip(ranged_entries, ranged_entries[1:]):
        if current.offset < previous.end:
            report.errors.append(
                "TOC ranges overlap: {!r} [0x{:x}, 0x{:x}) and {!r} "
                "[0x{:x}, 0x{:x})".format(
                    previous.name,
                    previous.offset,
                    previous.end,
                    current.name,
                    current.offset,
                    current.end,
                )
            )

    for name in EDGEOS_REQUIRED_ENTRIES:
        if name not in entries_by_name:
            report.errors.append("required EdgeOS TOC entry {!r} is missing".format(name))

    for name in ("rtt_a", "rtt_b"):
        entry = entries_by_name.get(name)
        if entry is not None and (entry.load != 1 or not (entry.boot & 0x01)):
            report.errors.append(
                "TOC entry {!r} must be loadable and bootable".format(name)
            )
    for name in ("rtapp_a", "rtapp_b"):
        entry = entries_by_name.get(name)
        if entry is not None and entry.load != 1:
            report.errors.append("TOC entry {!r} must be loadable".format(name))

    return entries_by_name


def _validate_mbr_layout(
    report: ValidationReport, entries_by_name: Dict[str, TocEntry]
) -> None:
    if len(report.mbr_partitions) != len(EDGEOS_MBR_ENTRIES):
        report.errors.append(
            "EdgeOS MBR must contain exactly {} populated partitions; found {}".format(
                len(EDGEOS_MBR_ENTRIES), len(report.mbr_partitions)
            )
        )

    partitions_by_range: Dict[Tuple[int, int], List[MbrPartition]] = {}
    for partition in report.mbr_partitions:
        partitions_by_range.setdefault((partition.offset, partition.size), []).append(
            partition
        )
        if partition.end > report.image_size:
            report.errors.append(
                "MBR partition {} exceeds image size".format(partition.index)
            )

    sorted_partitions = sorted(report.mbr_partitions, key=lambda item: item.offset)
    for previous, current in zip(sorted_partitions, sorted_partitions[1:]):
        if current.offset < previous.end:
            report.errors.append(
                "MBR partitions {} and {} overlap".format(
                    previous.index, current.index
                )
            )

    matched_indexes = set()
    for name in EDGEOS_MBR_ENTRIES:
        toc_entry = entries_by_name.get(name)
        if toc_entry is None or not toc_entry.size:
            continue
        matches = partitions_by_range.get((toc_entry.offset, toc_entry.size), [])
        if len(matches) != 1:
            report.errors.append(
                "TOC entry {!r} does not match exactly one MBR partition".format(name)
            )
            continue
        partition = matches[0]
        matched_indexes.add(partition.index)
        if partition.partition_type != 0x0C:
            report.errors.append(
                "MBR partition for {!r} has type 0x{:02x}, expected 0x0c".format(
                    name, partition.partition_type
                )
            )

    for partition in report.mbr_partitions:
        if partition.index not in matched_indexes:
            report.errors.append(
                "MBR partition {} does not match bin/app_a/app_b TOC layout".format(
                    partition.index
                )
            )


def _sha256_range(image: BinaryIO, offset: int, size: int) -> bytes:
    digest = hashlib.sha256()
    image.seek(offset)
    remaining = size
    while remaining:
        chunk = image.read(min(HASH_CHUNK_SIZE, remaining))
        if not chunk:
            raise ValueError(
                "short read while hashing payload at 0x{:x}".format(offset)
            )
        digest.update(chunk)
        remaining -= len(chunk)
    return digest.digest()


def _validate_packages(
    image: BinaryIO,
    report: ValidationReport,
    entries_by_name: Dict[str, TocEntry],
) -> None:
    for name in K230_PACKAGED_ENTRIES:
        entry = entries_by_name.get(name)
        if entry is None or entry.size < K230_PACKAGE_HEADER_SIZE:
            if entry is not None and entry.size:
                report.errors.append(
                    "K230 package {!r} is smaller than its {}-byte header".format(
                        name, K230_PACKAGE_HEADER_SIZE
                    )
                )
            continue
        if entry.end > report.image_size:
            continue

        header = _read_exact(image, entry.offset, K230_PACKAGE_HEADER_SIZE)
        magic, payload_length, crypto_type = K230_PACKAGE_FIXED_HEADER.unpack_from(header)
        if magic != K230_PACKAGE_MAGIC:
            report.errors.append(
                "K230 package {!r} has magic 0x{:08x}, expected 0x{:08x}".format(
                    name, magic, K230_PACKAGE_MAGIC
                )
            )
            continue
        if payload_length == 0:
            report.errors.append("K230 package {!r} has zero payload length".format(name))
            continue
        if crypto_type not in K230_CRYPTO_NAMES:
            report.errors.append(
                "K230 package {!r} has unsupported crypto type {}".format(
                    name, crypto_type
                )
            )
            continue
        if K230_PACKAGE_HEADER_SIZE + payload_length > entry.size:
            report.errors.append(
                "K230 package {!r} declares {} payload bytes outside its TOC extent".format(
                    name, payload_length
                )
            )
            continue

        verified: Optional[bool] = None
        if crypto_type == K230_CRYPTO_NONE:
            declared_digest = header[12:44]
            actual_digest = _sha256_range(
                image, entry.offset + K230_PACKAGE_HEADER_SIZE, payload_length
            )
            verified = actual_digest == declared_digest
            if not verified:
                report.errors.append(
                    "K230 package {!r} payload SHA-256 does not match its header".format(
                        name
                    )
                )
        else:
            report.notices.append(
                "K230 package {!r} uses {}; payload signature verification is "
                "not available in the Python standard library".format(
                    name, K230_CRYPTO_NAMES[crypto_type]
                )
            )

        report.packages.append(
            PackageCheck(
                name=name,
                payload_length=payload_length,
                crypto_type=crypto_type,
                sha256_verified=verified,
            )
        )


def validate_firmware_image(path: os.PathLike) -> ValidationReport:
    """Return a complete validation report for *path* without modifying it."""

    image_path = Path(path)
    report = ValidationReport(image_path=image_path)

    try:
        report.image_size = image_path.stat().st_size
    except OSError as exc:
        report.errors.append("cannot stat image: {}".format(exc))
        return report

    minimum_size = K230_TOC_OFFSET + K230_TOC_SIZE
    if report.image_size < minimum_size:
        report.errors.append(
            "image is too small: {} bytes; need at least {} bytes".format(
                report.image_size, minimum_size
            )
        )
        return report

    try:
        with image_path.open("rb") as image:
            report.mbr_partitions = _parse_mbr(_read_exact(image, 0, MBR_SIZE), report)
            report.toc_entries = _parse_toc(
                _read_exact(image, K230_TOC_OFFSET, K230_TOC_SIZE), report
            )
            entries_by_name = _validate_toc_entries(report)
            _validate_mbr_layout(report, entries_by_name)
            _validate_packages(image, report, entries_by_name)
    except (OSError, ValueError, struct.error) as exc:
        report.errors.append("cannot inspect image: {}".format(exc))

    return report


def _format_size(size: int) -> str:
    units = ("B", "KiB", "MiB", "GiB")
    value = float(size)
    for unit in units:
        if value < 1024.0 or unit == units[-1]:
            return "{:.2f} {}".format(value, unit)
        value /= 1024.0
    return "{} B".format(size)


def _print_report(report: ValidationReport) -> None:
    print("EdgeOS K230 firmware image check")
    print("  Image: {}".format(report.image_path))
    print("  Size: {} ({} bytes)".format(_format_size(report.image_size), report.image_size))
    print("  TOC entries: {}".format(len(report.toc_entries)))
    print("  MBR partitions: {}".format(len(report.mbr_partitions)))

    if report.packages:
        print("  K230 packages:")
        for package in report.packages:
            if package.sha256_verified is True:
                digest_status = "SHA-256 OK"
            elif package.sha256_verified is False:
                digest_status = "SHA-256 FAIL"
            else:
                digest_status = "signature check skipped"
            print(
                "    {}: payload={} bytes, crypto={}, {}".format(
                    package.name,
                    package.payload_length,
                    K230_CRYPTO_NAMES[package.crypto_type],
                    digest_status,
                )
            )

    for notice in report.notices:
        print("  NOTE: {}".format(notice))

    if report.ok:
        print("PASS: EdgeOS firmware image is structurally valid.")
    else:
        print("FAIL: {} validation error(s).".format(len(report.errors)))
        for error in report.errors:
            print("  - {}".format(error))


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = argparse.ArgumentParser(
        description=(
            "Validate the MBR, K230 TOC, EdgeOS A/B layout and packaged "
            "payloads in a complete .img firmware image."
        )
    )
    parser.add_argument("image", help="path to a complete EdgeOS .img image")
    args = parser.parse_args(argv)

    report = validate_firmware_image(args.image)
    _print_report(report)
    return 0 if report.ok else 1


if __name__ == "__main__":
    sys.exit(main())
