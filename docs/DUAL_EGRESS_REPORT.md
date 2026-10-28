# 双出口原型验收摘要（仓库版）

日期：2026-10-28。版本 0.1.0.0。完整现场审计留在本机，本文件不包含本机 GUID、地址、路由、DNS、节点名称、用户进程或订阅信息。

## 已完成

- 原生 C17 / Win32 / Release x64 / 静态 CRT 只读原型。
- 复用现有物理网卡扫描、应用选择与 TCP/UDP IPv4/IPv6 连接观察代码。
- 本机固定 Mihomo 命名管道只读接口，拒绝未知或写入端点。
- 生成自定义 Direct 与选定节点 Wi-Fi 绑定副本，保留原节点/策略组/DNS/TUN/规则。
- 用户选择的进程规则前置；受约束组仅包含已绑定副本及 REJECT。
- 缺失节点不会回退未绑定原节点；未知协议、MPTCP、链式节点不假装绑定成功。
- 无法证明真实物理出口的连接明确显示“未验证”。

## 已执行验证

| 检查 | 结果 |
|---|---|
| 本地 Release x64 构建 | 通过 |
| 原生 HTTP/YAML/导出/只读后端测试 | 30 通过，0 失败 |
| 配置转换模拟测试 | 35 通过，0 失败 |
| 当前匹配版本 Mihomo v1.19.29 `-t` | 退出码 0；只解析候选，不加载 |
| 原配置哈希比对 | 未改变 |
| 私有文件备份恢复演练 | 字节一致；不是运行时回滚验收 |
| 正式版 src 对比原 1.3.0 源码 | 31 个文件未改 |
| 本地 Win32 UI 检查 | 启动、诊断视图、内存应用规则预览已检查 |

## 尚未验证

双物理出口并发、TCP/UDP IPv4/IPv6 实际出口、DNS/节点域名解析路径、热点或校园网中断、节点切换、TUN 重启、完整 Verge 订阅刷新链、真实配置加载失败回滚，均未执行真实变更测试。

配置可解析不代表物理路径正确；Mihomo 入站源地址、Windows 源地址归属及节点公网 IP 都不能单独证明应用实际出口。当前不宣称故障保护有效。

Verge 2.5.2 会在增强脚本之后恢复界面选择的 mode；脚本错误也可能被跳过。因此必须在授权后明确选择 Rule，并核对最终配置和实时状态。原型只导出预览，无自动应用，无 WFP 规则、内核驱动、服务或计划任务。

## 依据与运行说明

[使用和构建](DUAL_EGRESS.md)；[Mihomo Direct](https://wiki.metacubex.one/config/proxies/direct/)；[Windows v1.19.29 接口绑定](https://github.com/MetaCubeX/mihomo/blob/v1.19.29/component/dialer/bind_windows.go)；[Verge v2.5.2 增强处理](https://github.com/clash-verge-rev/clash-verge-rev/blob/v2.5.2/src-tauri/src/enhance/mod.rs)。
