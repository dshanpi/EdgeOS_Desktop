# 使用 LYNX 将 EdgeOS Desktop 写入 CanMV-K230 V3 eMMC

本文用于 EdgeOS Desktop 的首次安装、完整重刷和故障恢复。默认目标是
DshanPI CanMV-K230 V3 的板载 eMMC；普通用户应使用完整、未压缩的 `.img`
并从设备偏移 `0x0` 写入。

> [!CAUTION]
> `edgeos-sdk-v1.0.0` 生成的 `v0.7.5` 固件虽然能够编译和打包，但会因镜像
> 生成器写入零长度 TOC 项而在开发板上启动失败。不得继续烧录或分发该版本。请用
> `edgeos-sdk-v1.0.2` 重新构建产品版本 `v0.7.7`。

## 1. 选择正确的文件

构建输出通常位于：

```text
output/k230_canmv_dongshanpi_edgeos_defconfig/
```

不同产物不能互换：

| 产物 | 用途 | 能否用 LYNX 烧录 |
| --- | --- | --- |
| `DshanPI_EdgeOS_Desktop_v0.7.7.img` | 包含 TOC、SPL、U-Boot、RT-Smart 和应用分区的完整出厂/恢复镜像 | **可以，使用此文件** |
| `DshanPI_EdgeOS_Desktop_v0.7.7.img.gz` | 完整镜像的压缩传输文件 | 先解压为 `.img`，不能直接烧录 |
| `DshanPI_EdgeOS_Desktop_v0.7.7_ota.kdimg` | 运行中 EdgeOS 的 A/B OTA 包 | **禁止使用 LYNX 烧录** |
| `DshanPI_EdgeOS_Desktop_v0.7.7_ota.kdimg.gz` | OTA 包的压缩传输文件 | **禁止使用 LYNX 烧录** |

如果只有 `.img.gz`，先解压并确认得到非空的完整镜像：

```bash
gzip -dk DshanPI_EdgeOS_Desktop_v0.7.7.img.gz
test -s DshanPI_EdgeOS_Desktop_v0.7.7.img
sha256sum DshanPI_EdgeOS_Desktop_v0.7.7.img
python3 /path/to/EdgeOS_Desktop/tools/check_firmware_image.py \
  DshanPI_EdgeOS_Desktop_v0.7.7.img
```

只有结构检查结束于
`PASS: EdgeOS firmware image is structurally valid.` 才能继续。本次
v1.0.2 实测镜像大小为 `2283798528` bytes，SHA-256 为
`77ee49eb3bc3f3483166777c7d03e56c29cc1840665706f56d69454a241fe8b8`，
11 个 TOC 项、3 个 MBR 分区与可检查负载摘要均通过。自行重新构建的
镜像如果摘要不同，必须记录新摘要并对该精确文件重新完成结构和板端验收。

`*_ota.kdimg` 只包含运行中 OTA 更新器要写入非活动槽的负载，不是完整的
出厂启动镜像。把它写到设备起始位置不会建立可由 BootROM/SPL 启动的完整布局。

## 2. LYNX 参数

在 LYNX 中使用以下组合：

| 项目 | 选择 |
| --- | --- |
| 芯片 | `K230` |
| 存储目标 | `EMMC`，即 K230 SDIO0 |
| 文件 | 解压后的完整 `DshanPI_EdgeOS_Desktop_v0.7.7.img` |
| 写入方式 | 完整/Raw 镜像 |
| 地址或偏移 | `0x0` |

如果 LYNX 在选择完整 `.img` 后自动固定起始地址，保持其 `0x0` 默认值即可。
不要把地址改为 `0xe0000`、`0x100000`、`0x200000` 或分区起始位置。
将镜像传输到 LYNX 后应再次核对缓存文件的 SHA-256 与本地值相同，
不要只根据文件名判断。

完整镜像内已经编码了启动数据的相对位置，例如 TOC 位于镜像内部
`0xe0000`、SPL 位于 `0x100000`、U-Boot 位于 `0x200000`。只有把镜像字节 0
写到 eMMC 字节 0，这些内部位置才会落在 BootROM 和 SPL 预期的位置。

本指南不使用物理 TF 卡。只有明确要制作 TF 启动介质时，才应选择
SDCARD/SDIO1，并按对应的官方流程操作；不要把 EMMC 和 SDCARD 目标混用。

## 3. 进入烧录模式并写入

1. 关闭开发板电源，建议拔除 TF 卡，避免 eMMC 与 TF 卡上同时存在不同系统。
2. 使用支持数据传输的 USB 线连接开发板烧录接口与主机。
3. 在 LYNX 中选好上一节的 K230、EMMC、完整 `.img` 和 `0x0`。
4. 按板卡硬件说明操作升级键/BootROM strap 与 `RST`，或从已运行的
   RT-Smart 串口执行 `reboot_to_upgrade`。LYNX 识别到 K230 下载设备后再继续。
5. 开始烧录，等待 LYNX 明确报告完成。写入期间不要断电、复位或拔线。
6. 烧录完成后退出烧录模式并复位开发板，观察串口启动日志。

本次 v1.0.2 实测连接为 K230 USB `2:8`、串口 `COM9`，LYNX 传输端
摘要与本地镜像一致，EMMC 烧录任务退出码为 0。USB 编号和串口名会
因主机而异，它们是本次证据，不是所有主机必须使用的固定值。

不同 LYNX 版本的按钮名称可能略有差异，但存储目标、文件类型和起始偏移
必须与上表一致。

## 4. 板端验收

一次完整发布验收至少应确认：

1. LYNX 写入过程成功完成，目标为 EMMC，起始偏移为 `0x0`。
2. 串口依次进入 SPL、U-Boot 和 RT-Smart，不再出现
   `K230 boot: invalid TOC entry 1`。
3. 首次启动可以因创建 `/data` 分区而自动重启一次；期间不要断电。
4. 重启后 `/data` 正常挂载，EdgeOS Desktop 可以进入桌面。
5. ST7701 显示、GC2093 摄像头、CST128 触摸控制器和 SDIO Wi-Fi 都成功初始化。
6. Wi-Fi 获取 IP、NTP 同步成功，连续运行期间无新串口异常。
7. 人工检查慢拖、快滑、惯性/反向滚动、滑动后点击、快速连点，
   以及摄像头切换、相册和 Wi-Fi 页面。
8. 发布 OTA 时再单独验证 A/B 更新路径。

本次 v1.0.2 已确认 LYNX 写入、完整启动、双路 GC2093、ST7701、CST128、
SDIO Wi-Fi、IP 和 NTP。Wi-Fi 首次连接配置失败后自动重试，随后成功连接
`Programmers` 并获得 `192.168.1.44`。串口未出现 invalid TOC、Page Fault 或
Illegal Instruction。触摸主观手感仍等待用户验收，不应根据驱动启动日志
提前判定为通过。`/data` 首次创建/挂载与 OTA A/B 路径在需要发布 OTA 时应单独
保留验收证据。

构建命令退出码为 0、成功生成 `.img` 或 LYNX 报告写入完成，都不能单独替代
板端启动验收。各 SDK 补丁版本的公开状态见
[`validation/README.md`](validation/README.md)。

## 5. 常见启动日志

| 日志或现象 | 判断与处理 |
| --- | --- |
| `K230 boot: invalid TOC entry 1` | 已撤回的 v1.0.0 镜像生成缺陷；使用 v1.0.2 重新构建 v0.7.7 完整镜像 |
| 上一条之后出现 `no mkimage signature but raw image not supported` | 自定义 TOC 启动失败后进入通用 SPL 回退路径的连锁错误，不是独立的镜像签名问题 |
| `HS200 tuning failed: -110`，随后显示 `MMC High Speed (52MHz)` | eMMC 的 HS200 调谐失败后已成功降速，通常不是致命错误；继续查看后续第一条真正的启动错误 |
| `Trying to boot from MMC1` | SPL 的通用启动设备名称；在这条回退路径中不能单凭该字符串断定系统正在访问物理 SDIO1/TF 卡 |
| LYNX 成功但板端没有有效启动链 | 再次确认使用完整、未压缩 `.img`，目标 EMMC，偏移 `0x0`，且没有误用 `_ota.kdimg` |
| 烧录后仍启动旧系统 | 拔除 TF 卡，重新确认烧录目标为 EMMC，并完全复位或重新上电 |

## 6. 路径名与物理介质

EdgeOS 在 RT-Smart 中使用 `/sdcard/app/dshanpi_aimodel` 等路径，SDK 构建目录中
也存在 `images/sdcard/`。这些是历史兼容的逻辑挂载名和构建路径名，不等同于
物理 microSD/TF 卡，也不会改变本指南应选择的 EMMC/SDIO0 目标。

## 7. 参考资料

- [Kendryte K230 Burning Tool](https://github.com/kendryte/k230_burning_tool)：K230
  烧录介质定义，其中 EMMC 对应 K230 SDIO0、SD Card 对应 K230 SDIO1。
- [K230 RTOS 固件烧录说明](https://www.kendryte.com/k230_rtos/zh/main/userguide/how_to_flash.html)：
  完整 `.img` 的 Raw Image 写入方式与目标介质选择。
- [DshanPI CanMV-K230 V3 更新 eMMC 系统](https://eai.100ask.net/CanaanK230/part1/UpdateEMMCsystem/)：
  开发板进入烧录模式与 eMMC 更新流程。
