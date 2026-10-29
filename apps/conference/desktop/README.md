# AV 会议 · Qt Widgets 桌面客户端

C++20 + Qt 6 Widgets 的自有会议产品，使用独立 RTC backend、逐轨 GraphEngine 和会议业务服务。Windows/macOS，单会最多 25 人。会议 GUI 不再使用 ImGui/SDL；其他 APP 的界面不受本产品构建影响。

## 功能

- 自有账号注册/登录、游客按九位会议号加入、复制邀请文本。
- 快速会议、单次预约、修改/取消预约、历史会议列表。
- 游客默认等候室审批，主持人锁会、移除、静音、邀请开麦、转让和结束。
- 宫格（每页九路）、演讲者、共享优先布局；摄像头选择、系统默认麦克风/扬声器、单人显示器共享。
- 可靠数据通道聊天，中文、多行、2000 字符上限；仅当前会话，不提供服务端聊天历史。
- 后台 I420 → QImage 转换，QPainter 绘制；每轨道只保留最新帧，只转换可见视频。

启动后从设置填写**业务服务地址**（默认 `http://127.0.0.1:8080`）。无需手工输入媒体 Token。用户名使用 3–32 位字母/数字/下划线，密码 10–128 字节；参会昵称支持中文。凭证仅保存在内存，退出程序后重新登录。

不包含录制、转写、白板、虚拟背景、移动端或浏览器参会。实际设备权限及听感需人工验收。

媒体主管线程持有一个 `RtcEngine`，各发布轨道独立连接输入、编码与发布节点，各订阅轨道独立连接订阅、解码、预览/共享音频输出。UI 只读取不可变快照并排队控制命令；可靠聊天通过同一 backend 的 `RoomServices`。独立构建禁用旧 Room SDK，不需要兼容层。

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

Qt 部署库须与目标架构、编译器 ABI 和最低系统版本匹配。Windows 及 ARM 的执行结果需在对应环境记录，本机 macOS x86_64 通过不代表其他平台已经验收。

## 自动化验证

```sh
QT_QPA_PLATFORM=offscreen \
AV_MEETING_SCREENSHOT_DIR="$PWD/out/conference-qt-macos-x86_64/screenshots" \
  out/conference-qt-macos-x86_64/Release/livekit_demo_tests
```

Qt Test 通过程序内事件及 `QWidget::render()` 验证页面、输入、布局和截图，不操作用户桌面。I420 测试覆盖奇数尺寸、步长、颜色和等比显示；异步测试覆盖最新帧批次及取消后的过期 HTTP 响应。

全链路联调运行 `tests/run_business_integration.py`，参数为 `--client`（`livekit_conference_business_integration`）、`--media-server`（本仓库 `livekit-cpp` 服务）、`--business-server`（Go `business-api`）。测试使用独立数据库、端口与合成媒体，不采集真实设备。

服务部署、授权语义和人工验收清单见聚合仓库 `docs/integration/conference/README.md`。
