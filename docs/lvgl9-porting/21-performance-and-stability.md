# 21 检查界面性能和稳定性

> 本节不追求一个漂亮的瞬时 FPS，而是固定程序、页面、操作和采样方法，证明进程持续存在、内存没有明显增长、空闲 CPU 可接受，并保留能够复核的原始数据。

![性能基线在 LVGL 交付链中的位置](images/guide-lvgl-delivery.svg)

## 学习目标

- 使用构建标识锁定本轮被测程序，避免测到旧二进制。
- 通过 `/proc` 记录进程身份、状态、VmSize、RSS、峰值 RSS 和线程数。
- 正确筛选 BusyBox `top` 中的目标进程行。
- 区分 5 分钟短测、30 分钟冒烟和 2 小时稳定性测试的证明范围。

## 固定测试版本

课程静态基线使用：

| 参数 | 取值 |
| --- | --- |
| LVGL | 9.4.0，提交 `c210a4efa...` |
| 资源 | `WITH_RESOURCES=1` |
| 刷新探针 | `WITH_REFRESH_PROBE=0` |
| 主循环 | `ADAPTIVE_SLEEP=1` |
| 颜色深度 | 32 bpp |
| 默认刷新周期 | 33 ms |
| LVGL OS | `LV_OS_NONE`，单 GUI 线程 |
| 构建标识 | `v853_lvgl9-21-static` |

刷新探针必须关闭，否则持续动画会改变空闲 CPU 基线。页面、固件、分辨率、触摸设备和测试动作也要记录；更换到 1024×768 D320C2403V1 时，应重新建立基线，不能与 480×800 数据直接横比。

## 1. 让 AI Agent 只读制定测量计划

先使用 **Read Only**，发送：

~~~text
请根据当前 V853 LVGL 9.4 工程制定性能与稳定性测试计划，暂时不要修改或编译。

检查 v853-port/main.c、lv_conf.h、Makefile 和已有 evidence，确认：
1. 构建参数为 resources=1、refresh_probe=0、adaptive_sleep=1；
2. 启动日志包含唯一 BUILD id、LVGL 版本、分辨率和触摸节点；
3. 当前 LV_USE_SYSMON、LV_USE_PROFILER、LV_USE_OS 的值；
4. 板端可用的 BusyBox top、/proc/<pid>/status、/proc/<pid>/stat、/proc/uptime；
5. 如何用 /proc/<pid>/stat 第 22 字段防止 PID 被复用；
6. 采样 CSV、top 原始输出、应用日志和 dmesg 的保存路径；
7. 先测基线，不进行任何优化。

不要安装工具、打开 profiler、修改源码、清理 SDK 或开始长时间测试。
最后按“被测版本、指标、采样周期、失败条件、输出文件”汇总。
~~~

当前 `lv_conf.h` 中 `LV_USE_SYSMON=0`、`LV_USE_PROFILER=0`。本节先用 Linux `/proc` 和 `top` 建立低开销基线；只有定位 LVGL 内部热点时才评估打开这些功能，因为诊断本身也会影响结果。

## 2. 构建静态基线程序

下面命令在 Ubuntu 虚拟机执行：

~~~bash
LVGL9_ROOT=/home/ubuntu/Downloads/lvgl9
PORT="$LVGL9_ROOT/v853-port"

make -C "$PORT" clean APP=v853_lvgl9-21-static
make -C "$PORT" \
    LVGL="$LVGL9_ROOT/lvgl" \
    APP=v853_lvgl9-21-static \
    WITH_RESOURCES=1 \
    WITH_REFRESH_PROBE=0 \
    ADAPTIVE_SLEEP=1
build_status=$?
printf 'BUILD_EXIT_CODE=%s\n' "$build_status"
file "$PORT/v853_lvgl9-21-static"
~~~

传板前可以从未 strip 文件另存部署版：

~~~bash
TOOLCHAIN=/home/ubuntu/100ask-course/sdk/tina-v853-100ask/prebuilt/gcc/linux-x86/arm/toolchain-sunxi-musl/toolchain
cp -a "$PORT/v853_lvgl9-21-static" "$PORT/v853_lvgl9-21-static.deploy"
"$TOOLCHAIN/bin/arm-openwrt-linux-muslgnueabi-strip" --strip-all \
    "$PORT/v853_lvgl9-21-static.deploy"
file "$PORT/v853_lvgl9-21-static.deploy"
~~~

课程存档中的部署版是 ARM EABI5、静态链接、已 strip，大小 526,280 字节。这个数值只用来识别当时的样本，不是所有重新构建都必须逐字节相同。

## 3. 启动被测程序并锁定 PID

把部署版传到开发板后，在板端执行：

~~~sh
touch_dev=/dev/input/event3
chmod +x /tmp/v853_lvgl9-21-static.deploy
/tmp/v853_lvgl9-21-static.deploy "$touch_dev" \
    >/tmp/21-static.log 2>&1 &
pid=$!
printf '%s\n' "$pid" >/tmp/21-static.pid

sleep 3
cat /tmp/21-static.log
test -r "/proc/$pid/status"
printf 'PROCESS_CHECK_EXIT_CODE=%s PID=%s\n' "$?" "$pid"
~~~

启动日志应包含：

~~~text
LVGL fbdev resolution: 480x800
BUILD: id=v853_lvgl9-21-static lvgl=9.4.0 resources=1 refresh_probe=0 adaptive_sleep=1
Touch device: /dev/input/event3
READY: tap five buttons and drag the slider
~~~

分辨率和触摸节点以当前硬件为准，但四个构建参数必须与基线一致。

## 4. 采集 60 秒可复用基线

下面脚本每 10 秒记录一次 `/proc`。它还保存进程启动时钟；如果 PID 消失或被复用，立即判定失败：

~~~sh
pid=$(cat /tmp/21-static.pid)
start_ticks=$(awk '{print $22}' "/proc/$pid/stat")
start_uptime=$(awk '{print int($1)}' /proc/uptime)

echo 'runtime_s,state,vmsize_kib,vmrss_kib,vmhwm_kib,threads' \
    >/tmp/21-static-60s.csv

i=0
while [ "$i" -le 60 ]; do
    test -r "/proc/$pid/stat" || exit 2
    current_ticks=$(awk '{print $22}' "/proc/$pid/stat")
    test "$current_ticks" = "$start_ticks" || exit 3

    state=$(awk '/^State:/ {print $2}' "/proc/$pid/status")
    vmsize=$(awk '/^VmSize:/ {print $2}' "/proc/$pid/status")
    vmrss=$(awk '/^VmRSS:/ {print $2}' "/proc/$pid/status")
    vmhwm=$(awk '/^VmHWM:/ {print $2}' "/proc/$pid/status")
    threads=$(awk '/^Threads:/ {print $2}' "/proc/$pid/status")

    printf '%s,%s,%s,%s,%s,%s\n' \
        "$i" "$state" "$vmsize" "$vmrss" "$vmhwm" "$threads" \
        >>/tmp/21-static-60s.csv

    [ "$i" -eq 60 ] && break
    sleep 10
    i=$((i + 10))
done

end_uptime=$(awk '{print int($1)}' /proc/uptime)
printf 'MONITOR_OK runtime=%s pid=%s start_ticks=%s\n' \
    "$((end_uptime - start_uptime))" "$pid" "$start_ticks"
~~~

再采集 CPU 原始输出：

~~~sh
top -b -d 1 -n 3 >/tmp/21-static.top
cat /tmp/21-static.top
~~~

分析时只统计第一列 PID **等于目标 PID** 的行。不能用简单的 `grep v853_lvgl9` 直接求平均，因为采集脚本或 `grep` 自己的命令行也可能包含这个字符串。

## 5. 执行人工场景

静态空闲基线通过后，固定执行一轮交互：

1. 五个区域各点击一次。
2. 滑块从左到右再返回。
3. 保持页面空闲 60 秒。
4. 观察动画、点击响应、图片与中文是否正常。
5. 保存 `/tmp/21-static.log`、CSV、`top` 和测试后的 `dmesg`。

没有统一的高速摄像或 GPIO 测量条件时，不要编造“触摸延迟 xx ms”。可以记录主观是否卡顿，但必须标注为人工观察，而不是精密数据。

## 2026-08-28 五分钟实测结果

课程存档完成了一次 300 秒短测：

| 指标 | 结果 |
| --- | --- |
| PID / start ticks | `1577 / 218987`，测试前后未变化 |
| 采样 | 每 10 秒一次，共 31 行 |
| 进程状态 | 全部为 `S` |
| VmSize | 全部为 3836 KiB |
| VmRSS / VmHWM | 全部为 660 / 660 KiB |
| 线程数 | 全部为 1 |
| 进程 CPU | 排除每组三屏的第一屏后，12 个样本为 0.0%～0.3% |
| 内核日志 | 本轮运行期间没有新增内核消息 |
| 结论 | 5 分钟内进程存活、内存平稳、空闲 CPU 较低 |

:::warning
这只证明一次 5 分钟短测。它不能替代 30 分钟冒烟、2 小时稳定性测试或三轮重复基线，也不能证明程序永远没有内存泄漏。
:::

## 6. 延长到 30 分钟和 2 小时

60 秒脚本通过后，再把总时长分别扩展到 1800 秒和 7200 秒，保持 10～60 秒的固定采样周期。长测期间不要临时改变页面、固件或后台负载。

失败条件至少包括：

- 进程退出、PID 被复用或状态长期为不可中断睡眠。
- RSS 或 VmHWM 持续单向增长且无法在空闲阶段回落。
- CPU 占用持续异常升高。
- 触摸停止响应、画面冻结、花屏或重启。
- `dmesg` 出现 OOM、崩溃、I/O 或 framebuffer 错误。

让 Agent 分析数据时，使用下面的提示词：

~~~text
请只分析本轮 V853 LVGL 稳定性证据，不修改代码。

输入包括：应用启动日志、CSV、top 原始输出、测试前后 dmesg、
固件/程序版本、运行路径和人工操作记录。

要求：
1. 先核对 BUILD id、PID 和 start_ticks，排除测错进程；
2. 计算 RSS、HWM、线程数和 CPU 的范围与趋势；
3. top 只统计第一列 PID 精确匹配目标 PID 的行；
4. 区分事实、推断和人工观察；
5. 明确本轮时长能证明什么、不能证明什么；
6. 有异常时只提出单变量实验，不自动修改或重新编译。
~~~

## 7. 有证据后再优化

当前基线已经关闭刷新探针，并使用自适应休眠。只有指标显示问题时才考虑下一项：

| 现象 | 单变量候选 |
| --- | --- |
| 空闲 CPU 高 | 检查 handler 等待上限、无效动画或频繁 invalidate |
| 滚动掉帧 | 检查绘制区域、缓冲大小、渲染模式，再评估硬件加速 |
| RSS 增长 | 检查重复创建对象、图片缓存和页面销毁路径 |
| 启动慢 | 分开测 ELF 装载、资源初始化和首帧时间 |
| 程序过大 | 字体子集、图片格式、未用模块和 strip 状态 |

每轮只改一个主要变量，重新构建并重复完全相同的场景。功能回归失败时立即回退该变量。

## 验收清单

- [ ] 被测程序的 BUILD id、版本、运行路径、页面和硬件参数已记录。
- [ ] 刷新探针关闭，资源和自适应休眠配置固定。
- [ ] PID 与 `/proc/<pid>/stat` start ticks 在测试前后相同。
- [ ] CSV 包含状态、VmSize、RSS、HWM 和线程数。
- [ ] CPU 只统计目标 PID 的进程行，并保留原始 `top`。
- [ ] 五点点击、滑块、中文和图片完成回归。
- [ ] 测试时长和结论边界明确，没有把 5 分钟写成长期稳定。
- [ ] 优化遵守单变量原则，并能恢复到基线版本。

## LVGL 9.4 官方参考

- [System Monitor](https://docs.lvgl.io/9.4/details/auxiliary-modules/sysmon.html)
- [Profiler](https://docs.lvgl.io/9.4/details/debugging/profiler.html)
- [Timer Handler](https://docs.lvgl.io/9.4/details/integration/overview/timer_handler.html)

## 版本与变更记录

- 2026-09-04：根据 V853 300 秒 `/proc`、BusyBox `top`、应用日志和 framebuffer 证据补全，并明确长期测试尚未完成。
