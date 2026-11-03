# NetPilot Lite 1.5.0 · 单 EXE 在线安装与原地更新

当前用户入口为 **NetPilotLite-Setup-x64.exe**：原生 C17/Win32、公开稳定 Release、签名 manifest、逐文件下载、同目录覆盖、启动验证及失败回滚。无需 Token，不生成 ZIP 用户安装包或更新包。

配置仍在 `%LOCALAPPDATA%\NetPilotLite`；保留 Princess 主题、托盘、设备控制及只读分流诊断。默认启动及更新均不切换网卡。

- [安装、更新、签名与构建](docs/PUBLIC_SETUP.md)
- [验收结果及未验证限制](docs/PUBLIC_SETUP_TEST_REPORT.md)
- Visual Studio Solution：`NetPilotLite.sln`，Release / x64。
- 本地发布文件：`dist/public`；用户只需下载其中的 Setup。

**当前状态：Windows 本地文件安装/覆盖/回滚验证已执行；公开 GitHub Release 在线端到端验收尚未完成。仓库仍为 Private，尚未发布 1.5.0。**

以下章节为历史记录。其私有 Token、ZIP、Inno 及 Web Setup 分发说明不再适用于当前版本，以以上 1.5.0 文档为准。

---
# NetPilot Lite 1.4.0 · 私有 GitHub 更新

私有仓库授权、DPAPI CurrentUser 加密保存、每个 Windows 开机周期后台检查、签名 ZIP 下载和独立更新器原地替换。保留 Princess 主题、托盘、网络控制及分流功能；不生成 Web Setup。

**本地构建和分层测试通过；真实私有 Release 在线升级尚待 Token、可信签名 Secrets 和正式 Release。**

使用说明：[私有更新与首次授权](docs/PRIVATE_UPDATES.md)。结果：[本次测试报告](docs/PRIVATE_UPDATE_TEST_REPORT.md)。

当前 GUI 普通权限启动，网卡操作时按需申请 UAC。开机登录只进托盘，不改变任何网卡状态。配置保留在 `%LOCALAPPDATA%\NetPilotLite\`。正式更新资产只在 `dist/update/`；首次安装器单独为 `dist/NetPilotLite-Setup-x64.exe`。

下列为旧版本历史，涉及匿名更新、Web Setup 和整包更新的说明已由 1.4.0 文档替代。

## 1.3.0 安装分发基础

新增 Web Setup、Inno Offline Setup 与 Portable ZIP，固定 AppId，签名验证，正常退出后原目录升级。**本次已构建，尚未进行隔离 Windows 安装验收，未发布 GitHub Release。**

详见 [安装](docs/INSTALLATION.md)、[更新](docs/UPDATING.md)、[旧版迁移](docs/MIGRATION.md)、[发布](docs/RELEASE.md)、[验收矩阵](docs/INSTALLER_ACCEPTANCE.md)。网络控制、GUI 绘制和主题逻辑保留；安装器不会修改网卡、Metric、DNS、代理或规则。

# NetPilot Lite 1.2.0

原生 C17 / Win32 的便携 Windows 10/11 x64 网络开关。保留已经可用的 Ethernet 设备控制核心，加入物理 Wi-Fi 独立控制、樱彩界面及本地原地更新。旧 NetPilot/MSI 项目没有更改。

## 直接使用

运行 `dist\NetPilotLite.exe`，在标准 Windows UAC 中确认。无需安装、.NET、PowerShell、服务或计划任务。默认只枚举和绑定设备，不自动启用/禁用任何网卡；拒绝 UAC 时程序不会执行。

- Ethernet 默认唯一匹配 Intel I219-V；Wi-Fi 默认唯一匹配 Intel AX203。可以在各卡片下拉框选择其他物理同类网卡。
- 使用 **接口 GUID + PnP 实例 ID** 绑定，不依赖 InterfaceIndex。设备禁用后仍由 SetupAPI 枚举。
- 主按钮只改变对应物理网卡启用状态。使用 Configuration Manager API，并重新查询实际 Windows 状态；驱动拒绝、设备丢失、状态未达到目标都会报告错误码。
- 区分已禁用、已启用但未连接、链路已连接、未知和设备异常。Wi-Fi 显示可读取的 SSID/IPv4；SSID 受位置权限限制时明确提示，不改变 Windows 隐私设置。
- 校园网模式只启用绑定的 Ethernet，保留 Wi-Fi 原状。
- 手机热点模式要求绑定 Wi-Fi 已连接。Wi-Fi 禁用时先征求同意启用；尚未连接则停止，保留 Ethernet 状态，提示先连接热点。链路连接并不保证互联网可用。
- 独立禁用当前唯一已连接网卡前提示可能断网。检测到远程桌面或会话状态无法确定时，拒绝所有禁用操作。
- 托盘单击或右键打开主题化快捷浮窗，提供两张网卡的启用/禁用、模式、设置及退出；双击打开主窗口。默认最小化/关闭收至托盘，设置中可更改关闭行为。退出不会恢复用户主动禁用的网卡。

**不会修改** Clash Verge、VPN/TUN 虚拟网卡、代理、DNS、Metric、默认网关或无关路由。不保证切换后的网络一定能访问互联网。

## 1.2.0 更新

默认 310 DIP Compact 托盘（272 DIP 内容高度），可切换 Comfortable。应用选择器提供已安装 / 正在运行 / 手动选择，后台查询注册表、Shell 包身份及进程；搜索、多选、去重、完整身份查看。无法解析 EXE 时保留提示，MSIX 保留 PFN/AUMID，不假冒网络出口约束。

“设置 → 更新”接入用户确认的 `AcerGL/NetPilotLite` Stable API。后台检查、下载进度、固定公钥签名验证与原地更新确认。当前仓库最新发布接口返回 404；私有仓库或尚无正式 Release 时可继续使用本地签名更新。没有自动公开发布。

正式文件版本 **1.2.0.0**。更新协议见 [UPDATE.md](docs/UPDATE.md)，发布准备见 [RELEASE.md](docs/RELEASE.md)，验收边界见 [V120_REPORT.md](docs/V120_REPORT.md)。旧版性能和观察模块说明保留在 docs。

## 应用连接监控

打开「设置 → 应用网络分流」。当前是 **Monitor Only / 只读观察**：后台查询 TCP/UDP 的 IPv4/IPv6 端点、进程路径、源地址所属接口及当前路由估计。只在本页打开时每 10 秒更新；最多显示 1024 个端点，超过时明确提示截断。UDP 表不提供远端流信息；PID/进程路径可能因进程退出、复用或权限限制无法确认。

可以添加 EXE、选择期望 Ethernet / Wi-Fi / System Default、启用观察、测试及删除规则。新增规则默认关闭，规则只比较连接快照。**不会强制应用改道，也不会阻止回落到其他网络。** 已有连接、DNS/DoH、QUIC 和代理/TUN 的物理出站没有得到验证。

本地代理的连接标为回环；代理自身的远端连接不能直接归给原应用。源地址属于物理网卡也不等于证明真实出口。没有持久化连接日志，没有安装 WFP 过滤器、服务或驱动。技术依据与能力矩阵见 [ROUTING_FEASIBILITY.md](docs/ROUTING_FEASIBILITY.md)。

## 窗口、主题及图片

窗口可拖动缩放、最大化；宽窗口封面在左、卡片在右，窄窗口上下排列，空间不足可滚动。支持逐显示器 DPI；保留正常窗口位置、尺寸及最大化状态。

「设置 → 外观」支持深色、浅色、樱彩、跟随系统和自定义主题；自定义强调色、背景色、卡片色；纯色、渐变及 PNG/JPEG/BMP 背景；Fit/Fill/Center 与遮罩强度。图片复制到配置目录，缺失时回退到主题底色。原图封面内嵌，独立 ICO 包含七种尺寸。

## 配置

`%LOCALAPPDATA%\NetPilotLite\settings.ini`，SchemaVersion=2。

保存双网卡绑定、主题/自定义颜色、封面/背景、位置/尺寸/最大化、托盘偏好、最多 32 条应用观察规则。配置原子替换并保留 `.bak`；旧 schema 迁移先保留 `.migration.bak`。保留未知配置键；高于当前版本的 schema 拒绝覆盖。原 1.0.0 的 HKCU 网卡绑定自动迁移读取，保留原注册表值。

## 更新

主文件始终为 `NetPilotLite.exe`，通过「从本地文件更新」覆盖当前原路径，保留快捷方式与配置，不改变网络状态。签名清单 `.exe.update` 必须与新 EXE 放在一起。内置临时更新器负责正常退出、等待、同卷替换、启动确认和失败恢复。

原 1.0.0 首次更新没有界面入口，需要使用包内独立 `NetPilotLite.Updater.exe --apply 新文件 原文件`。完整步骤、签名信任、恢复命令、UAC 及发布维护见 [LOCAL_UPDATE.md](docs/LOCAL_UPDATE.md)。从 1.2.0 起支持 GitHub Stable 在线检查；需要有效签名资产和可访问的 Release。

## Visual Studio 构建

安装 Visual Studio 2022 的「使用 C++ 的桌面开发」、MSVC v143 和 Windows SDK 10.0.22621 或更新版本（需要新版 DWM 枚举；运行时保留 Windows 10 实色回退）。打开 `NetPilotLite.sln`，选择 **Release | x64**。或运行 `build.cmd`。

- C17、Unicode、`/MT` 静态 CRT、ASLR、DEP、CFG。
- Solution 包含 GUI、独立 Updater 和安全测试项目。Updater 先构建，再嵌入 GUI 资源。
- `build.cmd` 额外构建原生图标工具和发布签名工具，并运行网卡模拟测试。
- `publish.cmd 私钥路径` 生成固定 `dist\NetPilotLite.exe`、签名清单、`version.json` 和 `SHA256SUMS.txt`。签名私钥不在源码包中；现有公钥必须保持一致。
- 工作区可选的 SDK NuGet 路径只是本机开发兼容设置；常规 Visual Studio 使用本机安装的 SDK，无需随源码打包 SDK。

## 验证

`bin\Release\NetPilotLite.Tests.exe`：模拟网卡测试；`--gui-smoke`：模拟界面/托盘/布局/主题测试；`--probe`：只读设备扫描。此测试程序在编译期移除了真实 CM_Enable_DevNode / CM_Disable_DevNode 调用。

附加选项：`--perf` 为 600 次绘图测量；`--ui-acceptance` 检查滚动像素一致性和资源；`--soak` 运行约两分钟；`--routing-ui` 检查只读规则页面；`--routing-tests` 包含纯模拟映射与真实 Windows 本机回环 TCP/UDP 测试；`--routing-probe` 只输出连接分类数量。所有测试都不修改真实网络配置。GUI 测试需交互桌面；在项目目录中运行以保存截图和证据。

`tests\build_update_tests.cmd 私钥路径` 生成 1.0.0、1.0.1、1.2.0 及故障候选测试文件，并构建 `UpdateTests.exe`。测试必须将old、prior、new、fail 四个候选目录复制到新的测试根目录后运行 `UpdateTests.exe 测试根目录`，不可指向真实用户程序目录。旧版 fixture 保留旧版源代码，只加入独立实例标识和测试忙碌标记；新版 fixture 使用同一 GUI、配置和启动协议，网卡操作只走 Mock。故障注入仅存在于测试编译中。

若保留了 Stage A 的只读 Mock 产品，可额外放到测试根目录 `stagea`，验证 1.1.0.0 → 1.2.0；没有此目录则明确跳过该可选项。不要用需要提权的正式 EXE 冒充 Mock fixture。

最新结果见 [V110_FINAL_REPORT.md](docs/V110_FINAL_REPORT.md)；[TEST_RESULTS.md](docs/TEST_RESULTS.md) 保留 1.0.1 历史结果。真实网络手动验收见 [MANUAL_ACCEPTANCE.md](docs/MANUAL_ACCEPTANCE.md)。开发过程没有改变真实网卡状态；Program Files、真实 UAC 交互、物理断电及 Windows 10 尚未实际验收。

新增测试：`--feature-tests [签名的 2.0.0 故障样本 EXE]`、`--feature-ui`、`--apps-probe`、`--online-probe`。后两项只读真实环境；前两项使用模拟数据。GitHub Actions 工程目录根下的 `.github/workflows` 仅已准备，尚未运行。

每次重复升级测试请运行 `tools\test-upgrade.ps1`：自动复制模拟样本到新的测试根目录，保留故障证据，避免复用残留的未来 schema 和中断事务目录。不要将正式用户 EXE 放入测试样本目录。

