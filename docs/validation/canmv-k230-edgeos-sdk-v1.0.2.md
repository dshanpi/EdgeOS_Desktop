# `canmv_k230` validation: `edgeos-sdk-v1.0.2`

Validation date / 验证日期: 2026-08-24

## 结论 / Result

`edgeos-sdk-v1.0.2` 与产品固件 `v0.7.7` 已在 `canmv_k230` 完成全量
构建、完整镜像结构校验、LYNX eMMC 写入和真实 DshanPI
CanMV-K230 V3 启动验证。SPL、U-Boot、RT-Smart 和 EdgeOS Desktop
已正常启动，没有出现 `K230 boot: invalid TOC entry 1`、
Page Fault 或 Illegal Instruction。

最终固件日志确认双路 GC2093、ST7701、CST128、SDIO Wi-Fi、
EdgeOS launcher/desktop、IP 获取和 NTP 同步。Wi-Fi 首次连接配置失败后
自动重试，随后成功连接 `Programmers` 并获得 `192.168.1.44`。板端持续
运行满 300 秒后按预期进入空闲屏保，串口保持在线且未出现新的启动或运行异常。

The full `canmv_k230` build, structural image gate, LYNX eMMC write, and real
CanMV-K230 V3 boot all passed for `edgeos-sdk-v1.0.2` / `v0.7.7`. Both GC2093
inputs, ST7701, CST128, SDIO Wi-Fi, EdgeOS, IP acquisition, and NTP were
confirmed, with no invalid TOC, Page Fault, or Illegal Instruction.
The board remained online and entered its idle screen saver as expected after
300 seconds, without a new boot or runtime fault on the serial console.

> [!IMPORTANT]
> 优化后的触摸驱动和 UI 已被编入并随固件启动，但“手感是否足够
> 丝滑”必须由用户完成主观实机验收。慢拖、快速滑动、惯性滚动、
> 反向滑动、滑动后立即点击、快速连点和相册滚动目前状态是
> **pending / 待验收**，本记录不将它们写为已通过。

## 构建与镜像 / Build and image

| 项目 | 实测值 |
| --- | --- |
| SDK 工作区 | `canmv_k230` |
| EdgeOS SDK 补丁版本 | `edgeos-sdk-v1.0.2` |
| 产品版本 | `v0.7.7` |
| 上游 manifest revision | `d207027db3ae457cd43629c80b8a42e3b79fd51a` |
| 锁定项目 | `sdk/manifests/upstream-lock.xml` 中 24 个不可变 revision |
| 板级配置 | `k230_canmv_dongshanpi_edgeos_defconfig` |
| 完整镜像 | `DshanPI_EdgeOS_Desktop_v0.7.7.img` |
| 文件大小 | `2283798528` bytes |
| SHA-256 | `77ee49eb3bc3f3483166777c7d03e56c29cc1840665706f56d69454a241fe8b8` |
| 全量构建 | 成功，退出码 0 |
| 固件结构检查 | PASS：11 个有效 TOC 项、3 个 MBR 分区、包负载范围与摘要校验通过 |

六个 SDK 补丁项目从锁定基线确定性重放后的目标为：

| 项目 | patched revision | patched tree |
| --- | --- | --- |
| SDK root | `2254d9fa97c2` | `5faa0521b6a6` |
| RT-Smart libs | `84df20c3c662` | `68e84cc975eb` |
| LVGL | `9157e4529b8c` | `3d118fd0f624` |
| MPP | `b9b3160384a3` | `93fd60bdfb15` |
| RT-Smart | `9165fcf343e0` | `96a6e0d4d565` |
| U-Boot | `8ffda06e12b4` | `0591aa81c39f` |

`--check`、`--apply`、重复幂等检查、应用集成、产品 defconfig 生成与
`check_sdk_compat.sh` 均通过；上表六个 tree 与实际构建工作树一致。

结构检查器从最终 TOC 解析到的关键负载长度包括：

| TOC 负载 | 长度 |
| --- | ---: |
| `spl` | `219372` bytes |
| `uboot` | `301760` bytes |
| `rtt` | `2243941` bytes |
| `rtapp` | `19879973` bytes |

本地产物可用以下门禁复核：

```bash
cd "$HOME/canmv_k230/output/k230_canmv_dongshanpi_edgeos_defconfig"
image=DshanPI_EdgeOS_Desktop_v0.7.7.img

test "$(stat -c %s "$image")" = 2283798528
printf '%s  %s\n' \
  77ee49eb3bc3f3483166777c7d03e56c29cc1840665706f56d69454a241fe8b8 \
  "$image" | sha256sum -c -
python3 "$HOME/canmv_k230/src/applications/EdgeOS_Desktop/tools/check_firmware_image.py" \
  "$image"
```

`check_firmware_image.py` 必须以下列文本结束：

```text
PASS: EdgeOS firmware image is structurally valid.
```

结构检查不只是检查文件非空。它必须证明最终 K230 TOC 内的启动项
均有非零偏移与长度、包范围没有越界，且未加密负载的摘要匹配。

## LYNX 写入 / LYNX flashing

| 项目 | 实测结果 |
| --- | --- |
| LYNX 设备目标 | USB `2:8`，Kendryte K230 |
| 串口 | `COM9` |
| 存储介质 | `EMMC` / K230 SDIO0 |
| 传输完整性 | LYNX 缓存文件 SHA-256 与本地上表摘要一致 |
| 烧录输入 | 未压缩完整 `.img`，从介质偏移 `0x0` 写入 |
| LYNX 任务 | `flash-1787575241857395500` |
| 烧录结果 | `success`，phase `complete`，progress `100`，exit code `0`，backend `reboot` |

写入前同时校验本地文件和 LYNX 传输端文件的 SHA-256，避免因同名
缓存镜像而烧录到旧固件。本次没有使用 `_ota.kdimg`、`.img.gz` 或内部
分区偏移作为 LYNX 输入。

## 板端启动与外设 / Board boot and peripherals

| 验收项 | 结果 |
| --- | --- |
| SPL / U-Boot / OpenSBI / RT-Smart | PASS；U-Boot SPL `00006-g94c9688c` |
| 系统版本 | `v0.7.7` |
| `K230 boot: invalid TOC entry 1` | 未出现 / Not observed |
| Page Fault / Illegal Instruction | 未出现 / Not observed |
| ST7701 640 × 480 显示 | 初始化成功，EdgeOS 界面正常出现 |
| GC2093 摄像头 | CSI0 与 CSI2 初始化成功 |
| CST128 触摸控制器 | `i2c3` 驱动初始化成功；主观交互手感待用户验收 |
| SDIO Wi-Fi | 初始化成功；首次配置连接失败后自动重试，连接 `Programmers` |
| IP | `192.168.1.44` |
| NTP | 同步成功 |
| EdgeOS Desktop | launcher 和 desktop 启动成功 |
| 300 秒空闲运行 | PASS；按预期进入 screen saver，串口保持在线且无新增异常 |

`HS200 tuning failed: -110` 如果紧接着出现
`MMC0: selected timing: MMC High Speed (52MHz)`，表示 U-Boot 已成功回退到
eMMC High Speed 模式；本次后续启动链和运行验证均通过。

## 待完成的主观验收 / Pending subjective acceptance

下列项目需要用户直接操作开发板。在收到用户结论前，本记录保持
`pending`：

1. 桌面慢速拖动，确认低于约 10 px 的起滑距离不再有明显粘滞。
2. 连续快速滑动、反向滑动和惯性滚动，确认不丢手势、不异常跳动。
3. 滑动结束后 50–150 ms 内点击，确认后续点击不被全局拖动抑制吞掉。
4. 快速连点和双击，确认没有误过滤，也不会把一次触摸识别为多次。
5. 相册长列表滚动、摄像头前后切换和 Wi-Fi 页面操作，确认同步解码、
   摄像头资源切换和网络轮询没有导致滑动卡顿。

用户完成上述检查后，应在后续验证记录或发布说明中记录开发板、
固件 SHA-256、通过/失败项和主观观察，不要只写“感觉可以”。
