/* Pure mapping cases plus real Windows endpoint queries. Sockets bind ONLY loopback;
   no adapter, route, firewall, DNS or proxy configuration is modified. */
static SOCKADDR_INET route_address(ADDRESS_FAMILY family, const WCHAR *ip, ULONG scope) {
    SOCKADDR_INET a = {0};
    a.si_family = family;
    InetPtonW(family, ip, family == AF_INET ? (void *)&a.Ipv4.sin_addr : (void *)&a.Ipv6.sin6_addr);
    if (family == AF_INET6)
        a.Ipv6.sin6_scope_id = scope;
    return a;
}
static void mapping_tests(void) {
    NP_SOURCE sources[2] = {0};
    NP_CONNECTION c = {0};
    sources[0].address = route_address(AF_INET, L"192.0.2.12", 0);
    sources[0].guid.Data1 = 12;
    sources[0].physical = TRUE;
    wcscpy_s(sources[0].name, 128, L"Ethernet test");
    c.local = sources[0].address;
    c.remote = route_address(AF_INET, L"198.51.100.20", 0);
    np_map_source(&c, sources, 1);
    CHECK(c.source_known && c.physical && !c.local_only && c.source_guid.Data1 == 12);
    NP_RULE r = {0};
    wcscpy_s(r.path, NP_PATH, L"C:\\Apps\\浏览器 A.exe");
    wcscpy_s(c.exe, NP_PATH, r.path);
    CHECK(np_rule_evaluate(&r, &c, &sources[0].guid, TRUE) == NP_RULE_OFF);
    r.enabled = TRUE;
    CHECK(np_rule_evaluate(&r, &c, &sources[0].guid, TRUE) == NP_RULE_SOURCE_MATCH);
    CHECK(wcsstr(np_rule_result_text(NP_RULE_SOURCE_MATCH), L"未验证") != NULL);
    GUID other = {99};
    CHECK(np_rule_evaluate(&r, &c, &other, TRUE) == NP_RULE_SOURCE_MISMATCH);
    CHECK(np_rule_evaluate(&r, &c, &other, FALSE) == NP_RULE_UNAVAILABLE);
    CHECK(np_rule_evaluate(&r, NULL, &other, TRUE) == NP_RULE_UNKNOWN);
    c.route_virtual = TRUE;
    CHECK(np_rule_evaluate(&r, &c, &sources[0].guid, TRUE) == NP_RULE_UNKNOWN);
    c.route_virtual = FALSE;
    c.exe[0] = 0;
    CHECK(np_rule_evaluate(&r, &c, &sources[0].guid, TRUE) == NP_RULE_UNKNOWN);
    wcscpy_s(c.exe, NP_PATH, L"C:\\Another\\浏览器 A.exe");
    CHECK(np_rule_evaluate(&r, &c, &sources[0].guid, TRUE) == NP_RULE_UNKNOWN);
    wcscpy_s(c.exe, NP_PATH, r.path);
    r.target = 2;
    CHECK(np_rule_evaluate(&r, &c, NULL, FALSE) == NP_RULE_DEFAULT);
    r.target = 0;
    sources[1] = sources[0];
    sources[1].guid = other;
    np_map_source(&c, sources, 2);
    CHECK(!c.source_known && !c.physical && c.source_guid.Data1 == 0);
    sources[0].physical = FALSE;
    np_map_source(&c, sources, 1);
    CHECK(c.source_known && !c.physical);
    CHECK(np_rule_evaluate(&r, &c, &sources[0].guid, TRUE) == NP_RULE_UNKNOWN);
    sources[0].physical = TRUE;
    c.local = route_address(AF_INET, L"0.0.0.0", 0);
    np_map_source(&c, sources, 1);
    CHECK(!c.source_known && !c.physical);
    c.local = route_address(AF_INET, L"127.0.0.1", 0);
    np_map_source(&c, sources, 1);
    CHECK(c.local_only && !c.physical && !c.source_known);
    c.local = sources[0].address;
    c.remote = route_address(AF_INET, L"127.0.0.1", 0);
    np_map_source(&c, sources, 1);
    CHECK(c.local_only && !c.physical);
    c.remote = route_address(AF_INET6, L"2001:db8::22", 0);
    c.local = route_address(AF_INET6, L"::ffff:192.0.2.12", 0);
    np_map_source(&c, sources, 1);
    CHECK(c.source_known && c.physical && c.source_guid.Data1 == 12);
    c.local = route_address(AF_INET6, L"::ffff:127.0.0.1", 0);
    np_map_source(&c, sources, 1);
    CHECK(c.local_only && !c.source_known);
    c.local = route_address(AF_INET6, L"::1", 0);
    np_map_source(&c, sources, 1);
    CHECK(c.local_only && !c.physical);
    c.local = route_address(AF_INET6, L"::", 0);
    np_map_source(&c, sources, 1);
    CHECK(!c.source_known);
    sources[0].address = route_address(AF_INET6, L"fe80::1234", 7);
    c.local = route_address(AF_INET6, L"fe80::1234", 0);
    np_map_source(&c, sources, 1);
    CHECK(!c.source_known);
    c.local.Ipv6.sin6_scope_id = 8;
    np_map_source(&c, sources, 1);
    CHECK(!c.source_known);
    c.local.Ipv6.sin6_scope_id = 7;
    np_map_source(&c, sources, 1);
    CHECK(c.source_known && c.physical);
    sources[0].address = route_address(AF_INET6, L"2001:db8::1234", 0);
    c.local = sources[0].address;
    np_map_source(&c, sources, 1);
    CHECK(c.source_known && c.physical);
    c.udp = TRUE;
    np_map_source(&c, sources, 1);
    CHECK(wcsstr(c.note, L"QUIC") != NULL && wcsstr(c.note, L"未验证") != NULL);
    CHECK(!wcscmp(np_rule_result_text((NP_RULE_RESULT)-1), np_rule_result_text(NP_RULE_UNKNOWN)));
    wprintf(L"Synthetic cases cover physical/virtual/unknown sources, duplicate IPs, IPv4-mapped IPv6, "
            L"link-local scopes, proxy loopback and non-enforcing rules.\n");
}
static void loopback_tables(ADDRESS_FAMILY family) {
    SOCKET listener = INVALID_SOCKET, client = INVALID_SOCKET, server = INVALID_SOCKET, udp = INVALID_SOCKET;
    NP_MONITOR *m = calloc(1, sizeof(*m));
    CHECK(m != NULL);
    if (!m)
        return;
    SOCKADDR_INET addr = route_address(family, family == AF_INET ? L"127.0.0.1" : L"::1", 0);
    int len = family == AF_INET ? sizeof(SOCKADDR_IN) : sizeof(SOCKADDR_IN6);
    listener = socket(family, SOCK_STREAM, IPPROTO_TCP);
    client = socket(family, SOCK_STREAM, IPPROTO_TCP);
    udp = socket(family, SOCK_DGRAM, IPPROTO_UDP);
    CHECK(listener != INVALID_SOCKET && client != INVALID_SOCKET && udp != INVALID_SOCKET);
    if (listener == INVALID_SOCKET || client == INVALID_SOCKET || udp == INVALID_SOCKET)
        goto cleanup;
    int b = bind(listener, (SOCKADDR *)&addr, len);
    CHECK(b == 0);
    if (b)
        goto cleanup;
    b = listen(listener, 1);
    CHECK(b == 0);
    if (b)
        goto cleanup;
    int n = len;
    b = getsockname(listener, (SOCKADDR *)&addr, &n);
    CHECK(b == 0);
    if (b)
        goto cleanup;
    b = connect(client, (SOCKADDR *)&addr, len);
    CHECK(b == 0);
    if (b)
        goto cleanup;
    server = accept(listener, NULL, NULL);
    CHECK(server != INVALID_SOCKET);
    if (server == INVALID_SOCKET)
        goto cleanup;
    SOCKADDR_INET udp_addr = route_address(family, family == AF_INET ? L"127.0.0.1" : L"::1", 0);
    b = bind(udp, (SOCKADDR *)&udp_addr, len);
    CHECK(b == 0);
    if (b)
        goto cleanup;
    n = len;
    b = getsockname(udp, (SOCKADDR *)&udp_addr, &n);
    CHECK(b == 0);
    if (b)
        goto cleanup;
    CHECK(np_monitor_scan(m, NULL));
    CHECK(m->errors[family == AF_INET ? 0 : 1] == 0 && m->errors[family == AF_INET ? 2 : 3] == 0);
    unsigned tcp_found = 0, udp_found = 0;
    BOOL pid_path = TRUE, loopback_safe = TRUE;
    WCHAR exe[NP_PATH];
    GetModuleFileNameW(NULL, exe, NP_PATH);
    for (size_t i = 0; i < m->count; ++i) {
        NP_CONNECTION *c = &m->rows[i];
        if (c->pid != GetCurrentProcessId() || c->local.si_family != family)
            continue;
        BOOL t =
            !c->udp && c->state == MIB_TCP_STATE_ESTAB &&
            (c->local.Ipv4.sin_port == addr.Ipv4.sin_port || c->remote.Ipv4.sin_port == addr.Ipv4.sin_port);
        BOOL u = c->udp && c->local.Ipv4.sin_port == udp_addr.Ipv4.sin_port;
        if (t || u) {
            tcp_found += t;
            udp_found += u;
            pid_path &= !_wcsicmp(c->exe, exe);
            loopback_safe &= c->local_only && !c->physical && !c->source_known;
            if (u)
                CHECK(wcsstr(c->remote_text, L"未知") != NULL);
        }
    }
    CHECK(tcp_found == 2);
    CHECK(udp_found == 1);
    CHECK(pid_path);
    CHECK(loopback_safe);
    wprintf(L"Real Windows IPv%u loopback: TCP established=%u, UDP bound=%u; PID/path and loopback "
            L"classification checked; external packets=0.\n",
            family == AF_INET ? 4 : 6, tcp_found, udp_found);
cleanup:
    if (server != INVALID_SOCKET)
        closesocket(server);
    if (client != INVALID_SOCKET)
        closesocket(client);
    if (listener != INVALID_SOCKET)
        closesocket(listener);
    if (udp != INVALID_SOCKET)
        closesocket(udp);
    free(m);
}
static int routing_tests(void) {
    mapping_tests();
    WSADATA wsa;
    int started = WSAStartup(MAKEWORD(2, 2), &wsa);
    CHECK(started == 0);
    if (!started) {
        loopback_tables(AF_INET);
        loopback_tables(AF_INET6);
        WSACleanup();
    }
    wprintf(L"Routing checks: %u passed, %u failed. Physical network configuration writes=0; no WFP filters "
            L"created.\n",
            passed, failed);
    return failed ? 1 : 0;
}
static int routing_probe(void) {
    NP_BACKEND b = np_windows_backend();
    NP_SNAPSHOT s = {0};
    NP_ERROR error = {0};
    if (!b.scan(NULL, &s, &error))
        return 1;
    NP_MONITOR *m = calloc(1, sizeof(*m));
    if (!m)
        return 1;
    BOOL ok = np_monitor_scan(m, &s);
    size_t local = 0, physical = 0, unknown = 0, virtual_source = 0, udp = 0, virtual_route = 0;
    for (size_t i = 0; i < m->count; ++i) {
        NP_CONNECTION *c = &m->rows[i];
        local += c->local_only;
        physical += c->source_known && c->physical;
        unknown += !c->source_known && !c->local_only;
        virtual_source += c->source_known && !c->physical;
        udp += c->udp;
        virtual_route += c->route_virtual;
    }
    wprintf(L"READ ONLY current-compartment endpoint snapshot: total=%zu truncated=%d TCP4/TCP6/UDP4/UDP6 "
            L"errors=%lu/%lu/%lu/%lu source-error=%lu\n",
            m->count, m->truncated, m->errors[0], m->errors[1], m->errors[2], m->errors[3], m->source_error);
    wprintf(L"Loopback=%zu physical-source=%zu virtual-or-unconfirmed-source=%zu unknown-source=%zu UDP=%zu "
            L"virtual-route-estimate=%zu\n",
            local, physical, virtual_source, unknown, udp, virtual_route);
    wprintf(L"No process names, IPs, GUIDs or endpoints persisted. No adapter, route, firewall, proxy or DNS "
            L"changes. Physical egress NOT verified.\n");
    free(m);
    return ok ? 0 : 1;
}
