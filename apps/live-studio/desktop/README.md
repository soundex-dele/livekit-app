# 直播助手桌面端（Qt 6）

直播场景合成、屏幕/窗口/摄像头素材、图片/文字/本地视频、混音、连麦、PK 和 H.264/AAC 本地录制。直播业务仍需要独立部署业务服务和媒体服务；桌面应用打包不包含这两个服务。

## macOS 构建

从聚合仓库根目录运行：

```sh
# 依赖须与目标 CPU 架构一致。
brew install cmake nasm llvm qt openssl@3 libvpx dav1d jpeg-turbo ffmpeg
./scripts/build/live-studio-macos.sh
```

脚本使用本机架构，编译应用与测试、执行 `studio_*` CTest，然后收集动态依赖并进行本地 ad-hoc 签名。产物：

- `livekit-app/out/live-studio-macos-<架构>/Release/av_live_studio.app`：开发构建。
- `livekit-app/out/live-studio-macos-<架构>/package/av_live_studio.app`：包含非系统动态依赖的打包版本。

可设置 `STUDIO_JOBS=2` 降低并行度、`STUDIO_ARCH=arm64` 或 `x86_64` 选择架构；交叉构建还需要该架构的全部第三方依赖，不能混用 Intel 与 ARM 的 Homebrew 库。`LIVEKIT_LLVM_ROOT` 可指定 LLVM 安装位置。

默认最低构建目标为 macOS 14，可通过 `STUDIO_MACOS_TARGET` 覆盖。最终最低系统版本还受 Homebrew 动态库影响；必须以打包依赖要求为准。公开分发需要发行者的 Developer ID 签名及公证，ad-hoc 签名只用于本地运行。

## macOS 平台实现

- Metal 合成，支持变换、裁剪、透明度、绿幕和多层混合。无 Metal 设备或无窗口测试使用 CPU 合成；CPU 路径不承诺多路 1080p 实时性能。
- ScreenCaptureKit 采集显示器、窗口和系统声音；系统声音排除本应用，防止监听音频再次进入直播。
- AVFoundation 采集摄像头和默认麦克风；AudioQueue 输出连麦监听。
- CoreGraphics/ImageIO 加载图片，AppKit 绘制中文文字和显示原生文件选择窗口。
- HTTPS 使用系统信任库，业务 HTTP 请求拒绝重定向，并限制响应大小及超时。
- 账号 Token 保存到 macOS Keychain；项目保存在 `~/Library/Application Support/AVPlatform/LiveStudio/project.json`。`--settings` 可选择独立项目目录，Keychain 账号也按该目录隔离。
- Qt 6 Widgets 使用逻辑像素与系统 DPI，跨屏字体和控件缩放由 Qt 管理。

仅在添加设备素材或开启相应声音后启动采集。请在「系统设置 → 隐私与安全性」中授予摄像头、麦克风、屏幕与系统音频录制权限。拒绝权限时界面显示错误，可以授权后刷新或重新启用。不要把自动化离线测试通过理解为真实设备已验收。

## 无窗口验证

```sh
build=livekit-app/out/live-studio-macos-x86_64
ctest --test-dir "$build" --output-on-failure -R '^studio_'
QT_QPA_PLATFORM=offscreen \
  "$build/Release/av_live_studio.app/Contents/MacOS/av_live_studio" \
  --hidden --frames 60 --settings "$build/offline-project" --screenshot "$build/studio.bmp"
```

CTest 使用隔离目录、合成素材和本地 HTTP fixture，不枚举或采集真实摄像头、麦克风、屏幕，也不操作用户桌面。Metal 测试只向离屏缓冲区绘制；没有 Metal 设备时明确跳过。`--hidden --test-density 2` 生成 2x 程序内渲染截图；Qt Test 另可配合 `QT_SCALE_FACTOR=2` 验证控件缩放。

实际设备采集、系统权限弹窗、音频可听、跨屏 Retina、Finder 启动，以及真实服务器的直播/连麦/PK 互通应另外人工验收。Windows 构建见 [产品说明](../README.md)，使用 Qt 6.8+、动态 CRT 和匹配的 WebRTC。macOS 变更需在 macOS 上另外编译及验收。


## 界面结构与离屏测试

- `src/main.cpp`：QApplication、参数检查、offscreen 运行和 QWidget::render 截图。
- `src/window.cpp`：场景、素材、属性、混音、账号、互动与项目保存。
- `src/preview.cpp`：QPainter RGBA 预览、旋转素材选择、拖动和方向键编辑。
- `src/session.cpp`：保留异步业务/媒体会话，离屏运行跳过保存的账号。
- `tests/widgets_test.cpp`：Qt Test 程序内控件测试，不使用桌面自动化。

隐藏模式必须指定独立配置目录；即使测试目录包含设备源和启用音频的旧项目，运行时也会过滤采集源并关闭音频。持久化项目保留原始内容。正式启动继续按原项目配置恢复采集。
