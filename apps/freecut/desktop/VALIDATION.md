# FreeCut Windows 验证记录

日期：2026-11-11。环境：Windows x64、VS2022 / MSVC 19.44、Qt 6.8.3、FFmpeg 7.1 共享库及匹配头文件，Release 构建。

## 自动化结果

| 检查 | 结果 |
| --- | --- |
| FreeCut 独立配置、编译、测试 | 10/10 CTest 通过 |
| 根工程同时构建 FreeCut 和播放器 | 构建通过，16/16 CTest 通过 |
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

## 本轮专业版布局与功能重构验证

2026-11-11 更新，使用 OpenCV 4.10 和 nndeploy-flow 原生媒体目标，模型与 Python 服务均未启用。

- 工程 v2 保存/读取、v1 非磁吸迁移、未来版本拒绝、主轨闭隙和一次事务撤销通过。
- 动画分割切点连续性、效果参数往返、非法转场余量拒绝通过。
- 原生 ColorGrade 的去饱和像素检查、Transition 的首尾和中间混合像素检查通过。
- 原生 TimeStretch 在 2× 速度下保持 440 Hz 测试音高；音频重新定位与新会话渲染一致。
- FFmpeg/libass 实际渲染 `FreeCut 中文`，字幕改变输出像素；实际 MP4 重新解码与预览画面的平均差异在有损编码容差内。
- Qt 离屏覆盖 SRT 中文往返、无效时间拒绝、Ctrl 多选拖动的一次事务与撤销、框选，以及原有编辑、拖放和 1×/2× 截图检查。
- 每个 RenderGraph 独立持有 EffectSession；同参数节点图在会话内复用，每类最多 8 个。图像转换和音频分块仍使用 CPU，未进行大型工程或 4K 实时性能验收。
- Windows 构建脚本完成编译、10/10 CTest、Qt/FFmpeg/OpenCV/MSVC 依赖安装和安装程序隐藏启动。安装程序加载带中文字的工程并输出 `screenshots/advanced-deployed.png`，退出码为 0。
- 联合构建 16/16 CTest 通过；播放器测试需要 Qt `bin` 加入 PATH 和 `QT_QPA_FONTDIR`。第一次直接运行缺少 Qt DLL 搜索路径而失败，补齐测试环境后通过，无需修改播放器代码。
- clang-format 20.1.8、Git 空白检查和 SDK 禁异常契约检查通过。第三方适配文件单独编译，只捕获外部异常，项目自身错误使用显式返回值。

可编辑的综合测试工程和导出视频位于 `out/freecut/sdk/src/engines/editor/fixtures/advanced.freecut.json` 与 `advanced.mp4`。素材为合成测试媒体，不代表真实摄影素材的主观质量验收。

## 2026-11-13 热重载与算法平铺验证

- Release 构建 `livekit_editor_tests`、`freecut_ui_tests` 和 `freecut` 成功；独立构建目录 `out/freecut` 的 **11/11 CTest 通过**。本轮未重新执行安装/部署或联合播放器构建。
- `editor_hot_reload` 检查真实主 GraphEngine 内的节点与边：ColorGrade、饱和度/对比度、Transition、TimeStretch、AudioFade 及独立格式转换节点；添加、参数替换、移除、重新添加和删除所属片段均通过。
- 同参数算法与转换节点实例复用；改变亮度仅替换 ColorGrade，改变转场算法保留转换器；无效果工程保留 7 个公共阶段（含片段路由和结果路由）。
- 热图与新建图逐帧 RGBA 相同、PCM 平均绝对差小于 0.0001；覆盖调色、两片段隔离、转场、变速、淡入、文字修改、画布/帧率/源位置修改以及素材重新定位。
- 未变素材的解码器保留；同 ID 改路径、同路径改文件时间戳、移除素材会失效或回收旧解码器。重复与重叠音频请求、AAC 开头预热包的复用回归通过。
- Session 更新后从新 generation 的下一帧续播，无重复帧、旧 generation 回流；无效更新保留旧工程，末尾更新触发一次结束事件。
- Qt offscreen 测试在播放中选择片段、添加和移除调色效果、连续编辑透明度后帧号仍推进；暂停编辑保持暂停，打开另一个工程暂停。原有多轨、导出和 1×/2× 离屏测试通过。
- clang-format 20.1.8、Git 空白检查与 SDK 禁异常契约检查通过；nndeploy/OpenCV 仍只在私有适配库中处理异常，编辑器主体保持禁异常编译。

- 后续图调度重构：decode 不再调用算法入口。测试检查 `decode → route → 转换/算法 → composite`、调色到变速/AudioWindow 的连续连接以及 `result_route → TransitionJoin → 转场 → TransitionCommit → output`；不存在 decode 直达 composite 的旁路。
- 算法边与缓存旁路声明不同事件类型，覆盖变速缓存命中、隐藏画面、静音、同帧转场双输入汇合；文字缓存继续受 8 张/128 MiB 上限约束。

本轮日志：`out/freecut/Testing/Temporary/LastTest.log`；合成媒体和导出结果位于 `out/freecut/sdk/src/engines/editor/fixtures/`（路径相对 livekit-app）。所有验证均为自动化，无桌面操作或真实音频设备人工验收。播放状态连续不等于无声学间隙；重载和 PCM 重置期间的设备听感、复杂工程实时性仍待人工/性能验证。

## 尚需人工及其他平台验证

自动化没有操作用户桌面或打开音频设备。真实扬声器输出与延迟、显示设备、跨屏 DPI、系统文件对话框需要单独人工验收。macOS/Linux 本次未构建或验收。软件合成和编码尚未进行大型工程性能基准测试。

当前 Qt 安装没有翻译资源目录，部署工具因此提示无法部署 Qt translations；应用自有中文界面已由离屏截图验证。
