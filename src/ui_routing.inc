/* Observation UI. There are deliberately no firewall/route mutation entry points. */
static HWND rules_list, connections_list;
static void routing_columns(void) {
    const int rules[] = {260, 120, 200}, connections[] = {170, 70, 120, 220, 220, 240, 240, 380};
    if (IsWindow(rules_list))
        for (int i = 0; i < 3; ++i)
            ListView_SetColumnWidth(rules_list, i, sp(rules[i]));
    if (IsWindow(connections_list))
        for (int i = 0; i < 8; ++i)
            ListView_SetColumnWidth(connections_list, i, sp(connections[i]));
}
static int selected_rule(void) {
    return rules_list ? ListView_GetNextItem(rules_list, -1, LVNI_SELECTED) : -1;
}
static void routing_theme(void) {
    HWND lists[] = {rules_list, connections_list};
    for (int i = 0; i < 2; ++i)
        if (IsWindow(lists[i])) {
            ListView_SetBkColor(lists[i], a.colors.card);
            ListView_SetTextBkColor(lists[i], a.colors.card);
            ListView_SetTextColor(lists[i], a.colors.foreground);
        }
}
static void rules_render(int selected) {
    if (!IsWindow(rules_list))
        return;
    SendMessageW(rules_list, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(rules_list);
    const WCHAR *targets[] = {L"Ethernet", L"Wi-Fi", L"System Default"};
    for (int i = 0; i < a.prefs.rule_count; ++i) {
        NP_RULE *r = &a.prefs.rules[i];
        LVITEMW item = {0};
        item.mask = LVIF_TEXT;
        item.iItem = i;
        item.pszText = r->label[0] ? r->label : r->family[0] ? r->family : r->path;
        ListView_InsertItem(rules_list, &item);
        ListView_SetItemText(rules_list, i, 1, (WCHAR *)targets[r->target]);
        ListView_SetItemText(rules_list, i, 2,
                             r->family[0] ? L"包身份 · 待验证"
                             : r->enabled ? L"观察中 · 未施加约束"
                                          : L"关闭 · 未施加约束");
    }
    if (selected >= 0 && selected < a.prefs.rule_count)
        ListView_SetItemState(rules_list, selected, LVIS_SELECTED | LVIS_FOCUSED,
                              LVIS_SELECTED | LVIS_FOCUSED);
    SendMessageW(rules_list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(rules_list, NULL, FALSE);
}
#include "ui_picker.inc"

typedef struct ROUTING_JOB {
    HWND window;
    BOOL mock;
    NP_SNAPSHOT physical;
} ROUTING_JOB;
static DWORD WINAPI routing_worker(void *param) {
    ROUTING_JOB *j = param;
    NP_MONITOR *m = calloc(1, sizeof(*m));
    if (m) {
        if (j->mock)
            np_monitor_mock(m, &j->physical);
        else
            np_monitor_scan(m, &j->physical);
    }
    if (!PostMessageW(j->window, WM_ROUTING_DONE, 0, (LPARAM)m))
        free(m);
    free(j);
    return 0;
}
static void routing_start(void) {
    if (a.routing_busy || a.quit_pending || !a.settings || a.settings_page != 5)
        return;
    ROUTING_JOB *j = calloc(1, sizeof(*j));
    if (!j)
        return;
    j->window = a.window;
    j->mock = a.mock;
    j->physical = a.snapshot;
    a.routing_busy = TRUE;
    text_if_changed(GetDlgItem(a.settings_content, 370), L"正在后台读取连接快照…只读，不发送探测流量。");
    HANDLE t = CreateThread(NULL, 0, routing_worker, j, 0, NULL);
    if (t)
        CloseHandle(t);
    else {
        a.routing_busy = FALSE;
        free(j);
        text_if_changed(GetDlgItem(a.settings_content, 370), L"无法启动连接读取线程。");
    }
}
static void routing_done(NP_MONITOR *m) {
    a.routing_busy = FALSE;
    free(a.monitor);
    a.monitor = m;
    if (!a.settings || a.settings_page != 5) {
        free(a.monitor);
        a.monitor = NULL;
        return;
    }
    WCHAR text[280];
    if (m)
        swprintf_s(text, _countof(text),
                   L"%02u:%02u:%02u · %zu 个端点%s · TCP4/6、UDP4/6 错误 %lu/%lu/%lu/%lu；源表 "
                   L"%lu\n仅当前网络隔离区快照；路由为估计，物理出口未验证。",
                   m->time.wHour, m->time.wMinute, m->time.wSecond, m->count,
                   m->truncated ? L"（已截断）" : L"", m->errors[0], m->errors[1], m->errors[2], m->errors[3],
                   m->source_error);
    else
        wcscpy_s(text, _countof(text), L"连接快照内存不足。未修改任何网络。");
    text_if_changed(GetDlgItem(a.settings_content, 370), text);
    ListView_SetItemCountEx(connections_list, m ? (int)m->count : 0, LVSICF_NOSCROLL);
    InvalidateRect(connections_list, NULL, FALSE);
}
static void routing_controls(HWND w) {
    slabel(w, 5, 0, L"Monitor Only · 只读观察\n规则只比较快照，不强制改道，也不会阻止连接。");
    sbutton(w, 5, 0, 1, 66, L"从已安装应用选择", 320);
    const WCHAR *targets[] = {L"期望 Ethernet", L"期望 Wi-Fi", L"System Default"};
    sbutton(w, 5, 1, 1, 66, L"从正在运行的程序选择", 328);
    sbutton(w, 5, 0, 1, 114, L"浏览 EXE…", 329);
    scombo(w, 5, 1, 1, 114, 321, targets, 3, 2);
    rules_list = cell(w, 5, 0, 2, 166, 150, WC_LISTVIEWW, L"",
                      LVS_REPORT | LVS_SINGLESEL | WS_TABSTOP | WS_BORDER, 360);
    const WCHAR *names[] = {L"应用完整路径", L"期望网络", L"观察状态"};
    int widths[] = {260, 120, 200};
    for (int i = 0; i < 3; ++i) {
        LVCOLUMNW c = {LVCF_TEXT | LVCF_WIDTH};
        c.pszText = (WCHAR *)names[i];
        c.cx = sp(widths[i]);
        ListView_InsertColumn(rules_list, i, &c);
    }
    ListView_SetExtendedListViewStyle(rules_list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    sbutton(w, 5, 0, 1, 330, L"启用 / 关闭观察", 322);
    sbutton(w, 5, 1, 1, 330, L"删除选中规则", 323);
    sbutton(w, 5, 0, 1, 378, L"测试观察规则", 324);
    sbutton(w, 5, 1, 1, 378, L"清空观察规则", 325);
    sbutton(w, 5, 0, 1, 434, L"刷新连接诊断", 326);
    sbutton(w, 5, 1, 1, 434, L"查看选中连接", 327);
    connections_list = cell(w, 5, 0, 2, 486, 260, WC_LISTVIEWW, L"",
                            LVS_REPORT | LVS_OWNERDATA | LVS_SINGLESEL | WS_TABSTOP | WS_BORDER, 361);
    const WCHAR *cols[] = {L"软件",           L"PID",      L"协议", L"本地源地址", L"远端",
                           L"源地址所属接口", L"路由估计", L"说明"};
    int widths2[] = {170, 70, 120, 220, 220, 240, 240, 380};
    for (int i = 0; i < 8; ++i) {
        LVCOLUMNW c = {LVCF_TEXT | LVCF_WIDTH};
        c.pszText = (WCHAR *)cols[i];
        c.cx = sp(widths2[i]);
        ListView_InsertColumn(connections_list, i, &c);
    }
    ListView_SetExtendedListViewStyle(connections_list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    cell(w, 5, 0, 2, 764, 92, L"STATIC", L"进入本页时读取；停留期间每 10 秒刷新。无连接记录持久日志。",
         SS_NOPREFIX, 370);
    slabel(w, 5, 868,
           L"Protected Rules 未启用：尚无隔离环境验收。\n本地代理进程的远端连接不能直接归给原应用。");
    rules_render(-1);
    routing_theme();
}
static void test_rule(void) {
    int i = selected_rule();
    if (i < 0)
        return;
    NP_RULE *r = &a.prefs.rules[i];
    if (r->family[0] || !r->path[0]) {
        MessageBoxW(a.settings, L"包身份已保留；当前观察后端尚不能验证包的物理出口。", L"待验证", MB_OK);
        return;
    }
    int matched = 0, source_match = 0, mismatch = 0, unknown = 0;
    const NP_ADAPTER *adapter =
        r->target < 2 && a.prefs.has[r->target] ? np_find(&a.snapshot, &a.prefs.binding[r->target]) : NULL;
    BOOL available = adapter && adapter->state == NP_ENABLED_CONNECTED;
    if (a.monitor)
        for (size_t k = 0; k < a.monitor->count; ++k) {
            NP_CONNECTION *c = &a.monitor->rows[k];
            if (_wcsicmp(r->path, c->exe))
                continue;
            ++matched;
            NP_RULE_RESULT result =
                np_rule_evaluate(r, c, adapter ? &adapter->identity.guid : NULL, available);
            if (result == NP_RULE_SOURCE_MATCH)
                ++source_match;
            else if (result == NP_RULE_SOURCE_MISMATCH)
                ++mismatch;
            else
                ++unknown;
        }
    WCHAR text[700];
    swprintf_s(text, _countof(text),
               L"观察规则：%s\n端点数 %d；源地址匹配 %d；不匹配 %d；其他/未验证 "
               L"%d。\n%s\n这是只读比较，不代表分流成功，也没有安装阻断策略。",
               r->enabled ? L"启用" : L"关闭", matched, source_match, mismatch, unknown,
               !matched                      ? L"没有该 EXE 的可观察连接；没有通过网络验收。"
               : r->target < 2 && !available ? L"期望接口当前不可用，系统仍可能使用其他接口。"
                                             : L"源地址匹配也不能证明代理/TUN 的物理出站。");
    MessageBoxW(a.settings, text, L"观察规则诊断", MB_OK | MB_ICONINFORMATION);
}
static BOOL routing_command(int id, WPARAM wp, LPARAM lp) {
    int index = selected_rule();
    if (id == 320 || id == 328 || id == 329) {
        picker_show(id == 320 ? 0 : id == 328 ? 1 : 2);
        return TRUE;
    }
    if (id == 321 && HIWORD(wp) == CBN_SELCHANGE && index >= 0) {
        int v = (int)SendMessageW((HWND)lp, CB_GETCURSEL, 0, 0);
        if (v >= 0 && v <= 2) {
            a.prefs.rules[index].target = v;
            rules_render(index);
            save();
        }
        return TRUE;
    }
    if (id == 322 && index >= 0) {
        a.prefs.rules[index].enabled = !a.prefs.rules[index].enabled;
        rules_render(index);
        save();
        return TRUE;
    }
    if (id == 323 && index >= 0) {
        memmove(&a.prefs.rules[index], &a.prefs.rules[index + 1],
                (a.prefs.rule_count - index - 1) * sizeof(NP_RULE));
        --a.prefs.rule_count;
        rules_render(-1);
        save();
        return TRUE;
    }
    if (id == 324) {
        test_rule();
        return TRUE;
    }
    if (id == 325) {
        if (MessageBoxW(a.settings, L"删除所有观察规则？这不会更改网络或防火墙。", L"清空规则",
                        MB_YESNO | MB_DEFBUTTON2) == IDYES) {
            a.prefs.rule_count = 0;
            rules_render(-1);
            save();
        }
        return TRUE;
    }
    if (id == 326) {
        routing_start();
        return TRUE;
    }
    if (id == 327 && a.monitor) {
        int n = ListView_GetNextItem(connections_list, -1, LVNI_SELECTED);
        if (n < 0 || (size_t)n >= a.monitor->count)
            return TRUE;
        NP_CONNECTION *c = &a.monitor->rows[n];
        WCHAR text[2400];
        swprintf_s(text, _countof(text),
                   L"%s\nPID %lu · %s · TCP state %lu\n%s → %s\n源接口：%s\n%s\n%s\n进程读取错误 "
                   L"%lu；路由查询错误 %lu。\n连接快照可能在进程退出后变化，不能用于强制安全决策。",
                   c->exe, c->pid, c->protocol, c->state, c->local_text, c->remote_text, c->source, c->route,
                   c->note, c->process_error, c->route_error);
        MessageBoxW(a.settings, text, L"连接诊断（只读）", MB_OK);
        return TRUE;
    }
    return FALSE;
}
static LRESULT routing_notify(NMHDR *h) {
    if (h->hwndFrom == rules_list && h->code == LVN_ITEMCHANGED) {
        int i = selected_rule();
        if (i >= 0)
            SendMessageW(GetDlgItem(a.settings_content, 321), CB_SETCURSEL, a.prefs.rules[i].target, 0);
    }
    if (h->hwndFrom == connections_list && h->code == LVN_GETDISPINFOW) {
        NMLVDISPINFOW *v = (NMLVDISPINFOW *)h;
        if ((v->item.mask & LVIF_TEXT) && a.monitor && v->item.iItem >= 0 &&
            (size_t)v->item.iItem < a.monitor->count) {
            NP_CONNECTION *c = &a.monitor->rows[v->item.iItem];
            static WCHAR pid[24];
            swprintf_s(pid, 24, L"%lu", c->pid);
            const WCHAR *cols[] = {c->app,         pid,       c->protocol, c->local_text,
                                   c->remote_text, c->source, c->route,    c->note};
            if (v->item.iSubItem >= 0 && v->item.iSubItem < 8)
                v->item.pszText = (WCHAR *)cols[v->item.iSubItem];
        }
    }
    return 0;
}
