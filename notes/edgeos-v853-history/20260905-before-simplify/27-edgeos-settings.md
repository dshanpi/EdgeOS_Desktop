# 27 适配 EdgeOS Desktop 设置中心

:::info 后续功能已进入第 28 节
本章保留只读系统信息页、自适应和部署的阶段记录。当前设置中心已继续扩展为分类功能页，Wi-Fi、背光、声音、时间偏好、启动应用与电源的实现和验证见 [28 接入设置中心常用功能](./28-edgeos-settings-services.md)。第 28 节还包含已正式部署的 EdgeOS 风格优化版：圆形返回按钮、彩色图标卡片、详情分组及居中弹窗。本章的只读页在新版中保留为 About，不再代表当前设置中心的全部能力或最新外观。
:::

> 本节在第 26 节核心桌面基础上，适配 Settings 设置中心。当前实现定位为“只读系统信息页”：显示开发板、内核、显示、触摸、根文件系统和源码身份。网络、语言、时间、亮度等系统设置只有在真实后端接通后才能标记为可用。

![正式部署并重启后运行的 Settings 页面](./images/edgeos-v853-settings-production-reboot-20260905.png)

*上图是 2026-09-05 将 SDK 正式构建产物部署到开发板、正常重启并完成重复导航后，从 framebuffer 抓取的 Settings 页面。图片中的参数是当次设备证据，不是页面布局依赖的固定常量。*

:::info 最新状态：已正式部署并验证重启
第 1～11 节记录独立预览流程，第 12 节记录发现“预览通过但正式程序未更新”的过程。随后按照第 13 节完成 SDK 软件包构建、正式路径部署和正常重启验证，板端当前保留新版 Settings，不再回退旧界面。请区分板端持久部署、SDK 构建产物与完整固件烧录，三者不是同一个验收项。
:::

<details>
<summary>查看 EdgeOS Desktop 设置中心产品界面参考</summary>

![EdgeOS Desktop 设置中心产品界面参考](../images/edgeos-desktop/settings-overview.jpg)

*产品图只用于参考信息层级和视觉语言，不作为本次 V853 验收证据。*

</details>

## 学习目标

- 梳理 Settings 入口、页面状态和返回导航。
- 区分运行时真实信息、编译期身份和静态占位文案。
- 让设置内容自动适应横向与竖向目标屏幕。
- 处理长文本、采集失败、内容滚动和底部状态遮挡。
- 通过 AI Agent 完成修改、构建、传输、日志和截图验证。

## 首轮预览实操结果

| 验证项 | 结果 | 直接证据 |
| --- | :---: | --- |
| LYNX 工作台 | 通过 | 工作台、唯一 ADB 设备和串口均在线；收尾时 `hardwareSafety.busy=false`、活动列表为空 |
| LYNX ADB Shell | 失败并受控降级 | 调用超过两分钟没有返回，主动终止等待；未把设备在线误写成命令成功 |
| LYNX 串口 | 未取得控制权 | 打开 `COM13` 返回 `PORT_BUSY:COM13`，没有强制关闭占用者 |
| 源码改造 | 通过 | 修改前后 SHA-256 已记录，新增真实 Touch、64 位存储计算、信息卡片和滚动内容区 |
| 严格构建 | 通过 | `BUILD_EXIT_CODE=0`，`-Wall -Wextra -Werror` 无 warning/error |
| 三端传输 | 通过 | 虚拟机、Windows、板端文件大小一致；Windows 与板端 MD5 一致 |
| 进入 Settings | 通过 | 真实 evdev 节点收到 `pressed → released → clicked`，随后出现 `EDGEOS_VIEW: settings` |
| 字段交叉验证 | 通过 | Board、Kernel、Display、Touch、Root filesystem、Source 均与第二证据一致 |
| 两列布局 | 首次失败，修正后通过 | 首次 framebuffer 发现单列留白；修正内容宽度余量后重新构建并抓图通过 |
| Back 返回 | 通过 | `EDGEOS_TOUCH: action=back` 后出现 `EDGEOS_VIEW: desktop` |
| 物理手指点击 | 未测试 | 本轮使用临时 evdev 测试器注入点击，不等同于触摸面板电气与坐标准确性验收 |
| 竖向目标屏幕 | 未测试 | 没有连接对应硬件，不能由自适应公式推断通过 |
| 正式服务恢复 | 通过 | 临时文件已删除，正式监督进程和正式桌面程序重新运行 |

:::info
本次 LYNX 用于设备身份、安全状态、ADB 与串口通道的前后核对。由于 LYNX ADB Shell 无返回且串口被占用，程序传输、framebuffer 抓取和只读交叉验证改用 LYNX 已确认的唯一宿主机 ADB 设备。文档保留这条受控降级链路，不宣称所有命令均由 LYNX 控制通道完成。
:::

## 功能边界

| 功能 | 修改前 | 本次实现与实测结果 |
| --- | --- | --- |
| 打开 Settings | 已有入口 | 两次注入点击均进入 `VIEW_SETTINGS`，完整事件链可见 |
| 返回桌面 | 已有 Back | 注入 Back 点击后返回 `VIEW_DESKTOP` |
| 板卡与内核信息 | 运行时读取 | 页面值与设备树及 `uname` 交叉验证一致 |
| 显示信息 | 运行时尺寸 | 页面值与本次 LVGL 启动日志一致 |
| Touch 信息 | 静态说明 | 已改为程序实际使用的设备名和事件节点，并经 sysfs 核对 |
| 根文件系统剩余空间 | 失败时也显示零 | 已改为 64 位计算并区分失败；成功路径与板端文件系统信息一致 |
| 长文本与内容滚动 | 单一正文 Label | 已改为可换行信息卡片和滚动内容区；当前横向页面完整，长文本失败分支仍未注入测试 |
| 网络、语言、时间、声音、亮度 | 没有后端 | 本次未实现，也未标记为可用 |
| 参数持久化 | 没有后端 | 本次未实现 |

:::warning
`EDGEOS_READY: settings=1` 只说明 Settings 功能被编译进程序，不能证明用户已经打开页面，也不能证明页面上的数据正确。
:::

## 预览阶段前置条件

- 已完成 [26 将 EdgeOS Desktop 核心 UI 移植到 V853](./26-edgeos-desktop-ui.md)。
- Lynx、AI Agent、虚拟机和开发板连接已经按前文配置完成。
- 正式桌面服务可以停止、启动并查询状态。
- 预览程序只部署到 `/tmp`，不替换正式程序、不刷写固件。

:::tip 本节操作方式
用户只需把文中的提示词发送给 AI Agent。源码读取、修改、编译、文件传输、进程控制、日志保存和 framebuffer 抓图均由 Agent 执行；用户只负责必须在物理屏幕上完成的点击和观察。
:::

## 1. 核对入口与导航

Settings 卡片在 `g_apps[]` 中注册为 `APP_READY`，目标页面为 `VIEW_SETTINGS`。完整跳转关系如下：

~~~text
VIEW_DESKTOP
    │
    ├── Settings 卡片 PRESSED / RELEASED
    │
    ├── CLICKED 且不是滑动
    │
    ▼
VIEW_SETTINGS
    │
    ├── 采集系统信息并创建页面
    │
    └── Back CLICKED
            │
            ▼
       VIEW_DESKTOP
~~~

进入 Settings 前仍要保留桌面卡片的拖动判断。快速滑动应用区时产生的点击应记录为 `click-ignored reason=drag`，不能误进入设置页。

当前 `show_settings()` 在每次进入页面时重新采样一次系统信息。页面停留期间不会周期刷新，因此它适合显示板卡身份和系统摘要，不适合显示需要实时更新的网络速率、CPU 使用率等指标。

## 2. 只读审计源码

先让 Agent 核对当前实现，不立即修改：

~~~text
请只读审计 EdgeOS Settings 页面，暂时不要修改、编译或运行板端程序。

源码：
/home/ubuntu/Downloads/edgeos/v853-port/main.c

请检查并报告：
1. Settings 在 g_apps 中的状态、目标 view 和图标；
2. 从桌面进入 Settings、从 Back 返回桌面的完整事件链；
3. show_settings() 读取了哪些 Linux 数据，每项的来源和失败回退值；
4. 显示尺寸和触摸节点来自运行时、编译期还是静态文案；
5. panel、heading、body、badge 的位置和尺寸如何计算；
6. 长板卡名称、长内核版本、长 Source ID 是否可能被截断或遮挡；
7. 正文是否支持换行和滚动，底部 badge 是否可能与正文重叠；
8. statvfs 失败是否能和真正的零剩余空间区分；
9. 页面日志能够证明什么、不能证明什么。

请按“已有能力、数据真实性、布局缺口、建议修改、验证边界”汇总。
全程只读，完成后停止等待。
~~~

本次只读审计确认修改前 `main.c` 的 Settings 基线包含以下信息：

| 页面字段 | 当前数据来源 | 当前风险 |
| --- | --- | --- |
| Board | `/proc/device-tree/model` | 读取失败时显示回退值；超长内容需要换行 |
| Kernel | `uname()` | 长版本字符串可能占用多行 |
| Display | `g_layout.width/height` | 来自启动时显示尺寸，运行中切换方向不会自动重排 |
| Touch | 静态 `auto detected` 文案 | 无法证明实际设备名和事件节点 |
| Root filesystem free | `statvfs("/")` | 失败和真实零值无法区分；计算应先转为 64 位 |
| EdgeOS source | `EDGEOS_SOURCE_ID` | 长字符串需要换行和截断提示 |

## 3. 设计信息模型

不要直接在 `show_settings()` 中把所有内容拼成一段长字符串。先将数据采集和页面渲染分开：

~~~c
typedef struct {
    char board[128];
    char kernel[160];
    char display[64];
    char touch[160];
    char rootfs[64];
    char source[128];
    bool board_ok;
    bool kernel_ok;
    bool rootfs_ok;
} edgeos_settings_info_t;
~~~

建议由 `collect_settings_info()` 负责采集，由 `show_settings()` 负责创建 LVGL 对象。这样可以单独测试错误回退，也能避免 UI 代码直接依赖多个 Linux 系统调用。

### 数据真实性规则

| 数据 | 显示规则 |
| --- | --- |
| Board | 成功时显示设备树型号；失败时明确显示 `Unavailable` |
| Kernel | 显示系统名、release 和架构；失败时标记未知，不伪造版本 |
| Display | 显示 LVGL 当前 display 的运行时信息，不使用固定屏幕常量 |
| Touch | 显示程序实际选择的设备名和节点，不写死 `eventN` |
| Root filesystem | 使用 64 位中间值计算；失败时显示 `Unavailable`，不显示假零值 |
| Source | 显示本次构建的 Source ID，并与启动日志保持一致 |

如果触摸设备是在 `main()` 中发现的，应把设备名和节点保存到全局运行时状态，再由 Settings 读取。不要在页面内重复扫描后得到与实际输入设备不同的结果。

## 4. 设计自适应布局

当前面板宽高已经跟随运行时布局，但正文使用单个 Label，标题和状态 badge 采用固定相对位置。内容变长时，正文可能与底部 badge 重叠。

推荐对象树：

~~~text
screen
├── status_bar
├── back_button
└── settings_content        可纵向滚动
    ├── page_heading
    ├── identity_group
    │   ├── Board
    │   └── Kernel
    ├── device_group
    │   ├── Display
    │   └── Touch
    ├── storage_group
    │   └── Root filesystem
    ├── build_group
    │   └── EdgeOS source
    └── status_badge        参与 Flex 排版，不覆盖正文
~~~

布局的目标规则如下。其中“紧凑空间退回单列”仍是设计要求；当前源码只按横竖方向选择列数，未实现最小卡片宽度判断，见第 12 节复核结果。

- 内容宽度和高度从 `g_layout` 的运行时结果取得。
- 外层内容区启用纵向滚动，不让 screen 本身滚动。
- 横向空间充足时使用两列信息卡片，竖向或紧凑空间使用单列。
- 卡片宽度由内容宽度、列数和 gap 计算，不绑定固定分辨率。
- Value Label 设置可用宽度并启用长文本换行。
- badge 作为 Flex 子项排在内容末尾，不使用覆盖正文的底部绝对定位。
- Back 始终位于内容区之外，滚动后仍可返回。

### 长文本策略

板卡型号、内核版本和 Source ID 都可能比预期长。不要通过减小字体强行塞入一行，应按以下顺序处理：

1. 为 Value Label 设置明确的可用宽度。
2. 使用 `LV_LABEL_LONG_WRAP` 自动换行。
3. 让卡片高度随内容增长。
4. 让外层内容区滚动。
5. 若采集缓冲区发生截断，记录日志并在页面上显示省略标志。

以上是完整的处理目标。当前预览版已配置换行、内容高度和外层滚动，但尚未实现第 5 项的截断检测与提示，也未完成长内容溢出实测。

## 5. 实施最小修改

审计结论确认后，发送：

~~~text
请只修改 /home/ubuntu/Downloads/edgeos/v853-port/main.c，
完善 Settings 只读系统信息页。

修改要求：
1. 将系统信息采集收敛到 edgeos_settings_info_t 和独立采集函数；
2. 保留 Settings 入口、VIEW_SETTINGS、Back 返回和桌面拖动防误触；
3. Board、Kernel、Display、Touch、Root filesystem、Source 都显示真实来源；
4. Touch 使用程序实际打开的设备名和节点；
5. statvfs 使用 64 位中间值，并区分读取失败与真实零值；
6. 将正文改成可换行的信息项或信息卡片；
7. 内容区使用 Flex 布局并允许纵向滚动；
8. 横向空间充足时两列，竖向或紧凑空间单列；
9. 状态 badge 参与正常排版，不能覆盖正文；
10. 保留当前颜色、字体和 EdgeOS 视觉风格；
11. 增加精简日志，记录字段采集状态、页面方向和列数，但不要泄露敏感数据；
12. 不实现网络、语言、时间、声音、亮度或持久化设置；
13. 不修改 LVGL 官方源码、Tina SDK、设备树或正式板端程序。

修改前保存 main.c 备份和 SHA-256。完成后给出变更摘要、关键 diff、
失败回退策略和仍需实机验证的项目，然后停止等待。
~~~

建议将修改限制在一个入口文件内。若 Touch 真实身份需要在多个函数间传递，可以增加小型运行时结构，但不要在本轮引入完整配置服务。

### 本次实际修改

Agent 只修改了 `v853-port/main.c`，修改前备份为：

~~~text
/home/ubuntu/Downloads/edgeos/v853-port/main.c.before-settings-20260904
~~~

| 项目 | 实际结果 |
| --- | --- |
| 修改前 SHA-256 | `85358bc3904897bf2c7f41742c0af881efa1c585ad2e2225ba9e6f2afd6190e0` |
| 修改后 SHA-256 | `de93423542940bb1ecbd064f53c451c0ea845168d5f093a0c0414c54a4187a05` |
| 差异规模 | 新增 151 行，删除 32 行 |
| 数据结构 | 新增 `edgeos_settings_info_t` |
| 数据采集 | 新增 `collect_settings_info()`，分别记录 Board、Kernel、Rootfs 的成功状态 |
| Touch | 启动时保存实际设备名和节点，Settings 直接读取同一运行时身份 |
| 存储 | 使用 `uint64_t` 计算可用字节，失败时显示 `Unavailable` |
| 页面 | 六张可换行信息卡片，横向两列、竖向单列，外层可纵向滚动 |
| 日志 | 新增 `EDGEOS_SETTINGS_DATA`，`EDGEOS_VIEW` 记录方向、列数和字段数 |

没有修改 LVGL 官方源码、Tina SDK、设备树或开发板正式程序。

## 6. 交叉编译

确认 diff 没有扩大功能边界后，继续发送：

~~~text
请在虚拟机中交叉编译修改后的 Settings 预览程序。

要求：
- 自动核对 EdgeOS、LVGL 9、v853-port 和交叉工具链的实际路径；
- 保留 -Wall -Wextra -Werror；
- 使用独立的预览程序和 deploy 文件名，不覆盖正式产物；
- Build ID 使用 edgeos-v853-27-settings-adaptive；
- Source ID 沿用已经核对的源码快照身份；
- 保存完整编译日志、退出码、ELF 信息和 SHA-256；
- 检查新增缓冲区、格式化字符串和整数计算是否有警告；
- 编译失败时只处理第一条有效错误，不关闭 Werror。

完成后按“源码身份、构建命令摘要、退出状态、产物身份、待办”汇总。
~~~

本次最终构建结果为：

~~~text
BUILD_EXIT_CODE=0
Build ID=edgeos-v853-27-settings-adaptive
Source ID=edgeos-sdk-v1.0.2-snapshot
ELF 32-bit LSB executable, ARM, EABI5, statically linked, stripped
deploy bytes=1692920
deploy sha256=2af59f0764ab87b5b0ce7421203d758ecfdf94b51afae0c967353a57be5b4ef9
~~~

完整日志保存在：

~~~text
/home/ubuntu/Downloads/edgeos/evidence/20260904/settings/build-second.log
~~~

日志共 472 行，检索 `warning:` 和 `error:` 没有命中。第一次构建也通过，但 framebuffer 验证发现布局实际变成单列；因此又修改内容宽度余量并执行了第二次完整构建。最终 SHA-256 只对应第二次产物。

构建成功只能证明代码和工具链兼容，不能证明 Settings 内容没有裁切，也不能证明 Touch 与 Back 已经通过。

## 7. 传输并临时运行

发送以下提示词，让 Agent 完成传输和板端启动：

~~~text
请把 Settings 预览程序临时部署到当前唯一的 V853 开发板。

请自动完成：
1. 通过已经配置好的工作台检查设备在线状态和硬件安全状态；
2. 确认唯一目标板并记录设备序列号；
3. 校验虚拟机产物哈希；
4. 将产物经可用通道传到 Windows 临时目录，再传到开发板 /tmp；
5. 核对板端文件大小或哈希；
6. 记录正式桌面服务状态，然后临时停止服务；
7. 使用程序实际发现的触摸设备前台运行预览版；
8. 等待 EDGEOS_BUILD、EDGEOS_DISPLAY、EDGEOS_TOUCH 和 EDGEOS_READY；
9. 若启动失败，保存错误、清理本轮临时文件并恢复正式服务。

不要刷写固件，不要替换 /usr/bin 下的正式程序，不要重启或断电。
启动成功后保持日志监控，等待我按你的提示点击 Settings。
~~~

设备序列号和触摸节点由 Agent 在当次环境中发现，不应要求用户从文档复制历史值。

### 本次 LYNX 与传输记录

LYNX 只读检查实际返回：

| 项目 | 实际结果 |
| --- | --- |
| 工作台 | `online=true`，项目和设备绑定正常 |
| ADB | 唯一设备在线，状态为 `device` |
| 串口 | `COM13` 在线，CH342 USB 串口 |
| 供电 | 最后已知命令状态为开启；不是实时电气测量值 |
| 自动任务 | 空 |
| 收尾安全状态 | `busy=false`，活动列表为空 |

LYNX ADB Shell 调用超过两分钟没有返回，Agent 主动终止等待；LYNX 串口随后返回 `PORT_BUSY:COM13`。Agent 没有抢占串口或开关电源，而是使用 LYNX 已确认的唯一宿主机 ADB 设备继续临时传输与验证。

最终产物经过三端核对：

| 位置 | 实际路径 | 文件身份 |
| --- | --- | --- |
| 虚拟机 | `v853-port/edgeos-v853-settings-adaptive.deploy` | 1692920 bytes；SHA-256 `2af59f...b4ef9` |
| Windows 临时目录 | `edgeos-v853-work/edgeos-v853-settings-adaptive.deploy` | 1692920 bytes；SHA-256 与虚拟机一致；MD5 `f497790f...747c7` |
| 开发板 | `/tmp/edgeos-v853-settings-adaptive` | 1692920 bytes；MD5 与 Windows 一致 |

正式服务停止前状态为运行中。Agent 只停止该服务并启动 `/tmp` 预览程序，没有覆盖 `/usr/bin/edgeos-v853`，也没有执行固件刷写、重启或断电。

## 8. 页面日志验证

预览程序稳定显示桌面后，让 Agent 进入交互验证模式：

~~~text
请开始验证 Settings 页面，并持续监控本轮预览日志。

先记录进入测试前的日志位置，然后只提示我执行一个动作：轻点 Settings。
我完成后，请检查并关联以下事件：
- Settings pressed；
- Settings released；
- Settings clicked；
- EDGEOS_VIEW: settings。

随后检查页面是否显示 Board、Kernel、Display、Touch、Root filesystem、Source，
并报告每项采集状态。不要仅凭 EDGEOS_READY 判断 Settings 已打开。
完成后保持页面，不要退出程序，等待抓图。
~~~

本次使用临时 ARM evdev 测试器向程序实际打开的输入节点注入一次点击。预览程序真实输出为：

~~~text
EDGEOS_TOUCH: app="Settings" event=pressed x=137 y=249
EDGEOS_TOUCH: app="Settings" event=released x=137 y=249
EDGEOS_TOUCH: app="Settings" event=clicked x=137 y=249 clicks=1 state="V853 READY"
EDGEOS_SETTINGS_DATA: board=ok kernel=ok rootfs=ok touch="gt9xxnew_ts" device=/dev/input/event3
EDGEOS_VIEW: settings orientation=landscape columns=2 fields=6
~~~

返回桌面后，Agent 再次注入 Settings 点击，`clicks=2` 且上述数据采集和建页日志再次完整出现，证明页面可以重复进入。测试器只写入一次标准 evdev 点击序列，测试结束后已从板端 `/tmp` 删除。

这组证据验证了 Linux 输入事件到 LVGL 导航的程序链路，但不能代替手指触摸的电气、坐标校准和操作手感验收。如果只有 `pressed` 和 `released`，没有 `clicked` 或 `EDGEOS_VIEW: settings`，也不能判定页面进入成功。

## 9. 抓图与数据核对

保持 Settings 页面不动，发送：

~~~text
请抓取当前 Settings 页面的 framebuffer 并完成数据核对。

请执行：
1. 读取 framebuffer 元数据和活动缓冲区；
2. 抓取并按实际像素格式转换为 PNG；
3. 保存到 docs/edgeos-v853-porting/images/，文件名包含 settings 和 real；
4. 检查标题、Back、全部信息项和状态 badge 是否完整；
5. 检查长文本换行、卡片间距、滚动范围和页面边缘；
6. 将页面 Board、Kernel、Display、Touch、Root filesystem、Source
   与板端只读命令或启动日志逐项交叉验证；
7. 对无法从截图或日志证明的项目标记“需要人工确认”；
8. 报告 PNG 路径、文件大小、程序哈希和本轮 Build ID。

不要把产品参考图作为开发板实测证据，也不要修改系统配置。
~~~

### 第一轮抓图发现布局错误

![第一次实机抓图显示卡片错误换成单列](./images/edgeos-v853-settings-first-check.png)

*第一轮程序日志报告 `columns=2`，但 framebuffer 显示六张卡片全部落在左侧单列，右侧出现大块留白。该图是失败证据，不是最终效果。*

Agent 对照代码和 framebuffer 后确认：两张卡片的计算宽度加列间距，超过了面板扣除边框后的实际内容宽度，因此 Flex 自动换行。随后给内容区保留额外边框余量，重新执行完整编译、传输、启动、点击和抓图流程。

### 第二轮抓图通过

![修正后的 V853 Settings 两列实机页面](./images/edgeos-v853-settings-adaptive-real.png)

*第二轮 framebuffer 显示六张信息卡片按两列排列，标题、Back、字段内容和底部状态完整可见。当前页面无需滚动即可看到所有字段。*

| 图片 | 大小 | SHA-256 |
| --- | ---: | --- |
| 第一次失败抓图 | 33808 bytes | `e297a20d0b85bf978d2c0f78634c25c8b46920eacd9e6d9621fc61366b80b6db` |
| 第二次最终抓图 | 30212 bytes | `709372bc80d0c39853a4efcad27d082c3cce6442bf9c45d8c589a77a56179d25` |

### 实际字段交叉验证

| 页面字段 | 页面实际值 | 第二证据 | 结果 |
| --- | --- | --- | :---: |
| Board | `sun8iw21` | `/proc/device-tree/model` 返回同一字符串 | 通过 |
| Kernel | `Linux 4.9.191 (armv7l)` | `uname -srm` 返回相同系统名、release 和架构 | 通过 |
| Display | 本次运行时显示信息 | `EDGEOS_DISPLAY` 与 framebuffer 元数据一致 | 通过 |
| Touch | `gt9xxnew_ts`、`/dev/input/event3` | sysfs 设备名和程序实际打开节点一致 | 通过 |
| Root filesystem | `3 MiB free` | 同一时段板端文件系统显示 3 MiB 可用 | 通过 |
| Source | `edgeos-sdk-v1.0.2-snapshot` | `EDGEOS_BUILD` Source ID 一致 | 通过 |

失败回退值、超长字符串和竖向布局没有在本轮硬件条件中触发，因此仍标记为未测试。单张 framebuffer 只能证明一个静态时刻，不能证明物理触摸手感或长期运行稳定性。

## 10. 返回与滚动验收

Settings 页面包含物理触摸行为，Agent 负责逐步提示并采集证据：

~~~text
请继续完成 Settings 的滚动和返回验收。

每次只提示我做一个动作并等待：
1. 若页面内容超过可见区域，提示我缓慢向上、向下滚动；
2. 检查第一项和最后一项都能到达，且 Back 始终可见；
3. 提示我轻点 Back；
4. 检查 EDGEOS_TOUCH: action=back；
5. 检查随后出现 EDGEOS_VIEW: desktop；
6. 再次进入 Settings，确认信息采集和布局仍稳定；
7. 若已连接另一种屏幕方向，重复进入、滚动和返回；否则标记未测试。

请保存对应日志时间段和截图，最后生成通过、失败、未测试三类结论。
~~~

本次向实际 evdev 节点注入 Back 点击后，日志为：

~~~text
EDGEOS_TOUCH: action=back x=69 y=69
EDGEOS_VIEW: desktop apps=8 orientation=landscape columns=4 card=239x192
~~~

| 项目 | 本次结论 |
| --- | --- |
| Back 事件和桌面恢复 | 通过 |
| 再次进入 Settings | 通过，第二次采集仍为六项成功 |
| 当前横向页面内容可达性 | 通过，六项和 badge 均在同一画面内，无需滚动 |
| 有溢出内容时的滚动范围 | 未触发，不能由 `LV_OBJ_FLAG_SCROLLABLE` 推断通过 |
| 物理手指点击与滑动 | 未测试 |
| 竖向目标屏幕 | 未测试 |

## 11. 资源检查与安全收尾

验证结束后，让 Agent 自动完成收尾：

~~~text
请完成 Settings 预览测试的资源检查和安全收尾。

请记录预览进程 PID、运行时长、VmSize、VmRSS、线程数和最后一段日志；
仅终止本轮预览进程，确认它退出后删除本轮 /tmp 临时文件；
恢复正式桌面服务，并验证监督进程、正式程序和 LCD 显示；
最后汇总保留的源码备份、构建日志、程序哈希、页面日志和截图路径。

不要删除正式程序，不要重启、断电或刷写固件。
任一步目标不明确或恢复失败时，立即停止并报告。
~~~

:::warning
Agent 的收尾报告必须包含正式服务的最终状态。只看到预览程序退出，不能证明开发板已经恢复到测试前状态。
:::

### 本次收尾结果

| 项目 | 实际结果 |
| --- | --- |
| 预览 PID | `29110` |
| 进程状态 | sleeping，单线程 |
| VmSize / VmRSS | `8248 kB` / `1628 kB` |
| 退出方式 | 只向预览进程发送 `SIGTERM` |
| 退出日志 | `EDGEOS_STOP: signal received view=1 clicks=2 paint=0` |
| 板端临时文件 | 预览程序和 evdev 测试器均已删除 |
| 虚拟机临时文件 | 测试器源码、二进制和临时构建脚本均已删除 |
| 固件与正式程序 | 未刷写，`/usr/bin/edgeos-v853` 未替换 |

第一次通过一次性 ADB Shell 调用服务脚本时，终端曾返回“supervisor started”，但连接关闭后监督进程随即退出。Agent 等待后再次查询，发现服务实际为 stopped，因此没有把启动提示当成恢复成功。随后使用独立会话启动正式监督服务，并从新的 ADB 连接复查：

~~~text
edgeos-v853 is running
/bin/sh /etc/init.d/S99edgeos-v853 supervise
/usr/bin/edgeos-v853 /dev/input/event3
~~~

最后一次 LYNX 复核显示工作台在线、唯一 ADB 设备为 `device`、`hardwareSafety.busy=false`、活动列表为空。至此测试现场恢复完成。

## 12. 固件与预览版交叉复测

本轮于 **2026-09-05** 在同一块开发板、同一块屏幕上完成。先抓取正式程序画面，再临时运行已核验的预览程序，最后恢复正式服务。没有修改源码、重新编译、替换正式程序或烧录固件。

### 版本身份与实机画面对比

| 对象 | 正式固件程序 | 自适应预览程序 |
| --- | --- | --- |
| Build ID | `edgeos-v853-30-tina-v1.0.3-gt967` | `edgeos-v853-27-settings-adaptive` |
| MD5 | `961d392b6aa9248bfe3d2e145e8aacfb` | `f497790fc3519f8643d55dbd949747c7` |
| Settings 布局 | 窄版单栏，右侧有大面积未利用区域 | 内容面板随屏幕宽度展开，当前横向显示两列卡片 |
| Touch 内容 | 固定驱动说明 | 实际设备名与程序使用的事件节点 |
| 验证结论 | 可以打开和返回，但不具备本章预览版的布局与信息改进 | 当前横向设备通过页面、字段和重复导航验证；尚未集成到正式固件 |

**正式固件实机画面：**

![正式固件的 Settings 单栏页面](./images/edgeos-v853-settings-firmware-20260905.png)

**预览版完成重复导航后的实机画面：**

![自适应预览版重复导航后的 Settings 页面](./images/edgeos-v853-settings-preview-20260905.png)

两张图均由板端 `/dev/fb0` 原始数据解码而来，不是浏览器模拟图，也不是设计稿。抓图时结合 framebuffer 的 stride、虚拟高度、色彩顺序与 pan 选择可见帧。截图可以验证软件输出，但不能替代对物理 LCD 色彩、背光和触摸体验的观察。

### 实际执行顺序

可将以下请求直接发给 Agent 复现本轮流程，无需用户逐条执行 Shell 命令：

~~~text
请交叉验证正式固件和 Settings 自适应预览版。

1. 通过 LYNX 核对设备身份、连接状态及活动硬件任务。
2. 读取正式程序 Build ID、哈希与服务状态，打开 Settings 并保存 framebuffer。
3. 核验虚拟机、Windows 和板端预览程序身份；只传到本轮专用 /tmp 路径。
4. 停止正式监督服务，临时启动预览程序，保存 PID 和启动日志。
5. 将六项页面信息与设备树、uname、framebuffer、输入节点及 df 的结果逐项核对。
6. 保存首次页面截图和资源快照，然后连续执行 20 轮进入 Settings 与 Back。
   将输入注入成功、页面事件和返回日志分别统计，不以点击发送成功代替页面验收。
7. 再次进入 Settings，复查资源并抓图；单独列出未经物理触摸验证的项目。
8. 返回桌面，只终止已核实身份的预览进程，删除本轮临时程序和测试器。
9. 恢复正式服务，从新的连接检查正式 PID、Build ID、哈希、桌面画面和 LYNX 状态。

不要刷写、重启、替换正式程序或宣称未接通的设置后端可用。
若 LYNX 控制接口不可用，请说明降级原因和实际使用的通道。
~~~

本轮 LYNX 用于开始与收尾的工作台、安全状态核对。沿用此前已确认的 LYNX 控制接口异常处理方式，传输、输入注入、系统读取和 framebuffer 抓取使用宿主机 ADB，目标固定为 LYNX 绑定的唯一设备 `20080411`。本轮没有再次等待异常 Shell 接口，也没有争抢串口。

### 字段与导航验证结果

| 验证对象 | 独立证据 | 本轮结果 |
| --- | --- | --- |
| Board | `/proc/device-tree/model` | 与页面 `sun8iw21` 一致 |
| Kernel | `uname -r`、`uname -m` | 与页面 `Linux 4.9.191 (armv7l)` 一致 |
| Display | LVGL 启动日志与 framebuffer 元数据 | 设备路径及可见尺寸一致；未把虚拟双缓冲高度当作可见高度 |
| Touch | `/sys/class/input/event3/device/name`、启动参数 | 均为 `gt9xxnew_ts`、`/dev/input/event3` |
| Root filesystem | `df -k /` 的 Available 为 3530 KiB | 向下取整后为页面的 `3 MiB free`；不是固件镜像大小 |
| EdgeOS source | 启动日志及源码的 `EDGEOS_SOURCE_ID` | 均为 `edgeos-sdk-v1.0.2-snapshot`；这是编译期标签，不能单独证明二进制内容相同 |
| 20 轮进入与返回 | Settings、数据采集、Back、Desktop 对应日志 | 全部完成，没有观察到退出或卡死 |
| 循环后页面 | 再次进入后的 framebuffer | 六张卡片与状态条完整可见，没有观察到叠加残影或遮挡 |
| 资源快照 | 同一预览进程、同为 Settings 页面 | 循环前后 VmSize 均为 `8248 kB`，VmRSS 均为 `1628 kB`，线程数均为 1 |

导航计数口径：首次进入后返回，再执行 20 轮完整往返，最后额外进入一次用于抓图，收尾再返回。因此本轮总计进入 Settings **22 次**、返回桌面 **22 次**。所有点击均由临时测试器向真实 evdev 节点注入，不是手指点击。资源快照只说明本轮未观察到 RSS 增长，不能证明长期无内存泄漏。

关键日志如下：

~~~text
EDGEOS_SETTINGS_DATA: board=ok kernel=ok rootfs=ok touch="gt9xxnew_ts" device=/dev/input/event3
EDGEOS_VIEW: settings orientation=landscape columns=2 fields=6
EDGEOS_TOUCH: action=back x=55 y=70
EDGEOS_VIEW: desktop apps=8 orientation=landscape columns=4 card=239x192
EDGEOS_STOP: signal received view=0 clicks=22 paint=0
~~~

### 设计复核与尚未完成的验证

当前画面已建立“页面标题 → 字段标题 → 信息值 → 状态条”的层级；字段间距清楚，Back 位于滚动内容区之外。面板下方留白不影响当前六项信息可达性，不必为了填满屏幕而拉伸卡片或加入无后端的开关。

源码复核还发现以下边界，本轮只记录，不把未执行的改进写成已完成：

| 项目 | 当前证据与待办 |
| --- | --- |
| 自适应策略 | 当前仅按横向两列、竖向一列切换；尚无基于最小卡片宽度的退回单列策略。需补窄横屏及竖向实机验收 |
| 长文本完整性 | 已启用换行，但 `read_one_line()` 使用定长缓冲且未报告截断；换行不能恢复采集阶段丢失的文本 |
| 卡片内部宽度 | Label 宽度按卡片宽度减两侧 padding 计算，未另扣边框；当前文本未见裁切，贴边长文本仍需压力验证 |
| 溢出与滚动 | 当前六项无需滚动；需构造长内容并验证最末字段、状态条及 Back 的可达性，不能仅凭滚动标志判定通过 |
| 数据刷新 | Board、Kernel、Root filesystem 在每次进入页面时采集；Touch 身份来自程序启动时记录。不是持续实时刷新，也未验证热插拔 |
| 状态条含义 | `CORE UI: V853 READY` 是固定文案，不能作为系统健康状态或采集全部成功的证明 |
| 设置中心能力 | 仍是系统信息页，不具备完整产品参考图中的网络、亮度、语言等设置操作与持久化后端 |

竖向布局、物理点击与滑动、故障注入以及长时间稳定性仍列为未测试，不因本轮 20 次往返通过而更改状态。

### 恢复现场

预览 PID `1470` 经 `/proc/1470/exe` 核实身份后收到 `SIGTERM`，退出日志记录 `view=0 clicks=22`。确认进程退出后，仅删除本轮两个 `/tmp/settings-check-20260905-*` 已明确命名的程序文件，没有使用通配符删除。

随后使用独立会话恢复监督服务，并从新连接确认正式 PID `1593` 正在运行，正式程序 MD5 仍为 `961d392b6aa9248bfe3d2e145e8aacfb`，启动日志和 framebuffer 均恢复为正式桌面。LYNX 收尾状态为 `online=true`、`hardwareSafety.busy=false`、活动列表为空。

:::info 此处为部署前的历史状态
本节复测结束时只完成临时预览，没有改变正式程序。后续正式部署结果见第 13 节，不要将本节“恢复旧服务”的收尾方式用于希望保留新版的部署任务。
:::

## 13. 正式部署与重启验证

用户反馈“设置 UI 还是没变”后，Agent 重新核实了 `/usr/bin/edgeos-v853`、运行进程和最近的 Settings 日志，确认板端仍为旧程序。本轮不再只运行 `/tmp` 预览，而是将经过 SDK 软件包构建的新版安装到正式服务的启动路径，并验证正常重启后仍然生效。

### 接入 SDK 软件包

实际软件包为：

~~~text
/home/ubuntu/100ask-course/sdk/tina-v853-100ask/package/gui/v853-edgeos-desktop
~~~

该包的 `Build/Prepare` 从 `V853_EDGEOS_ROOT/v853-port/main.c` 复制入口源码。上一次生成目录仍保存旧版 `main.c`，因此只更新独立预览文件不会自动更新板端程序。

本轮明确传入 `V853_EDGEOS_ROOT=/home/ubuntu/Downloads/edgeos` 和 `V853_LVGL9_ROOT=/home/ubuntu/Downloads/lvgl9`，将包的 `PKG_RELEASE` 从 `1` 更新为 `2`，并设置可区分的新 Build ID。构建后的源码 SHA-256 与已验证自适应源码一致：

~~~text
de93423542940bb1ecbd064f53c451c0ea845168d5f093a0c0414c54a4187a05
~~~

| 产物 | 实际结果 |
| --- | --- |
| SDK 软件包 | `v853-edgeos-desktop_0.1.0-2_sunxi.ipk`，808177 字节 |
| 构建结果 | `PACKAGE_EXIT_CODE=0`，SDK 汇报 make 用时 59 秒 |
| 新 Build ID | `edgeos-v853-27-tina-v1.0.4-settings-adaptive` |
| 正式 ELF 大小 | 1692912 字节 |
| 正式 ELF SHA-256 | `f0f5fd0904cfe9865c2fcf599779c8b630390ad8cecb6877cf517e401100cc24` |
| 正式 ELF MD5 | `ee9238cc6e8aeb45b7a08b7a95748439` |

本轮没有把旧预览程序直接改名冒充 SDK 构建产物。部署文件取自软件包的 `ipkg-sunxi/v853-edgeos-desktop/usr/bin/edgeos-v853`；SDK 与 Windows 比对 SHA-256，Windows 与板端比对 MD5。

### 部署操作

以下请求可直接交给 Agent：

~~~text
请将已验证的自适应 Settings 正式部署到当前 V853，验证后保留新版。

先通过 LYNX 确认设备身份且无活动烧录、供电任务。
核对 SDK 软件包真正使用的 main.c，使用独立 Build ID 构建正式软件包。
不要把旧生成目录、预览版或文件名当作正式产物身份依据。

备份板端原程序到 Windows，并检查只读系统中的原版是否仍可回退。
核对根文件系统剩余空间及三端文件哈希，先把新程序传到 /tmp。
在正式目录建立本轮专用待安装文件，校验并设置可执行权限。
停止正式监督服务，确认旧进程停止后原子替换 /usr/bin/edgeos-v853，sync，
再由原有服务启动，并从新连接核对 Build ID、PID 和哈希。

再次确认设备空闲后执行正常重启，不进入烧录模式。
等待系统和开机服务就绪，比较 boot_id，确认正式路径仍为新版。
完成 20 轮 Settings 与 Back 事件验证、字段核对、资源快照及 framebuffer 抓图。
清理本轮临时测试器，但不要终止新版正式服务、不要恢复旧程序。
最后将 LCD 停留在新版 Settings，并报告完整固件是否另行生成或烧录。
~~~

本轮 LYNX 开始和重启前均报告设备在线、绑定 ADB 设备 `20080411`、`hardwareSafety.busy=false`、活动列表为空。传输、部署和正常重启采用前文已说明的宿主机 ADB 降级通道；没有宣称 LYNX Shell 异常已修复。

原版程序已备份到 Windows，同时板端 `/rom/usr/bin/edgeos-v853` 仍保留原版 MD5 `961d392b6aa9248bfe3d2e145e8aacfb`。新版先写入 `/usr/bin/.edgeos-settings-production-20260905.new`，校验后停止服务，再在同一目录重命名到正式路径；部署脚本通过独立会话启动原有监督服务。

:::warning 覆盖层空间与固件边界
当前根目录采用只读系统加可写 overlay。新版程序持久保存在覆盖层中，因此正常重启不会丢失；但刷入会清除覆盖层的旧固件后仍可能恢复旧 UI。本轮部署后 `df -k /` 的 Available 为 1874 KiB，页面向下取整显示 `1 MiB free`，这是新版占用覆盖层空间的真实变化，不是显示错误。不要继续把备份或大文件堆放到根文件系统。
:::

### 重启后的实机验收

本轮使用正常系统重启，不是仅重启 UI 进程。重启前后 boot ID 分别为：

~~~text
before: c7c505af-3e8f-46f5-b6cc-17fabe97b3c8
after:  195c6054-cceb-4b00-868a-ad61f0cd76b2
~~~

ADB 刚恢复时，开机服务尚未启动，首次查询曾显示 stopped。Agent 等待启动流程完成后重新查询，确认自动运行新版，而不是手动补启动后宣称开机自启通过。

| 验收项 | 重启后结果 |
| --- | --- |
| 正式监督服务 | 自动启动，监督 PID `1111`，应用 PID `1173` |
| 程序路径 | `/usr/bin/edgeos-v853`，不是 `/tmp` |
| 程序身份 | 新 Build ID 正确，正式路径与 overlay 中的 MD5 均为 `ee9238cc6e8aeb45b7a08b7a95748439` |
| 20 轮完整往返 | 全部有进入 Settings、字段采集、Back 和桌面日志 |
| 总事件计数 | 22 次进入 Settings、21 次 Back；最后一次进入后保留页面供用户观察 |
| 字段采集 | 22 次均记录 Board、Kernel、Rootfs 成功；Touch 与 sysfs 一致 |
| 资源快照 | 循环前后 VmSize 均为 `8252 kB`，VmRSS 均为 `1632 kB`，单线程 |
| 布局 | 重启后的真实 framebuffer 显示两列六张卡片，标题、Back 和状态条可见 |
| 收尾 | 临时注入测试器已删除，新版正式服务继续运行，没有恢复旧版 |

![正式部署重启后保留在 LCD 上的 Settings](./images/edgeos-v853-settings-production-reboot-20260905.png)

点击仍是 evdev 注入测试，不是物理手指操作。正常重启通过也不等于已经完成断电冷启动、竖向屏幕、长内容滚动或采集失败分支的验证，这些项目继续保持未测试。

### 配套完整固件

完成板端部署后，Agent 使用相同的 EdgeOS 源码路径执行 SDK 集成构建与 `pack`，并为另一个 LVGL 软件包明确传入 `V853_LVGL9_SOURCE_DIR=/home/ubuntu/Downloads/lvgl9`。本轮保留了打包前的旧镜像，没有直接用新镜像覆盖唯一备份。

| 核验项 | 实际结果 |
| --- | --- |
| SDK 集成构建 | `FIRMWARE_BUILD_EXIT_CODE=0`，SDK 汇报 make 用时 2 分 1 秒 |
| 打包 | `PACK_EXIT_CODE=0` |
| 新固件 | `tina_v853-100ask-settings-adaptive.img`，30451712 字节 |
| 固件 SHA-256 | `ea846e78f3b38158962dc951ca23c9c9a0fd9417135598f535002a5cecd40370` |
| 三端关联 | SDK 正式程序、Windows 部署文件及 rootfs 内程序的 SHA-256 一致 |
| LYNX 只读检查 | `lynx_inspect_firmware` 识别为未加密 `IMAGEWTY`，包含 34 个文件，`last_error=null` |
| 完整镜像复核 | 按 LYNX 给出的 `rootfs.fex` 偏移提取后，再从 Squashfs 读取 `usr/bin/edgeos-v853`，哈希仍与部署程序一致 |
| 烧录验证 | 本轮未执行，不能把格式检查和覆盖层部署当作新完整固件烧录通过 |

新版完整固件保存在虚拟机证据目录中，并已复制到 Windows 的本轮 `edgeos-v853-work` 目录。SDK 标准输出 `out/v853-100ask/tina_v853-100ask_uart0.img` 也已更新；旧镜像保留为证据目录下的 `tina_v853-100ask_uart0.before.img`。

构建入口曾被 Bash 的 `set -e` 提前中断：Tina 的环境和 make 包装函数内部有允许失败后继续选择替代路径的探测，例如缺少 `spl` 时转到 `spl-pub`。Agent 检查了包装函数和日志，改为对整个函数调用显式检查返回值，没有修改 SDK 内部逻辑。最终成功以新 IPK、rootfs 内容哈希和新镜像为依据，而不是忽略失败继续宣布成功。完整 SDK 日志中仍有原有工具链、设备树等警告，本章不将全量构建表述为“零警告”。

### 回退与证据保留

若后续发现新版问题，可要求 Agent 在无硬件任务冲突时停止正式服务，核验 `/rom` 原版或 Windows 备份的哈希，恢复原程序并重新验证服务。不要直接删除整个 overlay，也不要为回退一个 UI 程序重刷所有分区。

虚拟机证据目录为 `/home/ubuntu/Downloads/edgeos/evidence/20260905-settings-production`，其中保留包 Makefile 备份、旧源码快照、构建脚本及日志。Windows 保留原程序 `edgeos-v853-before-production-20260905`、正式部署文件、重启后日志及 framebuffer 原始数据。板端临时程序由正常重启清除，重启后重新上传的事件测试器在验收结束后单独删除；以上证据文件未清理。

## 常见问题

| 现象 | 可直接发送给 AI Agent 的排查请求 |
| --- | --- |
| 点击 Settings 没有进入 | 请关联 pressed、released、clicked 和 drag 判定日志，确认是否被识别成滑动 |
| 页面只有标题没有正文 | 请检查信息采集结果、Label 文本长度、对象尺寸和可见区域 |
| 最后一项被 badge 遮挡 | 请让 badge 参与 Flex 排版，并验证内容区滚动范围 |
| 长内核版本超出卡片 | 请检查 Label 宽度和 `LV_LABEL_LONG_WRAP`，不要简单缩小字体 |
| Touch 显示内容与日志不同 | 请让页面读取程序实际打开的输入设备状态，不要再次猜测节点 |
| 剩余空间显示为零 | 请区分 `statvfs` 失败和真实零值，并检查 64 位计算顺序 |
| 横向正常、竖向裁切 | 请检查列数、卡片宽度、内容滚动和 Back 所在对象层级 |
| Back 没有返回桌面 | 请检查回调、触摸坐标和 `EDGEOS_VIEW: desktop` 日志 |
| 测试结束后桌面未恢复 | 请核对预览 PID、正式监督服务和 `/usr/bin` 正式进程，禁止盲目重启 |

## 验收清单

第 1～12 节的已勾选项主要来自预览验证；第 13 节额外完成了正式 SDK 程序部署与正常重启验收。完整固件烧录验收须另列，不能从覆盖层部署推断。

- [x] Settings 入口和 `VIEW_SETTINGS` 跳转关系已经审计。
- [x] 页面字段的数据来源和失败显示策略已经记录。
- [x] Touch 显示实际设备名和事件节点，不是静态占位值。
- [x] 根文件系统空间使用安全的 64 位计算并区分读取失败。
- [x] 信息项已启用长文本换行，内容区已启用纵向滚动。
- [x] 当前横向目标屏幕已保存页面截图和日志。
- [ ] 有溢出内容时的滚动范围已完成物理滑动验证。
- [ ] 竖向目标屏幕已保存真实页面截图和日志。
- [x] 注入的 Settings 点击事件和 `EDGEOS_VIEW: settings` 已关联验证。
- [ ] 手指点击 Settings、滚动和 Back 已完成人工验收。
- [x] 注入的 Back 事件和桌面恢复日志已关联验证。
- [ ] Board、Kernel、Rootfs 采集失败分支已在板端故障注入验证。
- [x] 没有把未接通的设置后端标记为可用。
- [x] 测试程序仅放在 `/tmp`，正式程序未被替换。
- [x] 测试结束后正式桌面服务已经恢复并复查。
- [x] 新版 SDK 正式程序已部署到 `/usr/bin/edgeos-v853`，不再只存在于 `/tmp`。
- [x] 正常重启后开机服务自动运行新版，并保存了新版 Settings 截图。
- [x] 正式版本完成 20 轮往返，收尾保留新版 Settings 页面。
- [x] 配套完整固件已构建、打包，并经 LYNX 格式检查和镜像内程序哈希核验。
- [ ] 新版完整固件已通过烧录后的硬件验收。

## 本节结论

本次已经在真实 V853 开发板上完成 Settings 数据采集改造、两轮严格构建、三端传输、evdev 导航、字段交叉验证、framebuffer 抓图、Back 返回和服务恢复。第一次抓图暴露的“日志两列、画面单列”问题经过修改后已由第二张实图验证解决。尚未完成的项目是手指触摸体验、有溢出内容时的物理滚动、竖向目标屏幕和采集失败分支；这些项目不能由当前截图或日志外推为通过。

Settings 当前仍是只读系统信息页，不代表网络、语言、时间、声音、亮度或持久化配置后端已经接通。

后续正式部署已按第 13 节完成：当前板端保留 SDK 构建的自适应版本，并通过正常重启验证，不再是“临时预览结束后恢复旧界面”的状态。

## 版本与变更记录

- 2026-09-05：构建 `v1.0.4-settings-adaptive` 正式 SDK 程序，完成持久部署、正常重启、20 轮导航和新实机截图；记录 overlay 空间、回退路径，并将收尾方式改为保留新版。

- 2026-09-05：增加正式固件与预览版实机截图对比、字段复核、20 轮导航测试、资源快照和恢复证据；明确预览尚未集成，并补充自适应、长文本、刷新及状态条的设计边界。

- 2026-09-04：基于当前 `v853-port/main.c` 的 Settings 入口、系统信息采集和布局实现新增本章。
- 2026-09-04：采用 AI Agent 对话驱动的源码审计、修改、编译、传输、抓图、输入事件验证和安全收尾流程。
- 2026-09-04：明确 Settings 当前为只读系统信息页，并补充 Touch 真实身份、长文本滚动、存储计算和验证边界。
- 2026-09-04：通过 LYNX 完成设备与安全状态核对，在真实开发板执行两轮预览；记录 LYNX ADB Shell 无返回、串口占用和宿主机 ADB 受控降级，并加入第一次失败与第二次通过的 framebuffer 图片。
