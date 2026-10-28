# Qt6 本地播放器

C++20 / Qt6 Widgets 桌面应用，参考 PotPlayer 的紧凑布局，保留 Windows/macOS 原生标题栏。播放内核使用公共 `LiveKit::Player` API；SDK 继续通过 FFmpeg 解封装和软件解码，独立于 Qt、SDL、ImGui 和 WebRTC。

## 功能

- 本地音视频播放、暂停、停止、进度定位及暂停后的单帧预览，保持视频宽高比。
- 右侧播放列表：添加、删除、清空、拖放排序、上下移动、上一集/下一集，顺序播放、单曲循环和列表循环。打开文件/拖到画面替换列表并播放；添加文件/拖到列表追加。
- 顺序模式播完列表后停止；坏文件显示错误并尝试下一项，列表循环中不会无限重试整张坏文件列表。删除当前项目或清空列表停止播放。
- UTF-8（含 BOM）外挂 SRT/WebVTT 字幕，支持中文、多行、重叠字幕、显示开关、±10 分钟毫秒偏移、字号和移除。加载失败保留旧字幕；换片清除字幕。上限 8 MB，样式标记按纯文本处理。
- 记忆音量、静音、窗口位置、列表、播放模式及字幕字号；最近 100 个文件支持续播，文件大小/修改时间改变后不使用旧位置。距离结尾不足 10 秒或已结束从头播放。启动恢复列表但不自动播放，设置中可关闭续播、清除历史。
- 全屏闲置 3 秒隐藏控制栏，鼠标移动或键盘操作显示；弹窗、菜单、拖动及控制栏键盘焦点期间不隐藏。

快捷键：空格播放/暂停，左右方向键跳转 5 秒，S 停止，M 静音，F 或双击画面全屏，V 字幕开关，F6 播放列表；打开文件使用 Ctrl+O / macOS Command+O。Esc 退出全屏，普通窗口不会退出应用。列表内 Delete/Backspace 删除所选项目。菜单与按钮共用动作与启用状态。

当前不包含网络播放、倍速、硬解、音轨选择、内嵌字幕、ASS/SSA、HDR、皮肤系统或文件关联。视频以 CPU RGBA 通过 QPainter 绘制；不承诺 4K 高帧率或音频硬件主时钟的长期同步精度。

## 依赖和构建

需要 CMake 3.24+、C++20 编译器、Qt 6.8+（Widgets / Multimedia / Svg，测试还需 Test）及匹配的 FFmpeg 开发包。测试使用 `ffmpeg` 命令行生成短素材，播放器本身不通过子进程播放。原 ImGui/SDL 界面已替换，无需下载它们。

### macOS

指定当前架构的 Qt 和 FFmpeg；不要混用 Intel 与 Apple Silicon 库：

```bash
QT_ROOT="$(brew --prefix qtbase)" \
FFMPEG_ROOT="$(brew --prefix ffmpeg)" \
./scripts/build-player-macos.sh
```

Qt 的独立模块包需已安装；Homebrew 环境可使用 `qtbase qtmultimedia qtsvg`。部署需要 Python 3.9+。脚本执行构建、CTest、Qt 部署、动态库路径修正和审计以及无窗口启动烟测，输出 `out/player-macos-qt/dist/livekit_local_player.app`。可通过 `BUILD_DIRECTORY`、`INSTALL_DIRECTORY`、`BUILD_JOBS` 覆盖目录和并行度。每次部署替换该输出目录下的同名应用包，用户设置保存在包外。产物使用本地 ad-hoc 签名，未进行发行签名、公证。

### Windows x64

需要 VS 2022 C++ 工具、Windows SDK 和 Qt MSVC x64 包：

```powershell
.\scripts\build-player-windows.ps1 `
  -QtRoot 'C:\Qt\6.8.3\msvc2022_64' `
  -FFmpegRoot 'D:\ffmpeg-dev'
```

脚本接受 FFmpeg 根目录或 `bin`，可通过 `-BuildDirectory`、`-InstallDirectory` 和 `-Configuration` 自定义构建。Qt 使用动态 CRT，播放器和 SDK 同步使用 `/MD` 或 `/MDd`；需提供匹配配置的 Qt 库。输出 `out/player-windows-qt/dist/bin/livekit_local_player.exe` 和依赖 DLL/插件。

保留原 FFmpeg 7.1 `b08d796` 包缺头文件时的固定版本下载与 SHA-256 校验流程；其他版本需提供匹配开发包。Qt/FFmpeg 的许可证与所选第三方编解码库应随分发包保留。

### 直接配置及根工程

```bash
cmake -S apps/player/desktop -B out/player-qt \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
  -DCMAKE_PREFIX_PATH=/path/to/Qt -DLIVEKIT_FFMPEG_ROOT=/path/to/ffmpeg
cmake --build out/player-qt --parallel 4
ctest --test-dir out/player-qt --output-on-failure
cmake --install out/player-qt --prefix out/player-qt/dist
```

`AV_SDK_SOURCE_DIR` 可指定 SDK 源码。根工程需设置 `AV_BUILD_PLAYER_DESKTOP=ON`，独立播放器配置同时关闭 `AV_BUILD_REMOTE_CONTROL_DESKTOP`、`AV_BUILD_SCREEN_CAST_DESKTOP`、`AV_BUILD_CONFERENCE_DESKTOP` 和 `AV_BUILD_LIVE_STUDIO_DESKTOP`。Windows 混合构建其他产品时，所有依赖必须匹配选定的 CRT。应用测试关闭时不要求 `ffmpeg` 命令行。

## 命令行与自动化

```text
livekit_local_player [本地文件] [--auto-exit] [--hidden]
                     [--frames N] [--screenshot output.png] [--verbose]
```

- `--auto-exit`：单文件排空后退出，打开/播放失败返回 1；必须指定媒体文件。
- `--hidden`：使用 Qt offscreen 平台，禁止窗口显示，音频送往丢弃输出端，不连接真实扬声器。
- `--frames N`：最多运行 N 次控制器更新循环（不是媒体帧数），0 不限制。
- `--screenshot`：通过 QWidget 内部渲染导出 PNG/BMP，不捕获用户桌面；若循环结束前未就绪或文件无法写入，返回 1。
- `--verbose`：启用详细媒体图日志；默认只记录警告及错误。

以上自动化参数任一存在时使用临时设置文件，禁用续播，不读取或覆盖用户历史。

## 结构与验证

`Controller` 使用 8 ms Qt 定时器非阻塞检查 SDK futures，合并连续定位，处理切换和取消。`VideoWidget` 绘制独立持有的 QImage，按 generation 隔离旧帧。`QtAudioOutput` 通过 QAudioSink 输出 48 kHz 双声道 float32，必要时转换 int16；处理部分写入，暂停保留缓存，定位/停止/关闭重置缓存。设备失败时提示并继续消费音频，避免播放图背压。

`PlaylistModel` 使用稳定 ID 保持重排后的当前项目，`Settings` 存储窗口状态和有界历史；`PlayerWindow` 统一菜单、按钮与快捷键。

CTest 包括字幕解析、素材生成、真实媒体 CLI 播放结束、空窗口截图、Qt Test 功能集、2× DPI 布局检查。Qt 测试使用 `QT_QPA_PLATFORM=offscreen` 和可替换的假音频输出，覆盖暂停缓存、定位清理、排空、模拟输出故障、异步关闭、文件切换、错误恢复、拖放、列表、字幕、设置和内部截图。SDK 的媒体图回归测试需在独立 SDK 构建目录执行。

所有自动化结果仅说明对应编译/无窗口路径。真实扬声器、显示器、原生全屏、多屏窗口恢复另需设备人工验收；Windows 不在当前 macOS 主机上执行验证。

## 本次验证记录（2026-10-27）

环境：macOS 15.7.4 x86_64、AppleClang 16、Qt 6.11.1、FFmpeg 8.1。本机 `ffmpeg@7` 前缀实际指向 FFmpeg 8.1，因此不将此次结果标记为 FFmpeg 7 验收。

- 独立应用 Release 构建通过；应用 CTest **6/6**，其中 Qt Test 主功能集 **13 项通过（含初始化/清理）**，另有 2× DPI 布局检查。
- SDK 独立播放器 CTest **4/4**；根工程仅启用播放器的 Release 构建通过，RTC 关闭。
- 已检查内部生成的空窗口、紧凑窗口、字幕、错误状态及 2× DPI 截图。
- macOS 部署显式包含 offscreen 平台插件；60 个 Mach-O 文件通过包内依赖、库标识、搜索路径和符号链接审计，ad-hoc 签名通过严格校验。
- 将 `.app` 和测试媒体复制到独立临时目录，移除开发插件/动态库环境覆盖后，真实媒体无窗口播放、自动退出及截图均通过；动态加载日志没有开发目录库路径。
- Windows 构建脚本已更新，尚未在 Windows 主机执行。macOS Apple Silicon、真实音频设备、显示器、多屏与原生全屏尚待对应设备验收。
