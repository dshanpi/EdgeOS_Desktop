# EdgeOS SDK 验证状态 / Validation status

本目录区分“能够编译和打包”、“固件结构有效”、“能在真实开发板启动”
和“交互体验验收完成”。只有完整 SDK 构建、镜像结构检查、LYNX 写入、
板端启动与人工交互检查都通过，才会将版本标记为完整验收通过。

This directory distinguishes successful compilation, a structurally valid
image, a successful real-board boot, and completed human interaction
acceptance. A release is fully accepted only after all four stages pass.

## 版本状态 / Version status

| SDK 补丁标签 | 产品固件 | 状态 | 说明 |
| --- | --- | --- | --- |
| `edgeos-sdk-v1.0.2` | `v0.7.7` | **启动与基础外设通过；触摸主观验收待完成 / Boot and core peripherals passed; subjective touch acceptance pending** | `canmv_k230` 全量构建、完整镜像检查、LYNX eMMC 写入和板端启动已通过；显示、摄像头、触摸控制器、SDIO Wi-Fi 与 EdgeOS 启动日志均已确认，300 秒空闲运行后正常进入屏保。[当前记录](canmv-k230-edgeos-sdk-v1.0.2.md) |
| `edgeos-sdk-v1.0.1` | `v0.7.6` | **已由 v1.0.2 取代 / Superseded by v1.0.2** | 历史记录保留当时 `canmv_k230` / `rtos_k230` 的构建与镜像证据；不再是当前支持流程。[历史记录](canmv-rtos-k230-edgeos-sdk-v1.0.1.md) |
| `edgeos-sdk-v1.0.0` | `v0.7.5` | **已撤回 / Withdrawn** | 编译、打包和旧版结构检查通过，但真实开发板因 TOC 缺陷启动失败；不得烧录或分发。[历史记录](rtos-k230-edgeos-sdk-v1.0.0.md) |

历史文档保留当时的工作区名、命令与测量结果，以便审计和理解问题演变；
它们不代表当前仍支持 `rtos_k230`。

Historical records retain their original workspace names, commands, and
measurements for auditability. They do not imply current `rtos_k230` support.

## 当前使用方式 / Current usage

新工作区只应使用 `edgeos-sdk-v1.0.2` 标签和与之一起发布的锁定
CanMV SDK manifest。发布标签对应的 commit 应由 Git 解析：

```bash
git clone --branch edgeos-sdk-v1.0.2 \
  https://github.com/dshanpi/EdgeOS_Desktop.git
cd EdgeOS_Desktop
test "$(git rev-parse HEAD)" = \
  "$(git rev-list -n1 edgeos-sdk-v1.0.2)"
git describe --tags --exact-match HEAD
```

Use only `edgeos-sdk-v1.0.2` and its bundled locked CanMV SDK manifest for a
new workspace. Resolve the release commit from the immutable tag instead of
copying a pre-release commit ID.

- 中文构建流程 / Chinese build workflow:
  [`../BUILD_RTOS_K230.md`](../BUILD_RTOS_K230.md)
- English build workflow:
  [`../BUILD_RTOS_K230_EN.md`](../BUILD_RTOS_K230_EN.md)
- 中文板级适配流程 / Chinese board-porting workflow:
  [`../PORTING_CANMV_K230.md`](../PORTING_CANMV_K230.md)
- English board-porting workflow:
  [`../PORTING_CANMV_K230_EN.md`](../PORTING_CANMV_K230_EN.md)
- 中文 LYNX eMMC 烧录 / Chinese LYNX eMMC flashing:
  [`../FLASH_LYNX.md`](../FLASH_LYNX.md)
- English LYNX eMMC flashing:
  [`../FLASH_LYNX_EN.md`](../FLASH_LYNX_EN.md)

## 发布验收门禁 / Release acceptance gates

新的 `canmv_k230` 验证记录至少应记录：

1. EdgeOS 标签、锁定 manifest、补丁集清单、补丁 SHA-256 与重放后各项目 revision/tree。
2. `canmv_k230` 全量 `make` 的退出码 0、最终完成标记和构建后兼容性检查。
3. 完整 `.img` 的文件大小、SHA-256、MBR 分区、TOC 范围与包负载摘要。
4. LYNX 传输端摘要与本地镜像一致，并使用 `K230`、`EMMC`/SDIO0、
   完整未压缩 `.img`、起始偏移 `0x0` 写入。
5. 串口依次进入 SPL、U-Boot、RT-Smart 和 EdgeOS Desktop，不得出现
   `K230 boot: invalid TOC entry 1`。
6. ST7701 显示、GC2093 摄像头、CST128 触摸、SDIO Wi-Fi 与 IP 获取的结果。
7. 人工验收慢拖、快速滑动、惯性滚动、反向滑动、滑动后点击、快速连点，
   以及相册、摄像头和 Wi-Fi 页面的交互。

Compilation or flashing alone is not sufficient. The record must separate
automated build/image gates, real-board boot evidence, peripheral evidence,
and subjective touch/UI acceptance; pending items must remain explicitly
pending.
