#include "lite.h"
#include <string.h>
#include <wchar.h>

void np_mock_init(NP_MOCK *m) {
    memset(m, 0, sizeof(*m));
    m->snapshot.count = 3;
    for (size_t i = 0; i < 2; ++i) {
        NP_ADAPTER *a = &m->snapshot.adapters[i];
        a->identity.version = NP_BINDING_VERSION;
        a->identity.guid.Data1 = (DWORD)i + 100;
        swprintf_s(a->identity.instance, NP_ID_LEN, L"PCI\\MOCK_%zu", i);
        a->physical_ethernet = TRUE;
        a->intel_i219 = i == 0;
        a->state = NP_ENABLED_CONNECTED;
        wcscpy_s(a->name, NP_NAME_LEN,
                 i ? L"模拟备用物理以太网" : L"Intel(R) Ethernet Connection (16) I219-V");
    }
    NP_ADAPTER *wifi = &m->snapshot.adapters[2];
    wifi->identity.version = 1;
    wifi->identity.guid.Data1 = 102;
    wcscpy_s(wifi->identity.instance, NP_ID_LEN, L"PCI\\MOCK_WIFI");
    wifi->physical_wifi = TRUE;
    wifi->intel_ax203 = TRUE;
    wifi->state = NP_ENABLED_CONNECTED;
    wcscpy_s(wifi->name, NP_NAME_LEN, L"Intel(R) Wi-Fi 6 AX203");
    wcscpy_s(wifi->ssid, _countof(wifi->ssid), L"Phone Hotspot (Mock)");
    wcscpy_s(wifi->ip, _countof(wifi->ip), L"192.0.2.10");
    wcscpy_s(m->snapshot.wifi, _countof(m->snapshot.wifi), L"Wi-Fi：已启用 · 已连接（模拟）");
}
static BOOL mock_scan(void *ctx, NP_SNAPSHOT *out, NP_ERROR *error) {
    NP_MOCK *m = ctx;
    ++m->reads;
    if (m->fail_read) {
        np_error(error, ERROR_READ_FAULT, 0, L"模拟读取失败");
        return FALSE;
    }
    *out = m->snapshot;
    return TRUE;
}
static BOOL mock_set(void *ctx, const NP_BINDING *binding, BOOL enable, NP_ERROR *error) {
    NP_MOCK *m = ctx;
    ++m->writes;
    m->last_target = *binding;
    if (m->fail_write) {
        np_error(error, ERROR_ACCESS_DENIED, 0x33, L"模拟驱动拒绝");
        return FALSE;
    }
    for (size_t i = 0; i < m->snapshot.count; ++i)
        if (np_identity_equal(&m->snapshot.adapters[i].identity, binding)) {
            if (!m->ignore_write)
                m->snapshot.adapters[i].state = enable ? NP_ENABLED_DISCONNECTED : NP_DISABLED;
            return TRUE;
        }
    np_error(error, ERROR_NOT_FOUND, 0, L"模拟目标丢失");
    return FALSE;
}
static BOOL mock_local(void *ctx) {
    return !((NP_MOCK *)ctx)->remote;
}
static ULONGLONG mock_now(void *ctx) {
    return ((NP_MOCK *)ctx)->clock;
}
static void mock_pause(void *ctx, DWORD ms) {
    ((NP_MOCK *)ctx)->clock += ms;
}
NP_BACKEND np_mock_backend(NP_MOCK *m) {
    NP_BACKEND b = {m, mock_scan, mock_set, mock_local, mock_now, mock_pause};
    return b;
}
