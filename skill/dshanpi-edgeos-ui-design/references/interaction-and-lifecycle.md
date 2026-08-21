# 交互与生命周期指南

## 页面模型

桌面主 screen 上预创建多个全屏 overlay，通过 `LV_OBJ_FLAG_HIDDEN` 切换。该方式降低反复创建对象造成的内存碎片，但“隐藏”不等于“退出完成”。每个页面都需要成对的 enter/leave 行为。

### Enter 清单

- 防御性关闭上次遗留的 dropdown、dialog 或 editor。
- 重置一次性状态、错误文案、选择高亮和滚动位置。
- 刷新语言、设置值、设备状态和文件列表。
- 按顺序申请显示、摄像头、编码器、UART 或网络 worker。
- 申请失败时回退已获得的资源，并向用户显示可恢复错误。
- 最后再显示页面并置于前景，避免用户看到半初始化状态。

### Leave 清单

- 立即阻止新的主操作。
- 关闭所有 dropdown、Picker、对话框、键盘和 Toast 关联状态。
- 停止 LVGL timer、后台线程或异步回调，并防止其访问隐藏/释放对象。
- 停止采集、编码、播放、UART 或其他硬件会话。
- 等待或确认资源真正归还后再恢复桌面。
- 隐藏页面并处理当前触点 release，防止返回手势点击穿透桌面。

## Dropdown 的 screen 层陷阱

LVGL dropdown 的展开列表会挂到当前 screen，而不是 dropdown 控件的父页面。只执行：

```c
lv_obj_add_flag(view, LV_OBJ_FLAG_HIDDEN);
```

会把页面隐藏，却把列表留在桌面上。正确模式是为页面集中管理 dropdown：

```c
static void page_close_dropdowns(void)
{
    lv_obj_t *dropdowns[] = {g_first_dropdown, g_second_dropdown};
    for (size_t i = 0; i < sizeof(dropdowns) / sizeof(dropdowns[0]); ++i) {
        if (dropdowns[i] != NULL && lv_dropdown_is_open(dropdowns[i]))
            lv_dropdown_close(dropdowns[i]);
    }
}
```

在返回回调中先关闭，再隐藏页面；在页面 show 入口再防御性调用一次。不要直接隐藏 dropdown list，因为 `lv_dropdown_close()` 还会恢复控件状态和选项状态。

## 点击、拖动与事件选择

- 普通按钮使用 `LV_EVENT_CLICKED`，让 LVGL 完成按下、移动、释放判定。
- 需要在按下瞬间中止硬件或捕获手势时才使用 `LV_EVENT_PRESSED`。
- `lv_indev_wait_release()` 会改变后续 focus/defocus 流程，不要依赖它替代 popup 清理。
- 滚动容器中的卡片记录 pressed 起点；移动超过触摸阈值后取消 clicked 动作。
- 装饰性子对象移除 CLICKABLE、CLICK_FOCUSABLE 和不需要的 SCROLLABLE。
- 桌面图标移除 `SCROLL_ON_FOCUS`，避免 pointer focus 导致网格自动滚动。

## 异步状态机

对 Wi-Fi、Cloud Model、OTA、媒体加载等操作定义显式状态：

```text
idle → preparing → running → success
                    └──────→ failure → retry/exit
```

- 状态只能由拥有操作的 worker 推进。
- UI 回调只发起操作，不在 LVGL 线程中执行长时间 I/O。
- worker 回调更新 UI 前确认页面/对象仍有效。
- 取消需要有确定的终态，不能让界面回到 idle 而后台仍写设备。
- 重试从干净状态开始，清除临时文件、旧 fd、旧 timer 和上次错误。
- 超时是失败状态，不是无限等待；错误文案区分 DNS、TLS、HTTP、摘要、存储和硬件阶段。

## 摄像头与媒体资源

- 页面是资源的唯一拥有者；不要让两个可见页面同时控制 VICAP/VO/VENC。
- 启动采用逐阶段 acquire；失败按反序 release。
- 停止采集和编码后，确认异步队列已 drain，再释放 VB block。
- 外部 AI 应用启动前保存必要桌面状态，关闭 LVGL/VO 所有权，再启动子进程。
- 子应用退出后重新初始化桌面资源；恢复失败时提供明确的重启路径。
- 连续进入/退出、拍照后返回、录像中返回、摄像头缺失都是必测路径。

## UART 与工具页

- 连接状态、端口、波特率、格式和计数器应持续可见。
- 发送动作在断开、队列满或协议请求进行中时禁用，并显示原因。
- 输入编辑器、接线说明和 VAXP 命令 dropdown 都属于页面临时状态，返回时统一关闭。
- 对负错误、短读、超时和校验失败使用有符号返回值；不得把负错误转换为巨大无符号长度交给解析器。

## 对话框与危险操作

- 删除、关机、重启、烧录模式和升级安装必须说明影响。
- 取消操作始终可见，并保持为安全默认。
- 确认后立即禁用按钮，防止重复执行。
- 任务完成或失败后关闭遮罩，不能留下透明但可点击的 top-layer 对象。

## 屏保与输入

- 屏保只在无其他全屏任务、对话框和关键操作时进入。
- 进入前记录活动状态，唤醒触摸仅用于唤醒，不穿透到底层按钮。
- 解锁手势有清晰方向和距离阈值，并在四个边缘触点上测试。
