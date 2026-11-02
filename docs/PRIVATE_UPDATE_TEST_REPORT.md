# 1.4.0 私有更新验收报告

日期：2026-11-02（Asia/Shanghai）。这是本地构建与分层验证报告，**不是完整私有 Release 在线升级已验收的声明**。

## 已执行结果

| 检查 | 实际结果 |
|---|---|
| Release x64 C17 编译 | 成功；最终构建无编译警告；静态 CRT；Web Setup 不在 solution/build 产物中 |
| 网卡核心模拟测试 | 44 通过，0 失败；测试版真实修改入口编译移除 |
| 原有签名/协议功能回归 | 87 通过，0 失败；提供了已签名测试文件 |
| 私有授权/开机策略 | 46 通过，0 失败；真实 DPAPI 加密、解密、篡改拒绝；真实启动 GUID 连续读取；其余 HTTP/新启动周期为注入模拟 |
| 新私有下载链路 | 11 通过，0 失败；同一客户端函数读取模拟私有 API、下载真实签名测试 ZIP、失败重试/取消/401/无 Release；未执行下载的测试镜像 |
| 更新 ZIP | 10 通过，0 失败；真实签名清单与 EXE 提取；篡改 ZIP/签名均阻止 |
| Win32 界面 | 69 通过，0 失败；密码框、授权按钮、Princess 更新窗口、正常关闭；网卡写入次数 0 |
| 原地升级和配置 | 124 通过，0 失败；实际 Windows 文件替换和模拟 GUI 进程，从 1.0.0 / 1.0.1 / 1.1.0 到 1.4.0；正常退出、健康确认、快捷方式/配置保留、单实例、失败回滚、中断恢复 |
| 路由回归 | 60 通过，0 失败；真实 IPv4/IPv6 loopback TCP/UDP 检查；没有创建 WFP 过滤或修改物理网络 |
| 发布包本地验证 | 签名、清单、哈希通过；脚本默认不发布；无效环境签名密钥被拒绝 |
| 首次 Setup | 新 `NetPilotLite-Setup-x64.exe` 编译成功；未执行真实安装 |
| 当前 TUN 网络 | 只读确认 Meta Tunnel 为 Up；同一 WinHTTP API/CDN 链路真实下载 GitHub CLI 公开校验文本 1971 字节成功；没有执行下载内容 |

原地替换测试首次在受限执行环境出现 Windows 5（拒绝访问），101 通过 / 23 失败；日志显示失败在文件替换阶段。使用已获授权的独立工作区测试目录重跑后 **124 / 0**。失败记录保留，没有将其掩盖为首次通过。真实网卡修改入口在两次运行中都已编译移除。

## 用户要求 A–P 对照

| 项 | 结论与范围 |
|---|---|
| A 私有仓库正常授权 | **模拟通过；真实 PAT 未验证**。连接器实际确认仓库 private=true，不等同于桌面 Token 授权 |
| B 无效 Token | 格式拦截、模拟 401 通过；没有拿用户真实 Token 测试 |
| C 过期/撤销 | 模拟 GitHub 401 拒绝并提示重新授权通过；没有在 GitHub 撤销用户 Token |
| D 已最新 | 语义版本相等与 GUI 已最新分支模拟通过 |
| E 存在新版 | 1.10.0 > 1.9.0、稳定 Release 解析与提示界面模拟通过 |
| F 同开机重复启动 | 同一真实 BootIdentifier 读取一致；临时状态目录完成标识去重和重试持久化通过；未实际反复启动用户生产实例 |
| G 新开机周期 | 注入不同 BootIdentifier 后允许检查通过；未真实重启 Windows |
| H 临时离线 | 注入连接失败、30/90 秒间隔与最多三次尝试通过 |
| I GitHub 超时 | 注入 WinHTTP timeout 返回实际错误分支通过；未人为使真实 GitHub 超时 |
| J 下载失败 | 新私有下载函数注入中断，阻止使用残缺文件，再次完整下载成功；取消通过 |
| K SHA-256 不匹配 | 实际篡改本地签名更新 ZIP，验证拒绝，通过 |
| L 签名失败 | 实际篡改清单签名，验证拒绝；无效发布 Secret 拒绝，通过 |
| M 正常覆盖 | 实际 Windows 测试进程与文件，固定路径替换、新版启动、快捷方式仍指原位置，通过；真实生产安装目录未操作 |
| N 失败回滚 | 签名有效但启动失败的测试程序、共享锁冲突、准备/替换/提交阶段中断注入，恢复测试通过；并非真实断电实验 |
| O 配置保留 | 真实临时配置文件：绑定、主题、图片、窗口、分流观察规则、未知字段、迁移备份通过；未改写用户配置 |
| P Clash TUN 中更新 | **只读传输探测通过，完整私有更新未验证**。Meta Tunnel Up 时公开 API/CDN 下载成功，没有更改 Clash 或网卡 |

尚未执行：真实用户 PAT 授权/过期/撤销、生产开机启动项的系统验收、真实 Windows 重启、Program Files UAC 批准/取消及跨账户路径、真实私有 Release 下载并替换生产软件、GitHub hosted 签名/发布工作流。当前账户的 UAC 拒绝仅有故障注入测试；不能写成实际点击 UAC 取消已验收。

## 交付与变更

- 新模块：`src/private_auth.c`、`src/private_update.c/.h`、`src/repository.h`、`src/ui_update_prompt.inc`。
- 更新集成：`src/online.c/.h`、`src/ui_online.inc`、`src/ui_settings.inc`、`src/settings.c`、`src/app.c`。
- 权限/更新器：`src/main.c`、`src/app.manifest`、`src/backend_win.c`、`src/updater.c`。保留已验证的独立事务、回滚和恢复逻辑。
- 发布：`tools/release.c`、`scripts/package-update.py`、`build-update.ps1`、`build-release.ps1`、`publish-release.ps1`、`detect-repository.ps1`、`validate-version.ps1`、两个 GitHub workflows。
- 工程/资源：solution、两个主/测试 vcxproj、Common.props、build.cmd、VERSIONINFO、首次安装脚本及 guard-only 构建入口。
- 测试：`tests/private_tests.inc`、`private-download-fixture.py`、`test_main.c`、`ui_features.inc`、`build_update_tests.cmd`、更新版本断言/资源；证据在本地 `artifacts/private-update/`。

本地有可信旧公钥及 DPAPI 私钥，可生成真实签名候选包。本次 **未导出或上传私钥**，未配置 GitHub Secrets，未创建标签、未发布正式 Release。仓库最新稳定 Release 读取仍为 404，因此最终自动更新闭环尚有外部前置条件。
