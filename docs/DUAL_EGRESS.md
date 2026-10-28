# NetPilot Lite 双出口原型 · 使用与构建

版本：0.1.0.0；日期：2026-10-28。当前交付范围是环境审计、配置预览、只读连接诊断。

## 打开与使用

双击 `NetPilotLite.DualEgress.exe`。它是独立诊断原型，不覆盖现有 `NetPilotLite.exe`，无需安装、.NET、服务或计划任务，也不请求管理员权限。

1. 查看 Ethernet、WLAN、Clash 模式、TUN 和诊断错误。读取失败显示未知，不会据此判断保护有效。
2. “查看 Mihomo 连接”展示 API 报告的应用、协议、规则、节点、入站地址和完整进程路径。向右滚动可查看更多列。API 未返回的字段显示未报告。
3. “查看核心 sockets”展示命名管道服务端 PID 对应的 Windows TCP/UDP IPv4/IPv6 表，区分源地址归属和路由推测。所有实际物理出口仍标为“未验证”。
4. 从运行中的应用列表或 EXE 文件选择器添加应用，选择 Ethernet Direct、Wi-Fi Proxy 或 System Default。所有选择只保存在本次窗口内存中。
5. 选择一个代理节点，导出到一个**新的** `.js` 文件。不会覆盖已有脚本，也不会自动应用。界面当前支持一个来源节点；脚本策略格式可表达多个节点。
6. 退出会丢弃内存中的选择。原型没有应用配置，所以不需要执行网络恢复；导出的文件不会被自动加载。

“启用分流”在本阶段不可用。原正式版的网卡按钮、Princess 主题和 Acrylic 托盘未修改。正式界面集成要等双物理出口实测通过后进行。

**不要把示例预览脚本直接当作完成配置。** 附带 `policy-example.json` 使用 `C:\NetPilot-Demo\game.exe`、`ai.exe` 演示路径，不是用户已选择的真实应用。原型不会在启动时修改网卡或 Clash。

## 预览脚本的含义

- 新增 `NPL-Dual-62d910 / Campus Direct`：direct，绑定实际别名“以太网”，允许 UDP，双地址族候选。
- 为选定的原节点生成独立副本，只改变名称和 `interface-name: WLAN`；原节点和协议参数保留。
- 新增 Hotspot Proxy 选择组，成员只有受约束副本和 REJECT，不加入 DIRECT 或未绑定原节点。
- 把用户选择的 PROCESS-PATH / PROCESS-NAME 规则放在原 rules 前，保留原来的规则顺序和 MATCH。
- System Default 的含义是“不新增该应用规则，继续使用原 Mihomo 规则”。它不让应用绕过 TUN。
- 原 DNS、TUN、Provider 和其他策略组不改。仅明确选择的游戏走校园网；其他应用仍遵循原规则，不能保证它们不使用热点。
- 实际 `interface-name` 使用 Windows 别名；附带 GUID 仅供 NetPilot/人工核验。Mihomo 不会检查脚本里的 GUID。网卡改名、换机、选错 Wi-Fi 后必须重新审查。

Clash Verge 2.5.2 会在脚本之后恢复界面选择的 mode，脚本异常也可能被跳过。因此 `mode: rule` 出现在候选 YAML 中，并不证明最终正在规则模式运行。脚本也不是系统级断网保护器。

## 未来人工应用前必须满足的条件（本次没有执行）

由用户另行授权真实网络变更后，才进入以下验收：

1. 备份现有 Profile 相关 Merge/Script/Rules/Proxies/Groups、控制面设置和最终配置；备份要按用户数据保护，不上传订阅凭据。
2. 确认 I219-V 和 AX203 的 GUID、别名、链路/IP、IPv4/IPv6 默认路由以及 Wi-Fi 确实连接手机热点。当前 I219-V 禁用，需要明确授权才可启用。
3. 通过 Clash Verge 支持的配置增强脚本组合原有脚本，不能盲目覆盖用户已有 `main`，不能直接改远程订阅源。增强执行顺序也可能影响规则优先级。
4. 在 Clash Verge 界面选择 Rule；检查**所有增强完成后的最终 YAML**，再用同版本 Mihomo `-t` 验证。单独运行本预览转换不等价于跑完 Verge 增强链。
5. 经授权应用后读回 `/configs`、`/proxies` 和连接命中规则，确认 mode、节点绑定与规则顺序。验证失败应停止宣称保护生效，恢复备份的增强项和原 mode，再验证旧配置。
6. 分别进行 TCP/UDP、IPv4/IPv6、DNS、节点域名解析、节点切换、热点/校园网断开、TUN 重启实验，关联受控请求与物理接口上的传输证据。不能把出口公网 IP 或本地源地址匹配当成全部证明。

当前没有可靠且经过验证的 Clash Verge 写入事务接口，所以本原型只导出。没有实现或宣称“一键应用/撤销”。删除已导出但未加载的预览文件不会影响网络；将来手动加载后，关闭本原型不会撤销 Clash 的配置。

## 构建

要求 Visual Studio 2022、Desktop development with C++、v143 工具集、Windows 10/11 SDK。应用代码为 C17，CRT 使用 `/MT` 静态链接。libyaml 0.2.5 以源代码静态编译，许可证在 `vendor/libyaml/License`。

打开 `NetPilotLite.sln`，将 `NetPilotLite.DualEgress` 设为启动项目，选择 Release / x64，单独生成该项目。也可在 VS Developer Command Prompt 执行：

```cmd
msbuild NetPilotLite.DualEgress.vcxproj /p:Configuration=Release /p:Platform=x64
bin\Release\NetPilotLite.DualEgress.exe --test
node prototype\dual-egress\transform.test.js
```

测试结果在 `bin/Release/dual-native-tests.txt`；Node 仅用于开发测试，运行 EXE 不需要 Node/Python。

只读摘要导出（输出必须为新文件）：

```cmd
NetPilotLite.DualEgress.exe --audit "D:\Reports\netpilot-dual-audit.txt"
```

若报告显示 Windows 错误 5，表示当前执行环境无法读取现有管道。不要因此启用新的 HTTP 管理端口或放宽管道权限。普通桌面用户运行已实测可读；受限制的自动化执行上下文曾被拒绝。

机器专用候选验证脚本和含凭据备份不上传。`audit_config.py` 为开发用只读元数据审计脚本，需 PyYAML；终端用户运行 EXE 不需要 Python。开发时仅运行 Core 的 `-t` 检查，未加载候选配置。

## 运行边界

只连接固定的本机 `\\.\pipe\verge-mihomo`，允许 GET `/version`、`/configs`、`/connections`、`/proxies`，不提供任意 URL、写入 API 或命令执行入口。HTTP 响应有长度和超时上限；未知协议响应直接失败。不同 Clash 客户端、非默认数据目录和自定义管道暂不支持。

网卡修改函数由 `NP_READONLY_BUILD` 编译为拒绝操作，缺少此定义时原型编译失败。程序的文件写入仅限用户选择的新预览文件/新摘要，以及显式 `--test` 的合成测试文件。导出不包含节点服务器、密码或订阅地址。连接详情仅在本机内存与窗口中展示，不上传。
