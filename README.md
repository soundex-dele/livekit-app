# livekit-app

AV Platform 的客户端应用仓库。

当前仅初始化仓库，尚未实现应用功能；技术栈、构建方式和运行入口待确定。

## 规划职责

- 页面、窗口布局、用户交互与系统授权引导。
- 通过业务服务完成账号、设备、会议与远控授权等业务操作。
- 通过 SDK 使用采集、传输、播放与剪辑能力。

客户端使用业务服务签发的短期 Token，不保存媒体服务 API Secret。

## 关联仓库

- [av-platform](https://github.com/soundex-dele/av-platform)：聚合仓库与平台架构文档。
- [livekit-sdk-cpp](https://github.com/soundex-dele/livekit-sdk-cpp)：原生 C++ RTC SDK。
- [livekit-server](https://github.com/soundex-dele/livekit-server)：业务服务。
- [livekit-cpp](https://github.com/soundex-dele/livekit-cpp)：媒体服务。
