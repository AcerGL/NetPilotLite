# NetPilot Lite 1.4.0 私有仓库更新

此版改用私有 GitHub Release 更新 ZIP，不生成 Web Setup，也不在更新时运行完整安装器。主程序仍是 `NetPilotLite.exe`，用户数据仍在 `%LOCALAPPDATA%\NetPilotLite\`。

## 首次使用

1. 将本版带签名侧文件的 `NetPilotLite.exe` 放到长期使用的位置，或使用首次安装包 `NetPilotLite-Setup-x64.exe`。已有运行实例应先正常退出。不要只搬走 EXE 的 `.update` 文件；后续在线更新会自动维护该文件。
2. 打开 **设置 → 更新**，确认仓库为 `AcerGL / NetPilotLite`。
3. 点击“打开 GitHub Token 设置”。创建 **Fine-grained personal access token**，Resource owner 选择自己的账户，Repository access 只选 **NetPilotLite**，Repository permissions 只添加 **Contents: Read-only**。GitHub 自动附带的 Metadata: Read 无需更改。不需要 Actions 或任何写权限。
4. 把 Token 粘贴到软件的密码输入框，点击“验证 / 更换授权”。不要发到聊天、日志或 Issue。软件验证仓库身份及 Release 列表读取能力后才保存；权限范围仍需在 GitHub 页面确认，API 不能证明 Token 没有额外权限。
5. 确认“随 Windows 登录启动”和“每个 Windows 开机周期自动检查一次”已勾选。首次普通启动会注册当前用户 Run 启动项；旧配置显式关闭自动检查时尊重该偏好，请自行勾选。
6. 点击“检查更新”进行首次检查。有版本时点击下载并安装；以后每个新启动周期后台检查，出现新版本可选择立即更新、稍后提醒或忽略此版本。

本次审计确认仓库是 Private，但最新稳定 Release 接口返回 404。目前仍需配置可信发布环境并发布首个新版 Release。**GitHub 连接器可读取仓库不代表桌面软件已获得 Token 授权。**

Token 存在 `github-auth.dpapi` 中，使用 Windows DPAPI CurrentUser 加密并绑定 owner/repository。更换授权只有验证成功才替换旧记录。“移除本机授权”删除加密记录；要让 Token 在所有设备失效，请在 GitHub Token 设置中撤销。软件不会使用只读 Token 调用撤销或写入 API。

## 更新机制

- 普通 GUI 使用 asInvoker，无须开机 UAC；用户主动操作网卡时通过同一 EXE 的固定参数入口申请 UAC，重新验证 GUID、PnP 身份、物理设备及远程会话条件。拒绝 UAC 会显示错误，不进行网卡操作。
- 当前用户 `HKCU\Software\Microsoft\Windows\CurrentVersion\Run\NetPilotLite` 指向固定 EXE 加 `--startup`，登录时只进托盘。不创建服务或计划任务。
- 动态查询 Windows BootIdentifier，并在 `update-state.ini` 保存完成标识、重试次数和忽略版本。一次启动周期最多三次尝试，失败后等待至少 30 秒、90 秒；完成后同周期重启程序不再自动提示。休眠/快速启动按 Windows 内核返回的启动标识判断；没有为测试实际重启电脑。获取标识失败时关闭本轮自动检查，保留手动检查。
- `GET /repos/{owner}/{repo}/releases/latest` 只接受非 draft、非 prerelease、严格稳定语义版本与固定 x64 资产。401 提示重新授权；404 会进一步检查仓库和 Release 列表权限，不能当作“已是最新版”。
- 私有文件从 `api.github.com/repos/.../releases/assets/{id}` 下载。Bearer 只发到 `api.github.com`；允许的 HTTPS GitHub 资产 CDN 重定向不携带 Token。使用 Windows 自动代理；不更改 Clash、DNS 或系统代理。下载取消可能等待当前 WinHTTP 调用超时；失败可重试完整下载，尚未实现字节范围续传。
- 独立 GUID 缓存目录中先下载清单与签名，再下载 ZIP。验证固定 ECDSA P-256 公钥签名、清单版本/架构/大小、ZIP SHA-256、窄格式 ZIP，以及内部 EXE 的产品、x64、版本和独立签名。任何检查失败都不执行文件。SHA-256 不是发布者认证依据。
- ZIP 仅包含 `NetPilotLite.exe` 和 `NetPilotLite.exe.update`。独立更新器已经嵌入主程序，执行时提取到临时位置；同时交付命名为 `NetPilotUpdater.exe` 的独立副本。无需下载完整安装器。
- 更新器正常请求旧实例退出；备份、同目录 staging、ReplaceFile 与持久日志；新版通过启动事件报告配置和界面已初始化。失败恢复旧 EXE 并启动旧版；未完成事务在下次启动时恢复。不强杀全部 NetPilot 进程。
- 普通可写目录无需额外 UAC；受保护目录申请 UAC。为保护 CurrentUser 配置，拒绝在此流程中改用另一个管理员账户。使用当前账户批准 UAC 后，新版可能继承管理员权限；下次 Windows 登录按普通权限启动。跨账户管理员更新尚不支持。
- 更新只替换程序及签名侧文件。网卡状态、配置、主题、背景、绑定、分流策略、日志和订阅源都不在更新包中。

## 构建与签名

Visual Studio 2022 C++、Windows SDK、Python 3（仅构建期）即可。主程序 C17、Win32、Release x64、静态 CRT。

```powershell
./build.cmd
./scripts/build-update.ps1 -SigningKey C:\TrustedSigner\private.dpapi -Tag v1.4.0
# 如需第一次安装的安装包，额外指定已验证的 Inno Setup 7 编译器：
./scripts/build-release.ps1 -SigningKey C:\TrustedSigner\private.dpapi -ExpectedTag v1.4.0 -FirstInstall -InnoCompiler C:\TrustedTools\ISCC.exe
```

正式资产在 `dist/update/`：更新 ZIP、`.zip.sha256`、版本 JSON、`JSON.update`（128 字节签名记录）、`SHA256SUMS.txt`。更新 ZIP 不包含安装器或用户数据。

现有信任根 `assets/update-key.bin` 的 SHA-256：
`635fb466ec7cd573b10cea71adc0ea5825ca62d92e554636b0e7d3334685d820`。
签名记录包含用途标识、四段 Windows 版本、文件大小、SHA-256、64 字节 ECDSA 签名；签名覆盖前面的所有字段。清单用 NPLCAT1，程序用 NPLREL1，不能相互替代。此签名机制独立于 Authenticode；目前没有配置 Authenticode 代码签名证书。

GitHub Actions 的 `release-signing` environment 需要 Secret **NETPILOT_SIGNING_KEY_B64**，内容是与现有公钥匹配的 104 字节 Windows CNG BCRYPT_ECCPRIVATE_BLOB 的 Base64。工作流只在签名步骤注入，工具验证公钥一致；不能用新密钥替代而指望旧客户端接受。现有本机 DPAPI 私钥不能原样搬到 GitHub hosted runner；应在可信离线环境中由密钥持有者完成受控迁移。本次没有解密导出私钥、设置 Secret 或生成冒充现有身份的新密钥。

`.github/workflows/release.yml`：稳定标签 → Windows 构建与测试 → 受环境保护的签名 → 包验证及回滚测试 → `release-publication` 环境 → 私有 Draft Release → 上传回读比对 → 发布稳定 Release。建议在 GitHub 为两个 environment 配置 required reviewers；YAML 声明环境不代表平台已配置审批。没有密钥会失败停止。普通 main/PR 工作流只产出未签名审阅构建，客户端不会接受它。

本地发布脚本默认只验证，只有显式 `-PublishApproved` 才写 GitHub；不会修改仓库可见性。既有标签 Release 不覆盖，上传或回读失败留下未发布 Draft 供诊断。正式发布不上传 Web Setup 或首次安装器。

当前工作区没有 `.git`，不能声称从本地 remote 读取了仓库；本次使用已有配置及 GitHub 连接器核验身份。正常 Git clone 可运行 `scripts/detect-repository.ps1 -WriteDefaults`，从 origin 解析 owner/repo、通过已有 gh 身份读取 private 状态并生成默认仓库头文件。应用设置仍允许修改。

## 人工真实验收

先在软件内完成授权，并在可信发布环境提供更高版本的正式签名 Release。随后验证一次 Windows 重启、发现新版本提示、稍后/忽略、确认下载、受保护目录 UAC 取消、同路径更新与新版启动。保持 Clash TUN 开启并检查配置前后哈希；全过程不需要禁用任何网卡。真实用户 PAT、真实 Windows 重启和 Program Files UAC 更新未在本次自动测试中执行。
