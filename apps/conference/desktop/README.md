# AV 会议 · Qt Widgets 桌面客户端

C++17 + Qt 6 Widgets 的自有会议产品，使用独立 RTC backend、逐轨 GraphEngine 和会议业务服务。Windows/macOS，单会最多 25 人。会议 GUI 不再使用 ImGui/SDL；其他 APP 的界面不受本产品构建影响。

## 界面

首页采用导航栏、左侧会议启动卡和右侧日程面板；会中采用浅色会议信息与成员／聊天面板、深色视频舞台（每页九宫格 / 演讲者 / 共享优先）和固定底部工具栏。等候室、登录、入会、预约、组织输入及操作确认均使用公共 UI 规范。窗口变窄时工具栏自动切为带提示的图标按钮、侧栏收窄。

公共主题、表单和弹窗来自 `packages/native/qt` 的 `av_ui_qt`，设计规范见 [全应用 UI 规范](../../../docs/ui-design.md)。会议专用自绘控件和页面样式位于 `qt/theme.*`，页面结构位于 `qt/window.cpp`，视频瓦片位于 `qt/video_widget.cpp`。保留原有控件 objectName、授权和异步关闭行为。

## 功能

- 自有账号注册/登录、游客按九位会议号加入、复制邀请文本。
- 快速会议、单次预约、修改/取消预约、历史会议列表。
- 游客默认等候室审批，主持人锁会、移除、静音、邀请开麦、转让和结束。
- 宫格（每页九路）、演讲者、共享优先布局；摄像头选择、系统默认麦克风/扬声器、单人显示器共享。
- 可靠数据通道聊天，中文、多行、2000 字符上限；仅当前会话，不提供服务端聊天历史。
- 后台 I420 → QImage 转换，QPainter 绘制；每轨道只保留最新帧，只转换可见视频。

启动后从设置填写**业务服务地址**（默认 `http://127.0.0.1:8080`）。无需手工输入媒体 Token。用户名使用 3–32 位字母/数字/下划线，密码 10–128 字节；参会昵称支持中文。凭证仅保存在内存，退出程序后重新登录。

不包含录制、转写、白板、虚拟背景、移动端或浏览器参会。实际设备权限及听感需人工验收。

媒体主管线程持有同步 `client::MediaEngine`，基于新 Room 接口管理逐轨 GraphEngine 采集、编解码和输出。UI 只读取快照并排队控制命令；可靠聊天通过 Room 数据通道。connect 返回 bool，有值操作返回 Result<T>，void 控制直接记录日志；保留 RoomObserver，没有操作完成回调。停止时 close 取消阻塞请求并关闭媒体图。重连使用 SFU 下发的 refresh_token；再次入会由业务 API 获取新凭据。

SDK 构建需开启 LIVEKIT_BUILD_CLIENT、RTC_NATIVE、RTC_GRAPH_NODES、RTC_MEDIA 和 SERVER_SDK，会议目标只链接新 MediaEngine。

## 构建

需要 Qt 6.8+ 的 Core、Gui、Widgets、Network、Test，以及现有 SDK/WebRTC 工具链。

在 `livekit-app` 目录：

```sh
# Intel Mac；Apple Silicon 替换为 conference-qt-macos-arm64。
cmake --preset conference-qt-macos-x86_64 \
  -DQt6_DIR=/usr/local/opt/qtbase/lib/cmake/Qt6 \
  -DOPENSSL_ROOT_DIR=/usr/local/opt/openssl@3 \
  -DCMAKE_PREFIX_PATH='/usr/local/opt/libvpx;/usr/local/opt/dav1d;/usr/local/opt/jpeg-turbo' \
  -DWEBRTC_PREBUILT_DIR="$PWD/../webrtc/out/macos-x86_64"
cmake --build --preset conference-qt-macos-x86_64
ctest --preset conference-qt-macos-x86_64
cmake --install out/conference-qt-macos-x86_64 --prefix "$PWD/out/conference-qt-macos-x86_64/package" --component Conference
```

也可独立配置 `cmake -S apps/conference/desktop -B out/conference-standalone`，附加对应 Qt、SDK 工具链和依赖路径。内部构建目标仍为 `livekit_conference_demo`，应用显示名为「AV 会议」。Mac 安装步骤收集 Qt 插件、媒体动态库，最后执行本地 ad-hoc 签名。

Windows 使用 `conference-qt-windows` configure/build/test 预设。通过 `Qt6_DIR` 指向 MSVC x64 动态 Qt；使用 `x64-windows` vcpkg 依赖及 `/MD`，WebRTC 和所有静态库也必须使用同一运行库，不能复用 `/MT` 的旧产物。示例：

```powershell
cmake --preset conference-qt-windows -DQt6_DIR=C:/Qt/6.8.3/msvc2022_64/lib/cmake/Qt6
cmake --build --preset conference-qt-windows
ctest --preset conference-qt-windows
cmake --install out/conference-qt-windows --config Release --prefix out/conference-qt-windows/package --component Conference
```

Windows 构建会自动通过当前 Qt 安装中的 `windeployqt` 将 Qt DLL 和平台插件部署到可执行文件旁，可直接运行 `out/conference-qt-windows/Release/livekit_conference_demo.exe`，无需把 Qt 添加到系统 `PATH`。Qt 测试目标同时部署 Test 库和 offscreen 插件，支持直接无窗口运行。分发应用时仍使用上述安装命令，交付完整的 `package` 目录，不要只复制 exe。

Qt 部署库须与目标架构、编译器 ABI 和最低系统版本匹配。Windows 及 ARM 的执行结果需在对应环境记录，本机 macOS x86_64 通过不代表其他平台已经验收。

## 自动化验证

```sh
QT_QPA_PLATFORM=offscreen \
AV_MEETING_SCREENSHOT_DIR="$PWD/out/conference-qt-macos-x86_64/screenshots" \
  out/conference-qt-macos-x86_64/Release/livekit_demo_tests
```

Qt Test 通过程序内事件及 `QWidget::render()` 验证页面、输入、布局和截图，不操作用户桌面。offscreen 平台不带字体，程序内截图需挂系统中文字体（Windows 上设 `QT_QPA_FONTDIR=C:/Windows/Fonts`），否则中文会渲染成方框；这不影响真实桌面运行。I420 测试覆盖奇数尺寸、步长、颜色和等比显示；异步测试覆盖最新帧批次及取消后的过期 HTTP 响应。

`conference_widgets_hidpi` 以 `QT_SCALE_FACTOR=2` 启动独立测试进程。`AV_MEETING_SCREENSHOT_DIR` 还会导出紧凑首页、登录、设置、加入会议及字段错误弹窗截图；这些检查属于无窗口自动化，不代表真实显示设备已人工验收。

全链路联调运行 `tests/run_business_integration.py`，参数为 `--client`（`livekit_conference_business_integration`）、`--media-server`（本仓库 `livekit-cpp` 服务）、`--business-server`（Go `business-api`）。测试使用独立数据库、端口与合成媒体，不采集真实设备。

服务部署、授权语义和人工验收清单见聚合仓库 `docs/integration/conference/README.md`。
