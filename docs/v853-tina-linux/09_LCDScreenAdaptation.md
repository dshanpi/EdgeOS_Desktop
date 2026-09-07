# 09 使用 AI Agent 适配 3.2 寸 LCD 与触摸（可选）

> 本节使用 AI Agent，把已经验证过的 D320C2403V1 LCD 与 GT967 Touch 适配内容审计并应用到 V853 Tina SDK。Agent 负责比对文件、检查配置一致性和执行受控修改；屏幕参数、写入范围与最终验收仍由我们确认。

## 学习目标

- 理解 V853 LCD 从设备树、Kconfig 到面板驱动注册的完整链路。
- 使用 AI Agent 只读比较适配包与 SDK，避免无差别覆盖源码。
- 在限定的 18 个文件内完成 D320C2403V1 与 GT967 适配。
- 编译并打包包含新驱动的固件，检查退出码、打包日志和实际产物。
- 在开发板上分别验证 LCD 显示、GT967 输入事件与触摸方向。

## 前置条件

- 已完成 [06–07 V853 SDK 配置与编译](06-v853-sdk-build.md)，能够选择 `v853_100ask-tina` 配置。
- Ubuntu 虚拟机可以通过 SSH 连接。本课程环境使用 `192.168.153.162`，登录用户为 `ubuntu`。
- SDK 路径为：

  ~~~text
  /home/ubuntu/100ask-course/sdk/tina-v853-100ask
  ~~~

- 已提取的适配包路径为：

  ~~~text
  /home/ubuntu/Public/v853-d320c2403v1-lcd-touch-adaptation
  ~~~

- 适配包来源可信，并且没有在提取后继续手工修改。
- 插拔 LCD 或 Touch 排线前，必须先断开开发板电源。

:::warning
本节可能修改 SDK 源码和配置。先使用 **Read Only** 完成比对，确认差异和备份方案后，才把 SDK 工作区切换为 **Workspace Write**。不要授权 Agent 使用 `sudo`、删除目录、清理 SDK、联网下载或修改系统文件。
:::

## 本次适配对象

| 项目 | 参数 |
| --- | --- |
| 开发板 | 100ASK V853 |
| Tina 配置 | `v853_100ask-tina` |
| LCD 模组 | D320C2403V1 |
| LCD 驱动名 | `d320fpc2403` |
| LCD 控制器初始化 | JD9168S |
| 分辨率 | `1024×768` |
| 显示接口 | MIPI DSI，4 lane，RGB888 |
| 像素时钟 | 24 MHz |
| 背光 | PWM9，5 kHz |
| LCD 复位 GPIO | PE17 |
| Touch 控制器 | Goodix GT967，最多 5 点 |
| Touch 总线 | TWI2 |
| Touch 中断/复位 | PH7 / PH8 |
| Touch 输出坐标 | `1024×768` |

> 课程适配包中的 LCD 已经过实机确认；GT967 已完成驱动和设备树适配，但触摸事件、坐标方向仍必须在实际硬件上验证。

## 先理解驱动生效链路

LCD 驱动不是只增加一个 `.c` 文件。它需要同时打通下面的关系：

~~~text
board.dts / uboot-board.dts
        │  lcd_driver_name = "d320fpc2403"
        ▼
Linux 与 U-Boot 的 Kconfig
        │  CONFIG_LCD_SUPPORT_D320FPC2403_MIPI=y
        ▼
Makefile 编译 d320fpc2403.o
        ▼
panels.h 声明 + panels.c 注册
        ▼
d320fpc2403.c 执行上电、复位、JD9168S 初始化、开背光
        ▼
U-Boot 开机显示 + Linux framebuffer
~~~

Touch 适配则由 `board.dts` 中的 TWI2、地址、坐标和 GPIO 参数连接到 `gt9xxnew` 驱动，最终生成 `/dev/input/eventN` 输入节点。

Linux 与 U-Boot 都有一套 LCD 驱动，是因为开机阶段由 U-Boot 控制显示，进入系统后由 Linux 接管。只修改其中一套，容易出现“开机有画面、进入系统黑屏”或相反的现象。

## 1. 为 Agent 设置最小工作范围

在已经连接到 `192.168.153.162` 的 AI Agent 中，把下面目录设为 SDK 工作区：

~~~text
/home/ubuntu/100ask-course/sdk/tina-v853-100ask
~~~

初始权限选择 **Read Only**。适配包不在 SDK 内；Agent 读取它时，只额外授权下面这个目录的只读访问，不要直接授权整个 `/home/ubuntu`：

~~~text
/home/ubuntu/Public/v853-d320c2403v1-lcd-touch-adaptation
~~~

如果使用的 Agent 支持多工作区，也可以把 SDK 设为可写工作区、把适配包设为只读参考目录。此时仍先保持全部只读，完成下一步后再改变权限。

## 2. 让 Agent 先做只读审计

把下面的提示词完整发送给 Agent：

~~~text
请只读审计 V853 LCD/Touch 适配，暂时不要编译，也不要修改或复制任何文件。

SDK=/home/ubuntu/100ask-course/sdk/tina-v853-100ask
BUNDLE=/home/ubuntu/Public/v853-d320c2403v1-lcd-touch-adaptation

请完成：
1. 确认两个目录都存在，SDK 配置目标是 v853_100ask-tina；
2. 阅读适配包 README.md，列出适配目标和限制；
3. 递归列出适配包 device/ 与 lichee/ 下的普通文件，确认总数；
4. 使用 cmp 将每个文件与 SDK 中相同相对路径的文件比较；
5. 按 SAME、DIFFERENT、MISSING 三类输出相对路径和数量；
6. 对 DIFFERENT 文件只展示 diff --stat 和关键配置差异，不输出超长初始化数组；
7. 检查这 18 个目标路径是否已有未提交改动；
8. 按“环境、文件清单、比对结果、已有改动、建议”汇总，然后停止等待。

禁止写文件、cp、mv、编译、clean、rm、sudo、联网下载和系统级安装。
~~~

本课程当前 SDK 快照的预期结果是：适配包共 **18 个文件**，与 SDK 中对应文件全部为 `SAME`。这表示 SDK 已经包含适配，不需要再次覆盖；可以直接进入“检查关键配置”，再决定是否重新编译固件。

若 Agent 报告 `DIFFERENT` 或 `MISSING`，不要马上批准复制。先确认差异确实属于本屏适配，同时检查目标文件中有没有自己尚未保存的修改。

## 3. 核对 18 个文件白名单

### 板级配置与编译配置

| 文件 | 作用 |
| --- | --- |
| `device/config/chips/v853/configs/100ask/board.dts` | Linux LCD、GT967 与引脚参数 |
| `device/config/chips/v853/configs/100ask/uboot-board.dts` | U-Boot LCD 参数 |
| `device/config/chips/v853/configs/100ask/linux/config-4.9` | Linux LCD 和 Touch 驱动开关 |
| `lichee/brandy-2.0/u-boot-2018/configs/sun8iw21p1_defconfig` | U-Boot LCD 驱动开关 |

### Linux LCD 驱动

~~~text
lichee/linux-4.9/drivers/video/fbdev/sunxi/disp2/disp/Makefile
lichee/linux-4.9/drivers/video/fbdev/sunxi/disp2/disp/lcd/Kconfig
lichee/linux-4.9/drivers/video/fbdev/sunxi/disp2/disp/lcd/panels.c
lichee/linux-4.9/drivers/video/fbdev/sunxi/disp2/disp/lcd/panels.h
lichee/linux-4.9/drivers/video/fbdev/sunxi/disp2/disp/lcd/d320fpc2403.c
lichee/linux-4.9/drivers/video/fbdev/sunxi/disp2/disp/lcd/d320fpc2403.h
~~~

### U-Boot LCD 驱动

~~~text
lichee/brandy-2.0/u-boot-2018/drivers/video/sunxi/disp2/disp/Makefile
lichee/brandy-2.0/u-boot-2018/drivers/video/sunxi/disp2/disp/lcd/Kconfig
lichee/brandy-2.0/u-boot-2018/drivers/video/sunxi/disp2/disp/lcd/panels.c
lichee/brandy-2.0/u-boot-2018/drivers/video/sunxi/disp2/disp/lcd/panels.h
lichee/brandy-2.0/u-boot-2018/drivers/video/sunxi/disp2/disp/lcd/d320fpc2403.c
lichee/brandy-2.0/u-boot-2018/drivers/video/sunxi/disp2/disp/lcd/d320fpc2403.h
~~~

### Linux GT967 驱动

~~~text
lichee/linux-4.9/drivers/input/touchscreen/gt9xxnew/gt9xx.c
lichee/linux-4.9/drivers/input/touchscreen/gt9xxnew/gt9xx.h
~~~

适配写入只能涉及上面的路径。若 Agent 计划修改其他文件，先拒绝并让它说明必要性；不要因为“可能相关”就扩大写入范围。

## 4. 让 Agent 检查关键配置

继续保持 **Read Only**，发送下面的提示词：

~~~text
请继续只读检查本次适配的配置闭环，不要修改文件。

请逐项给出“文件路径、当前值、是否一致”：
1. Linux board.dts 中 lcd_used、lcd_driver_name、lcd_x/lcd_y、lcd_dclk_freq、
   lcd_if、lcd_dsi_lane、lcd_dsi_format、PWM 和 lcd_gpio_0；
2. uboot-board.dts 中对应 LCD 参数是否与 Linux 一致；
3. Linux 与 U-Boot 是否都启用 CONFIG_LCD_SUPPORT_D320FPC2403_MIPI=y；
4. 两侧 Makefile、Kconfig、panels.h、panels.c 是否完成 d320fpc2403 的编译和注册；
5. 面板驱动的 open/close flow 是否包含供电、复位、DSI 初始化、TCON、背光，
   关闭时是否发送 Display Off 和 Sleep In；
6. Linux board.dts 中 GT967 是否使用 TWI2、地址 0x14、PH7 中断、PH8 复位、
   1024×768 坐标；
7. gt9xx.h 中是否保持 GTP_DRIVER_SEND_CFG=0、GTP_AUTO_UPDATE=0、
   GTP_AUTO_UPDATE_CFG=0，并说明这些保护项的作用。

最后只报告不一致项和风险；全部一致时明确写出“配置闭环完整”。
禁止写文件、编译、清理或自动修复。
~~~

正确的关键值应为：

~~~dts
lcd_used             = <1>;
lcd_driver_name      = "d320fpc2403";
lcd_if               = <4>;
lcd_x                = <1024>;
lcd_y                = <768>;
lcd_dclk_freq        = <24>;
lcd_dsi_lane         = <4>;
lcd_dsi_format       = <0>;
lcd_pwm_ch           = <9>;
lcd_pwm_freq         = <5000>;
lcd_gpio_0           = <&pio PE 17 1 0 3 1>;
~~~

水平时序为 `hbp=16`、`ht=1050`、`hspw=5`，垂直时序为 `vbp=22`、`vt=796`、`vspw=2`。这些值来自已经验证的适配，不要让 Agent 根据经验随意“优化”。

Touch 的关键配置应为：

~~~dts
ctp_used              = <1>;
ctp_name              = "gt9xxnew_ts";
ctp_twi_id            = <0x2>;
ctp_twi_addr          = <0x14>;
ctp_screen_max_x      = <0x400>;
ctp_screen_max_y      = <0x300>;
ctp_int_port          = <&pio PH 7 6 1 3 0xffffffff>;
ctp_wakeup            = <&pio PH 8 1 1 3 0xffffffff>;
~~~

`0x400` 和 `0x300` 分别是十六进制的 1024 和 768。GT967 复位后的实际应答地址可能表现为 `0x5d`；不能只根据一次 `i2cdetect` 就把设备树中的 `ctp_twi_addr=<0x14>` 改掉。

`GTP_DRIVER_SEND_CFG=0` 用来保留触摸模组的出厂配置，`GTP_AUTO_UPDATE=0` 与 `GTP_AUTO_UPDATE_CFG=0` 用来避免向 GT967 自动写入不匹配的固件或配置。除非拿到模组供应商确认过的配置和固件，否则不要打开它们。

## 5. 存在差异时再授权 Agent 写入

只有只读审计确认存在需要应用的差异，并且 18 个目标文件没有不明修改时，才把 SDK 工作区切换为 **Workspace Write**。适配包保持只读。

:::danger
如果目标文件中已有自己的修改，先停止。不要让 Agent 用适配包覆盖这些文件，也不要执行 `git checkout`、`git reset --hard` 或 `rm`。应先备份并人工合并差异。
:::

发送下面的提示词：

~~~text
请把已审计的 D320C2403V1 LCD 与 GT967 适配应用到当前 SDK。

SDK=/home/ubuntu/100ask-course/sdk/tina-v853-100ask
BUNDLE=/home/ubuntu/Public/v853-d320c2403v1-lcd-touch-adaptation

执行边界：
1. 只允许写入刚才确认的 18 个白名单相对路径；
2. 若 18 个文件全部 SAME，立即停止，不做任何复制；
3. 写入前再次检查目标路径的未提交修改；发现不明修改立即停止；
4. 在 /home/ubuntu/Public 下新建带时间戳的备份目录，按原相对路径备份所有已存在的目标文件；
5. 先输出备份目录、待写文件和 diff --stat，等待我确认；
6. 我确认后，使用 cp -a 逐个复制，只创建白名单文件所需的父目录；
7. 复制后逐个使用 cmp 校验，要求 18 个文件全部一致；
8. 输出 git diff --stat，并按“备份、写入、校验、剩余风险、恢复方法”汇总。

禁止修改适配包，禁止写白名单外的文件，禁止 sudo、联网下载、clean、rm、
git checkout、git reset、编译、打包或烧录。任何一步失败都立即停止，不要自动扩大操作范围。
~~~

Agent 展示计划后，重点核对：

- 备份目录位于 `/home/ubuntu/Public`，不是 SDK 内的临时输出目录。
- 待写文件没有超出 18 个白名单路径。
- 现有文件使用 `cp -a` 备份，没有直接删除。
- Agent 没有把“全部相同”误判为仍需覆盖。

写入完成后，把工作区权限切回 **Read Only**。备份先保留到新固件完成实机验收以后。

## 6. 编译并打包新固件

LCD 同时修改了 Linux 与 U-Boot，GT967 修改了 Linux，因此只编译一个用户程序无法让适配生效。完成源码审计或写入后，按 [06–07 V853 SDK 配置与编译](06-v853-sdk-build.md) 中已经验证过的环境执行完整编译与打包。

需要 Agent 执行时，重新把 SDK 切换为 **Workspace Write**，发送：

~~~text
请只编译并打包已经完成审计的 V853 LCD/Touch 适配，不再修改源码。

在同一个 Bash 会话中：
1. cd /home/ubuntu/100ask-course/sdk/tina-v853-100ask
2. source build/envsetup.sh
3. lunch v853_100ask-tina
4. 确认 TARGET_PLAN=100ask、TARGET_BOARD=v853-100ask、TARGET_KERNEL_VERSION=4.9
5. mkernel -j4，只运行一次；结束后立即保存并打印 MKERNEL_EXIT_CODE
6. 退出码为 0 才执行 make -j4，只运行一次；立即保存并打印 MAKE_EXIT_CODE
7. 退出码为 0 才执行 pack，只运行一次；立即保存并打印 PACK_EXIT_CODE
8. 三个退出码都为 0 时，检查 boot.img、rootfs.img 和
   out/v853-100ask/tina_v853-100ask_uart0.img 均存在且非空；
9. 输出完整固件的路径、大小和修改时间。

如果第 06–07 节已经生成了 SDK 内的本地兼容工具，只把实际存在的工具目录加入 PATH。
禁止 sudo、apt、联网下载、clean、rm、修改源码、自动修复或烧录。
任一步失败立即停止，只报告第一条有效错误及附近日志。

最后按“配置、三个退出码、三个产物、结论”汇总。
~~~

三个退出码必须都为 `0`。完整烧写固件应为：

~~~text
out/v853-100ask/tina_v853-100ask_uart0.img
~~~

旧固件可能仍在 `out/` 中，所以“文件存在”不能代替本轮退出码。还要确认修改时间属于本轮打包，并记录实际固件路径。随后按照 [08 固件烧录](08-firmware-flashing.md) 操作；烧录属于破坏性步骤，必须由现场人员确认目标设备和固件后执行。

## 7. 在开发板验证 LCD

下面的命令在**开发板串口终端**执行，不是在 Ubuntu 虚拟机或 SSH 编译终端中执行：

~~~sh
dmesg | grep -Ei 'd320|lcd|dsi|disp|framebuffer|fb'
cat /sys/class/graphics/fb0/virtual_size
~~~

预期 `virtual_size` 的可见宽高与 `1024,768` 对应。若系统使用双缓冲，虚拟高度可能是 768 的整数倍；此时还要结合应用打印的实际宽高判断，不能只看第二个数字。

继续完成目视验收：

- U-Boot 阶段和 Linux 启动后都能正常显示。
- 画面完整铺满，没有持续花屏、闪烁、滚动或裁切。
- 红、绿、蓝颜色正常，没有明显通道交换。
- 背光可以正常打开，亮度稳定。

## 8. 在开发板验证 GT967 Touch

先查找输入设备，不要固定写死 `/dev/input/event3`：

~~~sh
dmesg | grep -Ei 'gt967|gt9|goodix|ctp|touch|input|i2c|irq'
cat /proc/bus/input/devices
cat /proc/interrupts | grep -Ei 'gt9|goodix|ctp'
~~~

从 `/proc/bus/input/devices` 中找到 Goodix、GT9xx 或 `gt9xxnew_ts` 对应的 `eventN`。若固件已经带有 `getevent` 或 `evtest`，可使用现有工具观察该节点；本节不为测试额外安装软件。

触摸验收至少包含：

1. 依次点击左上、右上、左下、右下和中心。
2. 水平和垂直方向各拖动一次。
3. 确认 X/Y 没有交换，左右和上下没有镜像。
4. 多点应用可用时，确认最多 5 点报告稳定。

如果方向错误，先记录五点坐标，再依据证据一次只调整 `ctp_revert_x_flag`、`ctp_revert_y_flag` 或 `ctp_exchange_x_y_flag` 中的一项。不要同时修改三个标志再猜结果。

> [10 外设验证：LCD 与 Touch](10-peripheral-validation.md) 中的 `480×800` 和 `ft6336` 是课程默认固件的另一套实测基线。烧录本节的 D320C2403V1/GT967 固件后，应以 `1024×768` 和 GT967/GT9xx 输入设备为验收目标，不能把两套硬件参数混用。

## 常见问题

| 现象 | 优先检查 |
| --- | --- |
| 18 个文件全部显示 `SAME` | 当前 SDK 已包含适配，不要重复覆盖；继续检查配置闭环和固件产物 |
| U-Boot 和 Linux 都没有背光 | `lcd_used`、PWM9、供电、排线方向以及背光使能流程 |
| 背光亮但没有画面 | 驱动名、Kconfig、Makefile、panel 注册、DSI lane、时序和初始化序列 |
| U-Boot 有画面，进入 Linux 后黑屏 | Linux 的 `board.dts`、`config-4.9` 和 Linux 面板注册 |
| Linux 正常，但 U-Boot 没有画面 | `uboot-board.dts`、U-Boot defconfig 和 U-Boot 面板注册 |
| 画面滚动、裁切或颜色异常 | 分辨率、24 MHz 像素时钟、水平/垂直时序、DSI format；不要同时改多项 |
| 没有 Touch 输入节点 | TWI2、PH7/PH8、驱动配置、设备树状态和启动日志 |
| `i2cdetect` 看到 `0x5d` | 不要仅凭扫描结果修改 `ctp_twi_addr=<0x14>`；结合复位和驱动日志判断 |
| 有坐标但方向错误 | 记录五点坐标，再逐项调整 revert/exchange 标志 |
| 编译成功但仍是旧显示参数 | 核对本轮 `PACK_EXIT_CODE`、固件修改时间和实际烧录文件 |
| Agent 准备修改第 19 个文件 | 拒绝操作，要求说明必要性并重新限定白名单 |

## 验收清单

- [ ] Agent 先以 Read Only 完成了适配包和 SDK 的 18 文件比对。
- [ ] `lcd_driver_name` 与 Linux、U-Boot 面板注册名完全一致。
- [ ] Linux 与 U-Boot 都启用了 `CONFIG_LCD_SUPPORT_D320FPC2403_MIPI=y`。
- [ ] LCD 参数为 1024×768、4-lane MIPI DSI、24 MHz，PWM 和 GPIO 配置正确。
- [ ] GT967 使用 TWI2、PH7/PH8，坐标范围为 1024×768。
- [ ] 存在差异时，写入前已备份，且只修改 18 个白名单文件。
- [ ] `MKERNEL_EXIT_CODE=0`、`MAKE_EXIT_CODE=0`、`PACK_EXIT_CODE=0`。
- [ ] 完整固件存在且非空，已记录路径、大小和修改时间。
- [ ] LCD 在 U-Boot 与 Linux 阶段均显示正常。
- [ ] GT967 五点点击、拖动、方向与多点报告通过实机验证。
- [ ] 验收完成前保留源码备份，Agent 权限已经切回 Read Only。

## 版本与变更记录

- 适用配置：`v853_100ask-tina`，Linux `4.9`，U-Boot `2018`。
- 适配对象：D320C2403V1 LCD（JD9168S）与 Goodix GT967 Touch。
- 2026-09-04：根据课程 SDK 与独立适配包的实际文件、设备树参数和驱动注册关系编写；当前课程 SDK 的 18 个对应文件与适配包一致。
