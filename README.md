# DshanPI EdgeOS Desktop

[English](README.md) | [简体中文](README_CN.md)

DshanPI EdgeOS Desktop is an embedded desktop and AI application launcher for DshanPI CanMV-K230 V3. Built with LVGL on RT-Smart and designed for a 640 × 480 touchscreen, it brings camera, gallery, system settings, UART/VAXP debugging, cloud model deployment, and on-device AI applications into one consistent desktop experience.

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

This repository depends on LVGL, MPP, the RT-Smart HAL, Mbed TLS, cJSON, nncase, OpenCV, VAXP headers, and image-generation tools supplied by the CanMV K230 SDK. It cannot be built or run as a standalone Linux desktop application.

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
git clone https://github.com/dshanpi/DshanPI_EdgeOS_Desktop.git
cd DshanPI_EdgeOS_Desktop
git lfs pull
```

`.gitattributes` routes common binary resources through Git LFS. Build directories, `k230_bin/`, firmware images, and compiler intermediates are excluded from version control.

## Integrating with the CanMV K230 SDK

Place the repository directly under the SDK application directory:

```text
canmv_k230/
└── src/applications/dshanpi_aimodel/
    ├── apps/
    ├── middleware/
    ├── system/
    ├── uart/
    └── Makefile
```

For example:

```bash
cd /path/to/canmv_k230/src/applications
git clone https://github.com/dshanpi/DshanPI_EdgeOS_Desktop.git dshanpi_aimodel
cd dshanpi_aimodel
git lfs pull
```

The application `Makefile` uses relative SDK paths, so keep the repository at this level under `src/applications/`.

Sub-application build scripts look for the toolchain under `$HOME/.kendryte/k230_toolchains/` by default. If the toolchain is installed elsewhere, point `K230_TOOLCHAIN_BIN` at its `bin` directory:

```bash
export K230_TOOLCHAIN_BIN=/opt/k230-toolchain/bin
```

## Building firmware

Select the DshanPI CanMV-K230 configuration and build from the SDK root:

```bash
cd /path/to/canmv_k230
make k230_canmv_dongshanpi_defconfig
time make log
```

A successful build ends with:

```text
Build K230 done, board k230_canmv_dongshanpi, config k230_canmv_dongshanpi_defconfig
```

Typical artifacts are written to:

```text
output/k230_canmv_dongshanpi_defconfig/
├── DshanPI_CanMV_V3_<system-version>.img
├── DshanPI_CanMV_V3_<system-version>_ota.kdimg
└── ...matching .gz and digest files
```

The system version is maintained by the SDK board file `boards/k230_canmv_dongshanpi/system-version.txt`. Do not maintain another version string in the UI or packaging scripts.

### Building the application only

After configuring the SDK and building its base libraries, run this command from the SDK root:

```bash
make -C src/applications/dshanpi_aimodel
```

This target builds the desktop and the AI sub-applications enabled by the application `Makefile`. Several sub-applications share model resources and output directories, so preserve the existing dependency order when changing parallel-build rules.

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
