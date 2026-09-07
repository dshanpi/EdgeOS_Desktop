# 22 把 LVGL 9 应用集成到 Tina SDK

> 当前 SDK 已经包含 `v853-lvgl9-demo` 软件包。本节不再从零猜目录，而是沿着实际 `Config.in`、软件包 `Makefile`、构建目录、IPK、rootfs 和最终固件逐级验收。

![外部工程进入 Tina SDK 与固件的完整交付链](images/guide-lvgl-delivery.svg)

## 学习目标

- 看懂当前 SDK 中 `v853-lvgl9-demo` 的真实结构和外部源码依赖。
- 正确选择软件包并验证 package 编译、IPK、rootfs 和完整固件。
- 使用 LVGL 提交校验和 BUILD id 防止混入错误源码或旧程序。
- 在板端证明运行的是 `/usr/bin/v853-lvgl9`，而不是 `/tmp` 中的旧测试版。

## 当前软件包结构

SDK 路径：

~~~text
/home/ubuntu/100ask-course/sdk/tina-v853-100ask
~~~

软件包路径：

~~~text
package/gui/v853-lvgl9-demo/
├── Config.in
├── Makefile
└── src/Makefile
~~~

| 项目 | 当前值 |
| --- | --- |
| `PKG_NAME` | `v853-lvgl9-demo` |
| `PKG_VERSION` | `9.4.0` |
| 菜单位置 | `Gui → Littlevgl` |
| 板级依赖 | `@TARGET_v853_100ask` |
| LVGL 固定提交 | `c210a4efa2f474d0223d3e91c79963e1ae4ac0bc` |
| 应用构建标识 | `v853_lvgl9-22-tina-c210a4efa` |
| 安装文件 | `/usr/bin/v853-lvgl9` |
| 链接方式 | ARM EABI5，静态链接；进入 rootfs 时 strip |
| 默认功能 | 资源开启、刷新探针关闭、自适应休眠开启 |

`v853-lvgl9-demo` 只安装可执行文件，没有添加自己的开机自启动服务。烧录后需要人工运行 `/usr/bin/v853-lvgl9 <eventN>`；不要把“文件已经进入固件”误写成“应用会自动启动”。

当前板级配置还选择了另一个软件包 `v853-edgeos-desktop`。它会安装 `/etc/init.d/S99edgeos-v853`，自动查找 FT6336、GT9xx、Goodix 或 GT967，并监督运行 `/usr/bin/edgeos-v853`。手工测试 demo 前必须临时停止该服务，避免两个程序同时占用 `/dev/fb0`。

## 1. 先解决外部源码路径

软件包没有把完整 LVGL 放进 SDK，而是默认从下面路径读取：

~~~text
/home/ubuntu/100ask-course/lvgl9
├── lvgl/
└── v853-port/
~~~

`package/gui/v853-lvgl9-demo/Makefile` 中对应关系为：

~~~makefile
V853_LVGL9_SOURCE_DIR?=$(abspath $(TOPDIR)/../../lvgl9)
V853_LVGL9_APP_DIR:=$(V853_LVGL9_SOURCE_DIR)/v853-port
V853_LVGL9_CORE_DIR:=$(V853_LVGL9_SOURCE_DIR)/lvgl
V853_LVGL9_REVISION:=c210a4efa2f474d0223d3e91c79963e1ae4ac0bc
~~~

:::warning
当前虚拟机实查发现：默认目录 `/home/ubuntu/100ask-course/lvgl9` 不存在，完整且与历史构建副本一致的源码位于 `/home/ubuntu/Downloads/lvgl9`。旧的 `out/.../compile_dir` 和 IPK 仍存在，但这不能证明全新构建可复现。
:::

本轮有两种处理方式：

1. 把已经核验的源码恢复到软件包默认目录。
2. 不搬动源码，在 make 命令行传入：

   ~~~text
   V853_LVGL9_SOURCE_DIR=/home/ubuntu/Downloads/lvgl9
   ~~~

本文采用第 2 种，不修改当前目录结构。先检查：

~~~bash
LVGL9_SOURCE=/home/ubuntu/Downloads/lvgl9

test -f "$LVGL9_SOURCE/v853-port/main.c"
test -f "$LVGL9_SOURCE/v853-port/lv_conf.h"
test -f "$LVGL9_SOURCE/v853-port/assets/fonts/ui_font_24.c"
test -f "$LVGL9_SOURCE/v853-port/assets/images/ui_logo.c"
git -C "$LVGL9_SOURCE/lvgl" rev-parse HEAD
git -C "$LVGL9_SOURCE/lvgl" status --short
~~~

提交必须等于：

~~~text
c210a4efa2f474d0223d3e91c79963e1ae4ac0bc
~~~

提交不一致或工作树含不明修改时停止，不要关闭软件包中的版本保护。

## 2. 让 AI Agent 只读审计软件包

把 SDK 设为 Agent 工作区，把实际 LVGL9 源码目录额外授予只读访问，先保持 **Read Only**：

~~~text
请只读审计 Tina SDK 中的 v853-lvgl9-demo 软件包，不要修改或编译。

SDK=/home/ubuntu/100ask-course/sdk/tina-v853-100ask
LVGL9_SOURCE=/home/ubuntu/Downloads/lvgl9

检查 package/gui/v853-lvgl9-demo/Config.in、Makefile、src/Makefile，
以及 target/allwinner/v853-100ask/defconfig 和当前 .config。

请确认：
1. PKG_NAME、PKG_VERSION、TARGET 依赖和菜单位置；
2. 外部 app/core 路径和 LVGL 提交保护；
3. Build/Prepare 的输入文件、资源与许可文件；
4. Build/Compile 的 TARGET_CC、CFLAGS、LDFLAGS、BUILD_ID 和三个功能开关；
5. install 阶段是否只安装 /usr/bin/v853-lvgl9；
6. .config 与板级 defconfig 是否都选择 CONFIG_PACKAGE_v853-lvgl9-demo=y；
7. 刷新探针是否保持关闭；
8. 当前默认源码目录缺失是否会导致干净重建失败。

按“源码、配置、编译、安装、风险”汇总，然后停止等待。
禁止写文件、clean、编译、打包、sudo、联网下载或自动修复。
~~~

## 3. 理解 Build/Prepare 为什么要检查输入

当前软件包在复制源码前会确认：

- `main.c`、`lv_conf.h`、字体 C 文件和图片 C 文件存在。
- LVGL 核心头文件和 `src/` 存在。
- Source Han Sans SC 许可文件存在。
- `git rev-parse HEAD` 与固定提交完全相同。

随后只把所需文件复制到：

~~~text
out/v853-100ask/compile_dir/target/v853-lvgl9-demo-9.4.0/
~~~

这能避免编译时直接污染外部源码，也让构建输入更明确。`compile_dir` 是生成目录，可以用于排错和取证，但不能反过来替代丢失的原始源码。

## 4. 确认软件包已被选中

进入 SDK，在同一个 Bash 会话加载环境：

~~~bash
cd /home/ubuntu/100ask-course/sdk/tina-v853-100ask
source build/envsetup.sh
lunch v853_100ask-tina
~~~

检查当前配置和板级默认配置：

~~~bash
grep -n '^CONFIG_PACKAGE_v853-lvgl9-demo=y' .config
grep -n '^CONFIG_PACKAGE_v853-lvgl9-demo=y' \
    target/allwinner/v853-100ask/defconfig
grep -n 'CONFIG_V853_LVGL9_DEMO_REFRESH_PROBE' .config
~~~

课程当前结果为：

~~~text
CONFIG_PACKAGE_v853-lvgl9-demo=y
# CONFIG_V853_LVGL9_DEMO_REFRESH_PROBE is not set
~~~

如果软件包未选中，应使用 `make menuconfig` 在 `Gui → Littlevgl` 中选择 `v853-lvgl9-demo`，保存后再次用 `grep` 验证。不要只根据 menuconfig 界面记忆判断。

> 已有失败记录证明：软件包未选中时，某些 package 命令也可能返回成功，但不会产生目标 IPK。因此“退出码为 0”和“软件包已选中、IPK 已更新”必须同时满足。

## 5. 单独编译软件包

确认 `source`、`lunch`、源码路径和配置后执行：

~~~bash
LVGL9_SOURCE=/home/ubuntu/Downloads/lvgl9

make package/v853-lvgl9-demo/compile V=s \
    V853_LVGL9_SOURCE_DIR="$LVGL9_SOURCE"
package_status=$?
printf 'PACKAGE_EXIT_CODE=%s\n' "$package_status"
~~~

只有修改了输入但依赖没有触发，或明确需要从头验证本软件包时，才先执行一次受限清理：

~~~bash
make package/v853-lvgl9-demo/clean \
    V853_LVGL9_SOURCE_DIR="$LVGL9_SOURCE"
~~~

这是软件包级清理，不是全局 `make clean`，仍会删除该包的生成目录。让 Agent 执行前必须确认路径只指向 `v853-lvgl9-demo`。

编译成功后检查：

~~~bash
ipk=out/v853-100ask/packages/base/v853-lvgl9-demo_9.4.0-1_sunxi.ipk
test -s "$ipk"
ipk_status=$?
printf 'IPK_CHECK_EXIT_CODE=%s\n' "$ipk_status"
ls -lh --time-style=long-iso "$ipk"

elf=out/v853-100ask/compile_dir/target/rootfs/usr/bin/v853-lvgl9
test -s "$elf"
file "$elf"
~~~

IPK 和 ELF 的修改时间都应属于本轮构建。旧文件存在不能证明这次命令成功。

## 6. 让 Agent 执行受控集成构建

只读检查通过后，把 SDK 切换为 **Workspace Write**，源码目录保持只读，发送：

~~~text
请编译并验证 Tina SDK 的 v853-lvgl9-demo 软件包，不修改任何源码。

SDK=/home/ubuntu/100ask-course/sdk/tina-v853-100ask
LVGL9_SOURCE=/home/ubuntu/Downloads/lvgl9

在同一个 Bash 会话中：
1. 进入 SDK，source build/envsetup.sh，lunch v853_100ask-tina；
2. 确认 TARGET_PLAN=100ask、TARGET_BOARD=v853-100ask；
3. 确认 .config 已选择 CONFIG_PACKAGE_v853-lvgl9-demo=y，刷新探针关闭；
4. 确认 LVGL HEAD 为 c210a4efa2f474d0223d3e91c79963e1ae4ac0bc；
5. 只运行一次 package/v853-lvgl9-demo/compile V=s，并通过
   V853_LVGL9_SOURCE_DIR 指向上面的实际源码目录；
6. 立即记录 PACKAGE_EXIT_CODE；
7. 退出码为 0 时检查 IPK 和 compile_dir/target/rootfs/usr/bin/v853-lvgl9，
   输出大小、修改时间和 file 检查结果；
8. 按“配置、源码提交、退出码、IPK、ELF、结论”汇总。

禁止修改源码、全局 clean、sudo、apt、联网下载、系统安装、pack 或烧录。
失败时不要自动修复，只报告第一条有效错误和附近日志。
~~~

## 7. 完整编译、打包并核对 rootfs

软件包编译通过后，再执行完整构建：

~~~bash
make -j4 V853_LVGL9_SOURCE_DIR="$LVGL9_SOURCE"
make_status=$?
printf 'MAKE_EXIT_CODE=%s\n' "$make_status"
~~~

`MAKE_EXIT_CODE=0` 后检查 rootfs 镜像确实包含程序：

~~~bash
rootfs=out/v853-100ask/rootfs.img
test -s "$rootfs"
unsquashfs -ll "$rootfs" | grep '/usr/bin/v853-lvgl9$'
~~~

再执行一次打包：

~~~bash
pack
pack_status=$?
printf 'PACK_EXIT_CODE=%s\n' "$pack_status"

firmware=out/v853-100ask/tina_v853-100ask_uart0.img
test -s "$firmware"
firmware_status=$?
printf 'FIRMWARE_CHECK_EXIT_CODE=%s\n' "$firmware_status"
ls -lh --time-style=long-iso "$firmware"
~~~

需要兼容工具时沿用 [06–07 V853 SDK 配置与编译](../v853-tina-linux/06-v853-sdk-build.md) 中已经生成并验证的 SDK 本地工具；不要在本节临时安装系统软件。

## 2026-08-28 主机侧实测记录

| 验收门 | 结果 |
| --- | --- |
| package 编译 | 通过，耗时记录为 1 分 22 秒 |
| IPK | 263,955 字节 |
| rootfs ELF | 529,416 字节，ARM EABI5，静态、已 strip |
| 完整 make | 通过，耗时记录为 2 分 24 秒 |
| pack | `Dragon execute image.cfg SUCCESS !` |
| 固件 | 29,716,480 字节 |

以上大小与耗时来自当时的实测版本，不要求重新构建后数值完全相同。学习时应以本轮退出码、产物检查和板端实际运行结果为准。

## 8. 烧录后验证最终程序

按照 [08 固件烧录](../v853-tina-linux/08-firmware-flashing.md) 完成烧录。上板后先检查并临时停止 EdgeOS 监督服务，再查找实际触摸节点：

~~~sh
/etc/init.d/S99edgeos-v853 status
/etc/init.d/S99edgeos-v853 stop

ls -l /usr/bin/v853-lvgl9

for name in /sys/class/input/event*/device/name; do
    printf '%s -> ' "${name%/device/name}"
    cat "$name"
done

/usr/bin/v853-lvgl9 /dev/input/event3
~~~

最后一行的 event 节点必须替换为本次启动实际值。启动日志应显示：

~~~text
BUILD: id=v853_lvgl9-22-tina-c210a4efa lvgl=9.4.0 resources=1 refresh_probe=0 adaptive_sleep=1
~~~

再检查：

- `/proc/<pid>/exe` 指向 `/usr/bin/v853-lvgl9`，不是 `/tmp` 中的测试程序。
- 五点按钮产生完整 `PRESSED/RELEASED/CLICKED`。
- 滑块可以连续左右拖动。
- 中文、Logo、显示方向和颜色正常。

测试结束并按 `Ctrl + C` 退出后，恢复桌面：

~~~sh
/etc/init.d/S99edgeos-v853 start
~~~

2026-08-28 实测固件中，内核为 `#82`，板端 ELF 为 529,416 字节；FT6336 位于 `event3`，程序成功处理五点和滑块 30% → 90% → 37%。这些数据证明当时的 SDK 集成闭环，不替代本轮重新构建与实机验收。

## 常见问题

| 现象 | 优先检查 |
| --- | --- |
| `BoardConfig.mk` 路径中板名为空 | 没有在同一 Bash 会话执行 `source` 和 `lunch` |
| 找不到 `/home/ubuntu/100ask-course/lvgl9` | 默认外部源码缺失；核验实际源码并传 `V853_LVGL9_SOURCE_DIR` |
| LVGL revision check 失败 | 源码提交不是固定值或版本检查被错误转义；不要绕过保护 |
| package 命令返回 0 但没有 IPK | 软件包可能未被选择；检查 `.config`、IPK时间和构建日志 |
| IPK 有程序但 rootfs.img 没有 | 完整 `make` 是否成功、软件包是否进入 rootfs 安装阶段 |
| 固件里有程序但开机没界面 | 当前软件包没有自启动规则，需要人工执行 `/usr/bin/v853-lvgl9` |
| demo 启动后被其他界面覆盖 | `S99edgeos-v853` 会监督并重启桌面，先用服务脚本停止它 |
| 运行后仍看到旧 BUILD id | 可能启动了 `/tmp` 旧程序或烧录了旧固件 |
| 找不到 `/dev/input/event3` | event 编号发生变化，按设备名称重新查找 |
| `pack` 成功但板端还是旧版 | 核对固件时间、实际烧录文件、启动日志和 `/proc/<pid>/exe` |

## 验收清单

- [ ] 实际外部源码路径存在，LVGL 提交与固定值完全一致。
- [ ] `.config` 和板级 defconfig 都选择了 `v853-lvgl9-demo`。
- [ ] 刷新探针关闭，资源和自适应休眠开启。
- [ ] `PACKAGE_EXIT_CODE=0`，IPK 与 rootfs ELF 都属于本轮构建。
- [ ] `MAKE_EXIT_CODE=0`，`rootfs.img` 中包含 `/usr/bin/v853-lvgl9`。
- [ ] `PACK_EXIT_CODE=0`，完整固件非空，并记录路径、大小和修改时间。
- [ ] 板端 `/proc/<pid>/exe` 指向 `/usr/bin/v853-lvgl9`。
- [ ] 显示、触摸、中文、图片和主循环完成回归。
- [ ] 已明确当前软件包不负责自启动。
- [ ] Agent 权限已经切回 Read Only，构建日志和回退信息已保存。

## LVGL 9.4 官方参考

- [Integration Overview](https://docs.lvgl.io/9.4/details/integration/overview/index.html)
- [Building LVGL](https://docs.lvgl.io/9.4/details/integration/overview/building_lvgl.html)
- [Configuration](https://docs.lvgl.io/9.4/details/integration/overview/configuration.html)
- [make Integration](https://docs.lvgl.io/9.4/details/integration/building/make.html)

## 版本与变更记录

- 2026-09-04：根据 SDK 中 `v853-lvgl9-demo` 的实际 Makefile、Config.in、defconfig、构建产物和 2026-08-28 上板记录补全；新增外部源码目录缺失的复现风险说明。
