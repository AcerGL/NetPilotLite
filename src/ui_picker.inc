typedef struct PICKER_STATE {
    HWND window, list, search, tabs, status;
    NP_APP_LIST *data;
    HIMAGELIST icons;
    HFONT font;
    int tab, map[NP_MAX_APPS], count;
    UINT dpi;
    UINT_PTR generation;
    BOOL busy;
} PICKER_STATE;
static PICKER_STATE picker;
static int picker_active;
typedef struct PICKER_JOB {
    HWND main;
    UINT_PTR generation;
    BOOL running, mock;
} PICKER_JOB;
static int pp(int x) {
    return MulDiv(x, (int)picker.dpi, 96);
}
static void picker_images(void) {
    ListView_SetImageList(picker.list, NULL, LVSIL_SMALL);
    if (picker.icons)
        ImageList_Destroy(picker.icons);
    picker.icons = ImageList_Create(pp(24), pp(24), ILC_COLOR32 | ILC_MASK, 16, 16);
    if (picker.data)
        for (size_t i = 0; i < picker.data->count; ++i)
            ImageList_AddIcon(picker.icons, picker.data->rows[i].icon ? picker.data->rows[i].icon
                                                                      : LoadIconW(NULL, IDI_APPLICATION));
    ListView_SetImageList(picker.list, picker.icons, LVSIL_SMALL);
}
static void picker_columns(void) {
    int widths[] = {200, 160, 110, 320, 340};
    for (int i = 0; i < 5; ++i)
        ListView_SetColumnWidth(picker.list, i, pp(widths[i]));
}
static BOOL rule_exists(const NP_APP_ENTRY *r) {
    for (int i = 0; i < a.prefs.rule_count; ++i) {
        NP_RULE *p = &a.prefs.rules[i];
        if (r->package && p->family[0] && !_wcsicmp(p->family, r->family) && !_wcsicmp(p->aumid, r->aumid))
            return TRUE;
        if (!r->package && r->path[0] && !_wcsicmp(p->path, r->path))
            return TRUE;
    }
    return FALSE;
}
static BOOL same_app(const NP_APP_ENTRY *x, const NP_APP_ENTRY *y) {
    if (x->package != y->package)
        return FALSE;
    if (x->package)
        return !_wcsicmp(x->family, y->family) && !_wcsicmp(x->aumid, y->aumid);
    return x->path[0] && !_wcsicmp(x->path, y->path);
}
static BOOL picker_add(const NP_APP_ENTRY *r) {
    if (rule_exists(r))
        return FALSE;
    if (a.prefs.rule_count >= NP_MAX_RULES)
        return FALSE;
    if (!r->package && !np_app_exe(r->path))
        return FALSE;
    NP_RULE *rule = &a.prefs.rules[a.prefs.rule_count++];
    ZeroMemory(rule, sizeof(*rule));
    rule->target = 2;
    wcscpy_s(rule->label, 256, r->name);
    wcscpy_s(rule->family, 128, r->family);
    wcscpy_s(rule->aumid, 256, r->aumid);
    if (!r->package)
        wcscpy_s(rule->path, NP_PATH, r->path);
    return TRUE;
}
static void picker_filter(void) {
    WCHAR query[256];
    GetWindowTextW(picker.search, query, 256);
    picker.count = 0;
    if (picker.data)
        for (size_t i = 0; i < picker.data->count; ++i) {
            NP_APP_ENTRY *r = &picker.data->rows[i];
            if (!query[0] || StrStrIW(r->name, query) || StrStrIW(r->publisher, query) ||
                StrStrIW(r->path, query) || StrStrIW(r->family, query))
                picker.map[picker.count++] = (int)i;
        }
    ListView_SetItemCountEx(picker.list, picker.count, LVSICF_NOSCROLL);
    WCHAR status[250];
    swprintf_s(status, 250, L"%d 项%s · 发布者信息未经签名认证 · 选择不会约束网络出口", picker.count,
               picker.data && picker.data->truncated ? L"（列表已截断）" : L"");
    SetWindowTextW(picker.status, status);
    InvalidateRect(picker.list, NULL, FALSE);
}
static DWORD WINAPI picker_worker(void *arg) {
    PICKER_JOB *j = arg;
    NP_APP_LIST *data;
    if (j->mock) {
        data = calloc(1, sizeof(*data));
        if (data) {
            data->count = 2;
            NP_APP_ENTRY *r = &data->rows[0];
            wcscpy_s(r->name, 256, L"示例浏览器 [模拟]");
            GetModuleFileNameW(NULL, r->path, NP_PATH);
            wcscpy_s(r->publisher, 256, L"模拟发布者");
            wcscpy_s(r->note, 180, L"模拟数据，不是已安装软件");
            r = &data->rows[1];
            wcscpy_s(r->name, 256, L"Store 示例 [模拟]");
            r->package = TRUE;
            wcscpy_s(r->family, 128, L"Mock_123");
            wcscpy_s(r->aumid, 256, L"Mock_123!App");
        }
    } else
        data = np_apps_scan(j->running);
    if (!PostMessageW(j->main, WM_PICKER_DONE, j->generation, (LPARAM)data))
        np_apps_free(data);
    free(j);
    return 0;
}
static void picker_load(void) {
    if (picker.busy || picker_active || picker.tab == 2 || a.quit_pending)
        return;
    PICKER_JOB *j = calloc(1, sizeof(*j));
    if (!j)
        return;
    j->main = a.window;
    j->generation = ++picker.generation;
    j->running = picker.tab == 1;
    j->mock = a.mock;
    picker.busy = TRUE;
    ++a.async_jobs;
    ++picker_active;
    SetWindowTextW(picker.status, L"正在后台读取应用…");
    HANDLE t = CreateThread(NULL, 0, picker_worker, j, 0, NULL);
    if (t)
        CloseHandle(t);
    else {
        free(j);
        --a.async_jobs;
        --picker_active;
        picker.busy = FALSE;
        SetWindowTextW(picker.status, L"无法启动读取线程");
    }
}
static void picker_done(UINT_PTR generation, NP_APP_LIST *data) {
    --a.async_jobs;
    --picker_active;
    if (!picker.window || generation != picker.generation) {
        np_apps_free(data);
        if (picker.window)
            picker_load();
        return;
    }
    picker.busy = FALSE;
    np_apps_free(picker.data);
    picker.data = data;
    picker_images();
    picker_filter();
    if (!data)
        SetWindowTextW(picker.status, L"内存不足，应用列表未能读取");
    else if (data->registry_errors || FAILED(data->shell_error)) {
        WCHAR text[200];
        swprintf_s(text, 200,
                   L"列表可能不完整：注册表错误 %lu；Shell HRESULT 0x%08lX。可用“正在运行”或“手动选择”。",
                   data->registry_errors, (DWORD)data->shell_error);
        SetWindowTextW(picker.status, text);
    }
}
static void picker_layout(void) {
    RECT r;
    GetClientRect(picker.window, &r);
    int w = r.right, h = r.bottom, p = pp(16);
    MoveWindow(picker.tabs, p, p, w - 2 * p, pp(34), TRUE);
    MoveWindow(picker.search, p, pp(62), w - pp(142), pp(30), TRUE);
    MoveWindow(GetDlgItem(picker.window, 504), w - pp(114), pp(62), pp(98), pp(30), TRUE);
    MoveWindow(picker.list, p, pp(104), w - 2 * p, h - pp(214), TRUE);
    MoveWindow(picker.status, p, h - pp(98), w - 2 * p, pp(42), TRUE);
    MoveWindow(GetDlgItem(picker.window, 505), p, h - pp(46), pp(144), pp(32), TRUE);
    MoveWindow(GetDlgItem(picker.window, 506), pp(174), h - pp(46), pp(130), pp(32), TRUE);
    MoveWindow(GetDlgItem(picker.window, 509), pp(318), h - pp(46), pp(130), pp(32), TRUE);
    MoveWindow(GetDlgItem(picker.window, 507), w - pp(114), h - pp(46), pp(98), pp(32), TRUE);
    BOOL manual = picker.tab == 2;
    ShowWindow(picker.list, manual ? SW_HIDE : SW_SHOW);
    ShowWindow(picker.search, manual ? SW_HIDE : SW_SHOW);
    EnableWindow(GetDlgItem(picker.window, 505), !manual);
    EnableWindow(GetDlgItem(picker.window, 504), !manual);
    EnableWindow(GetDlgItem(picker.window, 509), !manual);
    if (manual)
        SetWindowTextW(picker.status, L"便携软件可手动浏览 EXE；将验证文件并请你确认。不会执行选择的程序。");
}
static void picker_accept(void) {
    if (!picker.data || picker.tab == 2)
        return;
    int valid = 0, unresolved = 0, unique[NP_MAX_RULES];
    for (int n = ListView_GetNextItem(picker.list, -1, LVNI_SELECTED); n >= 0;
         n = ListView_GetNextItem(picker.list, n, LVNI_SELECTED)) {
        NP_APP_ENTRY *r = &picker.data->rows[picker.map[n]];
        if (rule_exists(r))
            continue;
        if (r->package || np_app_exe(r->path)) {
            BOOL duplicate = FALSE;
            for (int k = 0; k < valid && k < NP_MAX_RULES; ++k)
                if (same_app(r, &picker.data->rows[unique[k]])) {
                    duplicate = TRUE;
                    break;
                }
            if (!duplicate) {
                if (valid < NP_MAX_RULES)
                    unique[valid] = picker.map[n];
                ++valid;
            }
        } else
            ++unresolved;
    }
    if (!valid || a.prefs.rule_count + valid > NP_MAX_RULES) {
        MessageBoxW(
            picker.window,
            L"没有可添加的身份，或超过 32 条规则上限。无法解析的应用请从运行进程选择，或手动确认 EXE。",
            L"选择应用", MB_OK);
        return;
    }
    WCHAR text[400];
    swprintf_s(text, 400,
               L"添加 %d 个应用身份？%d 项无法解析，将跳过。\nEXE "
               L"可能只是启动器；多进程软件可继续添加实际进程。Store/MSIX "
               L"只保存包身份，本版不对包实施出口约束。\n新规则默认关闭，网络出口仍为待验证。",
               valid, unresolved);
    if (MessageBoxW(picker.window, text, L"确认应用身份", MB_YESNO | MB_DEFBUTTON2 | MB_ICONINFORMATION) !=
        IDYES)
        return;
    for (int n = 0; n < valid; ++n)
        picker_add(&picker.data->rows[unique[n]]);
    rules_render(-1);
    save();
    picker_filter();
}
static LRESULT CALLBACK picker_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        picker.window = w;
        picker.dpi = dpi_for(w);
        picker.font = surface_font(picker.dpi, 14, FW_NORMAL);
        picker.tabs = control(w, WC_TABCONTROLW, L"", WS_TABSTOP, 501);
        const WCHAR *tabs[] = {L"已安装", L"正在运行", L"手动选择"};
        for (int i = 0; i < 3; ++i) {
            TCITEMW t = {TCIF_TEXT};
            t.pszText = (WCHAR *)tabs[i];
            TabCtrl_InsertItem(picker.tabs, i, &t);
        }
        picker.search = control(w, L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, 502);
        SendMessageW(picker.search, EM_SETCUEBANNER, TRUE, (LPARAM)L"搜索应用、发布者或路径");
        picker.list = control(w, WC_LISTVIEWW, L"", WS_BORDER | WS_TABSTOP | LVS_REPORT | LVS_OWNERDATA, 503);
        ListView_SetExtendedListViewStyle(picker.list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
        const WCHAR *cols[] = {L"应用名称", L"发布者（元数据）", L"版本 / PID", L"EXE / 包身份 / 安装位置",
                               L"选择状态"};
        int widths[] = {200, 160, 110, 320, 340};
        for (int i = 0; i < 5; ++i) {
            LVCOLUMNW c = {LVCF_TEXT | LVCF_WIDTH};
            c.pszText = (WCHAR *)cols[i];
            c.cx = pp(widths[i]);
            ListView_InsertColumn(picker.list, i, &c);
        }
        ListView_SetBkColor(picker.list, a.colors.card);
        ListView_SetTextBkColor(picker.list, a.colors.card);
        ListView_SetTextColor(picker.list, a.colors.foreground);
        picker.status = control(w, L"STATIC", L"", SS_NOPREFIX, 508);
        control(w, L"BUTTON", L"刷新", BS_OWNERDRAW | WS_TABSTOP, 504);
        control(w, L"BUTTON", L"添加所选应用", BS_OWNERDRAW | WS_TABSTOP, 505);
        control(w, L"BUTTON", L"浏览 EXE…", BS_OWNERDRAW | WS_TABSTOP, 506);
        control(w, L"BUTTON", L"关闭", BS_OWNERDRAW | WS_TABSTOP, 507);
        control(w, L"BUTTON", L"查看完整身份", BS_OWNERDRAW | WS_TABSTOP, 509);
        for (HWND c = GetWindow(w, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT))
            SendMessageW(c, WM_SETFONT, (WPARAM)picker.font, FALSE);
        return 0;
    }
    case WM_SIZE:
        picker_layout();
        return 0;
    case WM_GETMINMAXINFO: {
        MONITORINFO mi = {sizeof(mi)};
        GetMonitorInfoW(MonitorFromWindow(w, MONITOR_DEFAULTTONEAREST), &mi);
        ((MINMAXINFO *)lp)->ptMinTrackSize = (POINT){min(pp(660), mi.rcWork.right - mi.rcWork.left),
                                                     min(pp(420), mi.rcWork.bottom - mi.rcWork.top)};
        return 0;
    }
    case WM_DPICHANGED: {
        picker.dpi = HIWORD(wp);
        HFONT old = picker.font;
        picker.font = surface_font(picker.dpi, 14, FW_NORMAL);
        for (HWND c = GetWindow(w, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT))
            SendMessageW(c, WM_SETFONT, (WPARAM)picker.font, TRUE);
        DeleteObject(old);
        picker_columns();
        picker_images();
        RECT *r = (RECT *)lp;
        SetWindowPos(w, NULL, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == 502 && HIWORD(wp) == EN_CHANGE)
            picker_filter();
        if (LOWORD(wp) == 504)
            picker_load();
        if (LOWORD(wp) == 505)
            picker_accept();
        if (LOWORD(wp) == 507)
            DestroyWindow(w);
        if (LOWORD(wp) == 509 && picker.data) {
            int n = ListView_GetNextItem(picker.list, -1, LVNI_SELECTED);
            if (n >= 0 && n < picker.count) {
                NP_APP_ENTRY *r = &picker.data->rows[picker.map[n]];
                WCHAR text[4000];
                swprintf_s(
                    text, 4000,
                    L"%s\n发布者（未认证）：%s\n版本：%s\nPID：%lu\n候选 EXE：%s\n安装位置：%s\nPackage "
                    L"Family "
                    L"Name：%s\nAppUserModelID：%s\n%s\n启动器可能与实际联网进程不同；可从运行进程继续添加。",
                    r->name, r->publisher, r->version, r->pid, r->path, r->location, r->family, r->aumid,
                    r->note);
                MessageBoxW(w, text, L"应用身份 · 待验证", MB_OK);
            }
        }
        if (LOWORD(wp) == 506) {
            NP_APP_ENTRY r = {0};
            if (browse(w, r.path, L"应用程序 (*.exe)\0*.exe\0\0")) {
                if (!np_app_exe(r.path)) {
                    MessageBoxW(w, L"所选文件不是可读取的本地 PE EXE。", L"无法添加", MB_OK | MB_ICONERROR);
                    return 0;
                }
                wcsncpy_s(r.name, 256, PathFindFileNameW(r.path), _TRUNCATE);
                if (MessageBoxW(
                        w, L"将此 EXE 添加为关闭的观察规则？所选程序不会运行，启动器与实际联网进程可能不同。",
                        L"确认 EXE", MB_YESNO | MB_DEFBUTTON2) == IDYES) {
                    picker_add(&r);
                    rules_render(-1);
                    save();
                }
            }
        }
        return 0;
    case WM_NOTIFY: {
        NMHDR *h = (NMHDR *)lp;
        if (h->hwndFrom == picker.tabs && h->code == TCN_SELCHANGE) {
            picker.tab = TabCtrl_GetCurSel(picker.tabs);
            ++picker.generation;
            picker.busy = FALSE;
            np_apps_free(picker.data);
            picker.data = NULL;
            picker_filter();
            picker_layout();
            picker_load();
        }
        if (h->hwndFrom == picker.list && h->code == LVN_GETDISPINFOW) {
            NMLVDISPINFOW *v = (NMLVDISPINFOW *)lp;
            if (picker.data && v->item.iItem >= 0 && v->item.iItem < picker.count) {
                int i = picker.map[v->item.iItem];
                NP_APP_ENTRY *r = &picker.data->rows[i];
                static WCHAR pid[80];
                swprintf_s(pid, 80, L"PID %lu", r->pid);
                const WCHAR *texts[] = {r->name, r->publisher[0] ? r->publisher : L"未知",
                                        r->pid ? pid : r->version,
                                        r->package   ? r->aumid
                                        : r->path[0] ? r->path
                                                     : r->location,
                                        rule_exists(r) ? L"已添加 · 待验证" : r->note};
                if ((v->item.mask & LVIF_TEXT) && v->item.iSubItem >= 0 && v->item.iSubItem < 5)
                    v->item.pszText = (WCHAR *)texts[v->item.iSubItem];
                if (v->item.mask & LVIF_IMAGE)
                    v->item.iImage = i;
            }
        }
        return 0;
    }
    case WM_DRAWITEM:
        return draw_item(lp);
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
        SetTextColor((HDC)wp, a.colors.foreground);
        SetBkColor((HDC)wp, a.colors.background);
        return (LRESULT)a.bg;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(w, &ps);
        FillRect(dc, &ps.rcPaint, a.bg);
        EndPaint(w, &ps);
        return 0;
    }
    case WM_CLOSE:
        DestroyWindow(w);
        return 0;
    case WM_DESTROY:
        ++picker.generation;
        picker.window = NULL;
        picker.busy = FALSE;
        np_apps_free(picker.data);
        picker.data = NULL;
        if (picker.icons)
            ImageList_Destroy(picker.icons);
        picker.icons = NULL;
        DeleteObject(picker.font);
        picker.font = NULL;
        return 0;
    }
    return DefWindowProcW(w, msg, wp, lp);
}
static void picker_show(int tab) {
    if (!picker.window) {
        RECT r;
        GetWindowRect(a.window, &r);
        CreateWindowExW(WS_EX_CONTROLPARENT, L"NetPilotLiteAppPicker", L"选择应用 · 仅添加观察身份",
                        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, r.left + 30, r.top + 30,
                        MulDiv(900, (int)a.dpi, 96), MulDiv(580, (int)a.dpi, 96), a.window, NULL,
                        GetModuleHandleW(NULL), NULL);
    }
    if (!picker.window)
        return;
    ++picker.generation;
    picker.busy = FALSE;
    np_apps_free(picker.data);
    picker.data = NULL;
    picker_filter();
    picker.tab = tab;
    TabCtrl_SetCurSel(picker.tabs, tab);
    picker_layout();
    RECT bounds;
    GetWindowRect(picker.window, &bounds);
    MONITORINFO monitor = {sizeof(monitor)};
    GetMonitorInfoW(MonitorFromWindow(picker.window, MONITOR_DEFAULTTONEAREST), &monitor);
    int width = min(bounds.right - bounds.left, monitor.rcWork.right - monitor.rcWork.left),
        height = min(bounds.bottom - bounds.top, monitor.rcWork.bottom - monitor.rcWork.top);
    SetWindowPos(picker.window, NULL,
                 max(monitor.rcWork.left, min(bounds.left, monitor.rcWork.right - width)),
                 max(monitor.rcWork.top, min(bounds.top, monitor.rcWork.bottom - height)), width, height,
                 SWP_NOZORDER);
    ShowWindow(picker.window, SW_SHOW);
    SetForegroundWindow(picker.window);
    picker_load();
}
