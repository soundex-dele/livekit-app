# 多业务客户端设计

客户端仓库承载多个独立业务 APP：远程控制、传屏，以及后续会议等。每个 APP 可包含 desktop、web、android、ios 等端，按实际实现建立目录。

当前已实现 `apps/remote-control/desktop`（C++20、ImGui、SDL2）和 `apps/remote-control/web`（TypeScript、Vite）。产品入口、配置、构建目标、静态资源和发布产物分别归属各端。

## 依赖与目录

业务 APP → 端内用例与适配 → SDK 公共 API / 业务 HTTP API。APP 之间不互相引用。公共代码有真实复用需求时抽到 `packages/native` 或 `packages/web`；业务专属跨端代码放到业务 `shared`，跨语言通过契约统一。

桌面端的 `src/app` 管启动；`domain` 放状态快照；`services` 协调业务与 SDK；`infrastructure` 封装 HTTP 和系统热键；`features/remote_control` 管 UI。Web 的 `src/app` 是入口，`services` 封装业务 API，`features/remote_control` 管控制界面和数据协议。

SDK 不依赖客户端工程。远控桌面端支持独立 CMake 配置，客户端根 CMake 通过产品选项编排。Web 使用 `/apps/remote-control/` 基路径，API 使用 `/api/v1/remote-control/`。没有旧路径兼容入口。

## 状态与安全

远控仍通过配对凭证、被控端本地同意、短期租约和 SDK 独立 watchdog 执行授权。控制/被控属于业务角色；多端不意味着每个角色都需要单独 APP。传屏自行定义发送/接收策略，不复用远控输入授权状态机。

产品 UI 负责展示、交互和本地同意；业务服务负责业务授权；SDK 负责媒体、输入和资源生命周期。APP 不持有媒体服务 API Secret，也不直接引用 GraphFlow 或 WebRTC 内部类型。

## 验证

端内测试随端维护，跨端场景测试由业务拥有。现有无窗口被控 fixture 发布合成视频并记录输入，不注入用户桌面。真实设备人工验收另行记录。

完整目录和公共包扩展规则见 [仓库结构](../../docs/architecture/repository-layout.md)，运行见 [远控说明](../../docs/integration/remote-control/README.md)。
