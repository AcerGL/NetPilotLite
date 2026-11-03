# 1.5.0 单 EXE 安装器验收报告

日期：2026-11-03，Asia/Shanghai。结论：**本地构建、真实公开下载、正式 Setup 首次安装及生产 GUI 启动通过；生产客户端跨版本自动覆盖更新尚未完成验收。**

## 公开发布与正式安装实测

仓库已由用户改为 Public。[v1.5.0 正式 Release](https://github.com/AcerGL/NetPilotLite/releases/tag/v1.5.0) 已发布，7 个独立资产；[发布任务 41578769124](https://github.com/AcerGL/NetPilotLite/actions/runs/41578769124) 成功。任务从已准备的本地签名文件建立草稿，构建固定公钥验证器，验证文件和签名、上传并下载回验后才公开。没有上传私钥，没有用户 ZIP 安装包。

实际 Windows WinHTTP 匿名获取 latest API，下载并验证 2 个运行文件和恢复 Setup 成功。正式下载的 Setup 已实际安装到 `%LOCALAPPDATA%\Programs\NetPilotLite`，生产 GUI 成功启动；再次只读核验主 EXE 1.5.0.0 的签名/SHA-256、开始菜单快捷方式及登录启动项均通过。与安装前快照比较，所有物理及虚拟适配器的 Status/AdminStatus 保持一致。

原计划使用独立测试目录，实际界面已在默认目录开始并完成安装；报告按实际路径记录，不宣称该次安装位于临时目录。安装前已备份用户配置和集成项；用户明确选择保留安装直接使用，因此保留登录启动及快捷方式。只读比较原有 8 个用户数据文件，哈希全部未变。

最初一次发布任务因浅克隆缺少被测试的源提交而失败，未公开草稿；增加完整历史获取后，新任务成功完成发布。此前 404 已被真实下载成功结果取代。
## 执行环境与边界

实际 Windows x64 主机，VS 2022 C17 Release /MT。Setup 是原生 `NetPilotLite-Setup-x64.exe`，没有完整安装器或 ZIP 内嵌。运行文件为主 EXE 与可信签名旁文件，图片和 CRT 已包含。

真实安装事务测试直接调用与 Setup 相同的安装代码，实际执行 Windows 文件复制/替换、签名校验、GUI 进程正常退出/启动确认、HKCU Run 和开始菜单快捷方式。为遵守不操作真实网卡的要求，被安装和启动的 GUI 使用 `NP_READONLY_BUILD + NP_MOCK_PRODUCT`。专用 `NetPilotLite.SetupTest` 启动项及快捷方式在结束时恢复，不覆盖用户的生产启动项。这不是生产 Setup 从 GitHub 下载到安装完成的端到端测试。

## 已执行的结果

源码提交 [`dfc3004`](https://github.com/AcerGL/NetPilotLite/commit/dfc30041e99b8bec5b7096a4988a89265ab2d4fd) 已同步到 `main`。[Windows 云端构建 41575582796](https://github.com/AcerGL/NetPilotLite/actions/runs/41575582796) 成功，实际完成核心 44、功能 75、开机/认证 46、路由 60 项检查。云端未签名，因此未运行 12 项需要签名样本的功能检查；这些已在本地执行。完整云端构建存在原有第三方 libyaml 警告；不能将本地增量无警告等同于整个仓库无警告。

Windows 真实 WinHTTP 匿名请求得到 HTTP 404 / Windows 1168，并明确提示公开 Release 不可用，没有误报“已是最新版”。这只确认当前 API 请求和错误处理，不是下载成功或在线安装通过。

| 测试 | 实际结果 |
|---|---|
| 最终 VS x64 构建 | 成功；核心模拟检查 44/44；此次增量构建未报告编译警告 |
| 生产 Setup/manifest/主程序验签及路径规则 | 18/18 |
| 公开逐文件下载协议 | 45/45，使用真实签名文件与模拟 HTTP；包括取消、中断、重试、404、超时、文件损坏和签名损坏 |
| Windows Setup 文件事务 | 68/68；实际 Windows API，含中文/空格路径、嵌套资源、快捷方式/登录项、运行中升级、健康检查失败回滚、旧清单恢复和未知用户文件保护 |
| 中断恢复 | 准备完成、首文件替换后两种故障注入：重新调用恢复后旧版签名有效且可启动；再次安装不能删除待恢复日志 |
| 配置与历史功能 | 87/87，正确的 2.0.0 已签名下载测试样本，无真实网卡写入 |
| 开机周期及历史私有认证回归 | 46/46；真实 Windows BootIdentifier/DPAPI，模拟 HTTP、假 Token；新公开路径不调用 Token 授权 |
| 路由观察回归 | 60/60；真实 Windows 本机 IPv4/IPv6 loopback，未修改系统网络配置 |
| GUI 回归 | 69/69；模拟产品、异步选择器及设置布局，网卡写入 0 |
| 保留的旧更新器和配置迁移 | 124/124；全新测试目录，旧版正常退出、固定路径、单实例、忙碌拒绝、共享锁失败、回滚及配置迁移 |

初次受限进程运行无法访问真实 HKCU/配置目录，已保留失败日志；使用允许访问 Windows 用户环境的测试执行后通过。功能回归曾使用错误工作目录及与固定 v2.0.0 测试元数据不匹配的 1.5.0 文件，修正测试调用后为 87/87；未降低验签要求。

## 严格验收对照

| 用户要求 | 结果与限制 |
|---|---|
| 1. 首次只下载一个 EXE | 通过；正式 Setup 下载、安装并启动生产 GUI |
| 2. 独立获取所有文件 | 通过；真实 WinHTTP / GitHub 公开资产下载及校验 |
| 3. 无需处理 ZIP | 新安装/发布路径没有 ZIP，交付无 ZIP |
| 4. 安装目录完整 | 实际 Windows 文件测试通过，包括嵌套资源 |
| 5. 开机一次检查 | BootIdentifier 及持久化去重测试通过；未为本任务实际重启 Windows |
| 6. 无更新不弹窗 | 版本比较与调用分支已验证；真实在线同版本启动待验 |
| 7. 有更新弹确认 | Princess 提示保留、模拟 UI 检查通过；真实远端新版待验 |
| 8. 确认后自动下载 | 应用→已签名 Setup→清单文件路径已接通；真实端到端待验 |
| 9. 自动覆盖原版本 | 实际 Windows 运行中替身升级通过，路径保持不变 |
| 10. 配置与用户数据保留 | 安装外数据不在清单中；配置/绑定/背景/规则测试和用户文件保护通过 |
| 11. 新版正常启动 | 通过；生产 Setup 安装后生产 GUI 已实际启动 |
| 12. 网络中断恢复 | 模拟下载中断后重试成功；未主动断开真实网络 |
| 13. 校验或签名失败阻止 | 真实签名样本被破坏后被拒绝，测试通过 |
| 14. 升级失败回滚 | 实际 Windows 启动失败和两处中断恢复通过；没有真实断电 |

## 未完成条件

- 云端 `NETPILOT_SIGNING_KEY_B64` 尚未配置；签名工作流应失败关闭。现有本地 DPAPI 发布密钥成功签名，密钥没有导出到交付目录。
- 尚未执行真实 Program Files 安装、真实 UAC 取消、生产自动更新 UI 全链路、物理断网/断电测试。Clash/TUN 配置保持原样；实际当前 Windows 环境的匿名下载已经成功，但没有切换或重启 TUN 做故障测试。
- 旧私有 ZIP 客户端迁移到本协议需首次运行新 Setup 一次；不能靠向旧客户端发布不同资产格式使其自动理解新协议。
- 本版 Setup 不提供卸载器。旧 MSI/Inno 文件保留作历史参考，不作为本次发布资产。

## 主要改动

`src/public_setup.[ch]`：匿名 Release、Setup 验签及逐文件下载。`src/install_manifest.c`：签名清单、目录和文件校验。`installer/web/main.c`：单 Setup 安装/更新 UI、参数、UAC 与取消重试。`installer/web/install.c`：安装事务、健康检查、集成项备份及恢复。`src/setup_launch.c`：启动检测待恢复事务。`src/trust.c` / `tools/release.c`：独立 Setup 签名用途。`src/ui_online.inc` / `app.c` / `main.c`：主程序公开更新与恢复接入。

构建发布：VS 项目、资源版本、`scripts/package-public.py`、`build-public.ps1`、`build-release.ps1`、`build-update.ps1`、`publish-release.ps1`、`validate-version.ps1`、`.github/workflows/{build,release}.yml`。测试：`tests/public_setup_tests.inc`、`setup_tests.c`、`public-fixture.py` 及构建测试样本。

本地原始证据在 `artifacts/public-setup`，交付目录提供可分享的结果报告；不会将设备详情、用户配置、Token 或发布私钥提交 GitHub。



