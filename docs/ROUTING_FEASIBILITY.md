# Stage B：Windows 应用网络分流可行性与边界

2026-10-24；在 Stage A 独立发布包生成后开始。当前交付范围是只读监控和可模拟的观察规则；没有安装过滤规则、服务、驱动，也没有修改路由。

## 结论

**不能把“给任意 EXE 选择一张网卡”作为本版已经可靠提供的能力。** 支持应用自身配置 socket 的场景与未经修改的任意第三方程序，应分别设计。普通用户态管理程序可以配置系统提供的过滤能力；普通非管理员通常无权修改这些策略。用户态并不等于无需提权，过滤也不等于重新选路。

| 能力 | Windows 支持范围 | 本版决定 |
|---|---|---|
| 应用识别 | ALE 应用身份、TCP/UDP 所属 PID、进程镜像路径 | 只读快照；处理进程退出、权限拒绝及 PID 快照竞争 |
| socket 指定接口 | 自有/协作应用可使用 IP_UNICAST_IF、IPV6_UNICAST_IF；IPv4 参数网络字节序，IPv6 主机字节序 | 不注入或改写第三方 socket |
| 应用过滤/阻止 | WFP 的不同层支持不同条件；系统防火墙也有程序、接口等规则 | 留待隔离验证，未安装规则 |
| 物理出口约束 | 源地址所属接口、选路估计、真实物理出站是不同证据；弱主机模型、隧道、代理等会影响判断 | 永不把地址匹配直接标为“出口已验证” |
| 路由选择 | IP Helper 查询目标/源地址的路由；普通路由表没有任意 PID 条件 | 不改全局 Metric 或默认路由 |
| 重定向/自定义数据包处理 | WFP connect/bind redirect 由 ALE callout driver 实现，可配合代理；还需处理重定向关联、UDP、IPv6和其他过滤器 | 不自动安装内核驱动或透明代理 |

依据：[Winsock IPv4](https://learn.microsoft.com/en-us/windows/win32/winsock/ipproto-ip-socket-options)、[IPv6](https://learn.microsoft.com/en-us/windows/win32/winsock/ipproto-ipv6-socket-options)、[ALE](https://learn.microsoft.com/en-us/windows/win32/fwp/application-layer-enforcement--ale-)、[WFP 架构](https://learn.microsoft.com/en-us/windows/win32/fwp/windows-filtering-platform-architecture-overview)。

## 十项技术问题的回答

1. **任意 Win32 程序固定物理出口？** 没有一个已验证的通用用户态开关可覆盖所有第三方程序、代理、隧道与协议。拥有应用 socket 的协作方案可以更可靠地控制它自己的发送接口；仍需检验驱动、系统策略及隧道边界。
2. **需要程序支持绑定吗？** 直接的 socket 方案需要应用支持。`bind` 选择本地地址与接口约束并非完全等价；不能仅更改 IP 地址便宣称物理隔离。
3. **用户态可做什么？** 查询连接、匹配地址、估计路由、管理经授权的原生过滤策略，或提供应用主动配置的代理。不能据此声称任意程序已改道。
4. **何时需要 callout driver？** WFP 的透明 connect/bind 重定向、定制包检查、修改与重注入属于 callout 场景。基本 allow/block 不必因此自行编写驱动。驱动也不会自动解决所有 VPN/代理兼容问题。[Microsoft 重定向说明](https://learn.microsoft.com/en-us/windows-hardware/drivers/network/using-bind-or-connect-redirection)
5. **仅改路由表能按进程分流吗？** 普通目标路由条目不携带进程选择条件。改 Metric 影响其他应用，不能用于伪装此功能。[GetBestRoute2](https://learn.microsoft.com/en-us/windows/win32/api/netioapi/nf-netioapi-getbestroute2)
6. **VPN/TUN 会改变边界吗？** 会。观察到虚拟接口不能推出隧道服务器的物理出口；Full Tunnel 与 Split Tunnel 要按每个连接/目的地址分别验证。其他网络隔离区的路由也不能用当前隔离区的查询作为事实。
7. **DNS、IPv6、UDP、QUIC？** DNS 可能由共享服务或应用 DoH 发起；IPv6 是独立协议路径；UDP 表没有远端端点，因此无法从该表还原每条 QUIC 流或其目标。不可把 TCP 验证推广至这些路径。[TCP 表](https://learn.microsoft.com/en-us/windows/win32/api/iphlpapi/nf-iphlpapi-getextendedtcptable)、[UDP 表](https://learn.microsoft.com/en-us/windows/win32/api/iphlpapi/nf-iphlpapi-getextendedudptable)
8. **Wi-Fi 断开怎么办？** 观察模式只报告目标不可用，**不阻止**系统通过其他连接继续联网。未来保护模式应在授权后阻止错误路径，并验证重路由/重授权、既有连接和 IPv4/IPv6；不应声称它会自动转到另一网卡。
9. **如何避免影响其他应用？** 当前只读。未来只对明确的 EXE 身份和验证过的条件配置过滤，使用单独事务/会话、明确删除接口、记录规则标识，拒绝泛化到全局路由。
10. **无自建内核驱动的可靠最小功能？** 当前的连接/端点快照、源地址映射、路由估计与规则差异诊断。应用主动支持的 socket/代理配置另行验证；系统原生阻断策略要先在隔离机测试，不在用户日用机试错。

## Mode A / B / C

- **Mode A — Monitor Only：** 显示进程镜像、PID、协议、本地/远端地址、源地址所属接口、路由估计和未验证说明。IPv4/IPv6 分开枚举；UDP 的远端明确标为未知。进入该页时读取，此页打开期间每 10 秒刷新，也可主动刷新；后台线程查询，无反向 DNS、无探测包、无持续日志。
- **Mode B — Application Rules：** 本版称“观察规则”。用户选择真实 EXE，期望 Ethernet/Wi-Fi/System Default；默认关闭，开启仅参与诊断比较。按完整路径匹配，零条预置规则。可删除、恢复默认、测试规则。状态始终区分“源地址匹配”与“物理出口未验证”，不显示“分流成功”。
- **Mode C — Protected Rules：** 未实现/未启用。WFP 动态会话可在结束时自动删除对象，但这种退出后放行的语义与跨崩溃持续保护存在取舍；不应同时承诺二者。若将来加入，需验证 BFE 中断、进程崩溃、重启、远程会话及完整清理。[动态会话](https://learn.microsoft.com/en-us/windows/win32/api/fwpmtypes/ns-fwpmtypes-fwpm_session0)

接口条件必须按层查询，不把任意条件放到不支持的层，也不把某层的本地接口条件直接解释成隧道外层的物理接口。[过滤层条件](https://learn.microsoft.com/en-us/windows/win32/fwp/filtering-conditions-available-at-each-filtering-layer)、[系统防火墙接口规则](https://learn.microsoft.com/en-us/windows/win32/api/netfw/nf-netfw-inetfwrule-get_interfaces)。

## Clash / VPN 验证矩阵

| 场景 | 可观察内容 | 不能推出的结论 | 当前验证 |
|---|---|---|---|
| 直接 TCP / IPv4、IPv6 | socket 端点、PID、源地址所属接口、当前路由估计 | 已观察到实际发出的物理包 | 模拟映射 + 本机只读表 |
| System Proxy | 应用到本地地址、代理自己的远端连接 | 代理远端流量必然对应某一应用 PID | 模拟本机连接边界；未切换 Clash 模式 |
| Clash TUN / Full VPN | 虚拟源接口或虚拟路由提示 | 虚拟接口就是 Wi-Fi/Ethernet | 模拟；没有真实模式验收 |
| Split Tunnel | 按目的地址不同的路由估计 | 全应用同一出口 | 模拟；没有真实模式验收 |
| UDP / QUIC | 本地 UDP 端点及 PID | 远端、每个数据包出口和应用层请求 | 表结构及模拟边界验证 |
| 目标接口中断 | 当前网卡/连接快照 | 已阻止回落到另一网卡 | 模拟诊断，未阻断 |

没有可用隔离 Windows 环境，所以不实施 B6–B8 的真实过滤控制验收。该限制不会阻碍已交付的 Stage A。没有更改 Clash 订阅、节点、代理/TUN 开关、VPN、DNS、网关、Metric 或任何真实网卡状态。
