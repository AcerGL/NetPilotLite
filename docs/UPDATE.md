# 本地及 GitHub 在线更新

版本：1.2.0 / Win32 1.2.0.0。正式 EXE 始终为 `NetPilotLite.exe`。

在“设置 → 更新”配置 Owner `AcerGL`、Repository `NetPilotLite`。这是用户明确提供的仓库，不是推测地址。更新频道固定 Stable；启动检查默认关闭。点击检查只发送 HTTPS GET，不下载安装。只有点击下载、签名验证通过、再点击安装并确认后，才会启动已有的独立更新器。

配置保存于 `%LOCALAPPDATA%\NetPilotLite\settings.ini` 的 Update 节，包含 Owner、Repository、Channel、CheckOnStartup、LastCheck。SchemaVersion 保持 2，以追加字段兼容；保存沿用临时文件/备份机制，旧版回退保留未知键。网卡绑定、主题、图片、窗口和应用身份规则继续保留。

协议：`GET https://api.github.com/repos/{owner}/{repo}/releases/latest`，WinHTTP 自动代理、系统 TLS 证书验证、有界超时。后台线程读取 JSON，GUI 不等待网络。Stable 使用三段整数 SemVer，可带 `v` 前缀，拒绝前导零、预发布、构建元数据及第四段。四段 Windows 文件版本的最后一段必须为 0，下一次发布增加 SemVer。

固定下载资产：`NetPilotLite.exe` 和 `NetPilotLite.exe.update`。忽略源码 ZIP，不解压远端内容。清单固定 128 字节，EXE 上限 64 MiB，API JSON 上限 1 MiB、深度 32、32768 tokens，日志最多 16383 UTF-16 字符。超限拒绝，不截断为有效数据。

先验证清单固定公钥签名，再下载 EXE；之后验证 SHA-256、大小、版本、产品、x64。存在 GitHub `sha256:` digest 时也必须匹配。缓存为用户目录下独立随机 UpdateCache GUID 目录；固定文件名、CREATE_NEW、不覆写既有缓存文件。取消或失败删除本次已知缓存文件；成功交给更新器后可能保留缓存，人工可在升级完成后清理。不会创建新的正式运行目录。

更新日志使用本机 RichEdit 展示 Markdown 文本及标题强调，保留列表/链接语法；不执行 HTML，不打开远端链接。这是有限 Markdown 展示，不是完整浏览器渲染器。

404 明确表示私有/无权访问/没有已发布稳定版之一，不能断言已是最新版。403/429 显示限流及可用的 Retry-After、Reset；407 提示代理身份验证，程序不改代理。私有仓库需要额外认证方案，本版不保存 PAT；当前可以继续使用本地签名更新。

真实回归边界：执行了本机只读 API 请求，返回 404；尚无正式 Release，未完成真实 GitHub 资产下载到生产程序升级的端到端验收。模拟 HTTP 下载验证、真实 Windows 文件替换/回滚与真实公网下载分别报告，不能相互替代。

协议依据：[GitHub Releases API](https://docs.github.com/en/rest/releases/releases)、[WinHTTP options](https://learn.microsoft.com/en-us/windows/win32/winhttp/option-flags)。
