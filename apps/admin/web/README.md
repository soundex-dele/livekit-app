# AV 运营管理后台

统一入口为 `/apps/admin/`，由业务服务的 `AV_ADMIN_WEB` 指向本目录的 `dist`。使用现有 Foundation 登录会话；平台管理员可以选择任意组织治理，组织管理员仅能管理所属组织。媒体画面和远控输入不会经过后台页面。

## 构建

要求 Node.js 24、npm 11，以及相邻的 `platform-foundation` 源码。四个 Foundation 包通过公开 tarball 引入，未使用源码 alias 或远程私有 registry。源码摘要、tarball 完整性和 npm 依赖分别由 `foundation-web.lock.json`、`package-lock.json` 固定。

在 `livekit-app` 运行：

```sh
node scripts/build-admin.mjs
```

脚本从锁定 Foundation 源码构建公开包，校验 tarball，安装依赖，检查 OpenAPI 类型，执行单元测试并生成 `dist`。可用 `--foundation-root <目录>` 指定完整 Foundation 仓库，路径不会写入锁文件。已有依赖可用 `--skip-install`。

审核 Foundation 源码变更后更新来源锁和 npm 完整性：

```sh
node scripts/build-admin.mjs --update-foundation-lock
```

后端更新合并契约后，在本目录运行 `npm run generate` 并审核生成类型。共享 DTO 直接引用生成的 `AVAdmin*` schemas；业务详情的开放 map 在 `src/types.ts` 描述。

## 开发与检查

```sh
npm run dev
npm run verify
npm run test:e2e
```

开发入口为 `http://127.0.0.1:3040/apps/admin/`，`/api` 默认代理到 `http://127.0.0.1:8080`，可通过 `AV_PROXY_TARGET` 覆盖。业务服务的 `PF_PUBLIC_ORIGIN` 必须与浏览器源一致。

`test:e2e` 使用 Playwright 无窗口浏览器与受控接口 fixture。可设置 `AV_CHROME_PATH` 指向已安装的 Chromium headless shell；测试不会操作用户桌面。截图位于 `out/screenshots`，失败跟踪位于 `out/playwright`。真实服务验证脚本为 `tests/real-service.mjs`，由平台集成测试负责预置测试服务和账号。

操作确认要求原因，失败重试沿用同一请求内容和幂等键。页面区分业务已更新、媒体操作排队与清理失败；有任务管理权限时可以重试关联任务。范围切换取消请求并丢弃旧响应。节点配置只读，远控授权只能撤销，不能由管理员替代本地同意。

自动化检查证明对应接口和无窗口页面行为，不代表实际音视频设备人工验收。
