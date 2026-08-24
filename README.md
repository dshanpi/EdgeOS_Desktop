# DshanPI EdgeOS Desktop

**简体中文** | [English](README_EN.md)

DshanPI EdgeOS Desktop 是面向 DshanPI CanMV-K230 V3 的嵌入式桌面与 AI 应用启动环境。项目基于 LVGL 和 RT-Smart，针对 640 × 480 触摸屏设计，将相机、相册、系统设置、UART/VAXP 调试、云模型部署和端侧 AI 应用组织为一致的桌面体验。

## 界面预览

以下截图均来自开发板实际运行画面。点击图片可查看 640 × 480 原图。

### 桌面系统

| 桌面首页 | AI 应用 |
| --- | --- |
| [<img src="assets/screenshots/desktop-home.jpg" alt="桌面首页" width="100%">](assets/screenshots/desktop-home.jpg) | [<img src="assets/screenshots/desktop-ai-apps.jpg" alt="AI 应用桌面" width="100%">](assets/screenshots/desktop-ai-apps.jpg) |
| **相机、双摄、人脸应用入口** | **手部、人体、驾驶、OCR 与 YOLO 应用入口** |
| 桌面工具 | 扩展能力 |
| [<img src="assets/screenshots/desktop-tools.jpg" alt="桌面工具" width="100%">](assets/screenshots/desktop-tools.jpg) | [<img src="assets/screenshots/desktop-extended.jpg" alt="桌面扩展能力" width="100%">](assets/screenshots/desktop-extended.jpg) |
| **网络摄像机、画板、CV、车牌与条码工具** | **云平台模型、USB 摄像头与 UART 调试器** |

### 设置与系统管理

| 设置中心 | 系统设置 | A/B OTA 更新 |
| --- | --- | --- |
| [<img src="assets/screenshots/settings-overview.jpg" alt="设置中心" width="100%">](assets/screenshots/settings-overview.jpg) | [<img src="assets/screenshots/settings-system.jpg" alt="系统设置" width="100%">](assets/screenshots/settings-system.jpg) | [<img src="assets/screenshots/ota-update.jpg" alt="A/B OTA 更新" width="100%">](assets/screenshots/ota-update.jpg) |
| **Wi-Fi、语言、时间、启动应用、相机与 VAXP** | **系统更新、电源、休眠与设备信息** | **安全清单校验与网络下载** |

### 实际应用界面

| 实例分割 | YOLO 多版本 | 网络摄像机 |
| --- | --- | --- |
| [<img src="assets/screenshots/object-segmentation.jpg" alt="实例分割应用" width="100%">](assets/screenshots/object-segmentation.jpg) | [<img src="assets/screenshots/yolo-models.jpg" alt="YOLO 多版本应用" width="100%">](assets/screenshots/yolo-models.jpg) | [<img src="assets/screenshots/network-camera.jpg" alt="网络摄像机应用" width="100%">](assets/screenshots/network-camera.jpg) |
| **实时目标检测与实例分割结果** | **YOLOv5、YOLOv8、YOLO11、YOLO26 模型切换** | **双摄拼接与 RTSP/RTMP 服务地址** |

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

## VAXP Host SDK

如需在外部主机上接收并解析 EdgeOS Desktop 通过 UART 输出的 VAXP 协议数据，请参考独立项目 [dshanpi/vaxp-host-sdk](https://github.com/dshanpi/vaxp-host-sdk)。该项目提供 Linux MPU 和 STM32 MCU 两套示例程序，以及对应的编译、烧录、波特率配置和 API 使用说明。

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
| 存储 | 推荐使用板载至少 8 GB eMMC；OTA 前 `/data` 可用空间须大于 OTA KDIMG |

本仓库依赖 CanMV K230 SDK 提供的 LVGL、MPP、RT-Smart HAL、Mbed TLS、cJSON、nncase、OpenCV 和固件打包工具。VAXP 协议头文件已随仓库放在 `third_party/vaxp/include/`，不再依赖 SDK 目录之外的本地文件。单独克隆本仓库不能在普通 Linux 主机上直接构建或运行桌面程序。

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
├── third_party/vaxp/          随仓库发布的 VAXP 1.0 协议头文件
├── tools/                     SDK 补丁、兼容性检查与应用集成脚本
├── sdk/                       锁定版本的可复现 SDK 补丁集与清单
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
git clone https://github.com/dshanpi/EdgeOS_Desktop.git
cd EdgeOS_Desktop
git lfs pull
git lfs fsck
```

`.gitattributes` 已为常见二进制资源启用 Git LFS。`build/`、`k230_bin/`、固件镜像和编译中间文件不会进入版本库。

仅浏览开发代码时可以克隆 `main`；用于生成固件时必须同时固定 SDK 清单和应用标签。不要把浮动 `main` 与任意“最新版”SDK 混合构建。

## 在 CanMV K230 SDK 中构建

面向普通用户的完整流程见 [`docs/BUILD_RTOS_K230.md`](docs/BUILD_RTOS_K230.md)，英文版见 [`docs/BUILD_RTOS_K230_EN.md`](docs/BUILD_RTOS_K230_EN.md)。文件名为了旧链接兼容而保留，当前流程只支持 `canmv_k230`。指南包含主机依赖、锁定 SDK 检出、工具链、Git LFS、方案 A、配置、全量构建、产物检查和故障排查。板级适配方法见 [`docs/PORTING_CANMV_K230.md`](docs/PORTING_CANMV_K230.md)，构建与板端验证状态见 [`docs/validation/README.md`](docs/validation/README.md)，使用 LYNX 写入板载 eMMC 的步骤见 [`docs/FLASH_LYNX.md`](docs/FLASH_LYNX.md)。

> [!CAUTION]
> `edgeos-sdk-v1.0.0` 虽然可以完成编译和镜像打包，但 Python genimage 在最终分区布局解析前缓存了 TOC，导致 `spl`、`uboot` 的长度错误地保留为 0；严格启动校验因此在开发板上报 `K230 boot: invalid TOC entry 1`，随后出现回退路径的 `no mkimage signature but raw image not supported`。该版本已撤回，不得继续烧录或分发其 `v0.7.5` 固件；请使用 `edgeos-sdk-v1.0.2` 重新构建产品版本 `v0.7.7`。

当前发布只支持由本项目锁定 manifest 创建的 `canmv_k230` SDK 工作区，构建目标是 `k230_canmv_dongshanpi_edgeos_defconfig`。任意最新 SDK、其他 K230 SDK 分支或仅修改工作区目录名，都不在当前支持范围内。

`edgeos-sdk-v1.0.2` / `v0.7.7` 已通过 `canmv_k230` 全量构建、完整镜像结构检查、LYNX eMMC 写入和板端启动；双路 GC2093、ST7701、CST128、SDIO Wi-Fi、IP 和 NTP 已从最终固件的实机日志确认，且未出现 invalid TOC、Page Fault 或 Illegal Instruction。优化后的触摸主观手感仍等待用户实机验收，未被提前标记为通过。详细证据见 [`docs/validation/canmv-k230-edgeos-sdk-v1.0.2.md`](docs/validation/canmv-k230-edgeos-sdk-v1.0.2.md)。

推荐将仓库直接放在 SDK 的应用目录中：

```text
canmv_k230/
└── src/applications/EdgeOS_Desktop/
    ├── apps/
    ├── middleware/
    ├── system/
    ├── uart/
    └── Makefile
```

例如：

```bash
(
set -e
cd /path/to/canmv_k230/src/applications
git clone --branch edgeos-sdk-v1.0.2 \
  https://github.com/dshanpi/EdgeOS_Desktop.git
cd EdgeOS_Desktop
test "$(git rev-parse HEAD)" = \
  "$(git rev-list -n1 edgeos-sdk-v1.0.2)"
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

专用配置 `k230_canmv_dongshanpi_edgeos_defconfig` 会自动启用 `DshanPI EdgeOS Desktop`，无需再手工修改配置。需要检查或调整功能时可另外执行 `make menuconfig`；此时应用会显示在 `Applications Configuration` 下。SDK 从本仓库的 Kconfig 发现菜单项，集成脚本则根据当前目录名幂等更新父目录的 `apps.mk`，将该选项映射到实际构建目录。菜单发现和参与构建是两个独立步骤，因此在新的 SDK 中或重置 `apps.mk` 后需要重新运行集成脚本。

仓库必须是 `src/applications/` 的直属子目录，但目录名可以自定义。无论源码目录叫什么，最终桌面启动文件都固定安装为 `/sdcard/app/dshanpi_aimodel`，以兼容 OTA 和子应用返回桌面的路径。这里的 `/sdcard` 是 RT-Smart 的逻辑挂载路径，SDK 中的 `images/sdcard/` 同样只是镜像构建目录名；两者都不表示固件必须写入物理 microSD/TF 卡。

> **SDK 兼容性：** 本仓库的 [`sdk/`](sdk/) 提供了可复现的方案 A 补丁集，包括播放器、触摸点击过滤、A/B OTA、媒体镜像、系统版本和专用产品配置等 SDK 扩展。补丁严格锁定 [`sdk/manifests/upstream-lock.xml`](sdk/manifests/upstream-lock.xml) 记录的 24 个官方上游版本，不用于任意版本的 SDK：`--check` 会在不改动源码的情况下检查完整锁定版本、工作区、补丁完整性和确定性重放结果，全部预检通过后才可用 `--apply` 一次性应用这组相互依赖的补丁。不要只挑选其中某个补丁，也不要在版本不匹配时强制套用。创建全新 SDK 时不要从浮动 CanMV 分支直接同步；请使用 [`sdk/README.md`](sdk/README.md) 中以 `edgeos-sdk-v1.0.2` 标签作为 repo manifest 源的锁定流程。

EdgeOS Desktop 不依赖 WebRTC，专用 defconfig 已将其禁用，以免引入与本产品无关的依赖；OTA 使用的 Mbed TLS 等功能仍会正常启用。

全新主机应先在 SDK 根目录运行 `make dl_toolchain`。SDK 顶层 Makefile 使用 `SDK_TOOLCHAIN_DIR` 作为工具链根目录，EdgeOS 独立子应用脚本使用 `K230_TOOLCHAIN_BIN` 作为 musl 工具链的 `bin` 目录。默认都位于 `$HOME/.kendryte/k230_toolchains/`；非默认安装示例：

```bash
export SDK_TOOLCHAIN_DIR=/opt/k230_toolchains
export K230_TOOLCHAIN_BIN="$SDK_TOOLCHAIN_DIR/riscv64-linux-musleabi_for_x86_64-pc-linux-gnu/bin"
```

## 构建固件

完成上一节的 SDK 补丁、集成、专用 defconfig 和兼容性检查后，在 SDK 根目录构建（如果已经执行了上一节命令块的最后一行，则无需重复）：

```bash
cd /path/to/canmv_k230
bash -o pipefail -c 'time make 2>&1 | tee edgeos-build.log'
```

部分 SDK 的 `make log` 在内部使用 `make | tee` 但没有启用 `pipefail`，因此内层编译失败时命令仍可能返回 0。上面的命令既保留日志，也会正确传递失败状态。如果整包构建在进入 EdgeOS 之前失败，可以运行 `make app` 单独诊断应用链路；日志出现 `[BUILD] applications EdgeOS_Desktop` 才表示已进入本仓库的构建。

构建成功后，日志末尾应出现：

```text
Build K230 done, board k230_canmv_dongshanpi, config k230_canmv_dongshanpi_edgeos_defconfig
```

典型产物位于：

```text
output/k230_canmv_dongshanpi_edgeos_defconfig/
├── DshanPI_EdgeOS_Desktop_v0.7.7.img
├── DshanPI_EdgeOS_Desktop_v0.7.7_ota.kdimg
└── ...对应的 .gz 与摘要文件
```

当前产品版本为 `v0.7.7`，由 SDK 板级文件 `boards/k230_canmv_dongshanpi/system-version.txt` 统一管理，不应在 UI 或发布脚本中重复维护。

### 使用 LYNX 写入 eMMC

开发板首次安装、恢复或完整重刷时，应在 LYNX 中选择 K230、`EMMC`（K230 SDIO0），使用解压后的完整 `DshanPI_EdgeOS_Desktop_v0.7.7.img`，并从地址/偏移 `0x0` 写入整个镜像。完整镜像已经包含启动链、TOC 和固定布局，不能改为从 `0xe0000`、`0x100000` 等内部偏移开始写入。

`DshanPI_EdgeOS_Desktop_v0.7.7_ota.kdimg` 仅供已经正常运行的 EdgeOS A/B OTA 客户端写入非活动槽，不是出厂/恢复镜像，**禁止在 LYNX 中烧录**。`.img.gz` 也不能直接写入，必须先解压得到 `.img`。完整操作、串口验收和故障判断见 [`docs/FLASH_LYNX.md`](docs/FLASH_LYNX.md)。

全量构建后应再次运行 `tools/check_sdk_compat.sh` 并显式提供交叉 `nm`，以检查生成的播放器静态库 ABI；完整命令和本次产物大小、SHA-256、分区表见 `canmv_k230` 构建指南和验证记录。

### 仅构建应用

完成 SDK 配置和基础库构建后，可从 SDK 根目录执行：

```bash
make -C src/applications/EdgeOS_Desktop
```

如果克隆时使用了其他目录名，请相应替换命令中的 `EdgeOS_Desktop`。该目标会构建桌面以及 Makefile 中启用的 AI 子应用。多个子应用共享模型资源和输出目录，修改并行构建规则时要保留现有依赖顺序。

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

OTA 发布顺序应为：先上传 KDIMG，再上传 `latest.json.sig`，最后原子替换 `latest.json`。设备端使用未压缩 `.kdimg`；`.kdimg.gz` 仅适合发布系统外部传输，不能直接作为设备下载目标。`*_ota.kdimg` 只属于运行中的 A/B OTA 流程，不能交给 LYNX 作为完整启动镜像烧录。

## UI 开发约定

- 以 640 × 480 为设计基准，优先保证首要操作、返回路径和状态反馈。
- 可点击目标至少 44 × 44 px；左上返回按钮应扩大点击区域。
- 可滚动卡片使用移动距离保护，避免拖动结束时误触应用。
- 全屏应用使用预创建并隐藏/显示的 overlay；退出时停止定时器、释放硬件资源并关闭弹层。
- LVGL dropdown 的列表挂在 screen 层。隐藏页面前必须显式 `lv_dropdown_close()`，不能只隐藏父容器。
- 长任务使用明确的加载、成功和失败状态；不可吞掉底层错误或让用户重复启动同一任务。
- 所有用户可见文案必须进入对应的多语言映射，并完成 CJK 字形检查。

更完整的设计思想、组件规则和评审清单位于 [`skill/dshanpi-edgeos-ui-design/`](skill/dshanpi-edgeos-ui-design/)。该目录可直接作为 Codex Skill 使用。

## 验证建议

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

`sdk/` 中的补丁保留其所修改上游项目和源文件的原有版权与许可证；仓库根目录的 MIT License 不会对这些上游补丁重新许可。
