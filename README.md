# DshanPI EdgeOS Desktop

[简体中文](#简体中文) | [English](#english)

## 简体中文

DshanPI EdgeOS Desktop 是面向 DshanPI CanMV-K230 V3 的嵌入式桌面与 AI 应用启动环境。项目基于 LVGL 和 RT-Smart，针对 640 × 480 触摸屏设计，将相机、相册、系统设置、UART/VAXP 调试、云模型部署和端侧 AI 应用组织为一致的桌面体验。

## 项目特点

- 面向 640 × 480 横屏的触控桌面、状态栏、应用网格和屏幕保护程序
- 相机、前后双摄画中画、相册、视频播放和触控画板
- Face Studio、Face Geometry、Hand Studio、Human Studio、Smart Driving 等统一 AI 场景应用
- OCR、目标检测、多版本 YOLO、CV Lite、车牌识别、条码识别和 AI 自学习
- RTSP/RTMP 网络摄像机、UVC USB 摄像头和 CanMV Cloud 模型部署
- UART 终端、回环测试以及 VAXP 协议调试
- Wi-Fi、语言、时区、默认相机、开机应用、休眠与电源管理
- 简体中文、繁体中文、English、日本語四种界面语言
- HTTPS、签名清单、包摘要校验和 A/B 分区保护的 OTA 客户端
- 针对触屏滚动、弹层生命周期、摄像头资源切换和异常恢复的专门处理

## 硬件与软件环境

| 项目 | 说明 |
| --- | --- |
| 目标板 | DshanPI CanMV-K230 V3 |
| SoC | Kendryte K230 |
| 系统 | RT-Smart |
| 显示 | ST7701，640 × 480 横屏 |
| UI | LVGL |
| AI 运行时 | nncase / KPU，依具体子应用而定 |
| 工具链 | CanMV K230 SDK 配套的 RISC-V musl 交叉工具链 |

本仓库依赖 CanMV K230 SDK 提供的 LVGL、MPP、RT-Smart HAL、Mbed TLS、cJSON、nncase、OpenCV、VAXP 头文件和固件打包工具。单独克隆本仓库不能在普通 Linux 主机上直接构建或运行桌面程序。

## 源码结构

```text
.
├── apps/
│   ├── main.c                 桌面、系统页面与内置应用 UI
│   ├── ai_registry.c/.h       AI 模式与场景注册表
│   ├── generated/             由设计资源生成的图标与屏保数据
│   ├── ai_demo/               离线 AI 算法源码及运行资源
│   └── */                     独立 AI、相机与媒体子应用
├── assets/                    图标等设计源资源
├── font/                      桌面字体源文件
├── middleware/
│   ├── camera_manager.*       单摄像头、VICAP、VO 与 JPEG 编排
│   └── dual_camera_manager.*  双摄画中画与编码资源编排
├── system/
│   ├── system_settings.*      持久化系统设置
│   ├── camera_settings.*      默认摄像头设置
│   ├── screenshot_service.*   截屏服务
│   ├── power_control.*        重启、关机和烧录模式
│   └── ota_*                  HTTPS、清单验签与 A/B OTA
├── uart/                      UART Lab、VAXP 与 AI 数据流
├── skill/                     DshanPI EdgeOS UI 设计 Skill
├── Kconfig
└── Makefile
```

主工程遵守以下依赖方向：

```text
apps → middleware → system
apps ─────────────→ system
```

- `system/` 不依赖 LVGL，也不创建界面。
- `middleware/` 封装硬件与媒体资源，不创建应用页面。
- `apps/` 负责交互和页面组织，通过下层 API 使用硬件能力。
- 可独立运行的 AI 子应用在启动前接管显示、摄像头和媒体资源，退出后由桌面恢复。

## 获取源码

仓库包含模型、字体、图片和音频等大型运行资源，首次克隆前请安装 Git LFS：

```bash
sudo apt install git-lfs
git lfs install
git clone https://github.com/dshanpi/DshanPI_EdgeOS_Desktop.git
cd DshanPI_EdgeOS_Desktop
git lfs pull
```

`.gitattributes` 已为常见二进制资源启用 Git LFS。`build/`、`k230_bin/`、固件镜像和编译中间文件不会进入版本库。

## 集成到 CanMV K230 SDK

推荐将仓库直接放在 SDK 的应用目录中：

```text
canmv_k230/
└── src/applications/dshanpi_aimodel/
    ├── apps/
    ├── middleware/
    ├── system/
    ├── uart/
    └── Makefile
```

例如：

```bash
cd /path/to/canmv_k230/src/applications
git clone https://github.com/dshanpi/DshanPI_EdgeOS_Desktop.git dshanpi_aimodel
cd dshanpi_aimodel
git lfs pull
```

该目录的 `Makefile` 使用相对路径接入 SDK，因此不要随意改变它在 `src/applications/` 下的层级。

子应用构建脚本默认从 `$HOME/.kendryte/k230_toolchains/` 查找工具链。如工具链安装在其他位置，请将其 `bin` 目录通过 `K230_TOOLCHAIN_BIN` 指定：

```bash
export K230_TOOLCHAIN_BIN=/opt/k230-toolchain/bin
```

## 构建固件

在 SDK 根目录选择 DshanPI CanMV-K230 配置并构建：

```bash
cd /path/to/canmv_k230
make k230_canmv_dongshanpi_defconfig
time make log
```

构建成功后，日志末尾应出现：

```text
Build K230 done, board k230_canmv_dongshanpi, config k230_canmv_dongshanpi_defconfig
```

典型产物位于：

```text
output/k230_canmv_dongshanpi_defconfig/
├── DshanPI_CanMV_V3_<系统版本>.img
├── DshanPI_CanMV_V3_<系统版本>_ota.kdimg
└── ...对应的 .gz 与摘要文件
```

系统版本由 SDK 板级文件 `boards/k230_canmv_dongshanpi/system-version.txt` 管理，不应在 UI 或发布脚本中重复维护。

### 仅构建应用

完成 SDK 配置和基础库构建后，可从 SDK 根目录执行：

```bash
make -C src/applications/dshanpi_aimodel
```

该目标会构建桌面以及 Makefile 中启用的 AI 子应用。多个子应用共享模型资源和输出目录，修改并行构建规则时要保留现有依赖顺序。

## 多语言与字体

界面语言枚举和持久化设置位于 `system/system_settings.h`，当前顺序为：

1. 简体中文 `DSHANPI_LANG_ZH_CN`
2. 繁体中文 `DSHANPI_LANG_ZH_TW`
3. English `DSHANPI_LANG_EN`
4. 日本語 `DSHANPI_LANG_JA`

桌面英文使用 LVGL Montserrat，中文和日文使用 `apps/ui_font_source_han_20.c` 中的 CJK 子集字体。新增或修改非 ASCII 文案时必须同步更新字体字形；否则设备会显示方框或缺字。

生成文件头部保存了 `lv_font_conv` 的完整选项。字体更新流程是：

1. 收集 `apps/main.c` 等 UI 源码中所有可见的非 ASCII 字符。
2. 去重后更新 `--symbols` 字符集合。
3. 使用 `font/SourceHanSansSC-Medium.otf` 重新生成 `apps/ui_font_source_han_20.c`。
4. 用四种语言逐页检查桌面、Cloud Model、UART Lab、Settings、Gallery、弹窗和 Toast。

不要手工修改生成字体文件中的位图数据。

## OTA 安全模型

网络 OTA 使用固定 HTTPS 发布源，并依次验证：

- TLS 服务器证书与主机名
- `latest.json` 的 ECDSA P-256 签名
- 产品、板型、频道和版本字段
- 固件文件名、长度、KDIMG magic 与 SHA-256
- A/B 非活动槽写入和启动状态

只读 OTA 公钥和根证书位于 `system/ota_trust_store.c`。生产私钥不得放入本仓库、固件镜像或下载服务器，应由离线发布环境或受保护的 CI Secret/HSM 保存。

OTA 发布顺序应为：先上传 KDIMG，再上传 `latest.json.sig`，最后原子替换 `latest.json`。设备端使用未压缩 `.kdimg`；`.kdimg.gz` 仅适合发布系统外部传输，不能直接作为设备下载目标。

## UI 开发约定

本仓库的界面不是通用桌面控件库，而是一套针对资源受限触摸设备建立的产品规范。核心约定包括：

- 以 640 × 480 为设计基准，优先保证首要操作、返回路径和状态反馈。
- 可点击目标至少 44 × 44 px；左上返回按钮应扩大点击区域。
- 可滚动卡片使用移动距离保护，避免拖动结束时误触应用。
- 全屏应用使用预创建并隐藏/显示的 overlay；退出时停止定时器、释放硬件资源并关闭弹层。
- LVGL dropdown 的列表挂在 screen 层。隐藏页面前必须显式 `lv_dropdown_close()`，不能只隐藏父容器。
- 长任务使用明确的加载、成功和失败状态；不可吞掉底层错误或让用户重复启动同一任务。
- 所有用户可见文案必须进入对应的多语言映射，并完成 CJK 字形检查。

更完整的设计思想、组件规则和评审清单位于 [`skill/dshanpi-edgeos-ui-design/`](skill/dshanpi-edgeos-ui-design/)。该目录可直接作为 Codex Skill 使用。

## 验证建议

提交 UI 变更前至少完成以下检查：

- 使用 `-Wall -Wextra -Werror` 交叉编译桌面与受影响子应用。
- 冷启动后检查桌面、状态栏、屏保、Wi-Fi、设置持久化和开机应用。
- 四种语言逐页检查缺字、截断、换行和控件溢出。
- 在滚动列表中区分拖动与点击，检查边缘返回按钮的命中率。
- 展开 Cloud Model、UART 等 dropdown 后直接返回，确认桌面没有残留弹层。
- 反复进入/退出相机、双摄、相册和 AI 应用，确认显示与摄像头资源能够恢复。
- 对 OTA 分别验证无更新、签名错误、摘要错误、断网、存储不足、写入失败和正常升级。
- 生成整卡镜像后在真实开发板上检查版本、触摸、显示、网络、摄像头和串口。

## 贡献

1. 将硬件操作放入 `middleware/` 或 `system/`，不要直接散落在页面回调中。
2. 新桌面入口必须有完整实现、图标、国际化文案、异常状态和返回清理逻辑。
3. 不提交 `build/`、`k230_bin/`、固件镜像、私钥、Wi-Fi 密码或设备本地配置。
4. 大型模型与媒体资源必须由 Git LFS 管理。
5. 提交说明应包含目标板、测试语言、实机验证步骤和已知限制。

## 许可证

仓库根目录中的原创代码按 [MIT License](LICENSE) 发布。

`apps/ai_demo/`、模型、字体、图片、音频、预编译库以及从 CanMV K230 SDK 或其他上游项目引入的文件，可能受各自许可证、模型条款或再分发限制约束。发布者和贡献者必须保留上游版权声明，并在商业分发或重新托管大型资源前逐项确认授权；根目录 MIT License 不会覆盖第三方材料原有的许可条件。

---

## English

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

This UI is a product system for a resource-constrained touchscreen rather than a general-purpose desktop widget library. Its main conventions are:

- Design for 640 × 480 first, keeping primary actions, status feedback, and a return path easy to reach.
- Make touch targets at least 44 × 44 px and enlarge the hit area of top-left back buttons.
- Protect cards inside scrolling containers against movement so a drag cannot launch an application.
- Implement full-screen apps as pre-created overlays that are shown and hidden; stop timers, release hardware resources, and close popups before exit.
- An LVGL dropdown list is attached to the screen layer. Call `lv_dropdown_close()` before hiding its owner page.
- Give long-running work explicit loading, success, and failure states; do not swallow lower-layer errors or allow the same task to start twice.
- Route every user-visible string through localization and verify that all required CJK glyphs are compiled into the firmware.

The complete design philosophy, component rules, and review checklist are available in [`skill/dshanpi-edgeos-ui-design/`](skill/dshanpi-edgeos-ui-design/). The directory can be installed directly as a Codex Skill.

## Validation recommendations

Before submitting a UI change, complete at least the following checks:

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
