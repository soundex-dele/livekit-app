# AV 应用 UI 规范

本规范覆盖会议、远程协助、传屏、直播助手和播放器桌面端，以及直播、投屏接收和远控 Web。应用保留原有业务接口、鉴权规则和媒体状态机，界面使用同一套视觉语言。

## 视觉与布局

工作台使用浅背景和白色内容区域，媒体画布使用深色。播放器和直播制作使用深色工作环境。当前按业务场景选择主题，不提供用户明暗主题切换。

| 角色 | 浅色 | 深色 |
| --- | --- | --- |
| 工作区背景 | `#F5F7FB` | `#0B1220` |
| 内容表面 | `#FFFFFF` | `#151F30` |
| 主操作背景 | `#2563EB` | `#2563EB` |
| 正文 | `#17243B` | `#E7EDF7` |
| 辅助文字 | `#53627A` | `#ADBCD1` |
| 媒体画布 | `#0B1220` | `#0B1220` |

- 使用系统字体：Segoe UI / Microsoft YaHei UI / PingFang SC / system-ui。Qt 常规正文 14 逻辑像素、标题 24、分区 16、辅助 12；Web 保持正文 14–16、移动输入框至少 16。
- 使用 4/8 间距体系。控件圆角 8，卡片和弹窗内容区域 12；主要内容内边距 24，紧凑侧栏 16。主要操作突出，危险操作具有文字说明。
- 明暗背景分别检查文字对比度。正常文字至少 4.5:1，状态同时使用文字与颜色；图标按钮具有 tooltip 和可访问名称。
- 图标由 Qt QPainter / Web SVG 绘制，不需要远程字体或图标服务。媒体内容不参与通用背景绘制。
- Qt 保留系统窗口边框，使用 Fusion 作为应用控件基础样式；打开/保存文件以及系统权限窗口保持操作系统实现。
- Windows 主窗口和业务弹窗的原生标题栏跟随应用主题：浅色为白色背景和深色文字，深色与内容表面一致。Windows 11 同步标题栏、文字和边框颜色，Windows 10 使用对应明暗的系统标题栏；高对比度模式保留系统标题栏配色。同步覆盖窗口句柄创建、重建和主题事件，不为普通子控件提前创建原生窗口。
- Web 断点为小于 680、680–1023、大于等于 1024 CSS 像素。窄屏堆叠卡片、折行操作、管理表格转卡片；视频等比显示，远控画面不放置拦截输入的覆盖层。

## 共享实现

### Qt Widgets

`packages/native/qt` 提供独立 CMake 目标 `av_ui_qt`，只有 Qt Widgets 依赖；测试另依赖 Qt Test。独立应用通过 `if(NOT TARGET av_ui_qt)` 添加该目录，再链接此目标。

公共头文件为 `<av/ui.hpp>`，命名空间 `av::ui`：

- `Theme`、`ButtonRole`、`Tone` 分别表达主题、按钮角色和反馈语义。
- `apply_theme()` 为窗口或子树设置主题，`theme_for()` 为弹窗继承宿主主题。局部样式只描述业务布局；不要在各应用复制通用控件 QSS。
- `set_button_role()`、`set_surface()`、`set_text_role()`、`set_tone()` 使用共享动态属性，不占用业务 `objectName`。
- `Card`、`StatusBadge`、`Notice`、`EmptyState` 用于内容分区、状态和反馈。
- `Dialog` 包含标题、说明、滚动内容和固定底部操作区；`content_layout()` 装配业务表单，`add_action()` 创建动作按钮。应用显式连接 accept/reject 或异步操作，不在组件库中调用业务接口。
- `MessageBox` 和 `InputDialog` 提供迁移静态调用的兼容形态，内部使用共享 Dialog。`choose_color()` 使用可主题化的 QColorDialog，保留透明度编辑。
- QWidget 对象由 Qt 父对象管理，所有接口在 GUI 线程使用。媒体帧、网络对象和授权状态不归公共 UI 库持有。

### Web

`packages/web/ui` 提供共享语义 CSS 变量、控件/布局类和 TypeScript DOM 组件；应用继续使用原生 TypeScript + Vite。弹窗、确认、账号组织入口由共享组件装配，业务回调继续保留在应用中。

保留现有可访问名称、DOM ID 和哈希路由。直播游客可以观看，互动按现有规则要求登录；投屏创建仍要求账号和组织；远控连接码流程不增加登录前置条件。

## 弹窗与反馈

- 首次聚焦适合的输入框或安全操作；Tab 顺序与视觉一致，Esc 取消，关闭后焦点返回入口。
- 确认类弹窗默认选择取消；结束会议、停止直播、清除历史和管理危险操作使用明确文案。主持人离会保留转让、结束和取消三个业务结果。
- 长内容在正文区域滚动，底部动作保持可见。对话框大小受当前屏幕可用范围约束。
- 异步提交禁用重复请求。`Dialog::set_busy()` 暂停内容和操作区输入，同时阻止用户取消；完成或失败后由业务恢复状态。错误后保留输入，并提供明确原因与恢复路径。
- 普通成功提示不抢焦点；重要失败保持可见。用户内容用纯文本展示，避免把文件名、错误或昵称解释为富文本。
- 远控授权继续显式允许/拒绝；媒体停止和窗口关闭继续等待原有异步释放；直播录制与推流状态独立。UI 更新不能绕过这些状态。

## 自动化验证

不操作用户桌面。Qt 使用 `QT_QPA_PLATFORM=offscreen`，Web 使用 Playwright 无头 Chromium、模拟 API 和合成媒体。

Windows 标题栏不包含在 Qt 离屏截图中，单独由 `shared_ui_window_theme` 使用 Windows 平台插件创建始终隐藏的窗口，读取 DWM 明暗属性，并验证真实颜色设置调用的参数和返回结果。DWM 的标题栏背景、文字和边框颜色仅支持设置，不支持读取。检查覆盖弹窗继承、句柄重建和主题更新，不显示或操作桌面窗口；系统不支持相应属性时明确跳过。

```powershell
$env:PATH = 'D:\Qt\6.8.3\msvc2022_64\bin;' + $env:PATH
$env:QT_QPA_FONTDIR = 'C:\Windows\Fonts'
$env:AV_MEETING_SCREENSHOT_DIR = 'D:/workspace/av-platform/out/ui-refresh/conference'
$env:AV_STUDIO_SCREENSHOT_DIR = 'D:/workspace/av-platform/out/ui-refresh/studio'
ctest --test-dir livekit-app/out/rtc-products-qt6 -C Release --output-on-failure -R 'shared_ui_widgets|conference_widgets|remote_host_render|screen_cast_render|studio_ui|studio_widgets'
ctest --test-dir livekit-app/out/player-standalone -C Release --output-on-failure
```

组件测试包括安全默认动作、Esc、忙碌、错误保留输入、长内容滚动以及明暗组件截图；`shared_ui_widgets_hidpi` 和应用 HiDPI 测试在独立进程使用 `QT_SCALE_FACTOR=2`。会议、直播和播放器补充实际业务弹窗截图。

Web 在各应用目录执行 `npm run build`、`npm test`、`npm run test:ui`，覆盖 390 / 800 / 1280 / 1440 宽度、默认/等待/错误/结束状态、账号和确认弹窗。

自动化结果仅说明构建、模拟数据和离屏路径。真实摄像头、音频输出、屏幕共享、远程输入设备、跨屏 DPI、系统文件对话框，以及 macOS 专属行为需要分别记录设备人工验收结果。

### 本次验证记录（2026-11-06）

验证环境为 Windows、Qt 6.8.3 / MSVC 2022，桌面应用使用 Release 构建。

| 验证项 | 结果 |
| --- | --- |
| 会议、远控、传屏、直播助手、播放器桌面构建 | 5 个应用均通过 |
| RTC 应用选定 UI 检查 | 13/13 通过；会议和直播最终修改后复验 7/7 通过 |
| 播放器检查（含离屏媒体播放及共享组件） | 8/8 通过 |
| 独立 Qt 共享组件检查 | 2/2 通过，覆盖 1× / 2× 缩放 |
| Web TypeScript / Vite 构建 | 3 个应用均通过 |
| Web 单元测试 | 16/16 通过 |
| Web 无头 UI 检查 | 3 套均通过，覆盖 390 / 800 / 1280 / 1440 宽度及组件 1× / 2× 缩放 |
| 补丁空白检查 | `git diff --check` 通过 |

本地[界面预览合集](../../out/ui-refresh/index.html)包含 8 个应用及共享组件的程序化截图。截图用于检查布局和视觉状态；Web 检查使用模拟 API 和合成媒体。上述检查不等同于真实设备人工验收，macOS 和真实设备交互尚未验证。

Windows 标题栏配色修复补充验证：7 个隐藏原生窗口场景全部通过（含初始化与清理为 9 passed，0 failed，0 skipped），覆盖样式刷新后的实际配色设置；两套共享组件离屏检查、11 项 RTC 应用 UI 检查及播放器 3 项 UI/fixture 检查通过。原生检查明细保存为 `out/ui-components/window-theme-results.txt`。
