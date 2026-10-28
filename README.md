# livekit-app

AV Platform 的客户端应用仓库。

已实现首版远程协助：`apps/remote-control/desktop/` 为 C++20 + ImGui + SDL2 被控端，`apps/remote-control/web/` 为 TypeScript + Vite 浏览器控制端。构建、启动及验收边界见 [远控运行文档](../docs/integration/remote-control/README.md)。视频会议观看客户端和本地文件播放器已从 SDK 迁入；账号及完整会议业务流程尚未实现。

## 设计文档

- [客户端设计](docs/design.md)：模块、会话状态、线程与渲染、业务接入、远控和验收计划。
- [平台总体设计](../docs/architecture/platform-design.md)：在 AV Platform 同级仓库布局下阅读。

## 规划职责

- 页面、窗口布局、用户交互与系统授权引导。
- 通过业务服务完成账号、设备、会议与远控授权等业务操作。
- 通过 SDK 使用采集、传输、播放与剪辑能力。

客户端使用业务服务签发的短期 Token，不保存媒体服务 API Secret。

## 关联仓库

- [av-platform](https://github.com/soundex-dele/av-platform)：聚合仓库与平台架构文档。
- [livekit-sdk-cpp](https://github.com/soundex-dele/livekit-sdk-cpp)：原生 C++ RTC SDK。
- [livekit-server](../livekit-server/README.md)：本地业务服务仓库，尚未创建远程。
- [livekit-cpp](https://github.com/soundex-dele/livekit-cpp)：媒体服务。

## 多业务组织

每个产品位于 `apps/<业务>/<端>`，远控桌面端和 Web 端已经迁入该结构。传屏位于 `apps/screen-cast`，会议和播放器分别位于 `apps/conference/desktop`、`apps/player/desktop`。各 APP 独立构建发布，公共媒体能力由 SDK 提供。

客户端根 CMake 可编排产品；桌面端也可用 `cmake -S apps/remote-control/desktop -B out/remote-control-desktop` 独立配置（需 Windows、VS2022 和 vcpkg 静态工具链）。Web 命令在对应 APP 的 web 目录运行。完整构建使用聚合仓库的 `scripts/build/remote-control.ps1`。

## 视频会议、播放器与直播助手

- [直播助手](apps/live-studio/desktop/README.md)：`av_live_studio`，Windows / macOS 平台实现、Metal 合成、原生采集和独立 `.app` 打包。
- [视频会议](apps/conference/desktop/README.md)：`livekit_conference_demo`，保留 Windows/macOS、Retina 适配和 macOS `.app` 打包。
- [本地播放器](apps/player/desktop/README.md)：`livekit_local_player`，Qt6 Widgets 界面，独立依赖 SDK 播放引擎，无需 WebRTC；构建脚本为 `scripts/build-player-windows.ps1` / `scripts/build-player-macos.sh`。

两者可从各自 desktop 目录独立配置，也可通过根 CMake 统一构建。根选项 `AV_BUILD_CONFERENCE_DESKTOP` 默认开启，`AV_BUILD_PLAYER_DESKTOP` 默认关闭（需额外提供 FFmpeg 和 Qt6 开发包）。`AV_SDK_SOURCE_DIR` 可覆盖同级 SDK 路径；根工程在添加产品前统一选择所需 SDK 引擎。应用测试由 `BUILD_TESTING` 控制，使用 `ctest --test-dir <build> -C Release --output-on-failure` 执行。

根目录提供 Windows、Linux、macOS CMake 预设。macOS 预设默认关闭目前仅支持 Windows 的远控和传屏应用。自动化验证使用无窗口驱动；真实设备体验仍需人工验收。

- [AV 直播伴侣](apps/live-studio/README.md)：自有平台直播、观看与互动；构建和运行默认关闭。
