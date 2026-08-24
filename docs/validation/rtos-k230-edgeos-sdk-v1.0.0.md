> [!CAUTION]
> **撤回勘误（2026-08-24）：** 本记录只证明 `edgeos-sdk-v1.0.0` 可以完成
> 编译、链接和镜像打包，后续开发板测试已经证明其 `v0.7.5` 固件**不可启动**。
> Python genimage 在最终分区布局解析前缓存了 TOC，导致 `spl`、`uboot`
> 的长度错误地保留为 0；严格启动校验因此拒绝了该无效镜像。典型日志为
> `K230 boot: invalid TOC entry 1`，随后出现 SPL 回退路径的
> `no mkimage signature but raw image not supported`。v1.0.0 已撤回，请勿烧录或
> 分发本页列出的固件与哈希；请改用 `edgeos-sdk-v1.0.1` 重新构建产品版本
> `v0.7.6`。当前状态见 [`README.md`](README.md)。
>
> **WITHDRAWAL ERRATUM (2026-08-24):** This record proves only that
> `edgeos-sdk-v1.0.0` completed compilation, linking, and image packaging.
> Subsequent board testing proved that its `v0.7.5` firmware is **not
> bootable**. Python genimage cached its TOC before resolving the final
> partition layout, leaving the `spl` and `uboot` sizes incorrectly set to
> zero; strict boot validation therefore rejected the invalid image. The characteristic failure is
> `K230 boot: invalid TOC entry 1`, followed by the cascading SPL fallback
> error `no mkimage signature but raw image not supported`. v1.0.0 is
> withdrawn: do not flash or redistribute the artifacts or hashes on this
> page. Rebuild product version `v0.7.6` from `edgeos-sdk-v1.0.1`; see the
> [validation status index](README.md).

# `rtos_k230` build validation: `edgeos-sdk-v1.0.0`

## 结论 / Result

2026-08-24 在一个名为 `/home/ubuntu/rtos_k230` 的 K230 RTOS SDK 工作区中，
从 GitHub 检出的 `edgeos-sdk-v1.0.0` 已完成方案 A 首次应用、专用 defconfig、
全量构建、构建后 ABI 检查和镜像结构检查。全量 `make` 退出码为 0。

On 2026-08-24, the GitHub `edgeos-sdk-v1.0.0` tag completed first-time Scheme A
application, the dedicated defconfig, a full build, post-build ABI checking,
and image-layout verification in a K230 RTOS SDK workspace named
`/home/ubuntu/rtos_k230`. The full `make` returned zero.

本记录证明源码和固件打包链路可用；它不代表已经完成开发板启动、摄像头、
触摸或真实网络 OTA 测试。

This record validates source integration and firmware packaging. It does not
claim hardware boot, camera, touch, or live network OTA validation.

## Version lock / 版本锁

| Item | Value |
| --- | --- |
| EdgeOS tag | `edgeos-sdk-v1.0.0` |
| EdgeOS commit | `18df75d569bd5ecdfd8ccec8d37bf343e530533d` |
| SDK manifest commit | `d207027db3ae457cd43629c80b8a42e3b79fd51a` |
| SDK root base | `3f18247b48635f83f439bc13e564ce90d8655d18` |
| Locked projects | 24, 0 missing, 0 mismatches before patching |
| Product defconfig | `k230_canmv_dongshanpi_edgeos_defconfig` |
| Product version | `v0.7.5` |

The six patched projects reproduced the deterministic revisions and tree IDs
from `sdk/manifest.json`:

| Project | Patched revision | Tree |
| --- | --- | --- |
| SDK root | `0fe500c3ce6d` | `3f13d880ccb5` |
| RT-Smart libs | `006e9008616e` | `3c2e88b` |
| LVGL | `9157e4529b8c` | `3d118fd` |
| MPP | `e12646b76b30` | `2353278` |
| RT-Smart | `4153c2cfed76` | `cb6b467` |
| U-Boot | `76b702b68dca` | `305aedc` |

The abbreviated tree values above are display-only. The complete expected IDs
remain authoritative in `sdk/manifest.json`.

## Host / 主机环境

| Item | Measured value |
| --- | --- |
| OS | Ubuntu 20.04.4 LTS, x86_64 |
| CPU | 8 logical CPUs |
| Memory | 7.7 GiB RAM, 975 MiB swap |
| repo | 2.46 from `canmv-k230/git-repo` |
| Git | 2.25.1 |
| Python | 3.8.10 |
| Cross GCC | `riscv64-unknown-linux-musl-gcc` 12.0.1 |
| Free disk before build | about 311 GiB |

## Commands and gates / 命令与门禁

The validation used the public workflow below. The exact fresh-checkout command
was also tested against GitHub and resolved all 24 projects from the release
tag.

```bash
(
set -e
cd "$HOME/rtos_k230/src/applications"
git clone --branch edgeos-sdk-v1.0.0 \
  https://github.com/dshanpi/EdgeOS_Desktop.git
cd EdgeOS_Desktop
test "$(git rev-parse HEAD)" = "18df75d569bd5ecdfd8ccec8d37bf343e530533d"
git lfs pull
git lfs fsck

./tools/apply_sdk_patches.sh --check
./tools/apply_sdk_patches.sh --apply
./tools/integrate_canmv_sdk.sh

cd "$HOME/rtos_k230"
make k230_canmv_dongshanpi_edgeos_defconfig
./src/applications/EdgeOS_Desktop/tools/check_sdk_compat.sh
bash -o pipefail -c \
  'time make 2>&1 | tee edgeos-v1.0.0-full-build.log'
K230_TOOLCHAIN_NM="${SDK_TOOLCHAIN_DIR:-$HOME/.kendryte/k230_toolchains}/riscv64-linux-musleabi_for_x86_64-pc-linux-gnu/bin/riscv64-unknown-linux-musl-nm" \
  ./src/applications/EdgeOS_Desktop/tools/check_sdk_compat.sh
)
```

Measured results:

- `git lfs fsck`: passed.
- Scheme A preflight: all 24 locked projects and all six patch series passed.
- Scheme A apply: passed; all six patched revisions/trees matched the manifest.
- Active config: EdgeOS defconfig, four partitions, user-space LVGL, EdgeOS app
  enabled, optional WebRTC disabled.
- Menu/build registration: the generated `Kconfig.app` sourced
  `src/applications/EdgeOS_Desktop/Kconfig`, `.config` contained
  `CONFIG_APP_ENABLE_LVGL_LAUNCHER=y`, and `apps.mk` mapped that option to the
  `EdgeOS_Desktop` directory. The full log then entered
  `[BUILD] applications EdgeOS_Desktop`.
- Full build: exit 0 in 13 minutes 30.222 seconds.
- Final log marker:
  `Build K230 done, board k230_canmv_dongshanpi, config k230_canmv_dongshanpi_edgeos_defconfig`.
- Post-build compatibility check with the cross `nm`: passed, including
  `kd_player_seek` in `libmp4_player.a`.
- Final build log scan: no fatal include errors, undefined references, missing
  files, or non-ignored make error markers. Four terminal-formatting commands
  in FFmpeg's `ffbuild/.config` prerequisite rule printed `Error 1 (ignored)`
  as directed by their upstream Makefile; the subsequent FFmpeg install, final
  link, packaging, and image stages completed.
- Generated SDK output directory: about 6.3 GiB.

The build emitted warnings from upstream U-Boot, MPP, OpenCV, nncase, and demo
sources. None were promoted to an error, and the final linker and image stages
completed.

## Artifacts / 产物

These hashes identify this validation run only. Build timestamps are embedded,
so they are not canonical release hashes for every rebuild.

| Artifact | Size (bytes) | SHA-256 in this run |
| --- | ---: | --- |
| `DshanPI_EdgeOS_Desktop_v0.7.5_ota.kdimg` | 1,094,729,728 | `ad17942147cefcfa126ba7b57df0a794262b44afd03931a5ca29e837c2551adf` |
| `DshanPI_EdgeOS_Desktop_v0.7.5.img` | 2,283,798,528 | `a1c3f711459174c50649db9b10d0ec942928a6c42a1563a4a4037e853ec60f93` |
| `DshanPI_EdgeOS_Desktop_v0.7.5.img.gz` | 1,130,400,262 | `231332c169ed6a149c410e15ec80148c45610e3a4f3718c2aecf45dda91b10f0` |

The OTA client's configured package limit is 1,280 MiB. The KDIMG in this run
was 247,447,552 bytes (about 236 MiB) below that limit. Device download still
requires `/data` free space greater than the complete uncompressed KDIMG.

The uncompressed factory image contained this DOS/MBR layout:

| Partition | Start sector | Sectors | Size | Type |
| --- | ---: | ---: | ---: | --- |
| `bin` | 225280 | 40960 | 20 MiB | FAT32 LBA |
| `app_a` | 266240 | 2097152 | 1 GiB | FAT32 LBA |
| `app_b` | 2363392 | 2097152 | 1 GiB | FAT32 LBA |

The factory image pre-created only these three partitions. The configured
fourth `/data` partition is created from the remaining card space by RT-Smart
on first boot; that hardware first-boot path was not exercised in this run.

`images/sdcard/revision.txt` recorded the actual patched SDK revisions. The
packaged application tree contained 71 files, and the desktop launcher
`images/sdcard/app/dshanpi_aimodel` was 9,120,568 bytes.

## Reproducing the validation / 复现

Follow the complete Chinese guide in [`../BUILD_RTOS_K230.md`](../BUILD_RTOS_K230.md)
or the English guide in [`../BUILD_RTOS_K230_EN.md`](../BUILD_RTOS_K230_EN.md).
The guide distinguishes a clean locked checkout from recovery of an existing
SDK and documents the old `kplayer.h` and DTLS-SRTP/Mbed TLS failure modes.
