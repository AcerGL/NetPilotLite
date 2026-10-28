# 安装器发布

当前本地版本 1.3.0.0；固定 AppId `{C47B553D-22CE-4CDD-BA10-2930FC8266D4}`。不发布新版本号目录，不改变 NetPilotLite.exe 文件名。

## 本地构建

Visual Studio 2022 C++ / Windows SDK / Inno Setup 7.1.0 x64。原 Solution 包含主程序、测试、便携更新器，并新增 WebSetup 与 InstallGuard 项目。也可：

```powershell
.\scripts\build-release.ps1 -SigningKey '绝对路径\private.dpapi' -InnoCompiler '绝对路径\ISCC.exe'
```

现有私钥由 DPAPI 保护，必须在相同受信任签名账号/机器使用；不复制到仓库、不重新生成公钥，不输出秘密。`installer/build.cmd` 单独构建原生安装辅助 DLL 与下载器。Inno 辅助 DLL 只从包内提取到临时目录，不部署常驻组件。

输出 Web、Offline、Portable 三种固定命名资产，以及 Offline `.update`、兼容旧便携版更新所需的主 EXE/`.update`、version.json、SHA256SUMS.txt 和 release-catalog.json/.update。目录清单用相同公钥但独立用途 magic 签名，覆盖 Web/Offline/ZIP/兼容文件的名称、大小和 hash。SHA-256 文件本身不作为发布者身份证明。

## GitHub Actions

`.github/workflows/release.yml` 仅显式 `v*` tag 或手动构建触发，不随普通 push 正式发布。

1. 普通 Windows runner 构建无签名主程序/Web 并运行只读协议配置测试，标记为 review-only。
2. 独立 `release-signing` 环境审批和受信任 Windows runner：要求 RELEASE_SIGNING_ENABLED、LICENSE_CONFIRMED、ASSETS_CLEARED；使用 SIGNING_KEY_PATH 和 INNO_COMPILER 构建签名三种资产、验证签名及模拟下载，校验 tag=源版本。
3. `release-publication` 独立环境审批：RELEASE_ALLOWED 必须显式开启，ACCEPTED_OFFLINE_SHA256 必须匹配上传的确切 Offline Setup，所有资产校验通过后才运行 gh release create。

需在 GitHub 实际设置环境审批人、runner、安全变量及 tag 保护；YAML 不能代替仓库管理设置。本次没有推送源码/tag，没有执行 Actions，没有创建 Release，没有改变仓库公开性。

## 未满足的正式发行条件

- 没有隔离 Windows 安装/升级/卸载/完整故障恢复验收证据。
- 指定仓库 latest API 当前返回 404，未能真实下载已发布 Offline Setup。404 可能意味着私有/无权访问/没有稳定 Release。
- 没有 Authenticode 商业代码签名证书；下载执行信任由内置 ECDSA 发布公钥提供，首次手动下载的 Setup/主程序可能显示未知发布者/SmartScreen。不得要求用户关闭防护。
- Inno 商业使用授权、应用 LICENSE 和插图分发权仍需发布前确认；保留 assets/NOTICE.md。
- 自动清空个人配置和未知 sidecar 自动迁移没有实现；卸载提供手动清理入口，迁移遇到未知 sidecar 会停止。
