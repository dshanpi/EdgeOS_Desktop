# EdgeOS SDK patch set / EdgeOS SDK 补丁集

This directory contains the complete, version-locked SDK change set required
by EdgeOS Desktop. It is intentionally separate from the application source:
the application remains buildable from a public checkout, while every SDK
change is reviewable and reproducible from an official CanMV K230 baseline.

本目录提供 EdgeOS Desktop 所需的完整、版本锁定 SDK 改动。应用源码与 SDK
改动保持分离；任何人都可以从公开仓库取得应用，并从官方 CanMV K230 基线
可复现地重建同一组 SDK 源码树。

## Fresh checkout / 全新检出

The safest workflow uses the EdgeOS release tag itself as the repo manifest
source. This makes `repo sync` consume the 24 immutable revisions in
`upstream-lock.xml`, rather than the moving branches in the upstream CanMV
manifest:

最安全的做法是直接将 EdgeOS 发布标签作为 repo manifest 源。这样 `repo sync`
会使用 `upstream-lock.xml` 中 24 个不可变 revision，而不是 CanMV 上游 manifest
里的浮动分支：

```bash
mkdir canmv_k230 && cd canmv_k230
repo init -u https://github.com/dshanpi/EdgeOS_Desktop.git \
  -b refs/tags/edgeos-sdk-v1.0.0 \
  -m sdk/manifests/upstream-lock.xml
repo sync -c -j"$(nproc)"

cd src/applications
git clone --branch edgeos-sdk-v1.0.0 \
  https://github.com/dshanpi/EdgeOS_Desktop.git
cd EdgeOS_Desktop
git lfs pull
./tools/apply_sdk_patches.sh --check
./tools/apply_sdk_patches.sh --apply
./tools/integrate_canmv_sdk.sh

cd ../../..
make k230_canmv_dongshanpi_edgeos_defconfig
./src/applications/EdgeOS_Desktop/tools/check_sdk_compat.sh
bash -o pipefail -c 'time make 2>&1 | tee edgeos-build.log'
```

`--check` never modifies an SDK. It verifies the complete SHA-256 inventory,
all 24 locked SDK projects, exact base revisions, deterministic patched commit
and tree IDs, worktree state, unfinished Git operations, and untracked paths
that would collide with patch additions. Every series is applied in a
temporary shared clone before any real project changes. `--apply` repeats that
full preflight before changing any project. Re-running either command is safe:
projects already at the expected patched tree are verified and skipped.

`--check` 不修改 SDK。它会校验完整 SHA-256 清单、24 个锁定项目、精确基线、
确定性的补丁提交与 tree ID、工作区状态、未结束的 Git 操作，以及会与新增
补丁路径冲突的未跟踪文件。所有补丁都会先在临时共享克隆中实际应用；
`--apply` 只有在全部预演成功后才修改真实项目。重复执行时会验证并跳过已经
处于目标 tree 的项目。

The scripts never run `repo sync`, `git fetch`, `git stash`, `git reset`, or
`git clean`. Unrelated untracked build products are left alone; any untracked
or ignored path that would be replaced by a patch is rejected before the first
real `git am`.

脚本不会自行运行 `repo sync`、`git fetch`、`git stash`、`git reset` 或
`git clean`。无关的未跟踪构建产物会保留；若未跟踪或已忽略路径会被补丁
覆盖，脚本会在第一次真实 `git am` 前拒绝执行。

## Patch inventory / 补丁组成

All six project series are one compatibility unit. Do not apply only selected
patches. Full revisions and expected tree IDs are recorded in
[`manifest.json`](manifest.json).

六个项目是一组不可拆分的兼容单元，请勿只挑选部分补丁。完整 revision 与目标
tree ID 见 [`manifest.json`](manifest.json)。

| Project path | Official base | Purpose |
| --- | --- | --- |
| `.` | `3f18247b4863` | Dedicated defconfig, product version, 1 GiB A/B image layout |
| `src/rtsmart/libs` | `4964b24f0208` | Touch, OTA and PMU APIs; fonts, LVGL opt-in, HAL/VG-Lite library precedence |
| `src/rtsmart/libs/3rd-party/lvgl/lvgl` | `c210a4efa2f4` | TJPGD 1/8 thumbnail decoding |
| `src/rtsmart/mpp` | `631ca8660b31` | Camera mirror, source MP4 player/seek, H.264 FLV muxer |
| `src/rtsmart/rtsmart` | `650f16563075` | Safe OTA sessions, slot mounts, touch and PMU kernel ABI |
| `src/uboot/uboot` | `56a131d12108` | Verified A/B selection, attempts, rollback and slot ATAG |

[`manifests/upstream-lock.xml`](manifests/upstream-lock.xml) freezes every
project in the complete SDK, not only the six modified projects. The patch set
was prepared from CanMV manifest revision
`d207027db3ae457cd43629c80b8a42e3b79fd51a` on 2026-08-24.

## Existing SDKs and recovery / 现有 SDK 与恢复

For an existing checkout, preserve local work first and inspect it with
`repo status`. The tool accepts only the exact revisions in `manifest.json`;
being merely "latest" or having similar files is not sufficient. A fresh
checkout is preferred over forcing patches onto a mismatch.

对已有 SDK，请先保存本地工作并运行 `repo status`。工具只接受
`manifest.json` 中的精确 revision；“看起来是最新版”并不等于兼容。版本不
匹配时应重新创建全新检出，不要强行套补丁。

All projects are preflighted before the first real `git am`. An external race
or I/O failure can still stop the real phase after earlier projects succeeded.
In that case, inspect the named project with `git status`, then either resolve
the conflict and run `git am --continue`, or review it and run
`git am --abort`. Re-run the same `--apply` command afterwards; verified
projects are skipped and the remaining projects continue.

真实应用阶段若因外部竞态或 I/O 故障中止，请先在报错项目运行 `git status`；
解决冲突后执行 `git am --continue`，或确认后执行 `git am --abort`。随后重新
运行同一条 `--apply`，已经完成且 tree 正确的项目会被跳过。

If a project reaches an unexpected tree, do not reset a working SDK blindly.
Preserve any diagnostics and recreate that project—or preferably the entire
SDK—from `upstream-lock.xml`, then run the verified apply again.

## Build notes / 构建说明

- Use `k230_canmv_dongshanpi_edgeos_defconfig`; it enables EdgeOS, user-space
  LVGL and cJSON, selects the four-partition A/B product layout, and disables
  the unrelated optional WebRTC/libpeer module.
- The factory image contains identical `app_a` and `app_b` filesystems. OTA
  packages carry `rtt`, `rtapp`, and `app` together and activate the inactive
  slot only after all three payloads pass readback SHA-256 verification.
- The image pre-creates `bin`, `app_a`, and `app_b`. On the first boot,
  RT-Smart creates the fourth `/data` partition from the remaining media,
  reboots once to rescan the MBR, then formats and mounts it. Use an 8 GB or
  larger microSD card, do not interrupt this first-boot sequence, and keep
  `/data` free space larger than the roughly 1.02 GiB uncompressed OTA KDIMG.
- Product version `v0.7.5` is stored in
  `boards/k230_canmv_dongshanpi/system-version.txt` and is used by both the UI
  header and firmware filenames.
- On an SDK tree that was built before a `repo sync`, stale Mbed TLS objects
  can survive a configuration-header change and cause unrelated DTLS-SRTP
  link errors. A fresh checkout does not have this problem. For an existing
  tree, use the targeted cleanup below and rebuild; do not delete the whole
  workspace:

```bash
make -C src/rtsmart/libs/3rd-party/mbedtls/mbedtls/library clean
```

## Licensing / 许可证

Patch files retain the authorship, copyright notices, and license terms of the
upstream projects and files they modify. In particular, the restored MP4
player includes its Canaan BSD-2-Clause notice, the minimal `libflv` subset
includes its upstream MIT notice, and TJpgDec keeps ChaN's notice. The
repository-root MIT License applies to original EdgeOS code; it does not
relicense SDK patches, imported models, fonts, media, or other third-party
material.

补丁保留各上游项目与源文件原有的作者、版权和许可证。仓库根目录 MIT License
只适用于 EdgeOS 原创代码，不会重新许可 SDK 补丁、模型、字体、媒体或其他
第三方材料。
