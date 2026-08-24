# Flashing EdgeOS Desktop with LYNX

This guide installs the complete EdgeOS factory image on the DshanPI CanMV-K230
V3 board's onboard eMMC. It applies to the `edgeos-sdk-v1.0.1` release and
product firmware `v0.7.6`.

> [!CAUTION]
> `edgeos-sdk-v1.0.0` compiled and packaged successfully, but the generated
> factory firmware is **not bootable**. Its K230 TOC can contain unresolved
> zero-sized boot entries, causing U-Boot SPL to stop at
> `K230 boot: invalid TOC entry 1`. Do not flash or redistribute a v1.0.0 image.

The build procedure is in [`BUILD_RTOS_K230_EN.md`](BUILD_RTOS_K230_EN.md).
Validation records are listed in [`validation/README.md`](validation/README.md),
including the current
[`canmv_k230` / `rtos_k230` v1.0.1 record](validation/canmv-rtos-k230-edgeos-sdk-v1.0.1.md).

## 1. Select the correct artifact

The output directory contains files with different purposes:

| Artifact | Purpose | Use with LYNX? |
| --- | --- | --- |
| `DshanPI_EdgeOS_Desktop_v0.7.6.img` | Complete disk image containing the MBR, K230 TOC, SPL, U-Boot, RT-Smart, RTApp, and application partitions | **Yes** |
| `DshanPI_EdgeOS_Desktop_v0.7.6.img.gz` | Compressed transport copy of the complete image | No; decompress it first |
| `DshanPI_EdgeOS_Desktop_v0.7.6_ota.kdimg` | A/B update package consumed by the updater on an already bootable EdgeOS installation | **Never** |
| `DshanPI_EdgeOS_Desktop_v0.7.6_ota.kdimg.gz` | Compressed transport copy of the OTA package | **Never** |

The OTA KDIMG does not contain a complete factory disk layout. Flashing it at
offset 0 cannot provision a bootable eMMC.

## 2. Verify the source tag and full image

When using a local source checkout, resolve the tag rather than copying an
unpublished commit ID into a script:

```bash
cd /path/to/EdgeOS_Desktop
tag_commit=$(git rev-list -n 1 edgeos-sdk-v1.0.1)
test -n "$tag_commit"
test "$(git rev-parse HEAD)" = "$tag_commit"
git describe --tags --exact-match HEAD
```

Validate the uncompressed full image before opening LYNX:

```bash
EDGEOS_ROOT=/path/to/EdgeOS_Desktop
IMAGE=/path/to/DshanPI_EdgeOS_Desktop_v0.7.6.img

test -s "$IMAGE"
python3 "$EDGEOS_ROOT/tools/check_firmware_image.py" "$IMAGE"
sha256sum "$IMAGE"
```

Continue only when the checker ends with:

```text
PASS: EdgeOS firmware image is structurally valid.
```

The checker validates the MBR, final K230 TOC, A/B ranges, package headers, and
unencrypted package payload hashes. In particular, the `spl` and `uboot` TOC
entries must have nonzero offsets and sizes. Compare the SHA-256 value with the
release or validation record for the exact image being installed; locally
rebuilt images can differ when build metadata is embedded.

## 3. Configure LYNX

Connect the board with stable power and use its documented download-mode or
LYNX detection procedure. Before starting the write, confirm every field below:

| LYNX field | Required value |
| --- | --- |
| Image/file | Uncompressed full `DshanPI_EdgeOS_Desktop_v0.7.6.img` |
| Chip/device | `K230` |
| Storage/medium | `EMMC(SDIO0)` |
| Write mode | Complete/raw image |
| Start/write offset | `0` (`0x0`) |

LYNX versions may format field names differently, but the values must remain
K230, onboard eMMC on SDIO0, raw image, and byte offset 0. The complete image
already places its TOC at internal offset `0xe0000`, SPL at `0x100000`, and
U-Boot at `0x200000`; do not enter any of those internal offsets in LYNX. The
first byte of the `.img` must be written to the first byte of the eMMC.

Use this sequence:

1. Power the board off and remove any TF/microSD card so another installed
   system cannot obscure the eMMC result.
2. Connect the board's programming interface to the host with a data-capable
   USB cable and provide stable power.
3. Select K230, `EMMC(SDIO0)`, complete/raw image, the full `.img`, and offset 0
   in LYNX.
4. Hold the board's `FEL` button, press and release `RST`, and release `FEL`
   after LYNX detects the device. The board documentation also permits entering
   download mode by powering on while `FEL` is held.
5. Start the write and wait for LYNX to report completion. Do not reset,
   disconnect, or remove power while erasing, writing, or verifying.
6. Leave download mode, reset or power-cycle the board, and capture the serial
   log from the beginning.

## 4. Understand eMMC and `/sdcard`

These names describe different layers:

- `EMMC(SDIO0)` in LYNX is the physical onboard eMMC and the factory-flash
  target.
- `MMC0` in the U-Boot log is that boot storage as seen by U-Boot.
- `/sdcard` in EdgeOS is a logical RT-Smart mount point for the active
  `app_a` or `app_b` filesystem stored on the onboard eMMC.
- `/data` is another logical partition on the same eMMC.

The `/sdcard` pathname does not imply that EdgeOS was flashed to or is running
from a removable microSD card. Do not change the LYNX target because application
files appear below `/sdcard`.

## 5. First boot and HS200 fallback

During first boot, DDR training messages appear before eMMC initialization. A
board can also print messages similar to:

```text
snps_sdhci mmc0@91580000: HS200 tuning failed: -110, attempts=1, ...
MMC0: selected timing: MMC High Speed (52MHz), 8-bit, clock=52000000 Hz
```

When the 52 MHz selection follows the HS200 warning, U-Boot has successfully
fallen back to eMMC high-speed mode. The HS200 line alone is therefore not the
cause of a later TOC error. Continue checking the subsequent boot messages.

The factory image pre-creates `bin`, `app_a`, and `app_b`. On first boot,
RT-Smart uses the remaining eMMC space to create `/data`, reboots once to rescan
the partition table, then formats and mounts it. Do not interrupt this first-boot
sequence.

## 6. Troubleshooting

| Symptom | Meaning and action |
| --- | --- |
| LYNX rejects the selected file | Confirm that it is the uncompressed full `.img`; decompress `.img.gz` first |
| `K230 boot: invalid TOC entry 1` | The image is v1.0.0, stale, damaged, or incorrectly generated. Stop using it, validate a v1.0.1/v0.7.6 full `.img`, and reflash it to `EMMC(SDIO0)` at offset 0 |
| `K230 boot: invalid TOC` after a passing local check | Verify the exact file passed to LYNX, its release hash, target medium, offset, and LYNX write verification; then reflash the complete image |
| `no mkimage signature but raw image not supported` after an eMMC boot failure | U-Boot fell through to another medium after failing to load the factory layout. Reflash the validated full `.img`; do not flash the OTA KDIMG |
| `HS200 tuning failed` followed by 52 MHz selection | The supported fallback succeeded; continue diagnosing later messages rather than treating this line as fatal |
| HS200 failure is followed by read errors and no timing fallback | Check power, cabling, board connection, eMMC access, and the LYNX write result before retrying |
| `Trying to boot from MMC1` after the TOC error | This is the generic SPL fallback path after the custom eMMC boot failed; the string alone does not prove that the original image was written to physical SDIO1/TF |
| The old firmware still boots | Recheck `EMMC(SDIO0)` and offset 0; the image may have been written to the wrong medium or location |
| Files are mounted below `/sdcard` | This is expected: `/sdcard` is the active application filesystem's logical path on eMMC |

Keep the complete serial log from power-on when reporting a failure. Include the
image filename and hash, LYNX chip/medium/offset values, structural-check output,
and the first error after the 52 MHz fallback.

## 7. References

- [Kendryte K230 Burning Tool](https://github.com/kendryte/k230_burning_tool)
  documents K230 media identifiers: eMMC is attached through SDIO0, while an
  SD card uses SDIO1.
- [K230 RTOS firmware flashing guide](https://www.kendryte.com/k230_rtos/zh/main/userguide/how_to_flash.html)
  describes raw-image flashing and target-medium selection for complete `.img`
  files.
- [DshanPI CanMV-K230 V3 eMMC update guide](https://eai.100ask.net/CanaanK230/part1/UpdateEMMCsystem/)
  shows the board-specific download-mode and eMMC update procedure.
