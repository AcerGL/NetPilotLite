# NetPilot Lite 1.0.1 验收报告

历史报告。当前 1.1.0 最终结果见 [V110_FINAL_REPORT.md](V110_FINAL_REPORT.md)。

日期：2026-10-23。构建与运行主机：Windows 11 x64。Visual Studio 2022 / MSVC v143、C17、Release x64、静态 CRT。

## 结论与范围

**网卡模拟检查 44 项通过；更新与配置检查 93 项通过；GUI 模拟检查通过。** 这里的数量为测试断言数量，不是 137 个独立 Windows 环境。

更新测试实际创建和替换了工作区中的 EXE，启动并关闭了独立标识的测试 GUI；不是仅测试返回码。旧版 fixture 来自原 1.0.0 GUI/控制代码，新版使用 1.0.1 GUI 与同一启动确认协议；均使用 Mock Network Backend，且真实网卡修改函数在编译期移除。

**没有在用户正在使用的正式程序目录执行更新，没有真实安装验收，没有隔离 Windows 虚拟机/沙盒，没有修改真实网卡。** 不能将这些测试等同于 Program Files、真实 UAC、实际断电或 Windows 10 的验收。

## 已完成检查

| 项目 | 结果及证据 |
|---|---|
| Release 构建 | 成功；`artifacts/final-build.txt`，正式主文件 `dist/NetPilotLite.exe` |
| 原有 Ethernet 控制回归 | 精确身份、只操作目标、禁用后可找回、重新启用、远程拒绝、驱动拒绝、状态不变超时、设备丢失/重复身份、失败后重查；保留原核心流程 |
| Wi-Fi / 模式 | 独立启用/禁用、Ethernet 保持不变、远程拒绝、未连接热点时保持有线、仅获准启用但未连接时停止、热点模式只禁用有线、校园网模式保留 Wi-Fi；合计 44 项控制检查通过，见 `artifacts/mock-tests.txt` |
| 真实网卡修改入口隔离 | Tests 及新 GUI fixture 的导入表均没有 `CM_Enable_DevNode` / `CM_Disable_DevNode`；Updater 没有网络修改 API。见导入表及 `artifacts/import-audit.json` |
| GUI / 托盘 | 启动 Mock 写入次数 0；托盘创建、最小化/恢复、两张绑定、两次状态重查切换均通过 |
| 布局 / DPI | 100/125/150/200% 的逻辑 DPI 各检查宽/窄布局，共 8 组；卡片无重叠，内容可滚动。这是应用布局模拟，不是修改了 Windows 显示缩放或实测多台显示器 |
| 外观 / 图片 | 五种主题、设置窗口与小视口滚动、三种图片显示模式、缺失图片路径失败处理；封面和背景异步解码并缓存。PNG 截图位于 `artifacts` |
| 封面与图标 | 封面 SHA-256 与用户原图相同；ICO 含 16/24/32/48/64/128/256，Alpha 保留；小尺寸使用清晰网口标识 |
| 配置持久化 | 双绑定、主题、自定义颜色、图片路径、窗口矩形、最大化/托盘选项回读一致；未知键保留；schema 1→2 先备份；未来 schema 拒绝覆盖；原子保存有备份 |
| 1.0.0→1.0.1 | 实际运行旧 Mock GUI，以正常退出命令关闭并等待进程退出；同目录替换为 1.0.1；校验版本及哈希、新 GUI 健康确认通过 |
| 原路径与快捷方式 | 测试路径包含中文和空格；在测试目录创建的 `.lnk` 目标前后一致，配置样本哈希一致。不涉及用户桌面上现有快捷方式的修改 |
| 单实例 | 第二次启动退出，目标窗口数量仍为 1；已提交恢复时已存在健康实例则不再启动副本。最终运行目录残留测试实例为 0 |
| 更新文件损坏 / 错误签名 | 验证拒绝，不能进入正常替换流程 |
| 新版启动失败 | 已签名故障候选在健康确认前退出；实际恢复旧 EXE、签名记录并重新启动旧 GUI |
| 回退启动等待 | 回退后等待旧 GUI 就绪，确认仍在运行；避免进程刚创建就被后续流程误认为就绪 |
| 更新中断 | 测试编译在 Prepared / Replaced / Committed 三个阶段注入中止；重新执行恢复：未提交阶段回旧版，已提交阶段保留新版并完成清理 |
| 用户取消 | 注入 `ERROR_CANCELLED`，原 EXE 与运行实例保持不变。**这是取消路径模拟，未实际点击 UAC 对话框** |
| 旧版忙碌 | 旧 GUI 拒绝正常退出，等待超时后返回 `ERROR_BUSY=170`，保持旧 EXE/实例；不强杀 |
| 替换文件被占用 | 实际持有禁止删除的文件句柄，`ReplaceFileW` 失败，日志 Windows 32，恢复结果为 complete，旧 EXE 校验通过 |
| 发布完整性 | 独立使用公钥验证发布签名，EXE 哈希与签名记录、版本 JSON 一致；见 `artifacts/release-verification.json` |
| 错误发布私钥 | 发布工具拒绝与固定公钥不匹配的私钥，已有签名清单保持不变；`artifacts/release-key-negative.txt` |

更新/配置断言合计 93 项，完整输出见 `artifacts/update-tests.txt`。生产构建不包含故障注入环境变量处理；这些钩子仅在 `NP_UPDATE_TEST` 编译中存在。测试遗留实例清理严格限定工作区的专用 Mock 窗口，不关闭用户正式程序。

## 只读真实环境检查

发现并唯一匹配：

- Intel(R) Ethernet Connection (16) I219-V：已禁用，PnP problem 22。
- Intel(R) Wi-Fi 6 AX203：已启用、链路已连接。

开发前后的只读检查均为以上状态。没有发送真实网卡修改请求，没有调用 Metric、DNS、网关、代理、Clash 或 VPN 配置修改接口。完整本机只读记录保留于工作区 `artifacts/readonly-probe.txt`；公开发布包不包含该机器的 PnP 实例标识。

## 仍需用户认可环境中的人工验收

- 在真实校园网/手机热点上执行两张网卡的启用与禁用，以及热点/校园网模式。
- 原正式 1.0.0 二进制到新版的实际用户目录升级、真实已有桌面快捷方式启动、原注册表绑定到 LocalAppData 的实际迁移。
- Program Files、标准/管理员不同账户、真实 UAC 同意/拒绝、ACL/杀毒软件限制。
- 真实突然断电、系统崩溃、存储设备写缓存及文件系统异常；当前验证的是持久化检查点的故障注入。
- Windows 10，以及真实多显示器 DPI 切换、睡眠唤醒、热插拔、较长时间运行。布局、设备/电源通知和低频轮询机制已实现，尚未完成这些硬件环境实测。

当前发布 EXE 没有 Authenticode 代码签名证书；内置更新公钥签名与 Windows 的“受信任发布者”是两种不同机制。Windows 仍可能提示未知发布者。

## 复现与交付

参见 `README.md`、`LOCAL_UPDATE.md`、`MANUAL_ACCEPTANCE.md` 和 `FILES_CHANGED.md`。正式主文件名固定，不按版本创建安装目录。SHA-256 以发布包内 `SHA256SUMS.txt` 为准。

Windows API 依据：[ReplaceFileW](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-replacefilew)、[BCryptSignHash](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptsignhash)、[Wi-Fi 与位置权限变更](https://learn.microsoft.com/en-us/windows/win32/nativewifi/wi-fi-access-location-changes)。SSID 权限受限时只报告限制，不自动变更隐私设置。
