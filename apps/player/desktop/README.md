# 本地文件播放器

ImGui + SDL2 桌面播放器，使用 SDK 公共 `LiveKit::Player` 接口。播放引擎及 API 文档见 [SDK Player](../../../../livekit-sdk-cpp/docs/player.md)。

## 构建与运行（Windows x64）

需要 VS 2022 C++ 工具、Windows SDK、CMake 3.24+。当前验证使用用户提供的 FFmpeg 7.1 包：

```powershell
cd D:\workspace\av-platform\livekit-app
.\scripts\build-player-windows.ps1 -FFmpegRoot 'D:\Downloads\ffmpeg-n7.1-241205-gpl-amd64-shared\bin'
.\out\player-windows-ffmpeg71\Release\livekit_local_player.exe 'D:\media\example.mp4'
```

脚本接受包根目录或 `bin` 目录，完成配置、构建和 CTest。此 7.1 包的版本标识为 `b08d796`，导入库位于 `bin`，缺少 FFmpeg 和 SDL 公共头文件。脚本从 FFmpeg 官方仓库下载对应完整提交的源码头文件，并下载 SDL 2.30.9 官方开发包，校验 SHA-256，缓存到 `out/player-deps`；针对 Windows x64 生成公共 `avconfig.h`。不修改下载目录中的原始包。不同版本缺少头文件时需要自行提供匹配开发包，不会复用 6.x 的 FFmpeg 头文件。

DLL 及其传递依赖自动复制到可执行文件旁边。分发时还需要遵守所选 FFmpeg、编解码库及 SDL 的许可证要求；本次采用的是用户提供的 GPL 构建。

示例使用 Dear ImGui 1.91.9b + SDL2，可不带参数直接启动中文播放器窗口。支持系统文件选择、输入路径、文件拖放、播放/暂停、停止、拖动进度条、前后跳转、音量/静音和全屏；视频保持宽高比。打开、定位和关闭通过 future 异步处理，错误显示在界面中，音频设备不可用时提示并静音播放。

快捷键：空格播放/暂停，左右方向键前后跳转 5 秒，S 停止并回到起点，M 静音，F 或双击画面切换全屏，Ctrl+O 打开文件；Esc 先退出全屏，窗口模式下退出程序。支持中文路径、纯音频、纯视频。暂停时拖动进度会清除旧画面，恢复播放后显示新位置；当前没有暂停定位后的单帧预览。

`--auto-exit` 在播放结束后退出，`--hidden` 供无窗口烟测使用，`--frames N` 限制测试运行帧数，`--screenshot output.bmp` 导出程序内部渲染截图。首次构建界面会下载并校验固定版本 ImGui，亦可通过 `FETCHCONTENT_SOURCE_DIR_IMGUI` 指定已有源码目录。

开发包齐全时也可直接配置：

```powershell
cmake -S apps/player/desktop -B out/player -G 'Visual Studio 17 2022' -A x64 `
  -DBUILD_TESTING=ON `
  -DLIVEKIT_FFMPEG_ROOT='D:/ffmpeg-dev'
cmake --build out/player --config Release --parallel 4
ctest --test-dir out/player -C Release --output-on-failure
```

`LIVEKIT_FFMPEG_INCLUDE` 可指定独立头文件目录，`LIVEKIT_PLAYER_SDL_ROOT` 可指定独立 SDL 开发包。`AV_SDK_SOURCE_DIR` 默认指向同级 SDK。播放器单独配置时关闭 RTC 和服务端 SDK，不需要 WebRTC、OpenSSL 或 vcpkg。`BUILD_TESTING=OFF` 关闭应用测试后不需要 ffmpeg 可执行文件。也可从 livekit-app 根工程设置 `AV_BUILD_PLAYER_DESKTOP=ON` 与其他产品一起构建；此选项默认关闭，因为需要额外的 FFmpeg/SDL 开发包。

## 测试归属

`tests/player_ui_tests.cpp` 验证纹理渲染、异步控制、文件切换、错误恢复和窗口布局；CTest 还覆盖空窗口和真实媒体播放结束自动退出，测试使用 SDL dummy 驱动。
SDK 保留解封装、解码、播放图及媒体流测试，分别在 SDK 构建目录运行。应用自动化检查不代表实际扬声器和显示器已人工验收。
