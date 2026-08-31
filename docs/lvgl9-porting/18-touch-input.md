# 18 让触摸屏控制 LVGL 界面（evdev）

> 把 Linux 收到的触摸事件交给 LVGL，界面才真正“点得动”。

![evdev 所在的 LVGL 输入链路](images/guide-lvgl-runtime.svg)

## 学习目标

- 看懂触摸屏到 LVGL 控件的输入数据流。
- 使用 LVGL 9.4 evdev 接口创建指针输入设备。
- 把输入设备绑定到第 17 节创建的显示对象。
- 根据第 10–11 节 Touch 验证的实测结果判断坐标是否正确。

## 前置条件

- 第 17 节的 fbdev 显示对象已经可用。
- 第 16 节已启用 `LV_USE_EVDEV`。
- [第 10–11 节外设验证的 Touch 参数卡](../v853-tina-linux/10-peripheral-validation.md) 已记录实际 event 节点、坐标范围和方向。

> event 编号可能变化。第 10–11 节的 Touch 参数卡没有实测记录时先返回补测，不把官方示例中的编号当作实际节点。

## 输入数据流

触摸屏 → Linux evdev 事件 → LVGL evdev 驱动 → `lv_indev_t` → 已绑定显示 → LVGL 控件。

## 三个关键对象

| 名称 | 作用 |
| --- | --- |
| `LV_USE_EVDEV` | 控制 evdev 驱动是否参与编译 |
| `lv_evdev_create()` | 用实测 event 节点创建指针输入设备 |
| `lv_indev_set_display()` | 将输入设备绑定到第 17 节的显示对象 |

本节参数卡：

| 参数 | 取值来源 |
| --- | --- |
| 设备名称 / 识别依据 | `【填写第 10–11 节 Touch 实测值】` |
| event 节点 | `【填写第 10–11 节 Touch 实测值】` |
| X/Y 轴代码 | `【填写第 10–11 节 Touch 实测值】` |
| X/Y 坐标范围 | `【填写第 10–11 节 Touch 实测值】` |
| 交换轴、镜像或旋转关系 | `【填写第 10–11 节 Touch 实测结论】` |
| 绑定的显示对象 | 第 17 节由 `lv_linux_fbdev_create()` 返回的对象 |

## 操作步骤

1. 准备环境与版本信息。
2. 执行本节操作并保存完整命令与日志。
3. 按验收标准验证结果。

## 验收标准

- [ ] `LV_USE_EVDEV` 已启用。
- [ ] `lv_evdev_create()` 使用第 10–11 节 Touch 验证的实测 event 节点。
- [ ] `lv_indev_set_display()` 已绑定第 17 节的显示对象。
- [ ] 点击、拖动和松开都能被控件识别。
- [ ] 屏幕四角坐标方向正确，没有交换轴或镜像问题。

## 常见问题

| 现象 | 优先检查 |
| --- | --- |
| 完全没有触摸 | event 节点是否仍与第 10–11 节 Touch 记录一致、程序是否有访问权限 |
| 有事件但控件不响应 | 输入设备是否已用 `lv_indev_set_display()` 绑定正确显示 |
| X、Y 方向颠倒 | 对照第 10–11 节 Touch 坐标范围，检查交换轴设置 |
| 左右或上下镜像 | 对照第 10–11 节 Touch 四角实测结果，统一处理坐标方向 |
| 点击位置漂移 | 节点是否选错、坐标范围和显示方向是否匹配 |
| 接口未定义 | `LV_USE_EVDEV` 和 evdev 驱动源码是否进入构建 |

## LVGL 9.4 官方参考

- [evdev 驱动](https://docs.lvgl.io/9.4/details/integration/embedded_linux/drivers/evdev.html)
- [evdev API](https://docs.lvgl.io/9.4/API/drivers/evdev/lv_evdev_h.html)
- [LVGL 输入设备](https://docs.lvgl.io/9.4/details/main-modules/indev/overview.html)
