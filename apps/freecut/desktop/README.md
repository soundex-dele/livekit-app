# FreeCut 桌面剪辑器

独立 C++17 / Qt6 Widgets 应用，通过 `LiveKit::Editor` 完成多轨预览和离线导出。媒体图使用 GraphEngine；不依赖 RTC、旧 VideoPlayer 或 CompositeEngine。

## 使用

1. 导入视频、音频或图片。素材库支持缩略图、文件拖入和缺失素材重新定位。
2. 选择轨道，双击素材或按“插入到播放头”；也可将素材拖入时间线。视频默认保留自带音频，图片默认持续 5 秒。
3. 拖动片段移动，拖动两端裁剪；右侧属性支持精确帧数、目标轨道、位置、缩放、透明度和音量输入。右侧“片段”列表支持键盘选择。
4. 保存 `.freecut.json` 工程或导出 H.264 + AAC MP4。导出使用开始时的工程快照，支持进度和取消。

时间线按工程帧定位，范围为左闭右开。新工程主视频轨默认开启磁吸，插入、移动和删除自动衔接；关闭磁吸后删除留下空隙。旧版工程默认关闭磁吸。其他轨道保持自由摆放，同轨禁止重叠；视频轨按列表顺序自下向上合成。锁定保护轨道片段；隐藏只影响画面，静音影响该轨道的所有音频。缩放基于素材等比适配画布后的大小；水平、垂直偏移以画布尺寸为单位，默认居中。

时间线支持 Ctrl 多选、空白区域框选、Ctrl+C / Ctrl+V 复制粘贴（保留相对轨道关系，需要足够的目标轨道）、轨道上移/下移、缩放适配、素材首帧缩略图和平铺显示、采样峰值波形。播放器拖动修改所选片段位置，Ctrl+滚轮调整缩放。

快捷键：`Ctrl+I` 导入、`Ctrl+Enter` 插入、`Ctrl+B` 分割、`Delete` 删除、`Ctrl+Z` 撤销、`Ctrl+Y` 重做、`Space` 播放暂停、左右方向键逐帧、`Ctrl+S` 保存。

默认工程为 1920×1080 / 30 fps。工程设置支持偶数尺寸，最大 4096×4096，以及 1–120 fps 的有理数帧率；已有片段时帧率不可修改。输入画面同样限制为 4096×4096。支持手动文字、SRT 字幕、基础转场、关键帧、调色、0.25×–4× 保音高恒定变速和音频淡入淡出；不包含自动字幕识别、曲线变速或硬件编码。

## 文本、效果与工程兼容

- 文本页可添加文字或导入 UTF-8 SRT；属性区调整内容、字体、字号、颜色、黑色描边、九宫格对齐及位置。SRT 导出保留文字和时间，不保存样式；同轨字幕不允许重叠。
- 转场页提供叠化、擦除、推拉。先选择前一个片段，在属性中设置转场帧数，再应用效果。双方需要留有源素材余量；不足时显示可用帧数。转场不缩短工程，两个转场区间不得重叠。
- 关键帧支持位置、缩放、旋转、透明度和音量。先调整数值，再在播放头添加或更新关键帧；可选择线性或缓入缓出，移除按钮清除所选属性的关键帧。
- 调色支持亮度、对比度、饱和度、色温。音频支持音量和以工程帧为单位的淡入淡出。修改速度后重新计算片段长度，保持音高；自由轨冲突会拒绝整个编辑事务。
- `.freecut.json` 使用 v2，兼容加载 v1，保存时升级；未知未来版本明确拒绝。保存采用临时文件加原子替换。旧程序不能加载 v2。

## nndeploy 集成

聚合仓库的 `nndeploy-flow` 子模块以 `13cce6f2ec230b2a5082d6cff752803818daf7f0` 为基线，工作区包含本次原生媒体嵌入改动。单独检出 SDK 时，可通过 `LIVEKIT_NNDEPLOY_ROOT` 指定对应源码。

嵌入目标 `nndeploy_flow::media` 复用宿主 GraphFlow targets，只编译模型无关的图像、音频和特效节点；不需要 Python、HTTP 服务或模型。完整 nndeploy 构建保持原有路径。媒体目标不包含 Tensor 分割事件桥接。

每个预览或导出 `RenderGraph` 只有一个主 GraphEngine。`EffectNodes` 根据工程生成节点与连接计划，不创建嵌套 GraphEngine。`ColorGrade`、`Transition`、`TimeStretch` 是直接注册到主图的 nndeploy 算法节点；饱和度/对比度、音频淡入淡出、libass 文字渲染也是独立节点。RGBA↔RGB、float PCM↔`AudioEvent` 由专门的转换节点完成，转换节点不承担算法处理。

处理顺序直接由图的连接表示。decode 只解码媒体并输出 `Layer`，独立的 `editor.route` 按片段 ID 选择处理链：

```text
timeline → decode → route → 当前片段的处理链 → composite → mix → result_route → output
                                                                └→ 转场链 → output
```

每个片段的处理链按启用的功能连接，没有启用的节点直接跳过：

- 调色：`RGBA转RGB → ColorGrade → 饱和度/对比度 → RGB转RGBA`，保留源 alpha。
- 变速：`float转AudioEvent → TimeStretch → AudioEvent转float → AudioWindow`。AudioWindow 缓存处理后的固定音频块并裁出当前帧需要的样本。
- 淡入淡出：`AudioFade` 位于变速/样本裁剪之后。
- 文字：文字片段经过 `TextRender` 输出图层，然后进入 composite。文字图像缓存有数量与字节上限。
- 转场：两侧片段先沿各自处理链完成合成与混音，再经 `result_route → TransitionJoin → 双输入RGBA转RGB → Transition → RGB转RGBA → TransitionCommit → output`。Join 的两个输入槽属于当前帧事务，到齐才发布下游事件，不跨帧或跨 generation 拼接图层。

转换终点恢复 `Layer` 后直接发布下一条边，decode 不调用效果方法、不等待写回再恢复流程。隐藏画面、静音或变速缓存命中通过显式 `bypass` 边继续，保证处理链最终到达输出。边声明 `Layer`、`MediaFrameEvent` 或 `AudioEvent` 类型；算法之间的转换关联记录持有当前 Work，并在转换完成或整帧失败时释放。

所有节点和边属于同一个主 GraphEngine。无效果时有 7 个公共节点（timeline、decode、route、composite、mix、result_route、output），效果节点按片段增删。独立路由节点只选择通路，具体算法执行顺序由边连接决定。

编辑器主体保持禁异常编译；仅私有第三方适配库捕获 nndeploy/OpenCV 异常并返回明确错误。处理是有界同步调用，取消在媒体块之间检查；没有宣称实现 GPU 加速或大型工程实时性能指标。

## 编辑时热重载

播放中修改属性、增删片段或效果、撤销/重做会保留播放状态，更新完成后继续推进。暂停中编辑保持暂停；新建/打开工程、主动定位和导出仍按对应操作暂停。UI 按工程 generation 丢弃旧帧与旧结束/错误通知，清掉旧 PCM 后接收新工程输出。

`EditorSession` 的同一工作线程串行执行渲染和工程更新：正在执行的一帧先结束，再调用 `reloadFromNodesDetailed` 切换主图订阅和连接。节点 ID 由片段 ID、算法和职责组成；同 ID、同参数复用实例，参数变化替换对应算法，启用/移除效果增删整条分支。新增节点 init/start，移除节点 stop/release，保留节点只重建订阅。文字或素材派生缓存失效后按需重建；路径、元数据与磁盘时间戳/大小未变的解码器保留。

工程校验失败保留原图；重载失败返回明确错误并停止本次续播，恢复失败或提交后清理失败会禁止继续使用状态不确定的图。`set_project(..., play_after_update=true)` 在渲染指定位置后从下一帧续播，默认参数仍保持暂停语义。导出持有独立工程快照和主图，不跟随预览编辑。

当前图沿边同步派发，帧与工程更新在同一工作线程串行执行，不使用节点私有任务队列。事件持有图像、音频和已求值片段的生命周期；轨道与素材仍引用当前工程。以后引入异步算法时，需要进一步扩展工程快照所有权、关联表同步、背压及更新时队列排空机制。编辑期间保持播放状态，但重载、缓存重建和音频缓冲重置可能带来短暂画面延迟或音频间隙；尚未保证无缝声学输出。

## Windows 构建与部署

需要 VS2022、CMake 3.24+、Qt 6.8+ MSVC x64（Widgets、Multimedia、Test），OpenCV 4.x 开发包，以及匹配的 FFmpeg 开发头文件与共享库（包含 avfilter）。FFmpeg 必须包含软件 H.264 编码器 `libx264` 或 `libopenh264`，AAC 编码器和基于 libass 的 `subtitles` 滤镜。中文渲染需要系统安装所选字体，默认 Microsoft YaHei；缺失时由 libass 选择可用字体。构建脚本不自动下载依赖。

```powershell
./scripts/build-freecut-windows.ps1 -FFmpegRoot <FFmpeg开发包> -QtRoot <Qt安装目录> -OpenCVDirectory <包含OpenCVConfig.cmake的目录>
# 若头文件独立存放，额外指定 -FFmpegInclude <匹配的头文件目录>
```

脚本依次配置、编译、测试、安装依赖，并启动 `--hidden --smoke` 离屏检查。默认输出为 `out/freecut-windows/dist/bin/freecut.exe`。

独立构建：

```powershell
cmake -S apps/freecut/desktop -B out/freecut -DCMAKE_PREFIX_PATH=<Qt安装目录> -DLIVEKIT_FFMPEG_ROOT=<FFmpeg开发包> -DOpenCV_DIR=<OpenCV配置目录>
cmake --build out/freecut --config Release --parallel 4
ctest --test-dir out/freecut -C Release --output-on-failure
cmake --install out/freecut --config Release --prefix out/freecut/dist
```

根工程通过 `AV_BUILD_FREECUT_DESKTOP=ON` 启用，默认关闭；可与播放器等产品共同构建。独立应用会启用 SDK 的 `LIVEKIT_BUILD_EDITOR`。工程文件仅存素材引用，移动素材后可重新定位；更短或不兼容的替代素材会明确报错。

## 验证方式

本次构建与测试结果见 [Windows 验证记录](VALIDATION.md)。

测试使用合成视频、图片和音频，覆盖编辑事务、暂停定位、跨片段播放、轨道合成和混音、工程往返保存、取消和失败输出保护，以及实际导出文件的编解码参数、时长、画面和两路音频。热重载测试对比新建图的像素/PCM，检查真实主图中的算法与转换节点、参数替换和节点复用，并验证播放中效果增删。Qt 使用 offscreen 平台，覆盖 1× / 2× 界面，并在构建目录 `screenshots/` 保存程序内截图。

```powershell
./out/freecut/Release/freecut.exe --hidden --screenshot <绝对截图路径>
```

`--hidden` 自动选择 offscreen 平台并关闭音频设备输出，不连接用户桌面。Windows 离屏字体需要 `QT_QPA_FONTDIR=C:/Windows/Fonts`，CTest 和构建脚本已配置。

真实扬声器延迟、显示设备、跨屏 DPI 和系统文件对话框仍需人工验收。macOS/Linux 保留可移植代码，本次不宣称完成平台构建和设备验收。软件合成和编码侧重正确性；预览过载时会暂停并提示降低工程分辨率。
