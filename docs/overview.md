---
slug: /
description: 从开发环境、V853 系统开发到 LVGL 9，使用 AI Agent 完成嵌入式 Linux UI 移植实战。
---

# 课程目录

这是一条从开发环境搭建走向嵌入式图形界面落地的实战路线。建议按章节顺序学习，每完成一节就对照页面中的验收清单保存结果。

:::tip 推荐学习顺序

先完成 Part 0 的统一环境，再进入 Tina SDK 编译和板级验证；确认 LCD 与触摸硬件正常后，最后开始 LVGL 9 移植。这样遇到问题时更容易判断是环境、硬件还是应用层导致。

:::

## 已开放课程

### [Part 0：课程准备](/docs/course-preparation)

安装 VMware 和 Ubuntu 24.04，建立课程工作区，并完成 Claude Code、Codex CLI、DeepSeek Harness 等 AI 辅助工具的首次配置。

### [Part 1：V853 系统开发](/docs/v853-tina-linux)

完成 Ubuntu 编译环境、Tina SDK 认识、V853 SDK 配置与编译、固件烧录，以及 LCD/Touch 外设验证。

### [Part 2：LVGL 9 移植](/docs/lvgl9-porting)

下载并固定 LVGL 9 源码，用 AI 搭建工程，让 LCD 显示、触摸可用、界面持续刷新，再添加字体和图片、检查性能并集成到 Tina SDK。

## 后续规划

| 阶段 | 主题 | 状态 |
| --- | --- | :---: |
| Part 3 | EdgeOS V853 移植 | 规划中 |
| Part 4 | V851s、V821、V861 跨平台验证 | 规划中 |
| Part 5 | Camera、MPP、NPU 与性能专题 | 规划中 |

> 后续阶段会按课程实施进度与跨平台验证结果逐步补充，当前导航只展示已经提供正文的章节。
