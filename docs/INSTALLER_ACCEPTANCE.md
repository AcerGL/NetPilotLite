# 安装器隔离验收矩阵（尚未执行）

用户此前确认没有 Windows Sandbox/VM，本次也未发现可用的 Sandbox、vmrun、VirtualBox 或 vmms 入口。**没有执行真实 NetPilot 安装。**以下不是通过清单。

在有快照的 Windows 10/11 x64 虚拟机中，复制 dist、scripts、docs；勿映射真实 LocalAppData 可写目录。可使用 `scripts/test-installer.ps1 -IsolatedWindowsConfirmed` 引导交互安装，该脚本只覆盖中英文安装/修复，不能替代下列完整矩阵。不要在宿主机添加确认参数。

| 类别 | 待验收项目 | 本机状态 |
|---|---|---|
| 安装 | 离线、断网、Web 初装，中文/英文、空格中文目录、UAC | 未运行 |
| 升级 | 真实 Inno 旧版→新版、自定义目录不变、重复安装、同版修复、拒绝降级 | 未运行 |
| 历史版本 | 1.0.0→1.1.0→1.2.0 历史版本只有便携/实验 MSI，不能编造旧 Inno 安装记录；需使用独立 VM 构建同 AppId 测试包 | 未运行 |
| 数据 | 绑定、主题、自定义图、规则、窗口、托盘、未知键、桌面快捷方式保持 | 独立文件/配置测试通过；真实安装未运行 |
| 失败 | DNS、GitHub 404、HTTP 超时、中断、错误 hash/signature、磁盘不足、UAC 拒绝 | 模拟 HTTP/损坏载荷已测；真实系统异常未运行 |
| 进程 | 旧托盘正常退出，忙时拒绝、另一会话占用、没有双实例 | 便携 mock 已测；安装器未运行 |
| 回滚 | ssInstall/文件部署/ssPostInstall/提交前后杀进程、VM 断电，重复运行安装器恢复；核对旧 EXE 和卸载版本一致 | 独立文件恢复已测；完整安装事务未运行 |
| 卸载 | 关闭托盘、仅删已安装文件、保留配置、可选手动清理、不影响其他应用 | 未运行 |
| 网络 | 前后 Get-NetAdapter / IP / route / DNS / proxy 快照对比，不做启用禁用动作 | 本机只读适配器状态一致；安装场景未运行 |

每种场景保留 Setup / Uninstall 日志、产物 SHA-256、前后配置与快捷方式摘要、安装登记导出、退出码和恢复结果。只有本次 exact Offline Setup 哈希通过矩阵后，才可在发布环境设置 `ACCEPTED_OFFLINE_SHA256`。本地 Mock 的成功不得填写该值。
