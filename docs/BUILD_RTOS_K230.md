# 在 `rtos_k230` 中构建 EdgeOS Desktop

本文面向需要把 EdgeOS Desktop 集成到 K230 RTOS SDK 并生成完整固件的用户。
`rtos_k230` 与 `canmv_k230` 只是本地目录名；只要工作区由本项目锁定的 K230
RTOS/CanMV repo manifest 创建，构建目标仍然使用
`k230_canmv_dongshanpi_edgeos_defconfig`。

本流程固定以下版本组合：

| 组件 | 固定版本 |
| --- | --- |
| EdgeOS Desktop | `edgeos-sdk-v1.0.0` |
| EdgeOS 提交 | `18df75d569bd5ecdfd8ccec8d37bf343e530533d` |
| 上游 manifest | `d207027db3ae457cd43629c80b8a42e3b79fd51a` |
| SDK 项目 | `sdk/manifests/upstream-lock.xml` 中 24 个不可变 revision |
| 目标板 | DshanPI CanMV-K230 V3 |

不要把这组补丁强制应用到任意“最新版”SDK，也不要只挑选其中一部分。

## 1. 主机准备

建议使用 Ubuntu 20.04 或 22.04 x86_64。安装 K230 SDK 的常用主机依赖、
Git LFS 和 repo：

```bash
sudo apt-get update
sudo apt-get install -y --no-install-recommends \
  make autoconf automake bison flex gcc g++ gawk libncurses5-dev \
  pkg-config libconfuse-dev libssl-dev python3 python3-pip \
  python-is-python3 cmake libyaml-dev scons mtools bzip2 curl git \
  git-lfs openssh-client rsync dosfstools ca-certificates wget fdisk
pip3 install pycryptodome gmssl scons==3.1.2

mkdir -p "$HOME/.bin"
export PATH="$HOME/.bin:$PATH"
curl -L https://raw.githubusercontent.com/canmv-k230/git-repo/stable/repo \
  -o "$HOME/.bin/repo"
chmod a+rx "$HOME/.bin/repo"
git lfs install
```

请确认至少预留约 20 GiB 可用空间。完整 EdgeOS 构建在本次验证中生成约
6.3 GiB 的输出目录，源码、工具链、repo 对象和临时镜像还需要额外空间。

## 2. 创建锁定的 SDK 工作区

推荐创建全新工作区。以下目录名有意使用 `rtos_k230`，以说明它不会改变
SDK 类型或构建目标：

以下命令要求 `$HOME/rtos_k230` 尚不存在；如果 `mkdir` 或后续任一步失败，
立即停止，不要在错误目录继续执行：

```bash
mkdir "$HOME/rtos_k230" &&
cd "$HOME/rtos_k230" &&
repo init \
  -u https://github.com/dshanpi/EdgeOS_Desktop.git \
  -b refs/tags/edgeos-sdk-v1.0.0 \
  -m sdk/manifests/upstream-lock.xml \
  --repo-url=https://github.com/canmv-k230/git-repo.git &&
repo sync -c -j"$(nproc)"
```

这里必须写完整的 `refs/tags/edgeos-sdk-v1.0.0`。只写
`-b edgeos-sdk-v1.0.0` 会被 repo 当成分支名并导致初始化失败。

检查工作区，然后下载 SDK 工具链：

```bash
repo status
make dl_toolchain
```

全新同步后，`repo status` 不应显示普通项目的 tracked 修改。嵌套 repo 目录
可能显示为未跟踪项，这是 repo 多项目工作区的正常表现。

## 3. 克隆并校验 EdgeOS

仓库必须是 `src/applications/` 的直属子目录：

```bash
(
set -e
cd "$HOME/rtos_k230/src/applications"
git clone --branch edgeos-sdk-v1.0.0 \
  https://github.com/dshanpi/EdgeOS_Desktop.git
cd EdgeOS_Desktop

test "$(git rev-parse HEAD)" = \
  "18df75d569bd5ecdfd8ccec8d37bf343e530533d"
printf 'Verified EdgeOS commit: %s\n' "$(git rev-parse HEAD)"
git describe --tags --exact-match HEAD
git lfs pull
git lfs fsck
)
```

最后两条命令很重要。本仓库包含大量由 Git LFS 管理的模型、字体、图片和
预编译资源；未下载的 LFS pointer 不能作为真实资源打入镜像。

## 4. 校验并应用方案 A 补丁

先执行只读预检，再一次性应用全部六组 SDK 补丁：

```bash
cd "$HOME/rtos_k230/src/applications/EdgeOS_Desktop" &&
./tools/apply_sdk_patches.sh --check &&
./tools/apply_sdk_patches.sh --apply &&
./tools/integrate_canmv_sdk.sh
```

`--check` 会校验补丁 SHA-256、24 个 SDK 项目、六个精确基线、确定性
HEAD/tree、Git 操作状态和未跟踪文件冲突。任何一项失败时都不要强制继续。

补丁会在六个 SDK Git 项目中生成本地提交。应用后不要对这个工作区盲目执行
浮动分支的 `repo sync`、reset 或 clean；升级时应使用新发布标签创建新的锁定
工作区。

## 5. 选择产品配置

回到 SDK 根目录，使用专用 EdgeOS defconfig：

```bash
cd "$HOME/rtos_k230"
make k230_canmv_dongshanpi_edgeos_defconfig &&
./src/applications/EdgeOS_Desktop/tools/check_sdk_compat.sh
```

即使 SDK 根目录叫 `rtos_k230`，也不要改用猜测的 `k230_rtos_*` 目标。
正确配置应至少包含：

```text
CONFIG_BOARD_CONFIG_NAME="k230_canmv_dongshanpi_edgeos_defconfig"
CONFIG_RT_PARTITION_NUMBER=4
CONFIG_RTSMART_3RD_PARTY_ENABLE_LVGL=y
CONFIG_APP_ENABLE_LVGL_LAUNCHER=y
```

专用配置已经启用 EdgeOS。`make menuconfig` 只用于检查或自定义；应用位于
`Applications Configuration` 下。如果只想确认菜单，退出后重新运行上述
defconfig，避免意外保存其他选项。

菜单发现和参与构建是两个步骤：`Kconfig` 提供菜单项，
`tools/integrate_canmv_sdk.sh` 在 `src/applications/apps.mk` 中注册实际目录。

## 6. 全量构建

必须使用能传递内部 make 失败状态的命令：

```bash
cd "$HOME/rtos_k230"
bash -o pipefail -c \
  'time make 2>&1 | tee edgeos-v1.0.0-build.log'
```

不要把 `make app` 当成最终验收。它适合在基础库已经生成后诊断应用；正式
结论必须来自全量 `make` 的退出码 0，并且日志末尾出现：

```text
Build K230 done, board k230_canmv_dongshanpi, config k230_canmv_dongshanpi_edgeos_defconfig
```

构建后再次执行兼容性检查。这次会同时检查已生成的播放器静态库符号：

```bash
K230_TOOLCHAIN_NM="${SDK_TOOLCHAIN_DIR:-$HOME/.kendryte/k230_toolchains}/riscv64-linux-musleabi_for_x86_64-pc-linux-gnu/bin/riscv64-unknown-linux-musl-nm" \
  ./src/applications/EdgeOS_Desktop/tools/check_sdk_compat.sh
```

## 7. 验证产物

```bash
cd "$HOME/rtos_k230/output/k230_canmv_dongshanpi_edgeos_defconfig"

test -s DshanPI_EdgeOS_Desktop_v0.7.5.img
test -s DshanPI_EdgeOS_Desktop_v0.7.5_ota.kdimg
sha256sum \
  DshanPI_EdgeOS_Desktop_v0.7.5.img \
  DshanPI_EdgeOS_Desktop_v0.7.5_ota.kdimg
fdisk -l DshanPI_EdgeOS_Desktop_v0.7.5.img
sed -n '1,120p' images/sdcard/revision.txt
```

完整镜像应包含 20 MiB `bin`、1 GiB `app_a` 和 1 GiB `app_b` 三个预创建
FAT 分区。设备第一次从至少 8 GB 的 microSD 启动时，RT-Smart 会从剩余空间
创建第四个 `/data` 分区、重启一次重新扫描分区表，然后格式化并挂载它。
第一次启动期间不要断电。

设备 OTA 使用未压缩的 `DshanPI_EdgeOS_Desktop_v0.7.5_ota.kdimg`；不要把
`.kdimg.gz` 直接配置成设备下载目标。下载 OTA 前，`/data` 的可用空间必须
大于未压缩 KDIMG；本次产物约 1.02 GiB，实际部署还应预留额外余量。

## 8. 非默认工具链目录

SDK 顶层构建和 EdgeOS 子应用使用两个不同变量。工具链不在默认目录时，用本节
完整命令替代第 6 节的构建和构建后检查：

```bash
(
set -e
export SDK_TOOLCHAIN_DIR="$HOME/k230_toolchains"
export K230_TOOLCHAIN_BIN="$SDK_TOOLCHAIN_DIR/riscv64-linux-musleabi_for_x86_64-pc-linux-gnu/bin"

cd "$HOME/rtos_k230"
make dl_toolchain
make k230_canmv_dongshanpi_edgeos_defconfig
bash -o pipefail -c 'time make 2>&1 | tee edgeos-v1.0.0-build.log'
K230_TOOLCHAIN_NM="$K230_TOOLCHAIN_BIN/riscv64-unknown-linux-musl-nm" \
  ./src/applications/EdgeOS_Desktop/tools/check_sdk_compat.sh
)
```

- `SDK_TOOLCHAIN_DIR`：SDK 顶层 Makefile 使用的工具链根目录。
- `K230_TOOLCHAIN_BIN`：EdgeOS 独立子应用构建脚本使用的 musl `bin` 目录。

在 defconfig、兼容性检查和构建命令中保持这两个变量一致。

## 9. 已有 SDK 的安全处理

推荐全新工作区。如果必须复用已有目录：

1. 先保存自己的源码和日志并运行 `repo status`。
2. 在 EdgeOS 中运行 `./tools/apply_sdk_patches.sh --check`。
3. 出现 revision mismatch 时停止，不要强制打补丁；使用第 2 节重建。
4. 旧的普通 DshanPI output 不要作为 EdgeOS 验收结果。

如果补丁前已经运行过集成脚本，SDK 根仓库可能只有 `apps.mk` 一项未暂存的
tracked 修改。只有在下面两个 `test` 都成功，而且人工确认 diff 只有集成脚本
生成的 EdgeOS 映射时，才使用这段恢复流程。它把旧 diff 保存到唯一的临时
文件；补丁后由幂等集成脚本重新生成映射，不会误弹出用户已有的 Git stash：

```bash
(
set -e
cd "$HOME/rtos_k230"
test -z "$(git diff --cached --name-only)"
test "$(git diff --name-only)" = "src/applications/apps.mk"
git diff -- src/applications/apps.mk

edgeos_apps_backup=
trap 'if [ -n "$edgeos_apps_backup" ]; then printf "apps.mk backup kept at %s\\n" "$edgeos_apps_backup"; fi' EXIT
edgeos_apps_backup=$(mktemp "${TMPDIR:-/tmp}/edgeos-apps-mk.XXXXXX.patch")
git diff --binary -- src/applications/apps.mk > "$edgeos_apps_backup"
test -s "$edgeos_apps_backup"
git apply --check --reverse "$edgeos_apps_backup"
git restore --worktree -- src/applications/apps.mk

cd src/applications/EdgeOS_Desktop
./tools/apply_sdk_patches.sh --check
./tools/apply_sdk_patches.sh --apply

cd ../../..
./src/applications/EdgeOS_Desktop/tools/integrate_canmv_sdk.sh
git diff -- src/applications/apps.mk
)
```

不要用这段命令隐藏其他项目的修改；`--check` 仍应作为是否继续的唯一门禁。
确认新的映射正确且构建完成前，保留打印出的备份文件。

## 10. 常见问题

| 现象 | 原因 | 处理 |
| --- | --- | --- |
| `fatal error: kplayer.h` | SDK 未应用完整方案 A | 回到精确锁定基线，运行 `--check` 和 `--apply` |
| 缺少 `lv_k230_touch_accept_click()` 或 `k230_ota_get_status()` | 只复制了应用或只打了部分补丁 | 使用完整六项目补丁集，不要挑选补丁 |
| `Applications Configuration` 中没有 EdgeOS | 仓库不在 `src/applications/` 直属目录，或未重新生成 Kconfig | 调整目录后重新执行 defconfig/menuconfig |
| 菜单中存在但没有进入 `[BUILD] applications EdgeOS_Desktop` | `apps.mk` 未注册 | 运行 `tools/integrate_canmv_sdk.sh` |
| `revision mismatch` | SDK 不属于 v1.0.0 的 24 项锁定基线 | 新建锁定工作区，不要 force |
| 模型文件很小或仍是 LFS pointer | 未完成 Git LFS 下载 | `git lfs pull && git lfs fsck` |
| 找不到交叉编译器 | 工具链未下载或路径变量不一致 | `make dl_toolchain`，检查两个工具链变量 |
| DTLS-SRTP/Mbed TLS undefined reference | 复用了配置变化前的旧 Mbed TLS 对象，或使用了普通 defconfig | 使用 EdgeOS defconfig；必要时按下方命令定向清理 |
| 生成 app FAT 镜像时报空间不足 | 使用了旧的 512 MiB 布局或资源不完整 | 确认完整方案 A 和 1 GiB A/B 配置 |

仅在复用旧 SDK 且确认是 Mbed TLS 旧对象时执行定向清理：

```bash
cd "$HOME/rtos_k230"
make -C src/rtsmart/libs/3rd-party/mbedtls/mbedtls/library clean
```

本项目的实际 `rtos_k230` 验证数据见
[`validation/rtos-k230-edgeos-sdk-v1.0.0.md`](validation/rtos-k230-edgeos-sdk-v1.0.0.md)。
