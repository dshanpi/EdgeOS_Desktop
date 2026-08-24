# `canmv_k230` / `rtos_k230` validation: `edgeos-sdk-v1.0.1`

## 结论 / Result

2026-08-24，`/home/ubuntu/rtos_k230` 与 `/home/ubuntu/canmv_k230`
分别完成了 `k230_canmv_dongshanpi_edgeos_defconfig` 全量构建。两次 `make`
均返回 0，并生成产品版本 `v0.7.6` 的完整 IMG 与 OTA KDIMG。

On 2026-08-24, both `/home/ubuntu/rtos_k230` and
`/home/ubuntu/canmv_k230` completed full
`k230_canmv_dongshanpi_edgeos_defconfig` builds. Both `make` invocations
returned zero and produced product-version `v0.7.6` factory IMG and OTA KDIMG
artifacts.

两份完整 IMG 均通过仓库内 `tools/check_firmware_image.py`：DOS/MBR、11 项
K230 TOC、A/B 分区映射、6 个 K230 包头及未加密 payload SHA-256 全部有效。
旧版中为 0 的 TOC entry 1 `spl` 与 entry 3 `uboot` 已分别写入最终实际尺寸
219,900 与 302,288 字节，因此不再触发
`K230 boot: invalid TOC entry 1`。

Both factory images passed `tools/check_firmware_image.py`: DOS/MBR, all 11
K230 TOC entries, the A/B partition mapping, six K230 package headers, and all
unencrypted payload SHA-256 digests were valid. TOC entry 1 (`spl`) and entry
3 (`uboot`), which were zero-sized in the withdrawn release, now contain their
final 219,900-byte and 302,288-byte extents.

本环境没有连接 K230 开发板，也没有正在运行的 LYNX 服务，因此本记录不虚构
板端启动结果。构建和镜像级门禁均已通过；最终硬件验收状态为“待使用 LYNX
烧录后确认”。烧录参数和串口验收标准见
[`../FLASH_LYNX.md`](../FLASH_LYNX.md)；英文见
[`../FLASH_LYNX_EN.md`](../FLASH_LYNX_EN.md)。

No K230 board or running LYNX service was available in this environment, so
this record does not claim a physical boot that was not performed. All build
and image-level gates passed; final hardware acceptance remains pending a LYNX
flash and serial-console confirmation.

## Version lock / 版本锁

| Item | Value |
| --- | --- |
| EdgeOS tag | `edgeos-sdk-v1.0.1` |
| SDK patch set | `edgeos-sdk-v1.0.1` |
| SDK manifest commit | `d207027db3ae457cd43629c80b8a42e3b79fd51a` |
| SDK root base | `3f18247b48635f83f439bc13e564ce90d8655d18` |
| SDK root patched revision | `19577799736b28e2bd8464e902e637509c9854ad` |
| SDK root patched tree | `a6bb86513cc0a7418d4e914421b634b3d8fbafba` |
| Locked projects | 24 |
| Product defconfig | `k230_canmv_dongshanpi_edgeos_defconfig` |
| Product version | `v0.7.6` |

`rtos_k230` 使用公开补丁可复现的确定性 revision。`canmv_k230` 保留了较早
本地应用补丁时形成的等价 commit ID；六个目标项目的最终 Git tree 均与
`sdk/manifest.json` 完全相同，因此源码内容一致。公开用户以 manifest 中的
确定性 revision/tree 为准。

`rtos_k230` uses the deterministic revisions reproduced by the public patch
set. `canmv_k230` retains equivalent commit IDs from an earlier local patch
application, but every one of the six target project trees exactly matches
`sdk/manifest.json`; their source content is identical.

## Gates and commands / 门禁与命令

The relevant commands were:

```bash
make k230_canmv_dongshanpi_edgeos_defconfig
PYTHONDONTWRITEBYTECODE=1 PYTHONPATH=tools \
  python3 -m unittest discover -s tools/genimage_py/tests -v
bash -o pipefail -c \
  'time make 2>&1 | tee edgeos-v1.0.1-v0.7.6-full-build.log'
python3 src/applications/EdgeOS_Desktop/tools/check_firmware_image.py \
  output/k230_canmv_dongshanpi_edgeos_defconfig/\
DshanPI_EdgeOS_Desktop_v0.7.6.img
```

Measured results:

- Post-build `tools/check_sdk_compat.sh` passed in both workspaces with the
  cross `nm` selected. Each run verified all 24 locked projects, reproduced
  all six patch series, accepted only the expected `apps.mk` registration,
  checked `kd_player_seek`, ran the SDK regression suite, and invoked the full
  firmware-image validator.
- Python genimage regression suite: 5/5 passed in both SDK trees. It covers
  final TOC synchronization, automatic offsets, separate KDIMG TOC output,
  partition-order protection, and non-zero process failure propagation.
- Host firmware checker suite: 10/10 passed.
- `rtos_k230` full build: exit 0, 2 minutes 54.537 seconds.
- `canmv_k230` full build: exit 0, 3 minutes 0.110 seconds.
- Both logs ended with
  `Build K230 done, board k230_canmv_dongshanpi, config k230_canmv_dongshanpi_edgeos_defconfig`.
- Both full images: 2,283,798,528 bytes, 11 TOC entries, three MBR
  partitions, and six K230 package payload hashes verified.

The upstream FFmpeg makefile still prints several explicitly ignored
`Error 1 (ignored)` formatting-rule messages. The subsequent installs, final
links, application build, KDIMG generation, full IMG generation, compression,
and top-level success marker all completed in both runs.

## Final TOC / 最终 TOC

Both images contain the same resolved layout:

| Index | Name | Offset | Size (bytes) | Load | Boot |
| ---: | --- | ---: | ---: | ---: | ---: |
| 0 | `ota_meta` | `0x000f0000` | 2,048 | 0 | `0x0` |
| 1 | `spl` | `0x00100000` | 219,900 | 0 | `0x0` |
| 2 | `uboot_env` | `0x001e0000` | 65,536 | 0 | `0x0` |
| 3 | `uboot` | `0x00200000` | 302,288 | 0 | `0x0` |
| 4 | `rtt_a` | `0x00a00000` | 20,971,520 | 1 | `0x3` |
| 5 | `rtapp_a` | `0x01e00000` | 31,457,280 | 1 | `0x0` |
| 6 | `rtt_b` | `0x03c00000` | 20,971,520 | 1 | `0x3` |
| 7 | `rtapp_b` | `0x05000000` | 31,457,280 | 1 | `0x0` |
| 8 | `bin` | `0x06e00000` | 20,971,520 | 0 | `0x0` |
| 9 | `app_a` | `0x08200000` | 1,073,741,824 | 0 | `0x0` |
| 10 | `app_b` | `0x48200000` | 1,073,741,824 | 0 | `0x0` |

## Artifacts / 产物

Build timestamps and local deterministic revision strings are embedded in
the images, so these hashes identify these validation runs rather than every
possible rebuild.

### `rtos_k230`

| Artifact | Size (bytes) | SHA-256 |
| --- | ---: | --- |
| `DshanPI_EdgeOS_Desktop_v0.7.6.img` | 2,283,798,528 | `2b82dba6b8bd9cade1592bdfdd58057a65ad90de4e44d99408d4927a1d420da2` |
| `DshanPI_EdgeOS_Desktop_v0.7.6_ota.kdimg` | 1,094,729,728 | `4302daf2bd882f057d62c004a5b4e7efb6ab06a8a76073b5cd7e054f40f14ea0` |
| `DshanPI_EdgeOS_Desktop_v0.7.6.img.gz` | 1,130,409,574 | `b086290645d075f465234e2f5ba9aaa6937da8b997dd18371fe1d284a0741ea0` |

### `canmv_k230`

| Artifact | Size (bytes) | SHA-256 |
| --- | ---: | --- |
| `DshanPI_EdgeOS_Desktop_v0.7.6.img` | 2,283,798,528 | `fb1491ca101da9e7738f85fbb828705603473959b64b97db5642a05037367886` |
| `DshanPI_EdgeOS_Desktop_v0.7.6_ota.kdimg` | 1,095,667,712 | `3b095c0eead358191e049fb43492364bbc2388884ddf683542b0d8d5b075862a` |
| `DshanPI_EdgeOS_Desktop_v0.7.6.img.gz` | 1,132,288,683 | `15ba407ed74f8727c3a8a937cbbca04f80baf89ec6975d8ea2144e980cbecf9a` |

## Hardware acceptance / 硬件验收

Use the uncompressed full `.img`, select K230 `EMMC / SDIO0`, and write from
offset `0x0`. Do not flash `*_ota.kdimg`. Remove the TF card for an unambiguous
eMMC test. Acceptance requires the serial log to pass TOC selection without
`invalid TOC entry`, start RT-Smart and the EdgeOS launcher, then exercise
touch, camera, and one reboot. Record the flashed image SHA-256 and serial log
in this document after the board test.
