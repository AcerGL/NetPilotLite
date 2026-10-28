# 统一更新策略

安装身份决定部署方式，下载/来源策略/签名验证为同一套 `src/online.c` 与 `src/trust.c`：

- 当前运行路径与固定 HKLM Inno AppId 登记一致：设置内在线更新、本地文件更新均使用 **Offline Setup**，由同一个安装器维护文件、快捷方式和卸载登记。
- 未注册的便携版：继续使用现有经过测试的原子 EXE 更新器，固定路径和 EXE 名称；不会暗中转成安装版。迁移需运行 Offline Setup 并明确选择。
- Web Setup：同一套 Offline Setup 下载与签名验证，普通权限下载，标准 UAC 启动安装器。下载完成后清理本次唯一缓存目录；程序崩溃遗留缓存不会被执行。

固定发布仓库来自用户确认的 `AcerGL/NetPilotLite`。普通应用仍保留配置仓库功能，但任意仓库不能绕过内置发布公钥。TLS 证书验证保持开启；仅允许指定 GitHub 资产地址及其官方资产 CDN 重定向，不改变代理设置。

签名为 ECDSA P-256 / SHA-256，128 字节小端清单：magic[8]、version[4]、size、hash[32]、signature[64]。签名覆盖前 64 字节摘要。三个用途隔离：主程序 `NPLREL1`、离线安装器 `NPLOFF1`、资产目录 `NPLCAT1`。不能用主程序签名授权执行安装器，不能用 Web Setup 代替 Offline Setup。

Offline Setup 的 VERSIONINFO ProductName 为 `NetPilot Lite Offline Setup`；Inno 在该字段末尾填充空格，验证仅移除尾部空格，仍严格检查产品名。主程序旧签名格式和信任公钥保持兼容。

下载显示真实字节/总量/百分比/速度，独立工作线程；提供取消和显式重试。WinHTTP 的单步超时 5/10/15/20 秒，总下载期限 4 分钟；取消最坏需等待正在进行的请求返回。安装开始后的取消由 Offline Setup 控制。相同版本的修复确认交给安装器，默认不降级。

当前未验证真实 UAC 取消、Program Files 覆盖、安装过程中断及卸载；详细矩阵见 INSTALLER_ACCEPTANCE.md。便携版更新的模拟进程/文件验证不等于安装器验收。
