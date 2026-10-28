# NetPilot Lite 1.3.0 双安装包交付报告

日期：2026-10-27。状态：**本地构建与签名完成，隔离安装验收未完成，未发布 GitHub。**

## 发布产物

| 文件（项目 dist 目录） | 字节 | SHA-256 |
|---|---:|---|
| NetPilotLite-Web-Setup.exe | 538624 | `6b7126919fdd88dad485b270bcc975da1f2744e53673bc07cc80a4b2c3a1b214` |
| NetPilotLite-Offline-Setup.exe | 4472874 | `15e37cdb7695972f9a3313116880b8dfa5e6e4e49c1628bcb771ace0e1f4d521` |
| NetPilotLite-Portable-x64.zip | 1248338 | `35562010e3ef32747f4eb6bb3bd1720c38a8e6b65e865c75db0e1b185f6de74c` |

主程序、Web Setup、Offline Setup 的版本为 **1.3.0.0**，Portable 内主程序相同。固定 AppId：`{C47B553D-22CE-4CDD-BA10-2930FC8266D4}`，不会随版本号变化。

回退基线：工作区 `work/v1.2.0-before-installers` 下 1.2.0 源码 ZIP 和发布 ZIP；未修改旧 .NET/MSI 项目。没有在当前目录初始化新 Git 仓库。

## 修改与实现

- `installer/offline/NetPilotLite.iss`：Inno Setup 7 x64，中英文、既有品牌、固定安装身份/原路径、快捷方式与卸载记录、同版确认、降级拒绝、旧 MSI/便携/开发目录区分。
- `installer/common/guard.c`：限定用途签名核验、路径/重解析/硬链接检查、正常退出、刷新到磁盘的备份/标记、部署清单精确比对、GUI 启动探测、失败恢复。没有网络控制代码，也不注册服务/任务。
- `installer/web/main.c` / `cache.c`：原生非提权下载器、真实百分比/字节/速度、工作线程、取消与重试、错误码、手动下载入口。标准 UAC 只用于运行已验签 Offline Setup。
- `src/online.c` / `trust.c`：共享 GitHub/WinHTTP 下载和固定公钥验证；主程序、Offline Setup、资产目录签名用途隔离。修正 Inno 产品名末尾填充空格的兼容性，不放宽产品、版本或架构约束。
- `src/app.c` / `ui_online.inc`：正式安装版的本地/在线更新使用 Offline Setup；便携版保留原子 EXE 更新。
- `src/main.c`：未完成安装标记阻止普通启动；健康探测是固定参数，启动不执行网络变更。
- 新增两个 Visual Studio 项目及 release 构建脚本、测试和文档。逐文件见 V130_FILES_CHANGED.md。

网卡控制、路由观察、绘图、主题、核心切换逻辑、配置序列化和托盘绘制文件与 1.2.0 基线字节一致。

## 测试结果

| 验证 | 结果 | 证据（artifacts/） |
|---|---|---|
| Release x64 C17 / 静态 CRT / Inno 编译 | 通过 | v130-release-build.txt、v130-inno-final-build.txt |
| 主程序 Mock | 44 通过 / 0 失败 | v130-release-build.txt |
| 原协议、配置、签名下载模拟 | 76 / 0 | v130-feature-tests.txt |
| Offline 签名/用途/下载中断/错误响应模拟 | 32 / 0 | v130-installer-protocol.txt |
| 原生 Guard 文件操作 | 22 / 0 | v130-guard-tests-final.txt |
| 便携更新及配置：旧 1.0.0 / 1.0.1 / Stage A 1.1.0.0 → 1.3.0 | 124 / 0 | v130-portable-update-final.txt |
| 主程序/Offline/资产目录固定公钥签名 | 通过；EXE 未执行 | v130-release-build.txt；最终校验再次运行 |
| 真实 GitHub latest 请求 | HTTP 404，不能断言已是最新或已发布 | v130-github-probe.txt |
| 真实网卡只读前后检查 | 无差异：I219-V 禁用，AX203 启用且链路连接 | v130-network-before.txt、v130-network-after.txt |
| 额外新版 GUI 回归 | 未执行；自动审批额度错误 | 见下文 |
| 真实 Inno 安装/修复/升级/卸载/UAC/断电 | **未执行** | 无隔离 Windows 环境 |

总计上述五组 Mock/协议/文件/更新测试 **298 项通过**。首轮旧更新测试仍断言 1.2.0，修正为目标 1.3.0 后在全新测试目录运行 124/0；一次错误工作目录的协议测试也未计入通过结果。失败日志保留，不以改断言代替失败行为检查。

文件 Guard 测试用真实签名 1.2.0/1.3.0 副本验证原路径复制、拒绝同版未确认/降级、硬链接/UNC/相对路径拒绝、旧文件备份/恢复及损坏备份拒绝；配置、背景、规则哨兵 hash 不变。没有启动生产 EXE 或安装器。便携测试启动的是编译掉真实网络写入功能的 Mock GUI，验证正常退出/启动 ACK/快捷方式保留、失败启动恢复、更新日志中断恢复、真实共享锁失败、单实例、模拟取消。

**以上不能证明 Inno 的卸载登记、快捷方式和文件系统整体事务回滚正确。**安装器中断后保留旧 EXE 和 pending 标记，尝试恢复文件，并要求重跑安装器修复元数据；尚不能宣称全自动断电恢复已经验收。

## 配置、迁移与网络

配置仍在 `%LOCALAPPDATA%\NetPilotLite`，schema 2，安装清单不触碰该目录。绑定、主题、图像、窗口、托盘和分流观察规则保存逻辑保持。安装器没有网卡启禁、Metric、DNS、网关、Clash/VPN 或规则变更代码。应用网络分流继续为 Monitor Only，没有持久驱动/过滤器/服务需要自动卸载。

卸载默认留数据，提供手动打开当前账户目录清理的可选入口；未实现自动递归清空。未知 sidecar settings.ini 会阻止迁移，未实现自动猜测配置格式；旧 MSI 仅检测提示和人工处理，不删除 Installer 缓存或任务。

## 发布工作流与安全条件

已准备显式 tag 构建 → 审批签名 → 单独审批发布工作流。签名 runner 使用现有 DPAPI 私钥，仓库只含公钥。发布要求 LICENSE_CONFIRMED、ASSETS_CLEARED、RELEASE_SIGNING_ENABLED、RELEASE_ALLOWED，以及与确切 Offline Setup 匹配的 ACCEPTED_OFFLINE_SHA256；详见 RELEASE.md。

本次未推送代码或 tag、未执行 GitHub Actions、未发布 Release、未改变仓库可见性。仍需设置实际审批环境/runner，完成插图许可与商业使用授权确认、隔离安装验收和真实 Release 下载。没有 Authenticode 产品证书；已实现独立发布公钥签名，不得让用户关闭 SmartScreen/UAC/证书验证。

## 自动审批限制

额外 GUI 回归启动请求被自动审批中止：审批额度已用完，无法完成审核，并非判断该操作不安全。该测试没有执行，也没有通过更低权限或其他方式绕过；已完成的文件/签名/Mock 结果不受此影响。

## 使用与下一步

目前可将三个候选产物交给隔离 Windows 环境测试；优先验收 Offline Setup，矩阵和引导脚本见 INSTALLER_ACCEPTANCE.md、scripts/test-installer.ps1。真实安装会请求 UAC、写程序目录/卸载记录/快捷方式，并正常关闭选定旧版本；没有网卡变更动作。不要把这份报告理解为用户宿主机已安装成功。
