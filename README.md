# livekit-app

AV Platform 的客户端应用仓库。

已实现首版远程协助：`apps/remote-control/desktop/` 为 C++20 + ImGui + SDL2 被控端，`apps/remote-control/web/` 为 TypeScript + Vite 浏览器控制端。构建、启动及验收边界见 [远控运行文档](../docs/integration/remote-control/README.md)。原设计中的账号、会议等完整客户端能力尚未实现。

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

每个产品位于 `apps/<业务>/<端>`，远控桌面端和 Web 端已经迁入该结构。后续传屏使用 `apps/screen-cast`，按实际实现建立平台端和共享包。各 APP 独立构建发布，公共媒体能力由 SDK 提供。

客户端根 CMake 可编排产品；桌面端也可用 `cmake -S apps/remote-control/desktop -B out/remote-control-desktop` 独立配置（需 Windows、VS2022 和 vcpkg 静态工具链）。Web 命令在对应 APP 的 web 目录运行。完整构建使用聚合仓库的 `scripts/build/remote-control.ps1`。
