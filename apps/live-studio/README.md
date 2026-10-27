# AV 直播伴侣

Windows 11 x64 原生直播工作台和浏览器观看端。桌面使用 C++20、ImGui、SDL2；媒体由 SDK Studio 引擎处理，业务服务管理账号、直播、聊天、连麦、礼物和 PK。直播输出使用自有 C++ SFU 的 VP8/Opus 链路。

## 构建

构建需要 PowerShell 7、VS2022、Windows SDK 10.0.26100+、CMake 3.24+、Go 1.25+、Node.js 22、vcpkg 静态依赖、匹配的 WebRTC 产物和 FFmpeg 开发包。

在聚合仓库根运行：

```powershell
.\scripts\build\live-studio.ps1 -VcpkgRoot D:\vcpkg `
  -FFmpegRoot D:\Downloads\ffmpeg-n7.1-241205-gpl-amd64-shared `
  -FFmpegInclude .\livekit-sdk-cpp\out\player-deps\FFmpeg-b08d7969c550a804a59511c7b83f2dd8cc0499b8 `
  -Package
```

输出为 `livekit-app/out/live-studio/Release/av_live_studio.exe`。完整客户端根工程选项为 `AV_BUILD_LIVE_STUDIO_DESKTOP=ON`，默认关闭。SDK 选项 `LIVEKIT_BUILD_STUDIO` 需要 RTC、Player 同时启用。

Studio 使用系统 D3D11、WGC、WASAPI、WIC，文件解码和录制使用 FFmpeg；合成采用 GPU 渲染后读回 I420 供既有 SDK 发布，不是端到端 GPU 零拷贝。硬件设备失败会在工作台显示错误。自动化测试可用 WARP，无需访问桌面采集设备。

当前示例 FFmpeg 包为 GPL 构建，分发前应随包提供该版本及其组件的许可证、对应源码或合规源码提供方式；SDL2 为 zlib、ImGui 为 MIT，其余依赖以各源码 LICENSE 为准。打包脚本汇集运行文件与可找到的许可证，当前包用于本地验收；公开分发前还需完成依赖及源码提供方式核对。

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

- 开播后可在互动面板复制固定观看地址。左侧添加素材及场景；选中素材后可在预览拖动，按 Shift 拖动调整尺寸，也可在属性面板填写变换与裁剪值。
- 绿幕支持背景色、相似度、平滑和抑制绿色溢色。图片支持 alpha；文字使用系统中文字体。
- 麦克风、系统声音和连麦监听显式启用；系统声音排除本进程树，连麦媒体不会循环发送。
- 输出固定 30 fps，可选 720p/1080p 横竖屏。开播或录制期间不能更改画布尺寸。
- 本地录制先写 MKV，再无损封装 MP4；MKV 保留用于故障恢复。已有同名文件不会覆盖。录制不依赖网络连接。
- 场景保存在 `%LOCALAPPDATA%/AVPlatform/LiveStudio/project.json`，账号凭证通过当前 Windows 用户的 DPAPI 加密。项目文件不包含账号密码或媒体 Token。
- 嘉宾最多 3 人。主播批准后，嘉宾主动点击“进入连麦”并授权浏览器摄像头与麦克风。PK 与嘉宾连麦互斥。
- 每账号按 Asia/Shanghai 日期获得 100 免费积分，不累计；1/10/50 分礼物事务扣减，不能给自己送礼。双主播同意后开始 5 分钟 PK，积分只计入有效时段；主动退出或断线超时判负，双方失联取消。服务重启取消进行中的 PK。

## 验证

```powershell
ctest --test-dir livekit-app/out/live-studio -C Release --output-on-failure
cd livekit-app/apps/live-studio/web
npm test
npm run test:integration
```

端到端测试启动独立业务与媒体进程、使用 headless Chrome 和合成音视频；输出位于 `livekit-app/out/live-studio/integration-*`，包含报告、日志与浏览器截图。可用 `STUDIO_BROWSER_CHANNEL` 指定已安装浏览器通道。

桌面测试参数为 `--hidden --frames 60 --settings <测试目录> --screenshot <BMP路径>`；不得使用真实账号配置目录进行自动化测试。SDK 测试执行 CPU/WARP 合成、绿幕、混音、项目恢复与 MKV→MP4 录制，不访问用户桌面。

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
