#include "routing.h"
#include <iphlpapi.h>
#include <netioapi.h>
#include <stdio.h>
#include <wchar.h>
#include <stddef.h>
static BOOL empty_address(const SOCKADDR_INET *a) {
    static const BYTE zero[16] = {0};
    return a->si_family == AF_INET ? a->Ipv4.sin_addr.S_un.S_addr == 0
                                   : !memcmp(&a->Ipv6.sin6_addr, zero, 16);
}
static BOOL loopback(const SOCKADDR_INET *a) {
    if (a->si_family == AF_INET)
        return (ntohl(a->Ipv4.sin_addr.S_un.S_addr) >> 24) == 127;
    return IN6_IS_ADDR_LOOPBACK(&a->Ipv6.sin6_addr) ||
           (IN6_IS_ADDR_V4MAPPED(&a->Ipv6.sin6_addr) && a->Ipv6.sin6_addr.u.Byte[12] == 127);
}
static BOOL address_equal(const SOCKADDR_INET *a, const SOCKADDR_INET *b) {
    if (a->si_family != b->si_family)
        return FALSE;
    if (a->si_family == AF_INET)
        return a->Ipv4.sin_addr.S_un.S_addr == b->Ipv4.sin_addr.S_un.S_addr;
    if (IN6_IS_ADDR_LINKLOCAL(&a->Ipv6.sin6_addr) && !a->Ipv6.sin6_scope_id)
        return FALSE;
    return !memcmp(&a->Ipv6.sin6_addr, &b->Ipv6.sin6_addr, 16) &&
           (!a->Ipv6.sin6_scope_id || a->Ipv6.sin6_scope_id == b->Ipv6.sin6_scope_id);
}
void np_map_source(NP_CONNECTION *c, const NP_SOURCE *sources, size_t count) {
    c->source_known = c->physical = FALSE;
    ZeroMemory(&c->source_guid, sizeof(c->source_guid));
    c->local_only = loopback(&c->local) || (!c->udp && loopback(&c->remote));
    wcscpy_s(c->source, _countof(c->source), L"未知 / 未指定源地址");
    wcscpy_s(c->note, _countof(c->note),
             c->udp ? L"UDP 远端未知（含 QUIC）；出口未验证" : L"物理出口未验证；地址匹配不等于出站证明");
    if (c->local_only) {
        wcscpy_s(c->source, _countof(c->source), L"本机回环");
        wcscpy_s(c->note, _countof(c->note), L"本机服务/可能代理；后续出口未验证");
        return;
    }
    if (empty_address(&c->local))
        return;
    SOCKADDR_INET address = c->local;
    if (address.si_family == AF_INET6 && IN6_IS_ADDR_V4MAPPED(&address.Ipv6.sin6_addr)) {
        DWORD v4;
        memcpy(&v4, &address.Ipv6.sin6_addr.u.Byte[12], 4);
        ZeroMemory(&address, sizeof(address));
        address.Ipv4.sin_family = AF_INET;
        address.Ipv4.sin_addr.S_un.S_addr = v4;
    }
    int match = -1;
    for (size_t i = 0; i < count; ++i)
        if (address_equal(&address, &sources[i].address)) {
            if (match >= 0) {
                wcscpy_s(c->source, _countof(c->source), L"地址归属不唯一 / 未验证");
                return;
            }
            match = (int)i;
        }
    if (match >= 0) {
        const NP_SOURCE *s = &sources[match];
        c->source_known = TRUE;
        c->physical = s->physical;
        c->source_guid = s->guid;
        swprintf_s(c->source, _countof(c->source), L"%s · %s", s->name,
                   s->physical ? L"物理" : L"虚拟/未确认");
        if (!c->physical)
            wcscpy_s(c->note, _countof(c->note), L"虚拟/未知接口；不支持验证当前代理或隧道的物理出口");
    }
}
NP_RULE_RESULT np_rule_evaluate(const NP_RULE *r, const NP_CONNECTION *c, const GUID *target,
                                BOOL available) {
    if (!r->enabled)
        return NP_RULE_OFF;
    if (r->family[0])
        return NP_RULE_UNKNOWN;
    if (r->target == 2)
        return NP_RULE_DEFAULT;
    if (!available)
        return NP_RULE_UNAVAILABLE;
    if (!c || !c->exe[0] || _wcsicmp(r->path, c->exe) || !c->source_known || !c->physical || c->local_only ||
        c->route_virtual || !target)
        return NP_RULE_UNKNOWN;
    return !memcmp(target, &c->source_guid, sizeof(GUID)) ? NP_RULE_SOURCE_MATCH : NP_RULE_SOURCE_MISMATCH;
}
const WCHAR *np_rule_result_text(NP_RULE_RESULT r) {
    static const WCHAR *text[] = {L"观察已关闭",
                                  L"系统默认 · 未施加约束",
                                  L"未验证 / 不支持当前代理模式",
                                  L"目标不可用 · 当前没有阻止连接",
                                  L"源地址匹配 · 物理出口未验证",
                                  L"源地址不匹配 · 当前没有阻止连接"};
    return r >= NP_RULE_OFF && r <= NP_RULE_SOURCE_MISMATCH ? text[r] : text[NP_RULE_UNKNOWN];
}
static BOOL physical_guid(const NP_SNAPSHOT *s, const GUID *guid) {
    if (!s)
        return FALSE;
    for (size_t i = 0; i < s->count; ++i)
        if (!memcmp(guid, &s->adapters[i].identity.guid, sizeof(GUID)))
            return s->adapters[i].physical_ethernet || s->adapters[i].physical_wifi;
    return FALSE;
}
static void endpoint(const SOCKADDR_INET *a, WCHAR *text, size_t capacity) {
    WCHAR address[80] = L"?";
    const void *ip =
        a->si_family == AF_INET ? (const void *)&a->Ipv4.sin_addr : (const void *)&a->Ipv6.sin6_addr;
    InetNtopW(a->si_family, ip, address, _countof(address));
    if (a->si_family == AF_INET6)
        swprintf_s(text, capacity, L"[%s%%%lu]:%u", address, a->Ipv6.sin6_scope_id, ntohs(a->Ipv6.sin6_port));
    else
        swprintf_s(text, capacity, L"%s:%u", address, ntohs(a->Ipv4.sin_port));
}
static void process_info(NP_MONITOR *m, NP_CONNECTION *c) {
    /* Cache only within this snapshot; never reuse a PID across refreshes. */
    for (size_t i = 0; i < m->count; ++i)
        if (m->rows[i].pid == c->pid) {
            wcscpy_s(c->exe, NP_PATH, m->rows[i].exe);
            wcscpy_s(c->app, _countof(c->app), m->rows[i].app);
            c->process_error = m->rows[i].process_error;
            return;
        }
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, c->pid);
    DWORD n = NP_PATH;
    c->process_error = ERROR_SUCCESS;
    if (!h || !QueryFullProcessImageNameW(h, 0, c->exe, &n)) {
        c->process_error = GetLastError();
        c->exe[0] = 0;
    }
    if (h)
        CloseHandle(h);
    const WCHAR *name = wcsrchr(c->exe, L'\\');
    if (c->exe[0])
        wcsncpy_s(c->app, _countof(c->app), name ? name + 1 : c->exe, _TRUNCATE);
    else
        swprintf_s(c->app, _countof(c->app), L"未知/已退出 (Win32 %lu)", c->process_error);
}
static void finish_row(NP_MONITOR *m, NP_CONNECTION *c, const NP_SOURCE *sources, size_t ns,
                       const NP_SNAPSHOT *physical) {
    process_info(m, c);
    endpoint(&c->local, c->local_text, _countof(c->local_text));
    if (c->udp)
        wcscpy_s(c->remote_text, _countof(c->remote_text), L"未知（UDP 仅本地端点）");
    else if (c->state == MIB_TCP_STATE_LISTEN)
        wcscpy_s(c->remote_text, _countof(c->remote_text), L"监听 / 无远端");
    else
        endpoint(&c->remote, c->remote_text, _countof(c->remote_text));
    swprintf_s(c->protocol, _countof(c->protocol), L"%s%u%s", c->udp ? L"UDP" : L"TCP",
               c->local.si_family == AF_INET ? 4 : 6,
               c->udp                             ? L""
               : c->state == MIB_TCP_STATE_ESTAB  ? L" 已连接"
               : c->state == MIB_TCP_STATE_LISTEN ? L" 监听"
                                                  : L" 其他状态");
    np_map_source(c, sources, ns);
    wcscpy_s(c->route, _countof(c->route), L"未知 / 不适用");
    if (!c->udp && !c->local_only && !empty_address(&c->remote) && c->state != MIB_TCP_STATE_LISTEN) {
        MIB_IPFORWARD_ROW2 route;
        SOCKADDR_INET best, src = c->local, dst = c->remote;
        src.Ipv4.sin_port = dst.Ipv4.sin_port = 0;
        c->route_error = GetBestRoute2(NULL, 0, &src, &dst, 0, &route, &best);
        if (!c->route_error) {
            MIB_IF_ROW2 row = {0};
            row.InterfaceLuid = route.InterfaceLuid;
            if (!GetIfEntry2(&row)) {
                c->route_virtual = !physical_guid(physical, &row.InterfaceGuid);
                swprintf_s(c->route, _countof(c->route), L"估计：%.100s · %s", row.Alias,
                           c->route_virtual ? L"虚拟/未知" : L"物理");
                if (c->route_virtual)
                    wcscpy_s(c->note, _countof(c->note), L"路由经虚拟/未知接口；物理出口未验证");
            }
        } else
            swprintf_s(c->route, _countof(c->route), L"估计失败：Win32 %lu", c->route_error);
    }
    ++m->count;
}
static void *endpoint_table(BOOL udp, ULONG family, DWORD *bytes, DWORD *error) {
    *bytes = 0;
    void *buffer = NULL;
    for (int attempt = 0; attempt < 4; ++attempt) {
        *error = udp ? GetExtendedUdpTable(buffer, bytes, FALSE, family, UDP_TABLE_OWNER_PID, 0)
                     : GetExtendedTcpTable(buffer, bytes, FALSE, family, TCP_TABLE_OWNER_PID_ALL, 0);
        if (!*error)
            return buffer;
        free(buffer);
        buffer = NULL;
        if (*error != ERROR_INSUFFICIENT_BUFFER || !*bytes || *bytes > 32 * 1024 * 1024)
            break;
        buffer = malloc(*bytes);
        if (!buffer) {
            *error = ERROR_OUTOFMEMORY;
            break;
        }
    }
    free(buffer);
    return NULL;
}
BOOL np_monitor_scan(NP_MONITOR *m, const NP_SNAPSHOT *physical) {
    ZeroMemory(m, sizeof(*m));
    GetLocalTime(&m->time);
    PMIB_UNICASTIPADDRESS_TABLE addresses = NULL;
    m->source_error = GetUnicastIpAddressTable(AF_UNSPEC, &addresses);
    size_t ns = addresses ? addresses->NumEntries : 0;
    NP_SOURCE *sources = ns ? calloc(ns, sizeof(*sources)) : NULL;
    if (ns && !sources) {
        ns = 0;
        m->source_error = ERROR_OUTOFMEMORY;
    }
    for (size_t i = 0; i < ns; ++i) {
        sources[i].address = addresses->Table[i].Address;
        sources[i].luid = addresses->Table[i].InterfaceLuid;
        MIB_IF_ROW2 row = {0};
        row.InterfaceLuid = sources[i].luid;
        if (!GetIfEntry2(&row)) {
            sources[i].guid = row.InterfaceGuid;
            sources[i].physical = physical_guid(physical, &row.InterfaceGuid);
            wcsncpy_s(sources[i].name, _countof(sources[i].name), row.Alias, _TRUNCATE);
        } else
            wcscpy_s(sources[i].name, _countof(sources[i].name), L"接口信息不可读");
    }
    if (addresses)
        FreeMibTable(addresses);
    int ok = 0;
    for (int kind = 0; kind < 4; ++kind) {
        BOOL udp = kind >= 2;
        ULONG family = kind % 2 ? AF_INET6 : AF_INET;
        DWORD bytes;
        BYTE *table = endpoint_table(udp, family, &bytes, &m->errors[kind]);
        if (!table)
            continue;
        if (bytes < sizeof(DWORD)) {
            m->errors[kind] = ERROR_INVALID_DATA;
            free(table);
            continue;
        }
        DWORD count = *(DWORD *)table;
        size_t stride =
            udp ? (family == AF_INET ? sizeof(MIB_UDPROW_OWNER_PID) : sizeof(MIB_UDP6ROW_OWNER_PID))
                : (family == AF_INET ? sizeof(MIB_TCPROW_OWNER_PID) : sizeof(MIB_TCP6ROW_OWNER_PID));
        size_t off = udp ? (family == AF_INET ? offsetof(MIB_UDPTABLE_OWNER_PID, table)
                                              : offsetof(MIB_UDP6TABLE_OWNER_PID, table))
                         : (family == AF_INET ? offsetof(MIB_TCPTABLE_OWNER_PID, table)
                                              : offsetof(MIB_TCP6TABLE_OWNER_PID, table));
        if (bytes < off || count > (bytes - off) / stride) {
            m->errors[kind] = ERROR_INVALID_DATA;
            free(table);
            continue;
        }
        ++ok;
        for (DWORD i = 0; i < count; ++i) {
            if (m->count == NP_MAX_CONNECTIONS) {
                m->truncated = TRUE;
                break;
            }
            NP_CONNECTION *c = &m->rows[m->count];
            c->udp = udp;
            c->local.si_family = c->remote.si_family = (ADDRESS_FAMILY)family;
            BYTE *raw = table + off + i * stride;
            if (!udp && family == AF_INET) {
                MIB_TCPROW_OWNER_PID *r = (void *)raw;
                c->pid = r->dwOwningPid;
                c->state = r->dwState;
                c->local.Ipv4.sin_addr.S_un.S_addr = r->dwLocalAddr;
                c->local.Ipv4.sin_port = (USHORT)r->dwLocalPort;
                c->remote.Ipv4.sin_addr.S_un.S_addr = r->dwRemoteAddr;
                c->remote.Ipv4.sin_port = (USHORT)r->dwRemotePort;
            } else if (!udp) {
                MIB_TCP6ROW_OWNER_PID *r = (void *)raw;
                c->pid = r->dwOwningPid;
                c->state = r->dwState;
                memcpy(&c->local.Ipv6.sin6_addr, r->ucLocalAddr, 16);
                c->local.Ipv6.sin6_scope_id = r->dwLocalScopeId;
                c->local.Ipv6.sin6_port = (USHORT)r->dwLocalPort;
                memcpy(&c->remote.Ipv6.sin6_addr, r->ucRemoteAddr, 16);
                c->remote.Ipv6.sin6_scope_id = r->dwRemoteScopeId;
                c->remote.Ipv6.sin6_port = (USHORT)r->dwRemotePort;
            } else if (family == AF_INET) {
                MIB_UDPROW_OWNER_PID *r = (void *)raw;
                c->pid = r->dwOwningPid;
                c->local.Ipv4.sin_addr.S_un.S_addr = r->dwLocalAddr;
                c->local.Ipv4.sin_port = (USHORT)r->dwLocalPort;
            } else {
                MIB_UDP6ROW_OWNER_PID *r = (void *)raw;
                c->pid = r->dwOwningPid;
                memcpy(&c->local.Ipv6.sin6_addr, r->ucLocalAddr, 16);
                c->local.Ipv6.sin6_scope_id = r->dwLocalScopeId;
                c->local.Ipv6.sin6_port = (USHORT)r->dwLocalPort;
            }
            finish_row(m, c, sources, ns, physical);
        }
        free(table);
    }
    free(sources);
    return ok != 0;
}
void np_monitor_mock(NP_MONITOR *m, const NP_SNAPSHOT *physical) {
    ZeroMemory(m, sizeof(*m));
    GetLocalTime(&m->time);
    NP_SOURCE source = {0};
    source.address.Ipv4.sin_family = AF_INET;
    InetPtonW(AF_INET, L"192.0.2.10", &source.address.Ipv4.sin_addr);
    wcscpy_s(source.name, 128, L"Wi-Fi（模拟）");
    source.physical = TRUE;
    if (physical && physical->count > 2)
        source.guid = physical->adapters[2].identity.guid;
    NP_CONNECTION *c = &m->rows[0];
    c->pid = 1234;
    c->state = MIB_TCP_STATE_ESTAB;
    c->local = source.address;
    c->local.Ipv4.sin_port = htons(53000);
    c->remote.Ipv4.sin_family = AF_INET;
    InetPtonW(AF_INET, L"198.51.100.20", &c->remote.Ipv4.sin_addr);
    c->remote.Ipv4.sin_port = htons(443);
    wcscpy_s(c->exe, NP_PATH, L"C:\\Mock\\Browser.exe");
    wcscpy_s(c->app, 128, L"Browser.exe [模拟]");
    wcscpy_s(c->protocol, 32, L"TCP4 已连接");
    endpoint(&c->local, c->local_text, 100);
    endpoint(&c->remote, c->remote_text, 100);
    np_map_source(c, &source, 1);
    wcscpy_s(c->route, 160, L"估计：Wi-Fi [模拟]");
    m->count = 1;
}
