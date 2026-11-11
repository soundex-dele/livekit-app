# FreeCut Windows 验证记录

日期：2026-11-11。环境：Windows x64、VS2022 / MSVC 19.44、Qt 6.8.3、FFmpeg 7.1 共享库及匹配头文件，Release 构建。

## 自动化结果

| 检查 | 结果 |
| --- | --- |
| FreeCut 独立配置、编译、测试 | 9/9 CTest 通过 |
| 根工程同时构建 FreeCut 和播放器 | 构建通过，15/15 CTest 通过 |
| 编辑模型 | 分割、裁剪、跨轨移动、重叠拒绝、锁定保护、撤销重做和保存状态通过 |
| 工程文件 | 版本化 JSON、往返保存、缺失素材引用保留、无效输入拒绝通过 |
| 时间线媒体 | 暂停定位、200 次连续定位合并、片段边界、空隙黑画面/静音、延迟视频时间戳及连续播放通过 |
| 多轨合成 | 缩放、透明度、叠层、隐藏、静音和 44.1 kHz 音频重采样混音通过 |
| 导出 | 实际 H.264 / AAC、48 kHz 双声道、画面尺寸、30 fps 与 30000/1001 fps、时长、三个时间点画面和两路音频频率通过 |
| 取消及错误 | 取消保留已有成片、源素材覆盖拒绝、损坏/缺失媒体、无效输出路径及临时文件清理通过 |
| Qt Widgets | 素材导入、分割、属性编辑拒绝、工程重载、预览输出、导出反馈及空工程错误通过 |
| Qt 时间线交互 | 跨轨拖动、左端裁剪、一次拖动一次提交、锁定阻止拖动、素材拖放及时间尺拖动通过 |
| UI / 回归 | FreeCut 和共享控件 1×/2× 离屏检查、隐藏原生标题栏检查、播放器回归通过 |
| Windows 安装 | Qt、FFmpeg 及 MSVC 运行库部署完成，部署后的 `freecut.exe --hidden --smoke` 返回 0 |
| 静态检查 | clang-format 20.1.8、变更空白检查、SDK 异常传播约束检查通过；编辑引擎使用禁异常编译 |

构建脚本实际执行了配置、编译、CTest、安装和部署程序离屏启动。

本地证据（相对 livekit-app 根目录）：

- 独立构建测试日志：`out/freecut/Testing/Temporary/LastTest.log`。
- 联合构建测试日志：`out/freecut-products/Testing/Temporary/LastTest.log`。
- 空工程、编辑工作区和 HiDPI 截图：`out/freecut/screenshots/`。
- 合成媒体、实际导出文件：`out/freecut/sdk/src/engines/editor/fixtures/`。
- 可运行部署程序：`out/freecut/dist/bin/freecut.exe`。分发时需要携带整个 `dist` 目录。

## 尚需人工及其他平台验证

自动化没有操作用户桌面或打开音频设备。真实扬声器输出与延迟、显示设备、跨屏 DPI、系统文件对话框需要单独人工验收。macOS/Linux 本次未构建或验收。软件合成和编码尚未进行大型工程性能基准测试。

当前 Qt 安装没有翻译资源目录，部署工具因此提示无法部署 Qt translations；应用自有中文界面已由离屏截图验证。
