# EdgeOS Desktop 的 CanMV K230 SDK 适配与补丁重放

本文记录将 EdgeOS Desktop 适配到 DshanPI CanMV-K230 V3 的可复现工程
流程。当前公开支持范围只包括本项目锁定的 `canmv_k230` SDK、
`k230_canmv_dongshanpi_edgeos_defconfig` 和板载 eMMC。旧文档中的
`rtos_k230` 实验记录仅作历史审计，不表示当前仍支持该工作区。

面向普通用户的完整命令见 [`BUILD_RTOS_K230.md`](BUILD_RTOS_K230.md)；
该文件名为了旧链接兼容而保留，内容已是 canmv-only 流程。

## 1. 先区分 defconfig 与 SDK 补丁

`deconfig` 通常是对 `defconfig` 的误写；Kconfig/RT-Thread/Linux 常见流程中的
标准名称是 **defconfig**。相关文件的职责如下：

| 项目 | 作用 | 不能做什么 |
| --- | --- | --- |
| `Kconfig` | 声明可选功能、依赖、菜单文案与默认值 | 不会自动把应用加入 `apps.mk`，也不会实现缺失 API |
| `defconfig` | 一份可评审、可重放的产品配置输入，用 `make <name>_defconfig` 生成当前配置 | 只能选择已经存在的代码与功能，不能生成头文件、驱动或函数 |
| `.config` | 当前工作区实际生效的展开配置，可由 defconfig 或 `menuconfig` 产生 | 它是构建产物，不应取代版本化的 defconfig |
| SDK patch | 修改 SDK 及其嵌套 Git 项目的源码、头文件、构建脚本、驱动、引脚与固件打包 | 不是单一板级选项，不能用一份 defconfig 代替 |

因此，`fatal error: kplayer.h: No such file or directory`、缺少
`lv_k230_touch_accept_click()` 或 `k230_ota_get_status()` 都不是“换一份
defconfig”就能修复的问题。它们说明 SDK 没有应用完整的方案 A
补丁，或应用与 SDK 版本不匹配。

正确顺序固定为：

```text
锁定 SDK 基线 → 完整重放 SDK 补丁 → 注册应用目录
                → 加载 EdgeOS defconfig → 兼容性检查 → 全量构建
```

## 2. 从锁定 CanMV SDK 基线开始

不要用浮动 `main` 或“今天最新的 `repo sync`”作为发布基线。SDK 顶层
和嵌套项目会同时变化，而一组邮件补丁只能对应明确的父提交。
发布用户应通过 EdgeOS 不可变标签中的 manifest 创建工作区：

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

`sdk/manifests/upstream-lock.xml` 中的 revision 是锁定基线，
`sdk/manifest.json` 记录补丁重放规则，`sdk/SHA256SUMS` 保护公开补丁不被
静默篡改。`repo status` 中不应有未保存的 tracked 修改。

## 3. 放置应用并取得大型资源

EdgeOS 必须位于 `src/applications/` 的直属子目录：

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

目录可以改名，但不能再套一层。`Kconfig` 仅负责在
`Applications Configuration` 中声明菜单；SDK 顶层的
`src/applications/apps.mk` 还必须建立配置符号与实际目录的映射。

## 4. 将方案 A 作为一个原子单元重放

先运行只读检查，只有全部通过才应用：

```bash
cd "$HOME/canmv_k230/src/applications/EdgeOS_Desktop"
./tools/apply_sdk_patches.sh --check
./tools/apply_sdk_patches.sh --apply
./tools/integrate_canmv_sdk.sh
```

补丁集跨越 SDK 顶层与多个嵌套 Git 项目，覆盖的板级职责包括：

- 产品版本、A/B 分区、完整镜像/OTA 打包、最终 TOC 同步和错误传递；
- DshanPI CanMV-K230 V3 的显示、GC2093 摄像头、SDIO Wi-Fi、I2C、
  背光、U-Boot pinmux 与 EdgeOS 启动配置；
- RT-Smart 引导、触摸 IRQ/采样时间戳、应用分区选择与系统接口；
- LVGL 输入/显示端口、媒体播放器、OTA、PMU、JPEG/字体和库链接；
- MPP 摄像头、VICAP/VO、播放/编码功能和显示时序；
- 对当前旋转软件渲染路径明确禁用 VG-Lite，避免引入未验证的混合路径。

不要只复制应用目录、只挑选一个看似相关的 `.patch`，也不要对
`revision mismatch` 使用 force。这些补丁是一个经过一起构建和启动验证
的兼容性单元。

## 5. 加载 defconfig 并检查菜单

```bash
cd "$HOME/canmv_k230"
make k230_canmv_dongshanpi_edgeos_defconfig
./src/applications/EdgeOS_Desktop/tools/check_sdk_compat.sh
```

发布配置至少应包含：

```text
CONFIG_BOARD_CONFIG_NAME="k230_canmv_dongshanpi_edgeos_defconfig"
CONFIG_APP_ENABLE_EDGEOS_DESKTOP=y
CONFIG_RTSMART_3RD_PARTY_ENABLE_LVGL=y
CONFIG_RT_PARTITION_NUMBER=4
# CONFIG_RTSMART_3RD_PARTY_LVGL_USE_VGLITE is not set
# CONFIG_RTSMART_HAL_ENABLE_VG_LITE is not set
```

如需查看菜单，执行 `make menuconfig`，EdgeOS 应出现在
`Applications Configuration` 下。只查看不修改时，退出后重新执行
EdgeOS defconfig，防止意外保存改变发布配置。

注意：“菜单可见”只证明 Kconfig 被发现；“构建日志出现
`[BUILD] applications EdgeOS_Desktop`”才证明 `apps.mk` 已注册并真正进入构建。

## 6. 全量构建与构建后门禁

```bash
cd "$HOME/canmv_k230"
make dl_toolchain
bash -o pipefail -c \
  'time make 2>&1 | tee edgeos-v1.0.2-build.log'

K230_TOOLCHAIN_NM="${SDK_TOOLCHAIN_DIR:-$HOME/.kendryte/k230_toolchains}/riscv64-linux-musleabi_for_x86_64-pc-linux-gnu/bin/riscv64-unknown-linux-musl-nm" \
  ./src/applications/EdgeOS_Desktop/tools/check_sdk_compat.sh
```

必须同时满足全量 `make` 退出码 0、日志末尾的完成标记与构建后
兼容性检查。`make app` 只适合诊断应用，不能代替固件发布验收。

## 7. 校验完整 `.img` 而不是只看构建退出码

```bash
cd "$HOME/canmv_k230/output/k230_canmv_dongshanpi_edgeos_defconfig"
image=DshanPI_EdgeOS_Desktop_v0.7.7.img

test -s "$image"
sha256sum "$image"
fdisk -l "$image"
python3 "$HOME/canmv_k230/src/applications/EdgeOS_Desktop/tools/check_firmware_image.py" \
  "$image"
```

只有检查器结束于下列文本才可继续烧录：

```text
PASS: EdgeOS firmware image is structurally valid.
```

当前 v1.0.2 实测镜像为 `2283798528` bytes，SHA-256 为
`77ee49eb3bc3f3483166777c7d03e56c29cc1840665706f56d69454a241fe8b8`，
11 个 TOC 项、3 个 MBR 分区和可检查负载摘要全部通过。不同时间或
工具环境的本地构建如果嵌入不同元数据，SHA-256 可能不同；此时必须保留
新摘要、运行结构检查并对该精确文件完成板端验收。

## 8. 使用 LYNX 写入板载 eMMC

详细界面操作见 [`FLASH_LYNX.md`](FLASH_LYNX.md)。关键门禁是：

1. 在主机上记录未压缩完整 `.img` 的文件大小和 SHA-256。
2. 将镜像传输到 LYNX 后再次核对摘要，避免同名缓存命中旧固件。
3. 通过串口命令 `reboot_to_upgrade`，或按板卡硬件说明操作升级键/
   BootROM strap 与 `RST`，进入 K230 下载模式。不要把其他 SoC 的按键名称套用到 K230。
4. 扫描并确认具体 K230 USB 目标；本次验证为 USB `2:8`、串口 `COM9`。
5. 介质选择 `EMMC`/SDIO0，输入选择完整 `.img`，从介质偏移 `0x0` 写入。
6. 烧录任务退出码 0 后重启，从上电开始保留完整串口日志。

不得用 `_ota.kdimg`、`.img.gz` 或 `.kdimg.gz` 作为 LYNX 原始镜像，
也不得把 `0xe0000`、`0x100000` 等镜像内部位置当成写入起点。

## 9. 板端启动与交互验收

自动日志与人工交互要分开记录：

- 启动链：SPL → U-Boot → RT-Smart → EdgeOS Desktop；不得出现
  `K230 boot: invalid TOC entry 1`。
- 显示：ST7701 完成初始化，640 × 480 画面方向、颜色和背光正常。
- 触摸：CST128 被识别，坐标方向正确；人工检查慢拖、快滑、反向滑动、
  惯性滚动、滑动后 50–150 ms 点击、快速连点与双击。
- 摄像头：GC2093 完成探测，前/后摄像头进入、切换、退出后画面可恢复。
- Wi-Fi：SDIO 设备初始化、关联、DHCP/IP 获取和 NTP 同步成功。
- 稳定性：持续观察串口，检查屏保、相册滚动、摄像头切换和 Wi-Fi 界面。

设备启动并不代表触摸手感自动通过。当前自动/日志证据和明确的待验收项见
[`validation/canmv-k230-edgeos-sdk-v1.0.2.md`](validation/canmv-k230-edgeos-sdk-v1.0.2.md)。

## 10. `repo sync` 之后如何处理

补丁应用脚本会在多个 SDK Git 项目中生成本地提交。`repo sync` 是更新/对齐
repo 项目的操作，它不是 EdgeOS 补丁管理器，也不会保证本地板级适配仍然存在。

发布用户的专业做法是新建工作区：

1. 保留当前可运行工作区和日志，不在其上盲目 reset/clean。
2. 使用新 EdgeOS 标签的锁定 manifest 创建新 `canmv_k230`。
3. 先 `repo sync`到锁定基线，再依次执行 `--check`、`--apply`、集成脚本、
   EdgeOS defconfig 和全量构建。
4. 新固件完成结构、烧录、启动和外设验收后，再退役旧工作区。

如果只是在同一个不可变 manifest 上重建，也必须先用 `repo status` 确认
没有用户修改。对齐回清单中的精确 revision 后，从 EdgeOS 目录重新运行
`--check` 和 `--apply`。不要为了让补丁“能打上”而忽略 revision mismatch。

维护者如果要跟进新的上游 CanMV SDK，应在临时工作区中将每个邮件补丁
rebase 到新父提交，审查树 diff，更新 manifest 和 SHA-256，从新基线进行
确定性重放，再重复全量构建与实机验收。一次 `repo sync` 不等于完成了这些适配工作。

## 11. 排障快查

| 现象 | 根因 | 处理 |
| --- | --- | --- |
| `fatal error: kplayer.h` | 应用被复制了，但媒体 SDK 补丁未应用 | 回到锁定基线，完整执行 `--check` / `--apply` |
| 缺少 `lv_k230_touch_accept_click()` / `k230_ota_get_status()` | 只打了部分补丁或应用与 SDK 不匹配 | 不要补单个头文件；重放整套方案 A |
| `Applications Configuration` 中没有 EdgeOS | 目录层级错误或 Kconfig 未重新生成 | 确保 EdgeOS 是 `src/applications/` 直属子目录，重跑 defconfig/menuconfig |
| 菜单可见，日志无 `[BUILD] applications EdgeOS_Desktop` | `apps.mk` 没有注册 | 重跑 `tools/integrate_canmv_sdk.sh` |
| `revision mismatch` | SDK 不是发布锁定基线，或 `repo sync` 后项目已变 | 新建锁定工作区；禁止 force |
| 触摸方向错、不起滑、滑动后点击丢失 | 板级 IRQ 边沿/坐标、旧输入端口或旧应用二进制 | 确认嵌套 defconfig 和触摸补丁已重放，清除受影响对象后重建并烧录精确新 SHA |
| 摄像头或 Wi-Fi 在 `repo sync` 后失效 | 板级配置或 pinmux/MPP 补丁被更新覆盖 | 从锁定基线重放整套补丁，核对 defconfig 和兼容性检查 |
| `K230 boot: invalid TOC entry 1` | 使用了已撤回/旧/损坏镜像，或最终 TOC 未同步 | 停止反复烧录；检查精确 `.img` 的 SHA 和结构，从 `0x0` 重刷 eMMC |
| LYNX 成功但仍启动旧系统 | 命中 LYNX 旧缓存、写入介质/偏移错误或 TF 卡干扰 | 对比本地/传输端 SHA，选 EMMC/SDIO0 与 `0x0`，拔除 TF 卡 |

所有修复都应落到可评审的应用提交、SDK 邮件补丁、锁定 manifest、
补丁摘要与验证记录，而不应只保留在某台开发机的未跟踪 diff 中。
