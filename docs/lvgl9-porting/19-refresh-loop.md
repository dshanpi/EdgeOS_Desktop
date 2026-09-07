# 19 让 LVGL 持续刷新并响应操作

> tick 告诉 LVGL“现在几点”，`lv_timer_handler()` 负责处理到期的刷新、输入和 Timer。本工程由 fbdev 驱动注册 Linux 单调时钟，再依据 `lv_timer_handler()` 的返回值自适应休眠。

![tick 与 lv_timer_handler 所在的运行链路](images/guide-lvgl-runtime.svg)

## 学习目标

- 找到当前工程真正使用的 tick 来源，不重复调用 `lv_tick_inc()`。
- 理解 33 ms 默认刷新周期和 1～20 ms 自适应休眠边界。
- 用同一动画、同一触摸步骤对比固定 5 ms 轮询与自适应等待。
- 使用板端 `top` 和事件日志判断优化是否保持功能正确。

## 当前工程的时间与刷新配置

| 项目 | 当前实现 |
| --- | --- |
| LVGL 版本 | 9.4.0，提交 `c210a4efa...` |
| 操作系统抽象 | `LV_USE_OS=LV_OS_NONE` |
| 默认刷新周期 | `LV_DEF_REFR_PERIOD=33` ms，约 30 Hz |
| tick 来源 | `lv_linux_fbdev_create()` 注册 `clock_gettime(CLOCK_MONOTONIC)` 回调 |
| 刷新入口 | 单一主线程循环调用 `lv_timer_handler()` |
| 自适应休眠 | 返回值限制在 1～20 ms 后传给 `usleep()` |
| 可视化探针 | 36×20 青色方块，启用后 2 秒横向移动、2 秒返回 |

## 1. 弄清楚 tick 从哪里来

当前 LVGL 9.4 的 `lv_linux_fbdev_create()` 内部会执行：

~~~c
lv_tick_set_cb(tick_get_cb);
~~~

对应回调使用：

~~~c
static uint32_t tick_get_cb(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    uint64_t time_ms = t.tv_sec * 1000 + (t.tv_nsec / 1000000);
    return time_ms;
}
~~~

因此本工程只要先创建 fbdev 显示，就已经获得毫秒级单调时钟。不要再增加一个线程调用 `lv_tick_inc()`，否则会形成两套时间来源。

> 单调时钟不受手工修改系统日期影响，适合动画和超时。它表示开机后的持续时间，不是日历时间。

## 2. 让 AI Agent 审计刷新循环

先使用 **Read Only**，发送：

~~~text
请只读审计 V853 LVGL 9.4 工程的 tick 与刷新循环，不要修改或编译。

检查 v853-port/main.c、v853-port/lv_conf.h，以及固定提交
c210a4efa2f474d0223d3e91c79963e1ae4ac0bc 中的：
lvgl/src/drivers/display/fb/lv_linux_fbdev.c
lvgl/src/tick/lv_tick.c

请确认：
1. lv_linux_fbdev_create() 是否通过 clock_gettime(CLOCK_MONOTONIC) 注册 tick 回调；
2. 应用是否又调用了 lv_tick_inc() 或 lv_tick_set_cb()；
3. LV_DEF_REFR_PERIOD 和 LV_USE_OS 的当前值；
4. 主循环是否只由一个线程调用 lv_timer_handler()；
5. 是否处理 LV_NO_TIMER_READY，并限制最小、最大休眠时间；
6. ENABLE_REFRESH_PROBE 的动画是否只用于测试；
7. Timer 或事件回调中是否存在阻塞 I/O、长时间 sleep 或跨线程调用 LVGL。

按“tick、handler、休眠、线程、风险”汇总。禁止修改 LVGL 官方源码、
Tina SDK 或系统配置。
~~~

## 3. 使用自适应等待循环

当前课程源码中的主循环为：

~~~c
#define GUI_SLEEP_MIN_MS  1U
#define GUI_SLEEP_MAX_MS 20U

while(1) {
    uint32_t wait_ms = lv_timer_handler();

    if(wait_ms == LV_NO_TIMER_READY) {
        wait_ms = LV_DEF_REFR_PERIOD;
    }

    if(wait_ms < GUI_SLEEP_MIN_MS) wait_ms = GUI_SLEEP_MIN_MS;
    if(wait_ms > GUI_SLEEP_MAX_MS) wait_ms = GUI_SLEEP_MAX_MS;

    usleep((useconds_t)wait_ms * 1000U);
}
~~~

下限 1 ms 防止返回 0 时形成忙循环；上限 20 ms 避免触摸输入长时间得不到处理。`LV_NO_TIMER_READY` 不是错误，本工程用 33 ms 默认周期替代后还会被上限收敛到 20 ms。

如果现有工程没有这段逻辑，可以让 Agent 修改：

~~~text
请只修改 v853-port/main.c，把固定 usleep(5000) 的主循环改为：
读取 lv_timer_handler() 返回值；LV_NO_TIMER_READY 时使用 LV_DEF_REFR_PERIOD；
最终把等待限制在 1～20 ms。保持单线程，不新增 tick 线程，不修改 LVGL 源码。

同时保留 USE_ADAPTIVE_SLEEP 编译开关，使 0 表示固定 5 ms，1 表示自适应等待。
先展示 diff，确认后再写入并编译。
~~~

## 4. 建立固定轮询与自适应轮询两个版本

下面命令在 Ubuntu 虚拟机执行。当前保留源码位于 `/home/ubuntu/Downloads/lvgl9`；若已恢复课程标准目录，只修改 `LVGL9_ROOT`：

~~~bash
LVGL9_ROOT=/home/ubuntu/Downloads/lvgl9
PORT="$LVGL9_ROOT/v853-port"

make -C "$PORT" clean APP=v853_lvgl9-19-fixed5
make -C "$PORT" \
    LVGL="$LVGL9_ROOT/lvgl" \
    APP=v853_lvgl9-19-fixed5 \
    WITH_RESOURCES=0 \
    WITH_REFRESH_PROBE=1 \
    ADAPTIVE_SLEEP=0
fixed_status=$?
printf 'FIXED_BUILD_EXIT_CODE=%s\n' "$fixed_status"

make -C "$PORT" clean APP=v853_lvgl9-19-adaptive
make -C "$PORT" \
    LVGL="$LVGL9_ROOT/lvgl" \
    APP=v853_lvgl9-19-adaptive \
    WITH_RESOURCES=0 \
    WITH_REFRESH_PROBE=1 \
    ADAPTIVE_SLEEP=1
adaptive_status=$?
printf 'ADAPTIVE_BUILD_EXIT_CODE=%s\n' "$adaptive_status"
~~~

每次更换宏参数前都要清理 `v853-port/build`。当前 Makefile 没有把编译参数写入对象依赖，不清理就可能复用上一组 `.o`，得到名字变了、逻辑却没变的假对照。

两个退出码都为 `0` 后，分别用 `file` 检查程序类型，确认文件非空，再传到开发板 `/tmp`。一次只运行一个版本。

## 5. 使用同一场景上板对比

对每个版本执行相同步骤：

1. 前台运行程序并预热 10 秒。
2. 确认青色方块移动速度稳定。
3. 依次点击五个按钮，并把滑块左右拖动一次。
4. 另开串口或 ADB shell，采集五屏 `top`：

   ~~~sh
   top -b -d 1 -n 5
   ~~~

5. 保存应用日志和 `top` 原始输出，停止当前程序后再测另一个版本。

启动日志必须能区分构建：

~~~text
BUILD: id=v853_lvgl9-19-fixed5 lvgl=9.4.0 resources=0 refresh_probe=1 adaptive_sleep=0
BUILD: id=v853_lvgl9-19-adaptive lvgl=9.4.0 resources=0 refresh_probe=1 adaptive_sleep=1
~~~

## 课程实测结果

2026-08-28 在 480×800、FT6336、同一页面上的短时采样为：

| 版本 | 排除第一屏后的 4 个进程 CPU 样本 | 功能结果 |
| --- | --- | --- |
| 固定 5 ms | 0.0%、1.0%、0.9%、0.9% | 动画运行 |
| 自适应 1～20 ms | 1.0%、0.5%、0.2%、0.0% | 五点点击和双向拖动通过 |

样本数量很少，BusyBox `top` 的分辨率也有限，因此这里只能证明自适应版本没有造成明显 CPU 恶化且功能正常，不能据此宣称精确节省了多少百分比。第 21 节会使用 `/proc` 和更长时间窗口建立稳定性基线。

## 线程规则

当前 `LV_USE_OS=LV_OS_NONE`，应用只有一个 GUI 线程。保持以下边界：

- 只有主循环调用 LVGL API 和 `lv_timer_handler()`。
- 其他线程如需更新界面，先把数据写入队列，再由 GUI 线程取出。
- Timer 和输入事件回调只做短操作，不执行网络、文件拷贝或长时间等待。
- 不要从信号处理函数直接创建、删除 LVGL 对象。

## 常见问题

| 现象 | 优先检查 |
| --- | --- |
| 日志提示 tick 没有更新 | 是否先调用了 `lv_linux_fbdev_create()`，fbdev 驱动是否编入程序 |
| 动画不动但触摸有日志 | tick 回调、刷新 Timer 和探针开关是否正确 |
| 动画过快或过慢 | 是否同时使用了 fbdev tick 与 `lv_tick_inc()` |
| 空闲 CPU 持续很高 | 是否忽略 handler 返回值、休眠下限是否为 0 |
| 触摸明显迟钝 | 最大休眠是否过大、事件回调是否阻塞 |
| 两个版本输出完全相同 | 更换宏前是否清理项目对象，BUILD 行是否真的不同 |
| 偶发崩溃或对象损坏 | 是否有多个线程同时调用 LVGL |

## 验收清单

- [ ] tick 只来自 fbdev 注册的 `CLOCK_MONOTONIC` 回调。
- [ ] 工程没有额外调用 `lv_tick_inc()` 形成第二套时间源。
- [ ] 自适应循环正确处理 handler 返回值和 `LV_NO_TIMER_READY`。
- [ ] 休眠范围限制为 1～20 ms，`LV_DEF_REFR_PERIOD=33` ms。
- [ ] A/B 两个版本的构建标识和宏参数可以从日志区分。
- [ ] 自适应版本动画稳定、五点点击和双向拖动正常。
- [ ] 原始应用日志与 `top` 输出已经保存，没有只记录主观感受。

## LVGL 9.4 官方参考

- [Timer Handler](https://docs.lvgl.io/9.4/details/integration/overview/timer_handler.html)
- [Timer](https://docs.lvgl.io/9.4/details/main-modules/timer.html)
- [Tick API](https://docs.lvgl.io/9.4/API/tick/lv_tick_h.html)
- [Threading Considerations](https://docs.lvgl.io/9.4/details/integration/overview/threading.html)

## 版本与变更记录

- 2026-09-04：根据 LVGL 9.4 fbdev tick 实现、V853 主循环和 2026-08-28 两组板端采样补全。
