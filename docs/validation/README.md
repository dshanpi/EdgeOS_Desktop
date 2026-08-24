# EdgeOS SDK 验证状态 / Validation status

本目录区分“能够编译和打包”与“能够在真实开发板启动”。只有完整 SDK 构建、
固件结构检查、LYNX 写入和板端启动验收全部完成，才能把一个版本标记为硬件
验证通过。

This directory distinguishes “builds and packages successfully” from “boots
on real hardware.” A release is hardware-validated only after the full SDK
build, image-layout checks, LYNX flashing, and on-board boot acceptance all
pass.

## 版本状态 / Version status

| SDK 补丁标签 | 产品固件 | 状态 | 说明 |
| --- | --- | --- | --- |
| `edgeos-sdk-v1.0.1` | `v0.7.6` | 构建与镜像已验证，板端待验收 / Build and image verified; board pending | 修复 v1.0.0 的 TOC 生成缺陷；[`canmv_k230` / `rtos_k230` 记录](canmv-rtos-k230-edgeos-sdk-v1.0.1.md)已包含两套全量构建、TOC 与包摘要结果 |
| `edgeos-sdk-v1.0.0` | `v0.7.5` | **已撤回 / Withdrawn** | 编译、打包和结构检查通过，但真实开发板启动失败；不得烧录或分发 |

v1.0.0 的历史构建数据和撤回勘误保留在
[`rtos-k230-edgeos-sdk-v1.0.0.md`](rtos-k230-edgeos-sdk-v1.0.0.md)。保留该记录
是为了说明缺陷如何逃过纯主机构建检查，不表示其中的镜像仍可使用。

The historical v1.0.0 build data and withdrawal erratum remain in
[`rtos-k230-edgeos-sdk-v1.0.0.md`](rtos-k230-edgeos-sdk-v1.0.0.md). It is kept
to document how a boot defect escaped host-only build checks, not to endorse
the listed images.

## 当前使用方式 / Current usage

新工作区只应使用 `edgeos-sdk-v1.0.1`。发布标签对应的 commit 不在标签创建前
硬编码到文档中，应直接由 Git 解析并校验：

```bash
git clone --branch edgeos-sdk-v1.0.1 \
  https://github.com/dshanpi/EdgeOS_Desktop.git
cd EdgeOS_Desktop
test "$(git rev-parse HEAD)" = \
  "$(git rev-list -n1 edgeos-sdk-v1.0.1)"
git describe --tags --exact-match HEAD
```

Use only `edgeos-sdk-v1.0.1` for a new workspace. Resolve and verify the
release commit from the immutable tag instead of embedding an unknown tag
commit in pre-release documentation.

- 完整中文构建流程 / Full Chinese build workflow:
  [`../BUILD_RTOS_K230.md`](../BUILD_RTOS_K230.md)
- 中文 LYNX eMMC 烧录与板端验收 / Chinese LYNX eMMC flashing and board
  acceptance: [`../FLASH_LYNX.md`](../FLASH_LYNX.md)
- 项目首页 / Project overview: [`../../README.md`](../../README.md)

## 发布验收门禁 / Release acceptance gates

新的验证记录至少应分别记录：

1. `canmv_k230` 与 `rtos_k230` 工作区各自的锁定 manifest、补丁 revisions 和
   trees。
2. 两个工作区全量 `make` 的退出码 0、最终完成标记和构建后兼容性检查。
3. 完整 `.img` 的文件大小、SHA-256、分区表和 TOC/范围检查。
4. 使用 LYNX 按 `K230`、`EMMC`/SDIO0、完整未压缩 `.img`、偏移 `0x0` 写入。
5. 开发板串口进入 SPL、U-Boot、RT-Smart 和 EdgeOS Desktop；不得出现
   `K230 boot: invalid TOC entry 1`。
6. 首次启动 `/data` 初始化、显示、触摸和至少一个基础应用的结果。

A new validation record must capture both SDK workspaces, complete build and
post-build gates, full-image hashes and layout checks, the exact LYNX EMMC
flash parameters, and real-board boot/UI results. Never mark a release as
hardware-validated from compilation alone.
