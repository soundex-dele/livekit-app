# AV 直播伴侣

Windows 11 x64 原生直播工作台和浏览器观看端。桌面使用 C++20、Qt 6.8+ Widgets；媒体由 SDK Studio 引擎处理，业务服务管理账号、直播、聊天、连麦、礼物和 PK。直播输出使用自有 C++ SFU 的 VP8/Opus 链路。

## 构建

构建需要 PowerShell 7、VS2022、Windows SDK 10.0.26100+、CMake 3.24+、Go 1.25+、Node.js 22、Qt 6.8+ MSVC、vcpkg x64-windows 动态 CRT 依赖、匹配的 /MD WebRTC 产物和 FFmpeg 开发包。

在聚合仓库根运行：

```powershell
.\scripts\build\live-studio.ps1 -VcpkgRoot D:\vcpkg -QtRoot D:\Qt\6.8.3\msvc2022_64 `
  -FFmpegRoot D:\Downloads\ffmpeg-n7.1-241205-gpl-amd64-shared `
  -FFmpegInclude .\livekit-sdk-cpp\out\player-deps\FFmpeg-b08d7969c550a804a59511c7b83f2dd8cc0499b8 `
  -Package
```

输出为 `livekit-app/out/live-studio-qt6/Release/av_live_studio.exe`。完整客户端根工程选项为 `AV_BUILD_LIVE_STUDIO_DESKTOP=ON`，默认关闭。SDK 选项 `LIVEKIT_BUILD_STUDIO` 需要 RTC_MEDIA、RTC_NATIVE、RTC_GRAPH_NODES 和 PLAYER。

Studio 使用系统 D3D11、WGC、WASAPI、WIC，文件解码和录制使用 FFmpeg；合成采用 GPU 渲染后读回 I420 供独立编码/发布图使用，不是端到端 GPU 零拷贝。硬件设备失败会在工作台显示错误。自动化测试可用 WARP，无需访问桌面采集设备。

## 媒体流水线

桌面 Session 的节目房间与互动房间各有一个 MediaSession 主管线程，独占 RtcEngine。每个房间分别创建视频和音频发布图：Studio 输出 → MediaInputNode → codec → RtcPublishNode。互动房间为嘉宾轨道创建独立订阅 → codec → 有界输出队列，由主管线程送入 Studio 合成。RoomEngine 仅负责信令和编码媒体收发。

节目输出包含嘉宾，互动输出只包含本地内容，防止循环发送。关闭互动房间会先停止所有接收图，再删除其远端素材；节目发布和本地录制继续。业务线程只读取连接快照，不跨线程操作 Engine。已移除旧 Room/帧流接口和 LiveKit::SDK 链接依赖。

SDK 的 `StudioGraph` 使用 GraphEngine 管理 9 个节点的连接及 init/start/stop/release 生命周期：时钟、素材源、混音、合成、音频输出、视频输出、预览、监听和录制。场景增删在素材源节点内更新设备和播放器，图的输出连接保持稳定。

```text
clock ──→ sources ──AudioInput──→ mixer ──MixedAudio──→ audio-output / monitor / recorder
  │           └──VideoLayers──→ compositor ──ComposedVideo──→ video-output / preview / recorder
  └──录制控制──→ recorder
```

媒体事件沿有类型的图边同步传递；同一工作线程驱动 10 ms 音频和 30 fps 视频节拍，设备采集及录制保留各自的线程和有界队列。大帧通过不可变共享数据分发，GraphEngine 不额外异步积压媒体事件。Local 输出不含远端音视频，供连麦房间发布；Program 输出含远端，供观众、预览和录制使用。监听只播放远端声音。

停止时先停止时钟驱动，再由图节点关闭采集并排空录制；释放时关闭四路输出，并在工作线程退出 COM apartment 前释放 D3D11 资源。节点处理异常会传回 Studio 的错误状态，录制失败独立报告并保留预览与发布。

当前示例 FFmpeg 包为 GPL 构建，分发前应随包提供该版本及其组件的许可证、对应源码或合规源码提供方式；Qt 按所使用发行版的 LGPL/GPL 或商业许可分发，其余依赖以各源码 LICENSE 为准。打包脚本汇集运行文件与可找到的许可证，当前包用于本地验收；公开分发前还需完成依赖及源码提供方式核对。

## 服务与账号

构建脚本同时编译包含 Studio admission 支持的 `livekit-cpp`。业务服务默认关闭直播功能，通过 `-live-studio` 开启。

```powershell
$env:LIVEKIT_API_KEY='your-key'
$env:LIVEKIT_API_SECRET='<至少 24 字符的随机密钥>'
$env:LIVE_STUDIO_ADMISSION_SECRET='<另一段至少 24 字符的随机密钥>'
.\scripts\run\live-studio.ps1
```

本机服务默认使用 API 8080、媒体 7880；脚本返回进程 ID，日志和 SQLite 位于 `out/live-studio-data`。桌面账号页填写 API 地址，注册用户名及至少 10 字符的密码。注册后可以开播。浏览器入口为 `/apps/live-studio/`，固定直播间地址使用 `#room/<room-id>`。

管理员由 `livekit-server/out/live-studio/studio-admin.exe -db <数据库路径> -name <管理员名>` 创建，密码通过 `STUDIO_ADMIN_PASSWORD` 环境变量提供。登录浏览器后显示管理入口，可以禁用账号、重置密码和强制关播。没有默认管理员密码。

公网部署：用反向代理终止 HTTPS/WSS，业务服务保留 loopback HTTP 供 SFU 入场校验；媒体客户端地址通过 `-media-url wss://...` 配置。转发 `/rtc` 的 WebSocket 升级，开放配置的 UDP 范围，并按媒体服务文档配置 TURN。不要把业务 API 内部 admission 密钥或媒体管理密钥发给客户端。SFU 环境 `LIVE_STUDIO_ADMISSION_PORT` 指向同机业务端口，默认 8080；其值和 admission secret 必须与业务服务一致。关闭业务服务时，新 Studio 入场校验失败关闭。

## 使用与规则

- 开播后可在互动面板复制固定观看地址。左侧添加素材及场景；选中素材后可在预览拖动或使用方向键移动，按 Shift 配合拖动或方向键调整尺寸，也可在属性面板填写变换与裁剪值。
- 绿幕支持背景色、相似度、平滑和抑制绿色溢色。图片支持 alpha；文字使用系统中文字体。
- 麦克风、系统声音和连麦监听显式启用；系统声音排除本进程树，连麦媒体不会循环发送。
- 输出固定 30 fps，可选 720p/1080p 横竖屏。开播或录制期间不能更改画布尺寸。
- 本地录制先写 MKV，再无损封装 MP4；MKV 保留用于故障恢复。已有同名文件不会覆盖。录制不依赖网络连接。
- 场景保存在 `%LOCALAPPDATA%/AVPlatform/LiveStudio/project.json`，账号凭证通过当前 Windows 用户的 DPAPI 加密。项目文件不包含账号密码或媒体 Token。
- 嘉宾最多 3 人。主播批准后，嘉宾主动点击“进入连麦”并授权浏览器摄像头与麦克风。PK 与嘉宾连麦互斥。
- 每账号按 Asia/Shanghai 日期获得 100 免费积分，不累计；1/10/50 分礼物事务扣减，不能给自己送礼。双主播同意后开始 5 分钟 PK，积分只计入有效时段；主动退出或断线超时判负，双方失联取消。服务重启取消进行中的 PK。

## 验证

```powershell
ctest --test-dir livekit-app/out/live-studio-qt6 -C Release --output-on-failure
cd livekit-app/apps/live-studio/web
npm test
npm run test:integration
```

端到端测试启动独立业务与媒体进程、使用 headless Chrome 和合成音视频；输出位于 `livekit-app/out/live-studio/integration-*`，包含报告、日志与浏览器截图。可用 `STUDIO_BROWSER_CHANNEL` 指定已安装浏览器通道。

桌面测试参数为 `--hidden --frames 60 --settings <测试目录> --screenshot <PNG或BMP路径>`；不得使用真实账号配置目录进行自动化测试。SDK 测试执行 CPU/WARP 合成、绿幕、混音、项目恢复与 MKV→MP4 录制，不访问用户桌面。

`studio_graph` 测试使用 WARP、合成色块和 PCM 验证真实图边的数据传递、Local/Program 隔离、场景更新、发布背压、图停止/重启/释放、录制收尾及节点异常传播，不访问真实采集或播放设备。

数据库备份使用 SQLite 在线备份或 `VACUUM INTO`，不要只复制运行中的主数据库而遗漏 WAL。恢复前停止业务和媒体进程，替换数据库后重启；未结束直播会在启动时终止，PK 不自动恢复。录制文件及本地场景需要另外备份。

当前验收状态见聚合仓库 `docs/integration/live-studio/VALIDATION.md`。容量测试已按用户要求暂缓，稳定帧率尚未验收。

自动化通过不代表真实摄像头、麦克风、显示器、扬声器及公网网络已完成人工验收。容量和音画同步以单独的测量报告为准。


## 原生联调与容量复测

```powershell
# 在 web 目录执行；通过桌面同一 Session 发布、接收嘉宾并进行 PK 合成。
$env:STUDIO_NATIVE='1'
node tests/integration.mjs
Remove-Item Env:STUDIO_NATIVE
# 录制文件完整解码与闪光/提示音偏差测量。
$env:STUDIO_FFMPEG_BIN='D:/your-ffmpeg/bin'
node tests/verify-recording.mjs <联调目录>/host_a.mp4
# 容量默认 5 × 20、1800 秒，可用 ROOM_COUNT / VIEWERS_PER_ROOM 缩小诊断规模。
$env:STUDIO_CAPACITY='1'
$env:STUDIO_DURATION_SECONDS='1800'
node tests/integration.mjs
```

容量脚本记录实际解码帧率、接收带宽、整机 CPU 和内存，采用本机同时推流、SFU、解码的拓扑。`passed` 只代表报告所列检查；容量测试的 RTP 计数不能证明音画同步。原生报告单独列出 `native_av.target_met`；`functional_pass_timing_failed` 代表功能闭环通过但同步未达标。

`-Package` 输出到 `out/packages/live-studio-<时间>`，包含桌面 EXE/DLL、server 下的 Go 服务、SFU、管理员工具、Web 静态文件及部署脚本。包内先设置前述环境变量，再执行 `start-services.ps1`；管理员工具位于 `server/studio-admin.exe`。脚本只启动后台服务，桌面程序由使用者自行打开。


## Qt 6 桌面端

桌面入口、窗口、对话框、预览绘制和事件循环全部使用 Qt 6，保留 SDK Studio / GraphEngine 合成、混音、录制与 Session 业务逻辑。原有 project.json、Windows DPAPI / macOS Keychain 账号存储保持兼容。Qt 原生控件支持中文输入法、键盘导航、剪贴板及屏幕 DPI 缩放。

Windows 构建脚本自动复用 `livekit-app/out/conference-qt-windows` 中带 `/MD` 标记的 WebRTC；也可用 `-QtWebRTCBuild <目录>` 指定其他匹配产物。服务端继续使用 `-WebRTCBuild` 的 `/MT` 产物。两者不能混用。未找到 `/MD` 预编译库时，从源构建。Qt 桌面端关闭 SDK 可选 SDL 播放窗口。首次构建需安装 x64-windows 的 openssl、libvpx、dav1d 等 SDK 依赖。

构建后通过 windeployqt 部署 Qt DLL 与平台插件；打包脚本使用 CMake 的 Qt 部署步骤收集插件。`studio_widgets` 测试通过 Qt Test 检查场景/素材编辑、项目恢复、锁定、键盘操作、画布切换及隐藏模式设备隔离，`studio_ui` / `studio_ui_portrait` / `studio_ui_hidpi` 生成程序内截图。

`--hidden` 自动选择 Qt offscreen 平台，必须显式传入隔离的 `--settings` 目录；此模式不加载账号、不枚举/采集设备、不播放本地视频或启用音频输入，只呈现颜色、文字、图片等离线素材，且不会将过滤结果写回项目。自动化验证不代表真实设备或真实服务器直播、连麦、PK 已完成人工验收。
