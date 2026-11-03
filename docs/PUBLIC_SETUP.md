# NetPilot Lite 1.5.0：单 EXE 安装与更新

用户入口固定为 `NetPilotLite-Setup-x64.exe`。原生 C17、Win32、x64、静态 CRT；没有 MSI、Inno、ZIP 运行依赖，也不注册服务或计划任务。

## 使用

1. 从本仓库正式 Release 下载 Setup，运行后选安装目录，点击开始安装。
2. 默认 `%LOCALAPPDATA%\Programs\NetPilotLite`，也可选择现有安装目录。Program Files 需要标准 UAC，同一 Windows 用户提权；换用另一个管理员账户会拒绝执行，以免写错用户配置。
3. Setup 匿名查询稳定 Release，下载并验证签名清单及每个文件；文件全部准备完成后才退出旧程序。创建当前用户开始菜单快捷方式、登录启动项并启动新版。
4. 主程序登录时只进托盘，后台每个 Windows 内核启动周期检查一次。网络失败最多三次尝试，间隔约 30 秒、90 秒。快速启动/休眠恢复可能仍属于同一内核启动周期。无更新不弹窗；稍后更新本周期不重复；忽略版本直到更高版本。
5. 确认更新后软件自动下载已验签的同版本 Setup，并传入固定版本和原安装目录。下载阶段可以取消或失败后重试；文件替换阶段不允许中断。当前实现失败重新下载，不支持 HTTP Range 断点续传。

公开仓库尚无正式 Release 时会明确报告 404，不会假报“已是最新版本”。旧 1.4 私有 ZIP 客户端不能直接识别本协议，需要首次运行新版 Setup 迁移一次；之后使用新协议自动更新。

## Release 文件

```
NetPilotLite-Setup-x64.exe
NetPilotLite-Setup-x64.exe.update
manifest.json
manifest.json.update
NetPilotLite.exe
NetPilotLite.exe.update
SHA256SUMS.txt
```

只有 Setup 是用户要下载的入口，其余由程序获取。当前主程序静态链接 CRT、资源内嵌，无额外运行 DLL。清单可映射独立资产到 `bin/`、`dependencies/` 或 `resources/` 的子目录；主程序仍为原目录根下固定的 `NetPilotLite.exe`，保留已有快捷方式。

`asset_name` 精确匹配 Release 资产名；`install_path` 是规范化相对路径。拒绝目录穿越、绝对路径、ADS、Windows 保留设备名称、重复名称、文件/目录冲突、链接和硬链接。当前限制 32 个运行文件，单文件 64 MiB，总计 256 MiB；清单 64 KiB。

## 签名与下载

客户端固定 ECDSA P-256 发布公钥，`.update` 为 128 字节二进制签名清单。主程序用途 `NPLREL1`，Setup `NPLSET1`，JSON 清单 `NPLCAT1`；绑定版本、长度、SHA-256，再验证 x64 PE 和 ProductName。SHA256SUMS 仅为便于人工核对，不是可信发布签名的替代品。

HTTPS 使用 WinHTTP、系统代理和受限 GitHub CDN 重定向；证书错误不会忽略。公开请求不读取或发送旧 Token。旧 DPAPI 授权文件作为用户数据保留，但新更新路径不使用它。

首次 Setup 尚未提供商业 Authenticode 证书。请从正确仓库下载；内嵌发布公钥不能证明一个来自不可信来源、被整体替换的首次安装器可信。自动更新会在运行 Setup 前先由已有客户端验证固定公钥签名。

## 原地事务与恢复

下载缓存使用唯一 `SetupCache-{GUID}`。目标同卷事务目录 `.NetPilotSetup-transaction` 中保存新文件、旧文件、先前签名清单、恢复 Setup、阶段日志与快捷方式/登录启动项备份。独占锁禁止两个 Setup 同时修改同一目标。

正常退出请求仅匹配窗口类及目标 EXE 完整路径，不批量结束进程。逐文件同卷替换前写入日志；全部文件复验后，用唯一事件检查新版初始化和进程存活，再提交快捷方式/登录项及 `.NetPilot-install` 元数据。多文件替换不是整目录单一原子操作，日志负责崩溃后的恢复。

失败时恢复旧文件，复验并重启旧版。未完成的事务禁止继续安装，保留证据。正常启动发现日志时启动可信恢复 Setup。主程序不能运行时，可以重新运行官方 Setup 并选择原目录，或：

```
NetPilotLite-Setup-x64.exe --recover --install-dir "D:\Tools\NetPilotLite"
```

旧安装器未清理的 `.NetPilotLite-install-pending` 属于旧协议，不能用新版盲目删除；仍需对应旧版恢复。没有真实断电测试；当前故障注入验证了准备完成和首文件替换后的恢复。

## 用户数据

`%LOCALAPPDATA%\NetPilotLite` 不属于安装清单，配置、背景、主题、绑定、应用观察规则、Mihomo 文件及日志不会被替换。安装目录中的未知同名文件也不覆盖。更新保留用户已经关闭的登录启动选择；开始菜单快捷方式仍指向原 EXE 路径。安装、更新和恢复均不调用网卡状态修改接口。

## 构建与发布

Visual Studio 2022 C++ 工具、Windows SDK，打开 `NetPilotLite.sln`，Release/x64。`build.cmd` 构建 GUI、Setup 和测试程序。保留旧工程作为参考，不用于新发布。

```
powershell -File scripts/build-public.ps1 -SigningKey C:\Secure\private.dpapi -Tag v1.5.0
```

输出 `dist/public` 独立文件，不生成 ZIP。签名密钥不是源码或交付物。

`.github/workflows/release.yml` 在正式 `vMAJOR.MINOR.PATCH` 标签执行构建、测试、签名、真实 Windows 文件事务测试、草稿上传、下载回验，再转为稳定 Release。`release-signing` 环境需配置与固定公钥匹配的 `NETPILOT_SIGNING_KEY_B64`；密钥缺失或不匹配会失败关闭。使用受保护发布环境，限制谁能创建稳定标签和批准签名。

v1.5.0 已通过 publish-prepared.yml 使用本地已签名文件完成正式发布和下载回验。远端签名 Secret 仍未配置；后续标签全自动构建签名发布仍需配置，不应将一次已准备文件的发布等同于未来版本自动签名已经就绪。GitHub Actions 自身 artifact 传输格式由平台控制；项目不创建或上传 ZIP 格式的用户安装/更新 Release 资产。

协议依据：[GitHub Release Assets API](https://docs.github.com/en/rest/releases/assets)。


