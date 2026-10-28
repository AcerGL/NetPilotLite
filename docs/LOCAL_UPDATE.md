# 本地原地更新（1.0.1）

正式主程序始终叫 `NetPilotLite.exe`。发布目录 `dist` 也是固定目录，不按版本创建运行目录。版本写在 Windows VERSIONINFO、关于页及 `version.json`；`SHA256SUMS.txt` 记录校验值。

## 日常使用

新版本发布包中应同时有：

- `NetPilotLite.exe`
- `NetPilotLite.exe.update`：128 字节的 ECDSA P-256 签名清单，包含产品格式、四段版本、大小、SHA-256 及签名。
- `version.json`、`SHA256SUMS.txt`

在旧版的「设置 · 关于 · 更新 → 从本地文件更新」选择**新包里的 EXE**，确认后会覆盖**旧版所在路径**的 `NetPilotLite.exe`。不能把下载文件先覆盖到正在运行的 EXE 上。新包与旧程序可以在不同磁盘；暂存文件在目标目录生成。

以 `D:\Tools\NetPilotLite\NetPilotLite.exe` 为例，更新前后快捷方式目标都保持这个路径。原配置统一在 `%LOCALAPPDATA%\NetPilotLite\settings.ini`；更新器不读写这些偏好，更不会恢复或更改网卡状态。

1.0.1 目前的「检查更新」只说明本地更新方式，不联网，不下载任何程序。

## 首次从旧的 1.0.0 更新

原来的 1.0.0 没有更新页面，因此首次需要使用新包中的独立更新器。示例（把两处路径改成实际位置）：

```cmd
NetPilotLite.Updater.exe --apply "D:\Downloads\NetPilotLite\NetPilotLite.exe" "D:\Tools\NetPilotLite\NetPilotLite.exe"
```

更新器会先验证文件，然后在需要时使用标准 Windows UAC。**拒绝 UAC 时，旧实例不会被请求退出，文件不会替换。** 不需要安装服务或计划任务。

为防止提升权限后执行任意旧 EXE，未带签名清单的旧版只接受本项目曾交付的 1.0.0 精确 SHA-256：

`d671cf6648164f343c08dcaba20ff16898f052d3dcb17a9df4e355d34f42f5ff`

其他自行编译的旧版需要用对应发布密钥为原文件生成清单。更新器会拒绝未知旧文件，不能靠修改版本文字绕过。不要删除验证以迁就不明版本。

## 事务与恢复

实现入口：`src/updater.c`，验证：`src/trust.c`，GUI 桥接：`src/update_launch.c`。

1. 验证发布者签名，再核对文件大小、SHA-256、x64 PE、产品名、VERSIONINFO 和递增版本。
2. 在原目录复制到 `.NetPilotLite.new`，重新核对，刷新文件缓冲。
3. 持有排他更新锁；将原/新签名记录及阶段写入 `.NetPilotLite.update-state`，通过同目录临时文件及持久化重命名提交日志。
4. 按窗口所属进程的实际映像路径定位旧实例，发送正常退出命令 106。这个命令也兼容原 1.0.0。等待旧进程退出；忙碌或无响应时拒绝更新，不强杀。
5. 使用 `ReplaceFileW` 在原卷替换，同时保存 `.NetPilotLite.previous`；保留原有文件安全信息。
6. 再验证新文件，持有禁止写入/删除的映像句柄，使用固定目标路径启动它。启动参数仅接受随机 GUID 健康确认，不能传任意命令。
7. 新程序完成配置加载、窗口创建和首次状态查询后确认启动；更新器等待确认，并确认进程继续存活。
8. 确认成功后写入正式签名清单、提交事务，再清理备份。失败则重新校验旧备份，正常关闭失败的新实例，尽可能恢复和启动旧版。不能安全替换时留下备份和日志，不伪报成功。

路径采用盘符绝对路径，目标文件名固定；不支持 UNC，拒绝重解析点及硬链接文件。更新器不接受 URL、脚本、任意启动命令或任意恢复文件路径。恢复时只使用固定文件名及已签名的记录。GUI 内置的更新器从自身资源提取，启动前核对提取文件哈希并锁定映像。

普通启动检测未完成的更新事务后调用恢复器；未提交阶段恢复旧版，已提交阶段确认新版并完成清理。**启动确认前不会删除旧备份。** 如果断电后主 EXE 缺失，可以使用发布包里的独立更新器：

```cmd
NetPilotLite.Updater.exe --recover "D:\Tools\NetPilotLite\NetPilotLite.exe"
```

恢复不修改用户配置或网络。实际文件状态与签名不匹配、备份缺失、文件仍被占用或权限不足时，停止并保留证据。具体阶段和 Windows 错误码见原目录 `NetPilotLite-update.log`（UTF-16，无 SSID、IP、设备 GUID 或凭据）。不要手动删除未完成事务的 `.previous` 和 `.update-state`。

运行时提取的临时更新器可能暂留在用户 Temp 的 `NetPilotLite-Update-{GUID}` 文件夹中；它不是另一个 NetPilot Lite 主程序，没有服务、任务或自动启动项。为避免提升权限时遍历可被替换的临时目录，本版本不进行宽泛递归清理；确认无更新器运行后可正常清理这些 Temp 文件。

## 发布维护

```cmd
build.cmd
publish.cmd "完整路径\private.dpapi"
```

`publish.cmd` 构建 Release x64、运行只含模拟修改入口的单元检查、签名并生成版本及校验清单，统一覆盖构建产物 `dist\NetPilotLite.exe`。它不启动正式程序，也不更新用户运行中的旧目录。

本机工作区的发布私钥保留在 `..\work\lite-release-key\private.dpapi`（以 NetPilotLite 项目目录为起点）。可运行 `publish.cmd "..\work\lite-release-key\private.dpapi"` 继续发布。该文件不在源码包内；请保留当前账户的密钥和 DPAPI 恢复材料。
`ReleaseTool.exe keygen private.dpapi assets\update-key.bin` **只用于建立新的独立信任链**。已交付的信任公钥不能随便替换，否则原用户无法验证后续更新。私钥由当前 Windows 用户的 DPAPI 保护，置于项目以外的私有工作目录；不在源码包、二进制包或资源中。后续发布必须保管好同一私钥及其 Windows DPAPI 恢复材料；换构建账户/机器前需要安全迁移密钥，不能通过重新生成密钥代替。

签名清单不是 Windows Authenticode 证书。当前 EXE 未购买/配置 Authenticode 证书，Windows UAC/SmartScreen 仍可能显示“未知发布者”。本地更新信任来自首次可信程序中固定的发布公钥；SHA-256 文本清单本身不承担认证功能。

## 验证边界

见 `TEST_RESULTS.md`。实际执行了工作区中的文件与进程更新测试，候选程序均编译移除真实网卡修改入口。没有在用户正式运行目录执行更新，没有在 Program Files 实测，没有操作实际 UAC 对话框，也没有物理断电测试。故障注入验证的是可重现的事务恢复逻辑，不能等同硬件断电或隔离 Windows 虚拟机验收。
