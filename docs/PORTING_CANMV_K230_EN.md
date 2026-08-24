# Porting EdgeOS Desktop to the CanMV K230 SDK

This document records the reproducible board-porting and patch-replay process
for EdgeOS Desktop on the DshanPI CanMV-K230 V3. The current public support
boundary is the project's locked `canmv_k230` SDK, the
`k230_canmv_dongshanpi_edgeos_defconfig` target, and the board's onboard eMMC.
Older `rtos_k230` experiments are retained only as historical evidence.

For copy-and-run user commands, see [`BUILD_RTOS_K230_EN.md`](BUILD_RTOS_K230_EN.md).
Its legacy filename is retained for link compatibility; its current workflow
is canmv-only.

## 1. Defconfig is not an SDK patch

`deconfig` is normally a misspelling of **defconfig**, the standard Kconfig
term. These artifacts have different responsibilities:

| Artifact | Responsibility | What it cannot do |
| --- | --- | --- |
| `Kconfig` | Declares options, dependencies, menu text, and defaults | It does not implement missing APIs or automatically register a directory in `apps.mk` |
| `defconfig` | A reviewable product configuration input expanded by `make <name>_defconfig` | It can select only code that already exists; it cannot create a header, driver, or function |
| `.config` | The expanded active workspace configuration produced from a defconfig or `menuconfig` | It is build state, not a replacement for the versioned product defconfig |
| SDK patch | Changes source, headers, build logic, drivers, pinmux, and image generation across SDK Git projects | It is not equivalent to a board-option list and cannot be replaced with one defconfig |

Consequently, `fatal error: kplayer.h: No such file or directory`, a missing
`lv_k230_touch_accept_click()`, or a missing `k230_ota_get_status()` is not
fixed by choosing another defconfig. It means that Scheme A is incomplete or
was applied to an incompatible SDK revision.

The required order is:

```text
locked SDK baseline -> replay the complete SDK patch set -> register the app
                    -> load the EdgeOS defconfig -> compatibility gate -> full build
```

## 2. Start from the locked CanMV SDK baseline

Do not use a moving `main` branch or an arbitrary “latest repo sync” as a
release baseline. The SDK superproject and nested projects evolve together,
while every mail patch has a specific parent commit.

Create the workspace from the immutable EdgeOS release tag and bundled
manifest:

```bash
mkdir "$HOME/canmv_k230" &&
cd "$HOME/canmv_k230" &&
repo init \
  -u https://github.com/dshanpi/EdgeOS_Desktop.git \
  -b refs/tags/edgeos-sdk-v1.0.2 \
  -m sdk/manifests/upstream-lock.xml \
  --repo-url=https://github.com/canmv-k230/git-repo.git &&
repo sync -c -j"$(nproc)"
```

`sdk/manifests/upstream-lock.xml` pins the upstream revisions,
`sdk/manifest.json` describes deterministic patch replay, and
`sdk/SHA256SUMS` protects the public patch files from silent changes. A fresh
`repo status` must not contain user-created tracked modifications.

## 3. Place and hydrate the application

EdgeOS must be an immediate child of `src/applications/`:

```bash
cd "$HOME/canmv_k230/src/applications"
git clone --branch edgeos-sdk-v1.0.2 \
  https://github.com/dshanpi/EdgeOS_Desktop.git
cd EdgeOS_Desktop

test "$(git rev-parse HEAD)" = \
  "$(git rev-list -n1 edgeos-sdk-v1.0.2)"
git lfs pull
git lfs fsck
```

The checkout may be renamed, but it must not be nested another level down.
Application `Kconfig` provides the `Applications Configuration` menu entry;
the SDK's `src/applications/apps.mk` separately maps the selected symbol to the
actual checkout directory.

## 4. Replay Scheme A as one atomic unit

Run the read-only gate before applying anything:

```bash
cd "$HOME/canmv_k230/src/applications/EdgeOS_Desktop"
./tools/apply_sdk_patches.sh --check
./tools/apply_sdk_patches.sh --apply
./tools/integrate_canmv_sdk.sh
```

The complete series spans the SDK superproject and nested repositories. It
provides the product version and A/B layout, final K230 TOC generation,
DshanPI display/camera/Wi-Fi/I2C/backlight configuration, U-Boot pinmux,
RT-Smart boot and touch behavior, LVGL ports, player/OTA/PMU APIs, MPP
camera/VICAP/VO/media behavior, and the verified software-rendering profile.

Do not copy only the application, cherry-pick a seemingly relevant patch, or
force through a `revision mismatch`. The series is one compatibility unit that
must be built and hardware-tested together.

## 5. Load the product defconfig and inspect the menu

```bash
cd "$HOME/canmv_k230"
make k230_canmv_dongshanpi_edgeos_defconfig
./src/applications/EdgeOS_Desktop/tools/check_sdk_compat.sh
```

The release configuration includes at least:

```text
CONFIG_BOARD_CONFIG_NAME="k230_canmv_dongshanpi_edgeos_defconfig"
CONFIG_APP_ENABLE_EDGEOS_DESKTOP=y
CONFIG_RTSMART_3RD_PARTY_ENABLE_LVGL=y
CONFIG_RT_PARTITION_NUMBER=4
# CONFIG_RTSMART_3RD_PARTY_LVGL_USE_VGLITE is not set
# CONFIG_RTSMART_HAL_ENABLE_VG_LITE is not set
```

`make menuconfig` should show EdgeOS under `Applications Configuration`. After
inspection, reload the EdgeOS defconfig so an accidental save cannot alter the
release profile. A visible menu item proves only Kconfig discovery; the build
log must contain `[BUILD] applications EdgeOS_Desktop` to prove `apps.mk`
registration and actual compilation.

## 6. Run the full build and post-build gates

```bash
cd "$HOME/canmv_k230"
make dl_toolchain
bash -o pipefail -c \
  'time make 2>&1 | tee edgeos-v1.0.2-build.log'

K230_TOOLCHAIN_NM="${SDK_TOOLCHAIN_DIR:-$HOME/.kendryte/k230_toolchains}/riscv64-linux-musleabi_for_x86_64-pc-linux-gnu/bin/riscv64-unknown-linux-musl-nm" \
  ./src/applications/EdgeOS_Desktop/tools/check_sdk_compat.sh
```

Accept the build only when full `make` exits zero, the final SDK completion
marker is present, and the post-build compatibility gate passes. `make app` is
useful for diagnosis but is not a firmware release gate.

## 7. Validate the complete image

```bash
cd "$HOME/canmv_k230/output/k230_canmv_dongshanpi_edgeos_defconfig"
image=DshanPI_EdgeOS_Desktop_v0.7.7.img

test -s "$image"
sha256sum "$image"
fdisk -l "$image"
python3 "$HOME/canmv_k230/src/applications/EdgeOS_Desktop/tools/check_firmware_image.py" \
  "$image"
```

Continue only when the checker ends with:

```text
PASS: EdgeOS firmware image is structurally valid.
```

The measured v1.0.2 image is `2283798528` bytes with SHA-256
`77ee49eb3bc3f3483166777c7d03e56c29cc1840665706f56d69454a241fe8b8`.
All 11 final TOC entries, three MBR partitions, ranges, and verifiable payload
hashes passed. A locally rebuilt image can have a different digest if build
metadata differs; record that new digest, rerun the structural gate, and use
that exact file throughout transfer, flashing, and board acceptance.

## 8. Flash onboard eMMC with LYNX

See [`FLASH_LYNX_EN.md`](FLASH_LYNX_EN.md) for the UI workflow. The immutable
gates are:

1. Record the local size and SHA-256 of the uncompressed full `.img`.
2. Verify the file again after LYNX transfer so a same-named cached image cannot
   silently select stale firmware.
3. Enter K230 download mode with the serial `reboot_to_upgrade` command, or use
   the board-documented boot key/BootROM strap and reset sequence. Do not apply
   another SoC family's button terminology to K230.
4. Scan and select the exact K230 USB target. The measured run used USB `2:8`
   and serial `COM9`.
5. Select `EMMC`/SDIO0 and write the complete `.img` from medium offset `0x0`.
6. Require flash-task exit code 0, reboot, and capture the serial log from
   power-on.

Never pass `_ota.kdimg`, `.img.gz`, or `.kdimg.gz` to the raw LYNX workflow.
Internal image positions such as `0xe0000` and `0x100000` are not flash start
offsets.

## 9. Board acceptance

Record automated boot evidence separately from human interaction evidence:

- Boot: SPL -> U-Boot -> RT-Smart -> EdgeOS Desktop, with no
  `K230 boot: invalid TOC entry 1`.
- Display: ST7701 initializes and the 640 x 480 orientation, colors, and
  backlight are correct.
- Touch: CST128 is detected and mapped correctly; manually test slow drag,
  fast and reverse swipes, momentum, a tap 50-150 ms after a swipe, rapid taps,
  and double taps.
- Camera: GC2093 probes, camera switching works, and the desktop recovers after
  exit.
- Wi-Fi: SDIO initialization, association, DHCP/IP acquisition, and NTP pass.
- Stability: watch serial output and exercise the screen saver, gallery scroll,
  camera switching, and Wi-Fi pages.

A successful boot does not automatically prove smooth touch feel. Current
objective evidence and explicitly pending subjective tests are recorded in
[`validation/canmv-k230-edgeos-sdk-v1.0.2.md`](validation/canmv-k230-edgeos-sdk-v1.0.2.md).

## 10. Replaying patches after `repo sync`

The patch tool creates local commits in several SDK Git projects. `repo sync`
updates or realigns repo projects; it is not an EdgeOS patch manager and does
not guarantee that local board adaptations survive.

For release work, use a new workspace:

1. Preserve the known-good workspace and its logs; do not blindly reset or
   clean it.
2. Initialize a new `canmv_k230` from the new EdgeOS tag's locked manifest.
3. Sync to that exact baseline, then run `--check`, `--apply`, the integration
   script, the EdgeOS defconfig, and the full build in order.
4. Retire the old workspace only after image, flash, boot, peripheral, and
   interaction acceptance passes on the new firmware.

Even when rebuilding the same immutable manifest, inspect `repo status` and
preserve user work first. Realign every project to the exact manifest revision,
then rerun `--check` and `--apply`. Never ignore a revision mismatch merely to
make a patch apply.

To support a newer upstream CanMV SDK, maintainers must rebase each mail patch
onto its new parent in a disposable workspace, review tree diffs, update the
manifest and checksums, prove deterministic replay, and repeat full build and
hardware acceptance. Running `repo sync` alone does not perform that port.

## 11. Troubleshooting

| Symptom | Root cause | Resolution |
| --- | --- | --- |
| `fatal error: kplayer.h` | The app was copied but the media SDK patches were not applied | Return to the locked baseline and run the complete `--check` / `--apply` workflow |
| Missing touch or OTA APIs | Only selected patches were applied, or the SDK revision is incompatible | Do not copy one header; replay all of Scheme A |
| EdgeOS absent from `Applications Configuration` | Wrong checkout depth or stale Kconfig state | Make EdgeOS an immediate child of `src/applications/`, then reload defconfig/menuconfig |
| Menu visible, but no `[BUILD] applications EdgeOS_Desktop` | `apps.mk` mapping is absent | Rerun `tools/integrate_canmv_sdk.sh` |
| `revision mismatch` | The SDK is not the release baseline or changed after sync | Create a locked workspace; never force |
| Touch direction, drag, or post-swipe taps regress | Board IRQ/coordinate profile, old LVGL input port, or stale application binary | Verify both defconfigs and the touch patch, rebuild affected objects, and flash the exact new SHA |
| Camera or Wi-Fi breaks after sync | Board profile, pinmux, or MPP changes were overwritten | Replay the complete patch set from the locked baseline and rerun compatibility gates |
| `K230 boot: invalid TOC entry 1` | Withdrawn, stale, damaged, or incorrectly finalized image | Stop reflashing blindly; verify the exact `.img` digest and structure, then write it to eMMC offset 0 |
| LYNX succeeds but old firmware boots | LYNX cache, wrong medium/offset, or a conflicting TF card | Compare local/transferred hashes, select EMMC/SDIO0 and `0x0`, and remove the TF card |

Every fix should end as reviewable application commits, SDK mail patches, a
locked manifest, patch checksums, and a validation record—not as an untracked
diff that exists only on one development host.
