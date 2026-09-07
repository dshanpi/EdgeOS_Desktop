# 18 让触摸屏控制 LVGL 界面（evdev）

> 本节把 Linux 已经识别的触摸设备接入 LVGL 9.4。先在开发板上按设备名称找到 `eventN`，再让应用通过命令行接收节点；不要把某次启动的 `event3` 当成永久配置。

![evdev 所在的 LVGL 输入链路](images/guide-lvgl-runtime.svg)

## 学习目标

- 看懂“触摸控制器 → Linux input → evdev → LVGL 控件”的输入链路。
- 使用 `lv_evdev_create()` 创建指针设备，并绑定第 17 节的显示对象。
- 用五点按钮、滑块和串口日志同时验证按下、释放、点击与拖动。
- 让 AI Agent 只修改 LVGL 9 应用，不误改内核、设备树或官方 LVGL 源码。

## 前置条件

- 已完成 [17 让 LVGL 界面显示到 LCD](17-lvgl9-on-lcd.md)，`/dev/fb0` 可以正常显示。
- `lv_conf.h` 中已经启用：

  ~~~c
  #define LV_USE_LINUX_FBDEV 1
  #define LV_USE_EVDEV       1
  ~~~

- LVGL 固定在 `c210a4efa2f474d0223d3e91c79963e1ae4ac0bc`，版本输出为 `v9.4.0-3-gc210a4efa`。
- 应用目录为 `<LVGL9_ROOT>/v853-port`。课程软件包默认期望 `LVGL9_ROOT=/home/ubuntu/100ask-course/lvgl9`。

:::warning
当前虚拟机中，保留的完整源码位于 `/home/ubuntu/Downloads/lvgl9`，而 `/home/ubuntu/100ask-course/lvgl9` 当前不存在。开始修改或重新编译前，必须先确认本轮使用哪一份源码；不要把 SDK 的 `out/.../compile_dir` 当成长期源码目录。
:::

## 本板实测基线

2026-08-28 的课程固件实测值如下：

| 项目 | 实测结果 |
| --- | --- |
| framebuffer | `/dev/fb0`，可见分辨率 480×800 |
| Touch 名称 | `ft6336` |
| 当次节点 | `/dev/input/event3` |
| 应用接口 | `lv_evdev_create(LV_INDEV_TYPE_POINTER, touch_path)` |
| 输入绑定 | `lv_indev_set_display(touch, disp)` |
| 实机结果 | 五个区域均产生完整事件，滑块可连续往返拖动 |

若已经完成 [09 使用 AI Agent 适配 D320C2403V1 与 GT967](../v853-tina-linux/09_LCDScreenAdaptation.md)，设备名称应改为 GT967、GT9xx 或 `gt9xxnew_ts`，显示目标应为 1024×768。两套硬件不能共用“名称和分辨率”结论，但查找 `eventN` 和接入 LVGL 的方法相同。

## 1. 在开发板上找到正确的 event 节点

下面的命令在**开发板串口终端**执行：

~~~sh
cat /proc/bus/input/devices

for name in /sys/class/input/event*/device/name; do
    printf '%s -> ' "${name%/device/name}"
    cat "$name"
done
~~~

不要只看节点编号，要同时记录设备名称。例如课程基线会看到：

~~~text
/sys/class/input/event3 -> ft6336
~~~

记录本次启动的节点：

~~~sh
touch_dev=/dev/input/event3
test -c "$touch_dev"
printf 'TOUCH_DEVICE=%s CHECK_EXIT_CODE=%s\n' "$touch_dev" "$?"
~~~

只有 `CHECK_EXIT_CODE=0` 才继续。使用 GT967 时，把变量换成实际发现的节点。

> `eventN` 会受到驱动加载顺序影响。应用接受命令行参数，比在源码中反复修改默认值更可靠。

## 2. 让 AI Agent 只读检查现有接入

把 LVGL 9 根目录设为 Agent 工作区，权限先选 **Read Only**，发送：

~~~text
请只读检查 V853 LVGL 9.4 的触摸接入，不要修改或编译。

源码根目录以当前实际存在的目录为准，应用位于 v853-port/main.c，
LVGL 固定提交必须是 c210a4efa2f474d0223d3e91c79963e1ae4ac0bc。

请确认：
1. lv_conf.h 中 LV_USE_EVDEV=1、LV_USE_LINUX_FBDEV=1；
2. main.c 允许 argv[1] 传入 /dev/input/eventN，而不是只能使用固定节点；
3. lv_evdev_create() 明确创建 LV_INDEV_TYPE_POINTER；
4. lv_indev_set_display() 把触摸绑定到 fbdev 返回的显示对象；
5. 创建了可点击的五点控件、可拖动滑块和可见指针；
6. PRESSED、RELEASED、CLICKED、VALUE_CHANGED 会打印到 stderr；
7. 本节不需要修改 Tina SDK、内核、设备树或 LVGL 官方源码。

按“配置、代码链路、缺失项、建议修改文件”汇总，然后停止等待。
~~~

当前课程源码的关键部分应与下面一致：

~~~c
const char * touch_path = argc == 2 ? argv[1] : "/dev/input/event3";

lv_display_t * disp = lv_linux_fbdev_create();
lv_linux_fbdev_set_file(disp, "/dev/fb0");

lv_indev_t * touch = lv_evdev_create(LV_INDEV_TYPE_POINTER, touch_path);
if(touch == NULL) {
    fprintf(stderr, "FAIL: could not open touch device %s\n", touch_path);
    return 1;
}
lv_indev_set_display(touch, disp);
~~~

这里强制使用 `LV_INDEV_TYPE_POINTER`，是因为课程基线的 FT6336 只上报多点触摸事件，自动识别不一定能得到期望的指针类型。GT967 也可以先沿用指针模式验证单指点击与拖动。

## 3. 让 Agent 补齐触摸测试界面

如果上一步发现缺失项，把权限切换为 **Workspace Write**，发送：

~~~text
请只修改 v853-port/main.c 和确有必要的 v853-port/lv_conf.h，
补齐 V853 的 LVGL 9.4 evdev 触摸测试。

要求：
1. 程序接受 0 或 1 个参数；argv[1] 是 /dev/input/eventN；
2. 无参数时可保留 /dev/input/event3 作为课程基线默认值，但启动时必须打印实际节点；
3. 使用 lv_evdev_create(LV_INDEV_TYPE_POINTER, touch_path)；
4. 使用 lv_indev_set_display(touch, disp) 绑定 /dev/fb0 对应显示；
5. 页面包含左上、右上、中心、左下、右下五个按钮和一个水平滑块；
6. 显示一个跟随触点的 18×18 红色圆点；
7. 把 PRESSED、RELEASED、CLICKED 和滑块 VALUE_CHANGED 的坐标打印到 stderr；
8. 创建失败时返回非 0，不静默继续。

先展示修改计划和 diff，等待我确认。禁止修改 LVGL 官方源码、Tina SDK、
内核或设备树；禁止 sudo、联网下载、删除源码、打包或烧录。
~~~

本课程的 480×800 页面使用了绝对坐标。若目标是 1024×768 的 D320C2403V1，应让 Agent 把布局改成百分比、Flex 或 Grid；不要只把屏幕宽高改成新数值，否则按钮仍会挤在左侧。

## 4. 交叉编译测试程序

先选定实际源码根目录。当前虚拟机可用的保留副本是：

~~~bash
LVGL9_ROOT=/home/ubuntu/Downloads/lvgl9
PORT="$LVGL9_ROOT/v853-port"

test -f "$PORT/main.c" && test -f "$PORT/lv_conf.h"
git -C "$LVGL9_ROOT/lvgl" rev-parse HEAD
~~~

提交必须是：

~~~text
c210a4efa2f474d0223d3e91c79963e1ae4ac0bc
~~~

确认后编译本节程序：

~~~bash
make -C "$PORT" clean APP=v853_lvgl9-18-touch
make -C "$PORT" \
    LVGL="$LVGL9_ROOT/lvgl" \
    APP=v853_lvgl9-18-touch \
    WITH_RESOURCES=0 \
    WITH_REFRESH_PROBE=0 \
    ADAPTIVE_SLEEP=1
build_status=$?
printf 'BUILD_EXIT_CODE=%s\n' "$build_status"
file "$PORT/v853_lvgl9-18-touch"
~~~

这里的 `clean` 只清理 `v853-port/build` 和指定应用产物，不是 Tina SDK 全局清理。若 Agent 计划清理 SDK 的 `out/`，应拒绝。

只有 `BUILD_EXIT_CODE=0` 且 `file` 显示 ARM 32-bit EABI5，才把程序传到开发板。传输方式沿用第 17 节，本文不限定 ADB、串口或网络。

## 5. 在开发板前台运行

当前课程配置还选择了 `v853-edgeos-desktop`，它通过 `/etc/init.d/S99edgeos-v853` 自动启动并在退出后重启。测试前先检查并临时停止它，否则仅结束应用进程会被监督脚本再次拉起：

~~~sh
/etc/init.d/S99edgeos-v853 status
/etc/init.d/S99edgeos-v853 stop
~~~

再退出其他可能占用 framebuffer 的旧程序，然后运行：

~~~sh
chmod +x /tmp/v853_lvgl9-18-touch
/tmp/v853_lvgl9-18-touch "$touch_dev"
~~~

正常启动至少会打印：

~~~text
LVGL fbdev resolution: 480x800
BUILD: id=v853_lvgl9-18-touch lvgl=9.4.0 ...
Touch device: /dev/input/event3
READY: tap five buttons and drag the slider
~~~

分辨率和 event 节点应以当前硬件为准。出现 `READY` 只证明初始化完成，还要进行下一步人工触摸。

本节测试完成并按 `Ctrl + C` 退出后，需要恢复桌面时执行：

~~~sh
/etc/init.d/S99edgeos-v853 start
~~~

## 6. 完成五点与拖动验收

依次点击左上、右上、中心、左下、右下按钮。每次正常点击应出现完整序列：

~~~text
TOUCH: TOP LEFT PRESSED  x=62 y=143  clicks=4
TOUCH: TOP LEFT RELEASED x=62 y=143  clicks=4
TOUCH: TOP LEFT CLICKED  x=62 y=143  clicks=5
~~~

再把滑块从左向右拖动并返回，日志中的百分比和坐标应连续变化。2026-08-28 集成固件实测记录包括：

| 区域 | 代表坐标 |
| --- | --- |
| 左上 | `(62,143)` |
| 右上 | `(391,188)` |
| 中心 | `(252,308)` |
| 左下 | `(68,720)` |
| 右下 | `(389,730)` |
| 滑块 | 30% → 90% → 37% |

这些坐标用于证明当时的 480×800 FT6336 基线，不是其他屏幕的校准常量。

## 常见问题

| 现象 | 优先检查 |
| --- | --- |
| `could not open touch device` | 节点是否存在、是否把实际 `eventN` 作为参数传入、旧节点是否已变化 |
| 有原始事件但控件不响应 | `LV_USE_EVDEV`、`LV_INDEV_TYPE_POINTER` 和 `lv_indev_set_display()` |
| 按下有日志但没有 `CLICKED` | 手指是否在控件外释放、坐标是否跳变、界面是否被滚动 |
| X/Y 交换或镜像 | 先记录四角原始结果；驱动层和应用层只选一处修正 |
| 指针能动但按钮位置不对应 | 显示旋转、输入坐标范围和应用布局是否一致 |
| 1024×768 上控件只在左侧 | 当前测试页是 480×800 绝对布局，改用 Flex、Grid 或百分比尺寸 |
| 程序启动后屏幕被覆盖 | 是否同时运行了 `lv_examples`，或 EdgeOS 监督脚本重新拉起了桌面 |

## 验收清单

- [ ] 通过设备名称找到本次启动实际的 `eventN`。
- [ ] `LV_USE_EVDEV=1`，触摸以 `LV_INDEV_TYPE_POINTER` 创建。
- [ ] 输入设备绑定到 `/dev/fb0` 对应的显示对象。
- [ ] 程序打印实际分辨率、版本、构建标识和触摸节点。
- [ ] 五个区域都有完整的按下、释放、点击事件。
- [ ] 滑块能够连续向两个方向拖动，红色指针跟随触点。
- [ ] 没有修改 Tina SDK、内核、设备树或官方 LVGL 源码。

完成本节后，显示和输入链路都已经接通。下一节将验证 LVGL 的单调时钟、刷新调度和 CPU 占用。

## LVGL 9.4 官方参考

- [evdev 驱动](https://docs.lvgl.io/9.4/details/integration/embedded_linux/drivers/evdev.html)
- [evdev API](https://docs.lvgl.io/9.4/API/drivers/evdev/lv_evdev_h.html)
- [LVGL 输入设备](https://docs.lvgl.io/9.4/details/main-modules/indev/overview.html)

## 版本与变更记录

- 2026-09-04：根据 V853 工程 `main.c`、`lv_conf.h` 与 2026-08-28 板端触摸日志补全操作步骤、Agent 边界和两套触摸硬件说明。
