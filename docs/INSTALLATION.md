# NetPilot Lite 1.3.0 安装交付（等待隔离验收）

三个固定文件名：

- `dist/NetPilotLite-Offline-Setup.exe`：Inno Setup 7.1.0 x64，包含已签名主程序，无需联网。标准管理员 UAC；默认 `C:\Program Files\NetPilot Lite`，允许首次自定义目录。
- `dist/NetPilotLite-Web-Setup.exe`：C17 / Win32 / WinHTTP、静态 CRT；普通权限下载，只有启动 Offline Setup 时请求 UAC。下载目标固定为 Offline Setup，绝不下载自身。
- `dist/NetPilotLite-Portable-x64.zip`：解压后运行 `NetPilotLite.exe`，不注册安装、不创建快捷方式；主程序保持原有 requireAdministrator manifest。

本次没有在用户电脑执行 NetPilot 安装、卸载或网络修改。当前产物是本地待验收候选，不能称为完成安装验收的正式发行版。

## 安装与修复

运行 Offline Setup，选择语言、目录及可选桌面快捷方式。已有同 AppId 的 Inno 安装必须使用登记原路径。相同版本需要明确确认修复；签名旧版本高于候选时拒绝降级。开始菜单和卸载记录由 Inno 管理，所有版本使用同一个产品身份：

`{C47B553D-22CE-4CDD-BA10-2930FC8266D4}`

对应 HKLM 64-bit 卸载项：`Software\Microsoft\Windows\CurrentVersion\Uninstall\{C47B553D-22CE-4CDD-BA10-2930FC8266D4}_is1`。

安装器验证候选/旧版的固定公钥签名、SHA-256、x64、产品与版本；请求程序通过正常退出命令保存数据；等待退出，不强杀。复制前保存旧主程序和签名，写入并刷新事务标记。部署后逐字节验证候选清单一致，启动健康探测，收到 GUI ready 后正常关闭探测实例，再提交。安装结束页可选择启动程序。

## 配置与卸载

配置仍在 `%LOCALAPPDATA%\NetPilotLite`，SchemaVersion 仍为 2（新增运行时安装模式不写入配置）。安装清单没有任何用户配置条目，也不注册服务、计划任务或网络过滤规则。

卸载默认保留所有用户数据，不改变网卡状态。结束时提供“打开当前账户数据目录，由用户手动选择删除”的清理入口；**尚未实现一键递归清空配置**，以免 UAC 换账号时误删其他账户数据。

## 日志与中断

Inno `SetupLogging=yes`，默认日志位于临时目录；也可用 `/LOG="绝对路径"` 指定。关键日志包含 `NetPilot phase=... context=administrator result=... code=...`。没有签名私钥、访问令牌或网络密码。

安装失败时尝试恢复旧签名 EXE；保留 `.NetPilotLite-install-backup` 与 `.NetPilotLite-install-pending`，再次运行可信安装器进行恢复/修复。1.3.0 普通启动遇到 pending 标记会阻止进入功能界面。备份验证失败、进程仍占用或恢复失败时，停止并保留证据，不伪装成功。

**重要边界：**旧 EXE 恢复不是卸载注册信息的完整事务回滚。发生安装器进程崩溃/断电后，需要重新运行可信安装器完成登记修复；真实 Windows 中的注册表、卸载日志、快捷方式一致性尚待隔离验收。不能宣称断电后自动恢复全部安装状态已经验证。

## 许可证

使用官方 Inno Setup 7.1.0，官方工具 Authenticode 校验为 Valid / Pyrsys B.V.。原始许可副本见 `installer/INNO-LICENSE.txt`，原版权与网站声明保留。许可正文允许使用和再分发并列明保留声明等条件；官方网站另请求商业用户购买许可，编译器显示 non-commercial use only。本次为本地开发；商业发布前应确认适用授权。参阅 https://jrsoftware.org/isdl.php 和 https://jrsoftware.org/files/is/license.txt 。没有将编译工具加入产品包。
