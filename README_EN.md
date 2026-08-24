# DshanPI EdgeOS Desktop

[简体中文](README.md) | **English**

DshanPI EdgeOS Desktop is an embedded desktop and AI application launcher for DshanPI CanMV-K230 V3. Built with LVGL on RT-Smart and designed for a 640 × 480 touchscreen, it brings camera, gallery, system settings, UART/VAXP debugging, cloud model deployment, and on-device AI applications into one consistent desktop experience.

## Interface preview

All screenshots below were captured from a running development board. Click an image to open the original 640 × 480 frame.

### Desktop

| Home screen | AI applications |
| --- | --- |
| [<img src="assets/screenshots/desktop-home.jpg" alt="Desktop home screen" width="100%">](assets/screenshots/desktop-home.jpg) | [<img src="assets/screenshots/desktop-ai-apps.jpg" alt="AI application desktop" width="100%">](assets/screenshots/desktop-ai-apps.jpg) |
| **Camera, dual camera, and face applications** | **Hand, human, driving, OCR, and YOLO applications** |
| Desktop tools | Extended capabilities |
| [<img src="assets/screenshots/desktop-tools.jpg" alt="Desktop tools" width="100%">](assets/screenshots/desktop-tools.jpg) | [<img src="assets/screenshots/desktop-extended.jpg" alt="Extended desktop capabilities" width="100%">](assets/screenshots/desktop-extended.jpg) |
| **Network camera, drawing, CV, plate OCR, and code tools** | **Cloud models, USB camera, and UART debugger** |

### Settings and system management

| Settings dashboard | System controls | A/B OTA update |
| --- | --- | --- |
| [<img src="assets/screenshots/settings-overview.jpg" alt="Settings dashboard" width="100%">](assets/screenshots/settings-overview.jpg) | [<img src="assets/screenshots/settings-system.jpg" alt="System controls" width="100%">](assets/screenshots/settings-system.jpg) | [<img src="assets/screenshots/ota-update.jpg" alt="A/B OTA update" width="100%">](assets/screenshots/ota-update.jpg) |
| **Wi-Fi, language, time, startup app, camera, and VAXP** | **System update, power, sleep, and device information** | **Signed update verification and network download** |

### Applications in action

| Instance segmentation | Multiple YOLO generations | Network camera |
| --- | --- | --- |
| [<img src="assets/screenshots/object-segmentation.jpg" alt="Instance segmentation application" width="100%">](assets/screenshots/object-segmentation.jpg) | [<img src="assets/screenshots/yolo-models.jpg" alt="Multiple YOLO generations" width="100%">](assets/screenshots/yolo-models.jpg) | [<img src="assets/screenshots/network-camera.jpg" alt="Network camera application" width="100%">](assets/screenshots/network-camera.jpg) |
| **Real-time object detection and instance segmentation** | **Switch between YOLOv5, YOLOv8, YOLO11, and YOLO26** | **Dual-camera composition with RTSP/RTMP endpoints** |

## Features

- Touch-first desktop, status bar, application grid, and screen saver for a 640 × 480 landscape display
- Camera, front/rear picture-in-picture camera, gallery, video playback, and touch drawing board
- Unified AI experiences including Face Studio, Face Geometry, Hand Studio, Human Studio, and Smart Driving
- OCR, object detection, multiple YOLO generations, CV Lite, licence plate recognition, code scanning, and self-learning AI
- RTSP/RTMP network camera, UVC USB camera, and CanMV Cloud model deployment
- UART terminal, loopback testing, and VAXP protocol debugging
- Wi-Fi, language, time zone, default camera, startup application, sleep, and power settings
- Simplified Chinese, Traditional Chinese, English, and Japanese UI languages
- HTTPS OTA client with signed manifests, package digest verification, and A/B partition protection
- Dedicated handling for touch scrolling, overlay lifetime, camera resource transfer, and error recovery

## VAXP Host SDK

To receive and parse VAXP protocol data exported by EdgeOS Desktop over UART, see the standalone [dshanpi/vaxp-host-sdk](https://github.com/dshanpi/vaxp-host-sdk) project. It provides examples for Linux MPUs and STM32 MCUs, together with build, flashing, baud-rate configuration, and API usage instructions.

## Hardware and software environment

| Item | Description |
| --- | --- |
| Target board | DshanPI CanMV-K230 V3 |
| SoC | Kendryte K230 |
| Operating system | RT-Smart |
| Display | ST7701, 640 × 480 landscape |
| UI framework | LVGL |
| AI runtime | nncase / KPU, depending on the application |
| Toolchain | RISC-V musl cross toolchain supplied with the CanMV K230 SDK |
| Storage | At least an 8 GB microSD; free space in `/data` must exceed the OTA KDIMG before updating |

This repository depends on LVGL, MPP, the RT-Smart HAL, Mbed TLS, cJSON, nncase, OpenCV, and image-generation tools supplied by the CanMV K230 SDK. The VAXP protocol headers are bundled in `third_party/vaxp/include/`; the build no longer relies on local files outside this repository. It cannot be built or run as a standalone Linux desktop application.

## Source layout

```text
.
├── apps/
│   ├── main.c                 Desktop, system pages, and built-in app UI
│   ├── ai_registry.c/.h       AI mode and scene registry
│   ├── generated/             Generated icon and screen-saver data
│   ├── ai_demo/               Offline AI sources and runtime resources
│   └── */                     Standalone AI, camera, and media apps
├── assets/                    Design source assets such as icons
├── font/                      Desktop font sources
├── middleware/
│   ├── camera_manager.*       Camera, VICAP, VO, and JPEG orchestration
│   └── dual_camera_manager.*  Dual-camera PIP and encoder orchestration
├── system/
│   ├── system_settings.*      Persistent system settings
│   ├── camera_settings.*      Default-camera settings
│   ├── screenshot_service.*   Screenshot service
│   ├── power_control.*        Restart, shutdown, and flashing mode
│   └── ota_*                  HTTPS, manifest verification, and A/B OTA
├── uart/                      UART Lab, VAXP, and AI data streaming
├── third_party/vaxp/          Bundled VAXP 1.0 protocol headers
├── tools/                     SDK patch, compatibility-check, and app-integration scripts
├── sdk/                       Reproducible, version-locked SDK patch set and manifests
├── skill/                     DshanPI EdgeOS UI design Skill
├── Kconfig
└── Makefile
```

The main application follows this dependency direction:

```text
apps → middleware → system
apps ─────────────→ system
```

- `system/` does not depend on LVGL and does not create UI objects.
- `middleware/` encapsulates hardware and media resources without creating application pages.
- `apps/` owns interaction and page composition and consumes lower-layer APIs.
- Standalone AI applications take ownership of the display, camera, and media resources before launch; the desktop restores them after the application exits.

## Getting the source

The repository contains large runtime assets such as models, fonts, images, and audio. Install Git LFS before cloning:

```bash
sudo apt install git-lfs
git lfs install
git clone https://github.com/dshanpi/EdgeOS_Desktop.git
cd EdgeOS_Desktop
git lfs pull
git lfs fsck
```

`.gitattributes` routes common binary resources through Git LFS. Build directories, `k230_bin/`, firmware images, and compiler intermediates are excluded from version control.

Clone `main` when browsing development sources. Firmware builds must pin both the SDK manifest and the application tag; do not combine a moving `main` with an arbitrary "latest" SDK.

## Building under rtos_k230 / the CanMV K230 SDK

The complete user workflow is in [`docs/BUILD_RTOS_K230_EN.md`](docs/BUILD_RTOS_K230_EN.md); the Chinese guide is [`docs/BUILD_RTOS_K230.md`](docs/BUILD_RTOS_K230.md). It covers host dependencies, the locked SDK checkout, toolchains, Git LFS, Scheme A, configuration, the full build, artifact checks, and troubleshooting. The project has completed an end-to-end build in a workspace actually named `rtos_k230`; see [`docs/validation/rtos-k230-edgeos-sdk-v1.0.0.md`](docs/validation/rtos-k230-edgeos-sdk-v1.0.0.md).

`rtos_k230` and `canmv_k230` are only local directory names. When the SDK comes from this project's 24-project lock, the build target remains `k230_canmv_dongshanpi_edgeos_defconfig`.

Place the repository directly under the SDK application directory:

```text
rtos_k230/                      # It may also be named canmv_k230
└── src/applications/EdgeOS_Desktop/
    ├── apps/
    ├── middleware/
    ├── system/
    ├── uart/
    └── Makefile
```

For example:

```bash
(
set -e
cd /path/to/rtos_k230/src/applications
git clone --branch edgeos-sdk-v1.0.0 \
  https://github.com/dshanpi/EdgeOS_Desktop.git
cd EdgeOS_Desktop
test "$(git rev-parse HEAD)" = "18df75d569bd5ecdfd8ccec8d37bf343e530533d"
git lfs pull
git lfs fsck
./tools/apply_sdk_patches.sh --check
./tools/apply_sdk_patches.sh --apply
./tools/integrate_canmv_sdk.sh
cd ../../..
make dl_toolchain
make k230_canmv_dongshanpi_edgeos_defconfig
./src/applications/EdgeOS_Desktop/tools/check_sdk_compat.sh
bash -o pipefail -c 'time make 2>&1 | tee edgeos-build.log'
)
```

The dedicated `k230_canmv_dongshanpi_edgeos_defconfig` enables `DshanPI EdgeOS Desktop` automatically, so no manual configuration is required. Run `make menuconfig` separately only when you want to inspect or adjust features; the application will appear under `Applications Configuration`. The SDK discovers the menu entry from this repository's Kconfig, while the integration script idempotently updates the parent `apps.mk` according to the checkout directory name and maps the option to the actual build directory. Menu discovery and build registration are separate, so run the integration script again in a new SDK checkout or after resetting `apps.mk`.

The repository must be a direct child of `src/applications/`, but its directory name may be changed. The installed launcher remains `/sdcard/app/dshanpi_aimodel` regardless of the source directory name, preserving the OTA and sub-application return paths.

> **SDK compatibility:** This repository's [`sdk/`](sdk/) directory contains the reproducible Scheme A patch set, including the player, touch-click filtering, A/B OTA, media mirroring, system version, and dedicated product-configuration extensions. The patches are strictly locked to all 24 official upstream revisions recorded in [`sdk/manifests/upstream-lock.xml`](sdk/manifests/upstream-lock.xml); they are not intended for arbitrary SDK revisions. `--check` validates the complete lock, worktrees, patch integrity, and deterministic replay without modifying source, while `--apply` applies the mutually dependent set only after every preflight check succeeds. Do not cherry-pick part of the set or force it onto a mismatch. For a fresh SDK, do not sync the moving CanMV branches directly; follow the [`sdk/README.md`](sdk/README.md) workflow that uses the `edgeos-sdk-v1.0.0` tag as the repo manifest source.

EdgeOS Desktop does not depend on WebRTC. The dedicated defconfig disables it so the product does not pull in unrelated dependencies; features such as Mbed TLS for OTA remain enabled.

On a fresh host, run `make dl_toolchain` from the SDK root first. The top-level SDK Makefiles use `SDK_TOOLCHAIN_DIR` as the toolchain root, while standalone EdgeOS sub-application scripts use `K230_TOOLCHAIN_BIN` as the musl toolchain `bin` directory. Both default below `$HOME/.kendryte/k230_toolchains/`; for a non-default installation:

```bash
export SDK_TOOLCHAIN_DIR=/opt/k230_toolchains
export K230_TOOLCHAIN_BIN="$SDK_TOOLCHAIN_DIR/riscv64-linux-musleabi_for_x86_64-pc-linux-gnu/bin"
```

## Building firmware

After applying the SDK patches, integrating the application, selecting the dedicated defconfig, and passing the compatibility check above, build from the SDK root (skip this if you already ran the final command in the previous block):

```bash
cd /path/to/rtos_k230
bash -o pipefail -c 'time make 2>&1 | tee edgeos-build.log'
```

Some SDK versions implement `make log` internally as `make | tee` without enabling `pipefail`, so the command can return zero even when the inner build fails. The command above both records the log and propagates the failure status correctly. If the firmware build fails before reaching EdgeOS, run `make app` to diagnose the application path separately; `[BUILD] applications EdgeOS_Desktop` in the log confirms that the build has entered this repository.

A successful build ends with:

```text
Build K230 done, board k230_canmv_dongshanpi, config k230_canmv_dongshanpi_edgeos_defconfig
```

Typical artifacts are written to:

```text
output/k230_canmv_dongshanpi_edgeos_defconfig/
├── DshanPI_EdgeOS_Desktop_v0.7.5.img
├── DshanPI_EdgeOS_Desktop_v0.7.5_ota.kdimg
└── ...matching .gz and digest files
```

The current product version is `v0.7.5`. It is maintained centrally in the SDK board file `boards/k230_canmv_dongshanpi/system-version.txt`; do not maintain another version string in the UI or packaging scripts.

After the full build, run `tools/check_sdk_compat.sh` again with the cross `nm` explicitly selected so the generated player archive ABI is checked. The complete command and the measured artifact sizes, SHA-256 values, and partition table are in the `rtos_k230` build guide and validation record.

### Building the application only

After configuring the SDK and building its base libraries, run this command from the SDK root:

```bash
make -C src/applications/EdgeOS_Desktop
```

If you used another checkout directory name, replace `EdgeOS_Desktop` in the command accordingly. This target builds the desktop and the AI sub-applications enabled by the application `Makefile`. Several sub-applications share model resources and output directories, so preserve the existing dependency order when changing parallel-build rules.

## Localization and fonts

The language enum and persistent setting are defined in `system/system_settings.h` in this order:

1. Simplified Chinese, `DSHANPI_LANG_ZH_CN`
2. Traditional Chinese, `DSHANPI_LANG_ZH_TW`
3. English, `DSHANPI_LANG_EN`
4. Japanese, `DSHANPI_LANG_JA`

The desktop uses LVGL Montserrat for English and the CJK subset compiled into `apps/ui_font_source_han_20.c` for Chinese and Japanese. Whenever visible non-ASCII text changes, regenerate the font subset; otherwise the device may display missing-glyph boxes.

The generated source header records the complete `lv_font_conv` options. Use this workflow to update the font:

1. Collect every visible non-ASCII character from `apps/main.c` and the other UI sources.
2. Deduplicate the characters and update the `--symbols` set.
3. Regenerate `apps/ui_font_source_han_20.c` from `font/SourceHanSansSC-Medium.otf`.
4. Inspect the desktop, Cloud Model, UART Lab, Settings, Gallery, dialogs, and Toast messages in all four languages.

Do not edit glyph bitmap data manually.

## OTA security model

Network OTA validates the following in order:

- TLS server certificate and host name
- ECDSA P-256 signature over `latest.json`
- Product, board, channel, and version fields
- Firmware filename, length, KDIMG magic, and SHA-256 digest
- Inactive A/B slot writing and subsequent boot state

Read-only OTA public keys and root certificates are stored in `system/ota_trust_store.c`. Production private keys must never be stored in this repository, a firmware image, or the download server. Keep them in an offline release environment or a protected CI secret/HSM.

Publish OTA files in this order: upload the KDIMG first, upload `latest.json.sig` next, and atomically replace `latest.json` last. The device downloads an uncompressed `.kdimg`. A `.kdimg.gz` file is suitable only for transport outside the device update flow.

## UI development conventions

- Design for 640 × 480 first, keeping primary actions, status feedback, and a return path easy to reach.
- Make touch targets at least 44 × 44 px and enlarge the hit area of top-left back buttons.
- Protect cards inside scrolling containers against movement so a drag cannot launch an application.
- Implement full-screen apps as pre-created overlays that are shown and hidden; stop timers, release hardware resources, and close popups before exit.
- An LVGL dropdown list is attached to the screen layer. Call `lv_dropdown_close()` before hiding its owner page.
- Give long-running work explicit loading, success, and failure states; do not swallow lower-layer errors or allow the same task to start twice.
- Route every user-visible string through localization and verify that all required CJK glyphs are compiled into the firmware.

The complete design philosophy, component rules, and review checklist are available in [`skill/dshanpi-edgeos-ui-design/`](skill/dshanpi-edgeos-ui-design/). The directory can be installed directly as a Codex Skill.

## Validation recommendations

- Cross-compile the desktop and affected sub-applications with `-Wall -Wextra -Werror`.
- After a cold boot, inspect the desktop, status bar, screen saver, Wi-Fi, persistent settings, and startup application.
- Check every page in all four languages for missing glyphs, truncation, wrapping, and overflow.
- Distinguish scrolling from tapping and test the hit area of controls near display edges.
- Expand each Cloud Model and UART dropdown, return immediately, and confirm that no popup remains on the desktop.
- Repeatedly enter and leave camera, dual-camera, gallery, and AI applications to verify display and camera recovery.
- Test OTA with no update, an invalid signature, a digest mismatch, network loss, insufficient storage, write failure, and a successful update.
- Validate the final full-card image on real hardware, including version, touch, display, network, camera, and UART behavior.

## Contributing

1. Put hardware access in `middleware/` or `system/`, not directly in page callbacks.
2. Add a desktop entry only after its implementation, icon, localized text, error states, and exit cleanup are complete.
3. Do not commit `build/`, `k230_bin/`, firmware images, private keys, Wi-Fi passwords, or device-local configuration.
4. Manage large models and media resources with Git LFS.
5. Include the target board, tested languages, hardware test procedure, and known limitations in each contribution.

## License

Original code at the repository root is released under the [MIT License](LICENSE).

Files under `apps/ai_demo/`, models, fonts, images, audio, prebuilt libraries, and material imported from the CanMV K230 SDK or other upstream projects may remain subject to their own licences, model terms, or redistribution restrictions. Publishers and contributors must preserve upstream notices and confirm redistribution rights before commercial distribution or rehosting large assets. The root MIT License does not replace the original terms of third-party materials.

Patches under `sdk/` retain the original copyright and license terms of the upstream projects and source files they modify. The repository-root MIT License does not relicense those upstream patches.
