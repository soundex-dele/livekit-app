# FreeCut 桌面剪辑器

独立 C++17 / Qt6 Widgets 应用，通过 `LiveKit::Editor` 完成多轨预览和离线导出。媒体图使用 GraphEngine；不依赖 RTC、旧 VideoPlayer 或 CompositeEngine。

## 使用

1. 导入视频、音频或图片。素材库支持缩略图、文件拖入和缺失素材重新定位。
2. 选择轨道，双击素材或按“插入到播放头”；也可将素材拖入时间线。视频默认保留自带音频，图片默认持续 5 秒。
3. 拖动片段移动，拖动两端裁剪；右侧属性支持精确帧数、目标轨道、位置、缩放、透明度和音量输入。右侧“片段”列表支持键盘选择。
4. 保存 `.freecut.json` 工程或导出 H.264 + AAC MP4。导出使用开始时的工程快照，支持进度和取消。

时间线按工程帧定位，范围为左闭右开。同轨禁止重叠，删除留下空隙；视频轨按列表顺序自下向上合成。锁定保护轨道片段；隐藏只影响画面，静音影响该轨道的所有音频。缩放基于素材等比适配画布后的大小；水平、垂直偏移以画布尺寸为单位，默认居中。

快捷键：`Ctrl+I` 导入、`Ctrl+Enter` 插入、`Ctrl+B` 分割、`Delete` 删除、`Ctrl+Z` 撤销、`Ctrl+Y` 重做、`Space` 播放暂停、左右方向键逐帧、`Ctrl+S` 保存。

默认工程为 1920×1080 / 30 fps。工程设置支持偶数尺寸，最大 4096×4096，以及 1–120 fps 的有理数帧率；已有片段时帧率不可修改。输入画面同样限制为 4096×4096。首版不含字幕、转场、关键帧、滤镜、变速和硬件编码。

## Windows 构建与部署

需要 VS2022、CMake 3.24+、Qt 6.8+ MSVC x64（Widgets、Multimedia、Test），以及匹配的 FFmpeg 开发头文件与共享库。FFmpeg 必须包含软件 H.264 编码器 `libx264` 或 `libopenh264`，以及 AAC 编码器。构建脚本不自动下载依赖。

```powershell
./scripts/build-freecut-windows.ps1 -FFmpegRoot <FFmpeg开发包> -QtRoot <Qt安装目录>
# 若头文件独立存放，额外指定 -FFmpegInclude <匹配的头文件目录>
```

脚本依次配置、编译、测试、安装依赖，并启动 `--hidden --smoke` 离屏检查。默认输出为 `out/freecut-windows/dist/bin/freecut.exe`。

独立构建：

```powershell
cmake -S apps/freecut/desktop -B out/freecut -DCMAKE_PREFIX_PATH=<Qt安装目录> -DLIVEKIT_FFMPEG_ROOT=<FFmpeg开发包>
cmake --build out/freecut --config Release --parallel 4
ctest --test-dir out/freecut -C Release --output-on-failure
cmake --install out/freecut --config Release --prefix out/freecut/dist
```

根工程通过 `AV_BUILD_FREECUT_DESKTOP=ON` 启用，默认关闭；可与播放器等产品共同构建。独立应用会启用 SDK 的 `LIVEKIT_BUILD_EDITOR`。工程文件仅存素材引用，移动素材后可重新定位；更短或不兼容的替代素材会明确报错。

## 验证方式

本次构建与测试结果见 [Windows 验证记录](VALIDATION.md)。

测试使用合成视频、图片和音频，覆盖编辑事务、暂停定位、跨片段播放、轨道合成和混音、工程往返保存、取消和失败输出保护，以及实际导出文件的编解码参数、时长、画面和两路音频。Qt 使用 offscreen 平台，覆盖 1× / 2× 界面，并在构建目录 `screenshots/` 保存程序内截图。

```powershell
./out/freecut/Release/freecut.exe --hidden --screenshot <绝对截图路径>
```

`--hidden` 自动选择 offscreen 平台并关闭音频设备输出，不连接用户桌面。Windows 离屏字体需要 `QT_QPA_FONTDIR=C:/Windows/Fonts`，CTest 和构建脚本已配置。

真实扬声器延迟、显示设备、跨屏 DPI 和系统文件对话框仍需人工验收。macOS/Linux 保留可移植代码，本次不宣称完成平台构建和设备验收。软件合成和编码侧重正确性；预览过载时会暂停并提示降低工程分辨率。
