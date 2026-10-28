#include "lite.h"
#include <string.h>
#include <wchar.h>

BOOL np_identity_equal(const NP_BINDING *a, const NP_BINDING *b) {
    return a && b && a->version == NP_BINDING_VERSION && b->version == NP_BINDING_VERSION &&
           !memcmp(&a->guid, &b->guid, sizeof(GUID)) && a->instance[0] && b->instance[0] &&
           !_wcsicmp(a->instance, b->instance);
}
const NP_ADAPTER *np_find(const NP_SNAPSHOT *snapshot, const NP_BINDING *binding) {
    const NP_ADAPTER *found = NULL;
    for (size_t i = 0; i < snapshot->count; ++i) {
        if (np_identity_equal(&snapshot->adapters[i].identity, binding)) {
            if (found)
                return NULL; /* Ambiguous identities never authorize a mutation. */
            found = &snapshot->adapters[i];
        }
    }
    return found;
}
int np_auto_select(const NP_SNAPSHOT *s) {
    int found = -1;
    for (size_t i = 0; i < s->count; ++i)
        if (s->adapters[i].physical_ethernet && s->adapters[i].intel_i219) {
            if (found >= 0)
                return -1;
            found = (int)i;
        }
    return found;
}
BOOL np_enabled(NP_STATE s) {
    return s == NP_ENABLED_DISCONNECTED || s == NP_ENABLED_CONNECTED || s == NP_ENABLED_LINK_UNKNOWN;
}
int np_auto_wifi(const NP_SNAPSHOT *s) {
    int found = -1;
    for (size_t i = 0; i < s->count; ++i)
        if (s->adapters[i].physical_wifi && s->adapters[i].intel_ax203) {
            if (found >= 0)
                return -1;
            found = (int)i;
        }
    return found;
}
const WCHAR *np_state_text(NP_STATE s) {
    switch (s) {
    case NP_DISABLED:
        return L"已禁用";
    case NP_INTERFACE_DISABLED:
        return L"已禁用（网络接口）";
    case NP_ENABLED_CONNECTED:
        return L"已启用 · 链路已连接";
    case NP_ENABLED_DISCONNECTED:
        return L"已启用 · 链路未连接";
    case NP_ENABLED_LINK_UNKNOWN:
        return L"已启用 · 链路状态暂不可读";
    case NP_DEVICE_ERROR:
        return L"设备异常 · 请查看设备管理器";
    default:
        return L"状态未知 · 暂不可操作";
    }
}
void np_error(NP_ERROR *e, DWORD win32, DWORD cr, const WCHAR *op) {
    memset(e, 0, sizeof(*e));
    e->win32 = win32;
    e->config_ret = cr;
    wcsncpy_s(e->operation, _countof(e->operation), op, _TRUNCATE);
}
NP_RESULT np_execute(NP_BACKEND *backend, const NP_BINDING *binding, BOOL enable, DWORD timeout) {
    NP_RESULT result = {0};
    NP_SNAPSHOT snapshot;
    NP_ERROR read_error = {0};
    const NP_ADAPTER *a;
    if (!backend->scan(backend->context, &snapshot, &result.error))
        return result;
    a = np_find(&snapshot, binding);
    if (!a || (!a->physical_ethernet && !a->physical_wifi)) {
        np_error(&result.error, ERROR_NOT_FOUND, 0, L"校验绑定的物理网卡");
        return result;
    }
    result.observed = TRUE;
    result.state = a->state;
    if (!enable && !backend->safe_local_session(backend->context)) {
        np_error(&result.error, ERROR_ACCESS_DENIED, 0, L"远程桌面或会话状态不确定，拒绝禁用");
        return result;
    }
    if ((enable && np_enabled(a->state)) || (!enable && a->state == NP_DISABLED)) {
        result.success = TRUE;
        result.unchanged = TRUE;
        return result;
    }
    if (!np_enabled(a->state) && a->state != NP_DISABLED && a->state != NP_INTERFACE_DISABLED) {
        np_error(&result.error, ERROR_NOT_READY, 0, L"设备真实状态不可安全操作");
        return result;
    }
    BOOL applied = backend->set_enabled(backend->context, binding, enable, &result.error);
    ULONGLONG start = backend->now(backend->context);
    /* Always re-enumerate, including when the API reports failure. No automatic restoration. */
    for (;;) {
        if (backend->scan(backend->context, &snapshot, &read_error)) {
            a = np_find(&snapshot, binding);
            result.observed = a != NULL;
            if (a)
                result.state = a->state;
            if (!applied)
                return result;
            if (a && ((enable && np_enabled(a->state)) || (!enable && a->state == NP_DISABLED))) {
                result.success = TRUE;
                return result;
            }
        } else {
            result.observed = FALSE;
            if (!applied)
                return result;
        }
        if (backend->now(backend->context) - start >= timeout) {
            if (read_error.win32)
                result.error = read_error;
            else
                np_error(&result.error, WAIT_TIMEOUT, 0, L"操作已请求，但实际状态未达到目标；未自动恢复");
            return result;
        }
        backend->pause(backend->context, 200);
    }
}
NP_RESULT np_hotspot(NP_BACKEND *b, const NP_BINDING *eth, const NP_BINDING *wifi, BOOL allow_enable,
                     DWORD timeout) {
    NP_RESULT r = {0};
    NP_SNAPSHOT s;
    const NP_ADAPTER *w, *e;
    if (!b->scan(b->context, &s, &r.error))
        return r;
    w = np_find(&s, wifi);
    e = np_find(&s, eth);
    if (!w || !w->physical_wifi || !e || !e->physical_ethernet) {
        np_error(&r.error, ERROR_NOT_FOUND, 0, L"热点模式需要已绑定的物理 Ethernet 和 Wi-Fi");
        return r;
    }
    if (!np_enabled(w->state) && allow_enable) {
        r = np_execute(b, wifi, TRUE, timeout);
        if (!r.success)
            return r;
        if (!b->scan(b->context, &s, &r.error)) {
            r.success = FALSE;
            return r;
        }
        w = np_find(&s, wifi);
    }
    /* Link must already exist. Enabling Wi-Fi alone does not prove a hotspot is connected. */
    if (!w || w->state != NP_ENABLED_CONNECTED) {
        r.success = FALSE;
        np_error(&r.error, ERROR_NOT_CONNECTED, 0, L"请先在 Windows 中连接手机热点；有线网卡保持原状");
        return r;
    }
    return np_execute(b, eth, FALSE, timeout);
}
