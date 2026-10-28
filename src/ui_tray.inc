static int fp(int value) {
    return MulDiv(value, (int)a.flyout_dpi, 96);
}
static HFONT surface_font(UINT dpi, int size, int weight) {
    return CreateFontW(-MulDiv(MulDiv(size, (int)dpi, 96), a.prefs.font_scale, 100), 0, 0, 0, weight, 0, 0, 0,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                       DEFAULT_PITCH, L"Segoe UI");
}
/* DIP layout, reflowed rather than scaling the former large panel. */
static int flyout_content_height(void) {
    int gap = a.prefs.tray_density ? 12 : 8, margin = a.prefs.tray_density ? 16 : 12;
    int line = MulDiv(20, a.prefs.font_scale, 100),
        button = MulDiv(a.prefs.tray_density ? 40 : 34, a.prefs.font_scale, 100);
    return margin * 2 + line + gap + line * 2 + gap + (line * 2 + 4) * 2 + gap + button + gap + button;
}
static int flyout_width(void) {
    return a.prefs.tray_density ? a.prefs.tray_size : 310;
}
static void flyout_layout(void) {
    if (!a.flyout)
        return;
    RECT r;
    GetClientRect(a.flyout, &r);
    int total = fp(flyout_content_height()), max = total - r.bottom;
    if (max < 0)
        max = 0;
    if (a.flyout_scroll < 0)
        a.flyout_scroll = 0;
    if (a.flyout_scroll > max)
        a.flyout_scroll = max;
    SCROLLINFO si = {
        sizeof(si), SIF_RANGE | SIF_PAGE | SIF_POS, 0, total - 1, (UINT)r.bottom, a.flyout_scroll, 0};
    SetScrollInfo(a.flyout, SB_VERT, &si, FALSE);
    GetClientRect(a.flyout, &r);
    int width = MulDiv(r.right, 96, (int)a.flyout_dpi), m = a.prefs.tray_density ? 16 : 12,
        g = a.prefs.tray_density ? 12 : 8;
    int cw = width - 2 * m, line = MulDiv(20, a.prefs.font_scale, 100), row = 2 * line + 4;
    int button = MulDiv(a.prefs.tray_density ? 40 : 34, a.prefs.font_scale, 100),
        toggle = MulDiv(64, a.prefs.font_scale, 100);
    int ids[] = {400,       403,        401,     ID_TOGGLE0,  402,    ID_TOGGLE1,
                 ID_CAMPUS, ID_HOTSPOT, ID_SHOW, ID_SETTINGS, ID_EXIT};
    int status = m + line + g, adapters = status + line * 2 + g, modes = adapters + row * 2 + g,
        footer = modes + button + g;
    RECT cells[] = {{m + 28, m, width - m, m + line},
                    {m, status, width - m, status + line * 2},
                    {m, adapters, width - m - toggle - g, adapters + row},
                    {width - m - toggle, adapters, width - m, adapters + row - 4},
                    {m, adapters + row, width - m - toggle - g, adapters + row * 2},
                    {width - m - toggle, adapters + row, width - m, adapters + row * 2 - 4},
                    {m, modes, m + (cw - g) / 2, modes + button},
                    {m + (cw + g) / 2, modes, width - m, modes + button},
                    {m, footer, m + (cw - 2 * g) / 3, footer + button},
                    {m + (cw + g) / 3, footer, m + (2 * cw - g) / 3, footer + button},
                    {m + (2 * cw + 2 * g) / 3, footer, width - m, footer + button}};
    HDWP d = BeginDeferWindowPos(11);
    for (int i = 0; i < 11 && d; ++i)
        d = DeferWindowPos(d, GetDlgItem(a.flyout, ids[i]), NULL, fp(cells[i].left),
                           fp(cells[i].top) - a.flyout_scroll, fp(cells[i].right - cells[i].left),
                           fp(cells[i].bottom - cells[i].top),
                           SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW | SWP_NOCOPYBITS);
    if (d)
        EndDeferWindowPos(d);
    RedrawWindow(a.flyout, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN);
}
static void flyout_fonts(void) {
    HFONT prior = a.flyout_font, heading = a.flyout_heading;
    a.flyout_font = surface_font(a.flyout_dpi, 14, FW_NORMAL);
    a.flyout_heading = surface_font(a.flyout_dpi, 16, FW_SEMIBOLD);
    for (HWND c = GetWindow(a.flyout, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT))
        SendMessageW(c, WM_SETFONT, (WPARAM)(GetDlgCtrlID(c) == 400 ? a.flyout_heading : a.flyout_font),
                     FALSE);
    DeleteObject(prior);
    DeleteObject(heading);
}
static RECT flyout_bounds(RECT, RECT, int, int);
static void flyout_resize(void) {
    RECT r;
    GetWindowRect(a.flyout, &r);
    MONITORINFO m = {sizeof(m)};
    GetMonitorInfoW(MonitorFromWindow(a.flyout, MONITOR_DEFAULTTONEAREST), &m);
    RECT anchor = {r.right, r.bottom + 8, r.right, r.bottom + 8};
    r = flyout_bounds(m.rcWork, anchor, fp(flyout_width()), fp(flyout_content_height()) + 2);
    SetWindowPos(a.flyout, NULL, r.left, r.top, r.right - r.left, r.bottom - r.top,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    flyout_layout();
}
static void flyout_style(void) {
    if (!a.flyout)
        return;
    NP_COLORS c = flyout_colors();
    BOOL dark = GetRValue(c.background) + GetGValue(c.background) + GetBValue(c.background) < 384;
    DwmSetWindowAttribute(a.flyout, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    DWM_WINDOW_CORNER_PREFERENCE corner = a.prefs.tray_round ? DWMWCP_ROUND : DWMWCP_DONOTROUND;
    DwmSetWindowAttribute(a.flyout, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
    enum DWM_SYSTEMBACKDROP_TYPE type = a.prefs.tray_style == 1 ? DWMSBT_NONE : DWMSBT_TRANSIENTWINDOW;
    a.acrylic_result = DwmSetWindowAttribute(a.flyout, DWMWA_SYSTEMBACKDROP_TYPE, &type, sizeof(type));
    a.acrylic = SUCCEEDED(a.acrylic_result) && type != DWMSBT_NONE;
    MARGINS m = a.acrylic ? (MARGINS){-1, -1, -1, -1} : (MARGINS){0, 0, 0, 0};
    if (FAILED(DwmExtendFrameIntoClientArea(a.flyout, &m)))
        a.acrylic = FALSE;
    flyout_fonts();
    if (IsWindowVisible(a.flyout))
        flyout_resize();
    RedrawWindow(a.flyout, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN);
}
static void flyout_update(void) {
    if (!a.flyout)
        return;
    for (int i = 0; i < 2; ++i) {
        WCHAR text[180];
        swprintf_s(text, _countof(text), L"%s\n%s", i ? L"Wi-Fi" : L"Ethernet",
                   state_text(i, a.cards[i].current));
        text_if_changed(GetDlgItem(a.flyout, 401 + i), text);
        HWND button = GetDlgItem(a.flyout, ID_TOGGLE0 + i);
        text_if_changed(button, np_enabled(a.cards[i].current) ? L"禁用" : L"启用");
        enable_if_changed(button, can(i, !np_enabled(a.cards[i].current)));
    }
    text_if_changed(GetDlgItem(a.flyout, 403), a.status);
    enable_if_changed(GetDlgItem(a.flyout, ID_CAMPUS), can(0, TRUE));
    enable_if_changed(GetDlgItem(a.flyout, ID_HOTSPOT), can(0, FALSE) && a.prefs.has[1]);
    enable_if_changed(GetDlgItem(a.flyout, ID_EXIT), a.busy != 2);
}
static RECT flyout_bounds(RECT work, RECT anchor, int width, int height) {
    if (width > work.right - work.left)
        width = work.right - work.left;
    if (height > work.bottom - work.top)
        height = work.bottom - work.top;
    int x = anchor.right - width, y = anchor.top - height - 8;
    if (y < work.top)
        y = anchor.bottom + 8;
    if (x < work.left)
        x = work.left;
    if (x + width > work.right)
        x = work.right - width;
    if (y < work.top)
        y = work.top;
    if (y + height > work.bottom)
        y = work.bottom - height;
    return (RECT){x, y, x + width, y + height};
}
static LRESULT CALLBACK flyout_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        a.flyout = w;
        a.flyout_dpi = dpi_for(w);
        control(w, L"STATIC", L"NetPilot Lite", SS_NOPREFIX, 400);
        control(w, L"STATIC", L"", SS_NOPREFIX, 401);
        control(w, L"STATIC", L"", SS_NOPREFIX, 402);
        control(w, L"BUTTON", L"", BS_OWNERDRAW | WS_TABSTOP, ID_TOGGLE0);
        control(w, L"BUTTON", L"", BS_OWNERDRAW | WS_TABSTOP, ID_TOGGLE1);
        control(w, L"BUTTON", L"校园网模式", BS_OWNERDRAW | WS_TABSTOP, ID_CAMPUS);
        control(w, L"BUTTON", L"手机热点模式", BS_OWNERDRAW | WS_TABSTOP, ID_HOTSPOT);
        control(w, L"STATIC", L"", SS_NOPREFIX, 403);
        control(w, L"BUTTON", L"主窗口", BS_OWNERDRAW | WS_TABSTOP, ID_SHOW);
        control(w, L"BUTTON", L"设置", BS_OWNERDRAW | WS_TABSTOP, ID_SETTINGS);
        control(w, L"BUTTON", L"退出", BS_OWNERDRAW | WS_TABSTOP, ID_EXIT);
        flyout_style();
        flyout_update();
        return 0;
    case WM_SIZE:
        flyout_layout();
        return 0;
    case WM_MOUSEWHEEL:
        a.flyout_scroll -= GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA * fp(56);
        flyout_layout();
        return 0;
    case WM_VSCROLL: {
        SCROLLINFO si = {sizeof(si), SIF_ALL};
        GetScrollInfo(w, SB_VERT, &si);
        switch (LOWORD(wp)) {
        case SB_TOP:
            a.flyout_scroll = 0;
            break;
        case SB_BOTTOM:
            a.flyout_scroll = si.nMax;
            break;
        case SB_LINEUP:
            a.flyout_scroll -= fp(32);
            break;
        case SB_LINEDOWN:
            a.flyout_scroll += fp(32);
            break;
        case SB_PAGEUP:
            a.flyout_scroll -= (int)si.nPage;
            break;
        case SB_PAGEDOWN:
            a.flyout_scroll += (int)si.nPage;
            break;
        case SB_THUMBPOSITION:
        case SB_THUMBTRACK:
            a.flyout_scroll = si.nTrackPos;
            break;
        }
        flyout_layout();
        return 0;
    }
    case WM_DPICHANGED: {
        a.flyout_dpi = HIWORD(wp);
        RECT *r = (RECT *)lp;
        flyout_fonts();
        SetWindowPos(w, NULL, r->left, r->top, r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        flyout_resize();
        return 0;
    }
    case WM_ACTIVATE:
        if (LOWORD(wp) == WA_INACTIVE)
            ShowWindow(w, SW_HIDE);
        return 0;
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE)
            ShowWindow(w, SW_HIDE);
        return 0;
    case WM_COMMAND:
        if (LOWORD(wp) == ID_SHOW || LOWORD(wp) == ID_SETTINGS)
            ShowWindow(w, SW_HIDE);
        SendMessageW(a.window, WM_COMMAND, wp, lp);
        return 0;
    case WM_DRAWITEM:
        return draw_item(lp);
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(w, &ps);
        RECT r;
        GetClientRect(w, &r);
        NP_COLORS c = flyout_colors();
        HDC mem = NULL;
        BP_PAINTPARAMS pp = {sizeof(pp)};
        pp.dwFlags = BPPF_ERASE;
        HPAINTBUFFER b = BeginBufferedPaint(dc, &r, BPBF_TOPDOWNDIB, &pp, &mem);
        if (b) {
            fill_color(mem, r, c.background);
            TRIVERTEX v[2] = {{0, 0, 0x6900, 0x9500, 0xe600, 0}, {r.right, fp(4), 0xec00, 0x9e00, 0xc900, 0}};
            GRADIENT_RECT g = {0, 1};
            GradientFill(mem, v, 2, &g, 1, GRADIENT_FILL_RECT_H);
            BufferedPaintSetAlpha(b, &r, a.acrylic ? (BYTE)(a.prefs.tray_opacity * 255 / 100) : 255);
            EndBufferedPaint(b, TRUE);
        } else
            fill_color(dc, r, c.background);
        DrawIconEx(dc, fp(a.prefs.tray_density ? 16 : 12),
                   fp(a.prefs.tray_density ? 16 : 12) - a.flyout_scroll, a.icon.hIcon, fp(20), fp(20), 0,
                   NULL, DI_NORMAL);
        EndPaint(w, &ps);
        return 0;
    }
    case WM_CLOSE:
        ShowWindow(w, SW_HIDE);
        return 0;
    case WM_DESTROY:
        DeleteObject(a.flyout_font);
        DeleteObject(a.flyout_heading);
        a.flyout_font = a.flyout_heading = NULL;
        a.flyout = NULL;
        return 0;
    }
    return DefWindowProcW(w, msg, wp, lp);
}
static void tray_menu(void) {
    if (a.flyout && IsWindowVisible(a.flyout)) {
        ShowWindow(a.flyout, SW_HIDE);
        return;
    }
    RECT anchor;
    NOTIFYICONIDENTIFIER id = {sizeof(id)};
    id.hWnd = a.window;
    id.uID = 1;
    if (FAILED(Shell_NotifyIconGetRect(&id, &anchor))) {
        POINT p;
        GetCursorPos(&p);
        anchor = (RECT){p.x, p.y, p.x + 1, p.y + 1};
    }
    MONITORINFO m = {sizeof(m)};
    GetMonitorInfoW(MonitorFromRect(&anchor, MONITOR_DEFAULTTONEAREST), &m);
    if (!a.flyout)
        CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_CONTROLPARENT, L"NetPilotLiteFlyout",
                        L"NetPilot Lite · 快捷控制", WS_POPUP | WS_BORDER | WS_CLIPCHILDREN | WS_VSCROLL,
                        anchor.left, anchor.top, 100, 100, a.window, NULL, GetModuleHandleW(NULL), NULL);
    if (!a.flyout)
        return;
    SetWindowPos(a.flyout, NULL, anchor.left, anchor.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    a.flyout_dpi = dpi_for(a.flyout);
    flyout_fonts();
    RECT r = flyout_bounds(m.rcWork, anchor, fp(flyout_width()), fp(flyout_content_height()) + 2);
    SetWindowPos(a.flyout, HWND_TOPMOST, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_NOACTIVATE);
    flyout_update();
    if (a.prefs.animations)
        AnimateWindow(a.flyout, 100, AW_BLEND);
    ShowWindow(a.flyout, SW_SHOWNORMAL);
    SetForegroundWindow(a.flyout);
    SetFocus(GetDlgItem(a.flyout, ID_TOGGLE0));
}
