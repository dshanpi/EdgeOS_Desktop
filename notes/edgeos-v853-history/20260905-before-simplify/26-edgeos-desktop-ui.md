# 26 将 EdgeOS Desktop 核心 UI 移植到 V853

> 本节使用 AI Agent 与 `lynx-100ask-v853-sdk` MCP 服务完成一次真实实操：检查工作台、虚拟机和开发板，审计并修改 EdgeOS 入口层，交叉编译预览程序，临时部署到开发板，抓取 framebuffer 画面，最后恢复原有桌面服务。全过程不刷写固件，也不把尚未接通的 Camera、MPP、NPU、OTA 等硬件服务标记为可用。

![V853 开发板运行 EdgeOS Desktop 自适应核心桌面](./images/edgeos-v853-desktop-adaptive-real.png)

*上图为本次实操从开发板 framebuffer 抓取的真实画面。它证明当前横向目标屏幕的桌面显示正常；竖向目标屏幕和触摸交互仍须分别进行实机验收。*

## 学习目标

- 使用已经配置好的 Lynx 工作台完成设备检查与板端取证。
- 区分 EdgeOS 可移植 UI 与平台相关硬件后端。
- 使用 AI Agent 把固定尺寸布局改造成运行时自适应布局。
- 通过对话让 AI Agent 完成交叉编译、临时部署、启动日志和 framebuffer 图像取证。
- 明确区分机器验证、画面验证和人工触摸验证。

## 本次实操结果

| 检查项 | 本次结果 | 证据 |
| --- | --- | --- |
| MCP 工作台 | 通过 | 工作台在线，硬件安全状态空闲 |
| 开发板连接 | 通过 | ADB 设备在线；串口 `COM13` 可识别 |
| Linux 基线 | 通过 | MCP 串口读取到 ARMv7 Linux、framebuffer 和触摸设备信息 |
| 源码改造 | 通过 | `main.c` 改为运行时布局参数，横向四列、竖向两列 |
| 严格编译 | 通过 | `-Wall -Wextra -Werror`，`BUILD_EXIT_CODE=0` |
| 板端启动 | 通过 | 输出 `EDGEOS_READY`，无初始化错误 |
| 当前横向屏幕显示 | 通过 | 八个入口、Hero、状态栏和底栏完整显示，见上图 |
| 运行资源 | 已记录 | `VmRSS 1608 kB`、`VmSize 8244 kB`、单线程 |
| 人工触摸与导航 | 本轮未执行 | 需要现场点击 Settings、Drawing、Back 并记录日志 |
| 竖向目标屏幕 | 本轮无对应硬件 | 代码路径已审计和编译，仍需在真实屏幕上验收 |
| 固件刷写 | 未执行 | 本轮只在 `/tmp` 临时运行预览程序 |

:::info
“ADB 在线”只说明通信链路可用，“程序已启动”只说明初始化成功，“抓图正常”只证明显示路径可用。触摸、导航和另一种屏幕形态必须保留独立验收记录。
:::

## 前置条件

- 已完成 [22 把 LVGL 9 应用集成到 Tina SDK](../lvgl9-porting/22-integrate-with-tina-sdk.md)。
- 已完成 [09 LCD 与 Touch 适配](../v853-tina-linux/09_LCDScreenAdaptation.md)。
- Windows 主机能够通过 SSH 登录编译虚拟机。
- 已按 [08 固件烧录](../v853-tina-linux/08-firmware-flashing.md) 完成 Lynx、AI Agent、虚拟机和开发板连接。
- 开发板 `/dev/fb0` 可显示，触摸屏存在可用的 `/dev/input/eventN`。
- EdgeOS、LVGL 和 SDK 的源码来源及许可已经核对。

:::tip 本节操作方式
除观察物理 LCD、点击触摸屏等必须由现场完成的动作外，不要求用户在终端逐条复制命令。用户只需把每一节给出的提示词发送给 AI Agent；Agent 负责选择 SSH、Lynx、ADB 等已配置通道，执行命令、传输文件、保存证据并恢复现场。若设备身份不唯一或安全检查失败，Agent 必须停止并说明原因。
:::

## 1. 认识 V853 EdgeOS 入口层

当前 V853 适配入口位于：

~~~text
<EDGEOS_ROOT>/v853-port/main.c
~~~

原始 EdgeOS 快照中的 Material 图标和中文字体来自：

~~~text
<EDGEOS_ROOT>/EdgeOS_Desktop/apps/
~~~

入口层重新连接 LVGL 与 V853 的显示、输入和系统信息：

~~~text
EdgeOS 图标与字体
        │
        ▼
V853 页面与导航状态机
        │
        ├── LVGL 9.4 fbdev ──> /dev/fb0
        ├── LVGL 9.4 evdev ──> /dev/input/eventN
        └── Linux /proc、uname、statvfs
~~~

UI 可以先运行，硬件服务必须等后端真实接通后再标记为可用。

## 2. 核对桌面入口与导航状态

当前 `g_apps[]` 注册了八个入口：

| 入口 | 当前状态 | 点击后进入 | 说明 |
| --- | --- | --- | --- |
| Settings | `APP_READY` | Settings 页面 | 显示板卡、内核、显示、Touch 和根文件系统信息 |
| Drawing | `APP_READY` | Drawing 页面 | LVGL Canvas 触摸画板 |
| Assets | `APP_DEMO` | 详情页 | 展示 Material 资源已经进入目标程序 |
| Port Status | `APP_READY` | 详情页 | 显示当前移植边界和待办项 |
| Source Info | `APP_READY` | 详情页 | 显示源码快照和 manifest 身份 |
| Build Info | `APP_READY` | 详情页 | 显示 LVGL、ARMv7、musl 和构建信息 |
| Licenses | `APP_READY` | 详情页 | 展示第三方资源许可 |
| About | `APP_READY` | 详情页 | 声明本阶段只完成核心桌面 |

页面状态为 `VIEW_DESKTOP`、`VIEW_SETTINGS`、`VIEW_DRAWING` 和 `VIEW_DETAIL`。点击卡片后进入目标页面，返回按钮统一调用 `show_desktop()`。未接通的 Camera、MPP、NPU、OTA 不应伪装成 `READY`。

## 3. 只读审计源码

本次虚拟机中的实际路径为：

~~~text
EDGEOS_ROOT=/home/ubuntu/Downloads/edgeos
LVGL9_ROOT=/home/ubuntu/Downloads/lvgl9
SDK=/home/ubuntu/100ask-course/sdk/tina-v853-100ask
~~~

先发送只读提示词：

~~~text
请只读审计 EdgeOS Desktop 的 V853 核心 UI，暂时不要修改或编译。

重点检查：
- /home/ubuntu/Downloads/edgeos/v853-port/main.c
- /home/ubuntu/Downloads/edgeos/v853-port/Makefile
- /home/ubuntu/Downloads/edgeos/EdgeOS_Desktop/apps/material_app_icons.c
- /home/ubuntu/Downloads/edgeos/EdgeOS_Desktop/apps/ui_font_source_han_20.c
- /home/ubuntu/100ask-course/sdk/tina-v853-100ask/package/gui/v853-edgeos-desktop/Makefile

请报告显示尺寸来源、入口状态、页面跳转、固定尺寸对象、拖动判定、
硬件服务边界，以及 Build ID、Source ID、manifest、LVGL revision 保护。
按“已完成、布局缺口、功能边界、建议修改文件”汇总，然后停止等待。
禁止修改、编译、clean、sudo、联网下载或自动修复。
~~~

审计发现原程序同时存在固定 UI 宽高、固定 Hero/Grid/Card/Panel/Canvas 尺寸，以及 framebuffer 严格等值检查。只修改两个尺寸宏不能实现多屏兼容。

## 4. 设计运行时自适应布局

本次把尺寸计算集中到 `edgeos_layout_t` 和 `init_layout(width, height)`，页面只消费计算结果：

~~~c
int32_t width = lv_display_get_horizontal_resolution(g_display);
int32_t height = lv_display_get_vertical_resolution(g_display);
bool landscape = width >= height;
uint32_t columns = landscape ? 4U : 2U;

int32_t margin = width / 40;
if(margin < 12) margin = 12;
int32_t content_width = width - 2 * margin;
int32_t card_width =
    (content_width - (int32_t)(columns - 1U) * margin) /
    (int32_t)columns;
~~~

| 区域 | 自适应规则 |
| --- | --- |
| 状态栏与底栏 | 宽度跟随 display，位置由运行时高度计算 |
| Hero | 使用统一边距和可用内容宽度 |
| 应用区 | 横向四列、竖向两列，Flex 自动换行 |
| 应用卡片 | 从内容宽度、列数和间距计算 |
| Settings / Detail | 根据父容器宽高和 padding 计算 |
| Drawing Canvas | 从剩余空间计算，并动态创建、销毁 draw buffer |
| 内容溢出 | 应用区允许纵向滚动 |

严格的 framebuffer 尺寸等值判断被移除，但仍拒绝无效的零宽或零高。桌面、Settings、Detail 和 Drawing 共用同一套布局参数。

## 5. 实施最小修改

只读审计通过后，仅授权写入 `v853-port/main.c`，发送：

~~~text
请只修改 /home/ubuntu/Downloads/edgeos/v853-port/main.c：
1. 从 LVGL display 读取实际宽高，不限制为单一 framebuffer 尺寸；
2. 集中计算边距、间距、内容宽高、列数和卡片尺寸；
3. 横向使用四列，竖向使用两列并允许纵向滚动；
4. Hero、Footer、Settings、Detail、Canvas 跟随可用空间；
5. 保留八个入口、图标、字体、点击、返回和拖动判定；
6. 不加入尚未接通硬件服务的假实现；
7. 不修改原始资源、LVGL、Tina SDK 或设备树；
8. 修改前备份并报告新旧 SHA-256；
9. 使用 -Wall -Wextra -Werror 编译。
~~~

本次备份为：

~~~text
/home/ubuntu/Downloads/edgeos/v853-port/main.c.before-adaptive-20260904
~~~

`main.c` 的 SHA-256 从：

~~~text
6bec2006633dabfa458a34b6b13c9f205de157cecb003b5b6486c8d901c1cb8d
~~~

变为：

~~~text
85358bc3904897bf2c7f41742c0af881efa1c585ad2e2225ba9e6f2afd6190e0
~~~

## 6. 交叉编译

源码修改完成后，不需要用户登录虚拟机手工拼接 `make` 命令。继续发送：

~~~text
请在虚拟机中交叉编译刚才修改的 EdgeOS 自适应预览程序。

实际目录：
- EdgeOS：/home/ubuntu/Downloads/edgeos
- LVGL 9：/home/ubuntu/Downloads/lvgl9
- V853 port：/home/ubuntu/Downloads/edgeos/v853-port

执行要求：
1. 先确认三个目录、Makefile、交叉编译器和源文件都存在；
2. 只清理本预览程序的构建产物，不清理 Tina SDK；
3. 将程序名设为 edgeos-v853-desktop-adaptive；
4. 将部署文件名设为 edgeos-v853-desktop-adaptive.deploy；
5. 设置 BUILD_ID=edgeos-v853-26-desktop-adaptive；
6. 设置 SOURCE_ID=edgeos-sdk-v1.0.2-snapshot；
7. 使用四个并行任务，并保留 -Wall -Wextra -Werror；
8. 把完整标准输出和错误输出保存到
   /home/ubuntu/Downloads/edgeos/evidence/20260904/part3/build.log；
9. 编译后检查退出码、ELF 架构、静态链接、strip 状态和 SHA-256；
10. 若失败，只分析第一条有效错误，不关闭 -Werror，也不扩大修改范围。

完成后请用“执行动作、构建结果、产物路径、文件身份、风险或待办”汇总。
~~~

Agent 会在虚拟机中完成环境变量设置、清理、编译和产物检查。用户只审核结果，不需要重复执行同一组命令。

本次实际结果：

~~~text
BUILD_EXIT_CODE=0
ELF 32-bit LSB executable, ARM, EABI5, statically linked, stripped
deploy sha256=2ba22df83524db8f26ab2436fc735b119f239f48d22f692a0050f698b563b324
~~~

完整日志位于：

~~~text
/home/ubuntu/Downloads/edgeos/evidence/20260904/part3/build.log
~~~

## 7. 传输并临时运行

确认编译结果通过后，发送以下提示词，让 Agent 自行完成“虚拟机 → Windows → 开发板”的文件传输和板端启动：

~~~text
请把刚才编译成功的 EdgeOS 预览程序临时部署到当前 V853 开发板并前台运行。

源文件：
/home/ubuntu/Downloads/edgeos/v853-port/edgeos-v853-desktop-adaptive.deploy

安全边界：
- 先通过 Lynx 查询工作台、ADB 设备和硬件安全状态；
- 必须确认当前只有一台目标开发板，记录设备序列号；
- 先校验虚拟机产物 SHA-256，再把它传到 Windows 临时工作目录；
- 通过已确认的 ADB 设备把程序传到板端 /tmp/edgeos-v853-desktop-adaptive；
- 传输后校验板端文件大小或可用哈希，确认不是空文件；
- 只临时停止 S99edgeos-v853 服务，并记录停止前的状态；
- 为 /tmp 中的预览程序增加执行权限，使用实际触摸节点前台运行；
- 保存 EDGEOS_BUILD、EDGEOS_DISPLAY、EDGEOS_TOUCH、EDGEOS_VIEW、EDGEOS_READY 日志；
- 不替换 /usr/bin/edgeos-v853，不写入固件分区，不重启或断电；
- 若设备不唯一、哈希不一致、正式服务无法停止或程序启动失败，立即停止并报告。

程序稳定进入桌面后保持运行，等待下一步抓图，不要提前清理或恢复服务。
~~~

Agent 可以根据当前可用通道选择 MCP、SSH、SCP 或 ADB；选择何种传输工具属于执行细节，但必须保留来源、目标、哈希和设备身份记录。本次没有替换 `/usr/bin/edgeos-v853`，也没有写入固件分区。

本次真实启动日志为：

~~~text
EDGEOS_VIEW: desktop apps=8 orientation=landscape columns=4
EDGEOS_BUILD: id=edgeos-v853-26-desktop-adaptive source=edgeos-sdk-v1.0.2-snapshot lvgl=9.4.0
EDGEOS_DISPLAY: fb=/dev/fb0 resolution=<runtime width>x<runtime height> orientation=landscape
EDGEOS_TOUCH: device=/dev/input/event3 type=pointer
EDGEOS_READY: desktop=1 settings=1 drawing=1
~~~

文档用占位符表示运行时分辨率，强调程序不依赖某一个固定数值；完整原始日志可作为项目内部测试附件保存。

## 8. 抓图验证

预览程序保持运行时，继续发送：

~~~text
请为当前开发板上的 EdgeOS 自适应预览程序保存显示证据。

请完成：
1. 读取 framebuffer 的可见尺寸、虚拟尺寸、位深、stride 和当前缓冲区偏移；
2. 抓取 framebuffer 原始数据到 Windows 临时工作目录；
3. 根据实际位深、通道顺序、stride 和活动缓冲区转换为 PNG，不能只按文件大小猜测；
4. 检查状态栏、Hero、八个入口和底栏是否完整，有无裁切、错位或异常留白；
5. 把最终 PNG 复制到文档的
   docs/edgeos-v853-porting/images/edgeos-v853-desktop-adaptive-real.png；
6. 检查 Markdown 引用能解析到该图片，并报告图片尺寸、文件大小和结论；
7. 保留必要证据后删除 Windows 中的原始 framebuffer 临时文件；
8. 此时暂不终止预览程序，等待下一步资源检查和安全收尾。

如果无法确定像素格式或活动缓冲区，不要生成误导图片，直接报告缺失信息。
~~~

Agent 负责读取参数、传输、转换、检查和归档。本次转换后的真实图像保存在：

~~~text
docs/edgeos-v853-porting/images/edgeos-v853-desktop-adaptive-real.png
~~~

画面检查确认状态栏、Hero、八个入口和底栏均完整可见，没有明显裁切或整体偏移。这些结论来自 framebuffer，不等同于肉眼确认亮度、色偏、刷新抖动或触摸准确度。

## 9. 检查资源并安全收尾

图片证据保存完成后，发送：

~~~text
请完成本轮 EdgeOS 预览程序的资源检查和安全收尾。

执行顺序：
1. 确认目标仍是本轮启动的预览进程，记录 PID 和运行时长；
2. 读取 VmSize、VmRSS、线程数和进程状态；
3. 保存预览程序最后一段日志；
4. 只向预览进程发送 SIGTERM，等待并记录 EDGEOS_STOP；
5. 确认预览进程已经退出，再删除本轮放入 /tmp 的程序和抓图临时文件；
6. 恢复 S99edgeos-v853 正式服务；
7. 验证监督进程、正式 /usr/bin/edgeos-v853 进程和 LCD 显示均已恢复；
8. 汇总清理了哪些临时文件，以及最终服务状态。

请采用必定执行恢复检查的收尾流程。不要重启、断电、刷写固件，
不要删除正式程序；任一步目标身份不明确时停止并报告。
~~~

预览程序持续运行后，本次 Agent 记录到：

~~~text
VmSize: 8244 kB
VmRSS:  1608 kB
Threads: 1
~~~

Agent 结束预览时发送 `SIGTERM`，程序输出：

~~~text
EDGEOS_STOP: signal received view=0 clicks=0 paint=0
~~~

`clicks=0` 和 `paint=0` 说明本轮没有执行物理触摸验收，不能把 Settings、Back 和 Drawing 写成已实测通过。

随后 Agent 删除 `/tmp` 中的预览程序和抓图临时文件，并恢复正式服务。本次收尾确认正式监督进程与 `/usr/bin/edgeos-v853 /dev/input/event3` 均重新运行。

## 10. 人工触摸验收

物理触摸和肉眼观察不能由 Agent 代替，但 Agent 可以负责启动测试、逐项提示、同步采集日志、抓图和生成结论。发送：

~~~text
请进入 EdgeOS 人机交互验收模式。

由你完成预览程序启动、日志监控、截图、计数和证据归档；
每次只提示我做一个必要的物理动作，等待我完成后再判断：
1. 提示我轻点 Settings，再检查页面日志和画面；
2. 提示我点击 Back，再检查是否返回桌面；
3. 提示我打开 Drawing 并在画布连续绘制，再检查 paint 计数；
4. 提示我返回并打开一个详情页，检查标题、正文和返回按钮；
5. 提示我快速滑动应用区，检查是否出现误点击；
6. 若已经连接竖向目标屏幕，再重复显示、触摸、滚动和返回测试；
   若没有对应硬件，明确标记为未测试，不得推断通过。

每一步请记录用户动作、对应时间段日志、截图和通过/失败结论。
最后由你恢复正式服务，并生成完整验收表。
~~~

现场人员只负责按 Agent 的提示观察 LCD 和操作触摸屏；程序哈希、日志、截图、过程关联和最终表格都由 Agent 自动保存。若需要包含整块 LCD 的现场照片，由现场人员拍摄并交给 Agent，Agent 再将照片与同一轮构建身份关联归档。

## 常见问题

| 现象 | 可直接发送给 AI Agent 的排查请求 |
| --- | --- |
| 串口返回 `PORT_BUSY` | 请检查串口占用者，只关闭本轮 Agent 打开的句柄；不要终止不明进程 |
| 程序拒绝当前显示尺寸 | 请只读检查初始化路径是否仍有固定宽高等值判断，并指出代码位置 |
| 桌面只占局部区域或越界 | 请检查 Hero、Grid、Panel、Canvas 的尺寸来源，并抓图标注异常对象 |
| 卡片滑动时误打开 | 请核对拖动阈值及 `click-ignored reason=drag` 日志，再指导我做一次滑动测试 |
| 找不到触摸屏 | 请枚举 input 设备、名称和事件节点，匹配目标触摸屏后再启动程序 |
| `-Werror` 编译失败 | 请保留完整日志，只分析并修复第一条真实警告，不得关闭 `-Werror` |
| 图标文件只有 LFS 文本 | 请停止编译，核对资源和 manifest；没有真实资源时不要继续 |
| 预览版被正式桌面覆盖 | 请检查正式监督服务状态，确认目标后再临时停止并重新运行预览程序 |
| 硬件入口看似可用但无功能 | 请核对后端实现；未接通时恢复待适配状态，不得标记 READY |

## 验收清单

- [x] MCP 工作台、ADB 和串口设备清单已经读取。
- [x] 板端 Linux、framebuffer、触摸节点和正式桌面进程已经核对。
- [x] 源码修改前备份和新旧 SHA-256 已保存。
- [x] 页面不再依赖唯一固定显示尺寸。
- [x] `-Wall -Wextra -Werror` 交叉编译通过。
- [x] 自适应预览程序已在开发板启动并输出 `EDGEOS_READY`。
- [x] 当前横向目标屏幕的 framebuffer 画面已经保存。
- [x] 没有刷写固件，也没有替换正式桌面程序。
- [x] 测试后已恢复正式桌面服务。
- [ ] Settings、Drawing、详情页和 Back 已完成本轮人工触摸验证。
- [ ] 滑动与点击冲突已完成本轮人工验证。
- [ ] 竖向目标屏幕已完成真实显示与触摸验收。

## 本节结论

本次实操已经完成 MCP 连通性检查、板端基线采集、运行时自适应改造、严格交叉编译、临时部署和真实 framebuffer 取证。当前横向目标屏幕的核心桌面显示通过；人工触摸导航、Drawing 和竖向目标屏幕仍然是明确待办项。这样的结论比“程序能启动”更可复现，也避免把尚未执行的验证写成已经完成。

## 版本与变更记录

- 2026-09-04：根据当前 `v853-port/main.c`、Tina 软件包和已有验证记录编写 Part 3 预览章节。
- 2026-09-04：移除对单一显示尺寸和特定参考平台的依赖，改为横竖屏自适应方案。
- 2026-09-04：通过 `lynx-100ask-v853-sdk` MCP 完成设备状态与板端基线检查；修改并编译自适应预览程序，在开发板临时运行并加入真实 framebuffer 图片、运行日志、资源数据和验证边界。
