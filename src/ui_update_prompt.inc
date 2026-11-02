/* UI-thread-only themed, modeless update notice. No nested message loop. */
static HWND update_notice;
static LRESULT CALLBACK update_notice_proc(HWND w, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
    case WM_CREATE: {
        WCHAR summary[512];
        swprintf_s(summary, 512, L"当前 %s  →  最新 %s\n发布时间：%s\n更新包：%.2f MiB", NP_VERSION,
                   online.release.tag, online.release.published, (double)online.release.size / 1048576.0);
        HWND text = CreateWindowW(L"STATIC", summary, WS_CHILD | WS_VISIBLE | SS_NOPREFIX, 24, 20, 540, 88, w,
                                  NULL, NULL, NULL);
        SendMessageW(text, WM_SETFONT, (WPARAM)a.normal, 0);
        HWND notes = CreateWindowW(MSFTEDIT_CLASS, L"",
                                   WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY, 24, 118,
                                   540, 210, w, NULL, NULL, NULL);
        SendMessageW(notes, WM_SETFONT, (WPARAM)a.normal, 0);
        markdown_notes(notes, online.release.body);
        const WCHAR *labels[] = {L"立即更新", L"稍后提醒", L"忽略此版本"};
        for (int i = 0; i < 3; i++) {
            HWND b = CreateWindowW(L"BUTTON", labels[i], WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                                   24 + i * 184, 348, 172, 42, w, (HMENU)(INT_PTR)(i + 1), NULL, NULL);
            SendMessageW(b, WM_SETFONT, (WPARAM)a.normal, 0);
        }
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wp) >= 1 && LOWORD(wp) <= 3) {
            int choice = LOWORD(wp);
            DestroyWindow(w);
            if (choice == 3)
                np_ignore_version(NULL, online.release.tag);
            if (choice == 1 && !online.busy && !a.quit_pending) {
                online.install_after_download = TRUE;
                show_settings();
                settings_page(6);
                online_start(TRUE);
            }
        }
        return 0;
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT *d = (DRAWITEMSTRUCT *)lp;
        WCHAR label[64];
        GetWindowTextW(d->hwndItem, label, 64);
        HBRUSH b = CreateSolidBrush(a.colors.card);
        FillRect(d->hDC, &d->rcItem, b);
        DeleteObject(b);
        SetTextColor(d->hDC, a.colors.accent);
        SetBkMode(d->hDC, TRANSPARENT);
        SelectObject(d->hDC, a.normal);
        DrawTextW(d->hDC, label, -1, &d->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        if (d->itemState & ODS_FOCUS)
            DrawFocusRect(d->hDC, &d->rcItem);
        return TRUE;
    }
    case WM_CTLCOLORSTATIC:
        SetTextColor((HDC)wp, a.colors.foreground);
        SetBkColor((HDC)wp, a.colors.background);
        return (LRESULT)a.bg;
    case WM_ERASEBKGND: {
        RECT r;
        GetClientRect(w, &r);
        FillRect((HDC)wp, &r, a.bg);
        return 1;
    }
    case WM_CLOSE:
        DestroyWindow(w);
        return 0;
    case WM_DESTROY:
        update_notice = NULL;
        return 0;
    }
    return DefWindowProcW(w, m, wp, lp);
}
static int update_prompt(void) {
    if (update_notice) {
        SetForegroundWindow(update_notice);
        return 0;
    }
    WNDCLASSW cls = {0};
    cls.lpfnWndProc = update_notice_proc;
    cls.hInstance = GetModuleHandleW(NULL);
    cls.lpszClassName = L"NetPilotPrivateUpdateNotice";
    cls.hCursor = LoadCursorW(NULL, IDC_ARROW);
    RegisterClassW(&cls);
    update_notice = CreateWindowExW(WS_EX_CONTROLPARENT, cls.lpszClassName, L"NetPilot Lite · 发现新版本",
                                    WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT,
                                    606, 448, a.window, NULL, cls.hInstance, NULL);
    if (update_notice) {
        ShowWindow(update_notice, SW_SHOW);
        SetForegroundWindow(update_notice);
    }
    return 0;
}
