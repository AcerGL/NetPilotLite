typedef struct SETTING_CELL {
    HWND window;
    int page, column, span, y, height;
} SETTING_CELL;
static SETTING_CELL cells[160];
static int cell_count;
static const WCHAR *pages[] = {L"通用",     L"网络适配器",   L"快捷操作", L"外观",
                               L"系统托盘", L"应用网络分流", L"更新",     L"关于"};
static const int page_height[] = {290, 360, 300, 920, 740, 968, 1190, 360};
static int sp(int v) {
    return MulDiv(v, (int)a.settings_dpi, 96);
}
static HWND cell(HWND w, int page, int col, int span, int y, int height, const WCHAR *cls, const WCHAR *text,
                 DWORD style, int id) {
    (void)w;
    HWND c = control(a.settings_content, cls, text, style, id);
    cells[cell_count++] = (SETTING_CELL){c, page, col, span, y, height};
    return c;
}
static HWND slabel(HWND w, int p, int y, const WCHAR *text) {
    return cell(w, p, 0, 2, y, wcschr(text, L'\n') ? 52 : 32, L"STATIC", text, SS_NOPREFIX, 0);
}
static HWND sbutton(HWND w, int p, int col, int span, int y, const WCHAR *text, int id) {
    return cell(w, p, col, span, y, 38, L"BUTTON", text, BS_OWNERDRAW | WS_TABSTOP, id);
}
static HWND scombo(HWND w, int p, int col, int span, int y, int id, const WCHAR **items, int count,
                   int selected) {
    HWND c = cell(w, p, col, span, y, 220, L"COMBOBOX", L"",
                  CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_TABSTOP, id);
    for (int i = 0; i < count; ++i)
        SendMessageW(c, CB_ADDSTRING, 0, (LPARAM)items[i]);
    SendMessageW(c, CB_SETCURSEL, selected, 0);
    return c;
}
static HWND slider(HWND w, int page, int y, int id, int lo, int hi, int value) {
    HWND c = cell(w, page, 0, 2, y, 32, TRACKBAR_CLASSW, L"", TBS_NOTICKS | WS_TABSTOP, id);
    SendMessageW(c, TBM_SETRANGE, TRUE, MAKELPARAM(lo, hi));
    SendMessageW(c, TBM_SETPOS, TRUE, value);
    return c;
}
static HWND check(HWND w, int page, int y, int id, const WCHAR *text, BOOL value) {
    HWND c = cell(w, page, 0, 2, y, 34, L"BUTTON", text, BS_AUTOCHECKBOX | WS_TABSTOP, id);
    SendMessageW(c, BM_SETCHECK, value ? BST_CHECKED : BST_UNCHECKED, 0);
    return c;
}
static void settings_fonts(void) {
    HFONT old = a.settings_font, heading = a.settings_heading;
    a.settings_font = surface_font(a.settings_dpi, 14, FW_NORMAL);
    a.settings_heading = surface_font(a.settings_dpi, 24, FW_SEMIBOLD);
    for (HWND c = GetWindow(a.settings, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
        SendMessageW(c, WM_SETFONT, (WPARAM)(GetDlgCtrlID(c) == 290 ? a.settings_heading : a.settings_font),
                     FALSE);
        WCHAR cls[30];
        GetClassNameW(c, cls, _countof(cls));
        if (!wcscmp(cls, L"ComboBox")) {
            SendMessageW(c, CB_SETITEMHEIGHT, (WPARAM)-1, sp(30));
            SendMessageW(c, CB_SETITEMHEIGHT, 0, sp(30));
        }
    }
    for (int i = 0; i < cell_count; ++i) {
        HWND c = cells[i].window;
        SendMessageW(c, WM_SETFONT, (WPARAM)a.settings_font, FALSE);
        WCHAR cls[30];
        GetClassNameW(c, cls, _countof(cls));
        if (!wcscmp(cls, L"ComboBox")) {
            SendMessageW(c, CB_SETITEMHEIGHT, (WPARAM)-1, sp(30));
            SendMessageW(c, CB_SETITEMHEIGHT, 0, sp(30));
        }
    }
    routing_columns();
    DeleteObject(old);
    DeleteObject(heading);
}
static void settings_scroll(HWND w, int next) {
    if (!cell_count)
        return;
    RECT r;
    GetClientRect(w, &r);
    int max = sp(page_height[a.settings_page] + 100) - r.bottom;
    if (max < 0)
        max = 0;
    if (next < 0)
        next = 0;
    if (next > max)
        next = max;
    a.settings_scroll = next;
    SCROLLINFO si = {sizeof(si),
                     SIF_RANGE | SIF_PAGE | SIF_POS,
                     0,
                     sp(page_height[a.settings_page] + 100) - 1,
                     (UINT)r.bottom,
                     next,
                     0};
    SetScrollInfo(w, SB_VERT, &si, TRUE);
    GetClientRect(w, &r);
    int nav = sp(172), x = sp(204), cw = r.right - x - sp(24), half = (cw - sp(16)) / 2;
    HDWP d = BeginDeferWindowPos(cell_count + 10);
    for (int i = 0; i < 8; ++i)
        if (d)
            d = DeferWindowPos(d, GetDlgItem(w, 300 + i), NULL, sp(12), sp(92 + i * 48), nav - sp(12), sp(40),
                               SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS | SWP_NOREDRAW);
    if (d)
        d = DeferWindowPos(d, GetDlgItem(w, 290), NULL, x, sp(24), cw, sp(42),
                           SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW);
    if (d)
        d = DeferWindowPos(d, GetDlgItem(w, 291), NULL, sp(20), sp(24), sp(148), sp(50),
                           SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW);
    if (d)
        d = DeferWindowPos(d, a.settings_content, NULL, x, sp(92), cw, r.bottom - sp(92),
                           SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW);
    if (d)
        EndDeferWindowPos(d);
    d = BeginDeferWindowPos(cell_count);
    for (int i = 0; i < cell_count; ++i) {
        SETTING_CELL *c = &cells[i];
        BOOL visible = c->page == a.settings_page;
        if (d)
            d = DeferWindowPos(d, c->window, NULL, (c->column ? half + sp(16) : 0), sp(c->y) - next,
                               c->span == 2 ? cw : half, sp(c->height),
                               SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW | SWP_NOCOPYBITS |
                                   (visible ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
    }
    if (d)
        EndDeferWindowPos(d);
    /* The fixed title is a sibling above scrolling controls. It covers their scrolled-out pixels. */
    SetWindowPos(GetDlgItem(w, 290), HWND_TOP, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOREDRAW);
    RedrawWindow(w, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN);
}
static void settings_refresh(void) {
    if (!a.settings)
        return;
    text_if_changed(GetDlgItem(a.settings, 290), pages[a.settings_page]);
    RedrawWindow(a.settings, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN);
}
static void settings_page(int page) {
    a.settings_page = page;
    a.settings_scroll = 0;
    settings_refresh();
    settings_scroll(a.settings, 0);
    if (page == 5) {
        routing_start();
        SetTimer(a.window, 6, 10000, NULL);
    } else
        KillTimer(a.window, 6);
}
static void default_appearance(void) {
    NP_SETTINGS d;
    np_settings_defaults(&d);
    a.prefs.theme = d.theme;
    a.prefs.background_mode = 0;
    a.prefs.image_mode = 0;
    a.prefs.mask = d.mask;
    a.prefs.card_opacity = d.card_opacity;
    a.prefs.font_scale = d.font_scale;
    a.prefs.density = d.density;
    a.prefs.animations = d.animations;
    a.prefs.custom_accent = d.custom_accent;
    a.prefs.custom_card = d.custom_card;
    a.prefs.custom_background = d.custom_background;
    a.prefs.cover[0] = a.prefs.image[0] = 0;
    np_image_free(&a.background_image);
    load_image(TRUE, NULL, FALSE);
    set_fonts();
    layout();
    appearance();
    DestroyWindow(a.settings);
    show_settings();
    settings_page(3);
}
#include "ui_routing.inc"
#include "ui_online.inc"
static LRESULT CALLBACK settings_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        a.settings = w;
        a.settings_dpi = dpi_for(w);
        a.settings_scroll = 0;
        a.settings_page = 0;
        cell_count = 0;
        a.settings_content = CreateWindowExW(WS_EX_CONTROLPARENT, L"NetPilotLiteSettingsContent", L"",
                                             WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, 0, 0, 1, 1, w, NULL,
                                             GetModuleHandleW(NULL), NULL);
        control(w, L"STATIC", L"NetPilot Lite\n设置中心", SS_NOPREFIX, 291);
        control(w, L"STATIC", pages[0], SS_NOPREFIX, 290);
        for (int i = 0; i < 8; ++i)
            control(w, L"BUTTON", pages[i], BS_OWNERDRAW | WS_TABSTOP, 300 + i);
        slabel(w, 0, 0, L"轻巧、独立的网络开关\n启动只读取状态，不自动切换网络。");
        a.tray_check = check(w, 0, 80, 205, L"关闭窗口时收至系统托盘", a.prefs.close_tray);
        slabel(w, 0, 140, L"网卡绑定、背景和偏好保存在本机用户目录。\n更新程序不会替换这些设置。");
        slabel(w, 1, 0, L"只绑定物理网卡\n使用设备 GUID 与 PnP 身份，不依赖易变化的接口序号。");
        slabel(w, 1, 80, L"Ethernet / Wi-Fi 的实时状态与绑定列表在主窗口。\n禁用后的设备仍可重新识别。");
        sbutton(w, 1, 0, 2, 164, L"打开网卡选择与实时状态", 270);
        slabel(w, 1, 230, L"网卡操作需要标准管理员权限。\n检测到远程桌面会话时拒绝禁用。");
        slabel(w, 2, 0,
               L"校园网模式：启用绑定的 Ethernet。\n手机热点模式：Wi-Fi 确认连接后才禁用 Ethernet。");
        sbutton(w, 2, 0, 1, 90, L"校园网模式", 272);
        sbutton(w, 2, 1, 1, 90, L"手机热点模式", 273);
        slabel(w, 2, 158,
               L"快捷操作仍会重新读取 Windows 状态验证结果。\n不会修改 DNS、Metric、代理或虚拟网卡。");
        cell(w, 3, 0, 2, 0, 100, L"STATIC", L"", SS_NOPREFIX, 299);
        a.theme_combo = scombo(w, 3, 0, 2, 116, 201, themes, 5, a.prefs.theme);
        sbutton(w, 3, 0, 1, 164, L"自定义强调色", 210);
        sbutton(w, 3, 1, 1, 164, L"自定义背景色", 211);
        sbutton(w, 3, 0, 1, 212, L"自定义卡片色", 212);
        sbutton(w, 3, 1, 1, 212, L"更换封面图片…", 230);
        const WCHAR *bg[] = {L"纯色背景", L"渐变背景", L"自定义图片"};
        const WCHAR *fit[] = {L"Fit · 完整显示", L"Fill · 填满裁剪", L"Center · 原尺寸"};
        a.bg_combo = scombo(w, 3, 0, 1, 266, 202, bg, 3, a.prefs.background_mode);
        a.fit_combo = scombo(w, 3, 1, 1, 266, 203, fit, 3, a.prefs.image_mode);
        sbutton(w, 3, 0, 1, 314, L"选择背景图片…", 213);
        sbutton(w, 3, 1, 1, 314, L"清除背景图片", 214);
        slabel(w, 3, 366, L"背景图片遮罩 · 0–100%（越高越不透明）");
        a.mask_slider = slider(w, 3, 406, 204, 0, 100, a.prefs.mask);
        slabel(w, 3, 450, L"卡片不透明度 · 70–100%（保留文字对比）");
        slider(w, 3, 490, 240, 70, 100, a.prefs.card_opacity);
        slabel(w, 3, 534, L"文字缩放 · 90–125%");
        slider(w, 3, 574, 241, 90, 125, a.prefs.font_scale);
        const WCHAR *density[] = {L"舒适间距", L"紧凑间距"};
        scombo(w, 3, 0, 2, 626, 242, density, 2, a.prefs.density);
        check(w, 3, 680, 243, L"启用浮窗淡入动画", a.prefs.animations);
        sbutton(w, 3, 0, 2, 738, L"恢复默认外观（保留网卡绑定）", 215);
        slabel(w, 3, 806, L"更换图片在后台解码；主窗口会即时预览。\n自定义图片保存在用户目录，更新时保留。");
        slabel(w, 4, 0, L"自定义 NetPilot 快捷浮窗\n不改变 Windows 任务栏或其他应用的通知区域。");
        const WCHAR *styles[] = {L"Acrylic · 跟随主题", L"Solid · 不透明", L"Princess · 粉蓝", L"Dark Glass",
                                 L"Light Glass"};
        scombo(w, 4, 0, 2, 80, 250, styles, 5, a.prefs.tray_style);
        slabel(w, 4, 132, L"浮窗底色不透明度 · 70–100%\nAcrylic 由系统支持情况决定；不支持时使用实色。");
        slider(w, 4, 190, 251, 70, 100, a.prefs.tray_opacity);
        check(w, 4, 238, 252, L"使用系统圆角（系统支持时）", a.prefs.tray_round);
        slabel(w, 4, 290, L"舒适模式宽度 · 320–460 DIP（紧凑模式 310 DIP）");
        slider(w, 4, 330, 253, 320, 460, a.prefs.tray_size);
        const WCHAR *icons[] = {L"简洁网口图标", L"樱彩角色图标"};
        scombo(w, 4, 0, 2, 382, 254, icons, 2, a.prefs.tray_icon);
        sbutton(w, 4, 0, 1, 434, L"浮窗强调色…", 255);
        sbutton(w, 4, 1, 1, 434, L"预览快捷浮窗", 256);
        slabel(w, 4, 498, L"单击或右键托盘图标：快捷浮窗。\n双击：打开主窗口；Esc / 点击外部：收起。");
        const WCHAR *panel_density[] = {L"Compact · 紧凑", L"Comfortable · 舒适"};
        scombo(w, 4, 0, 2, 560, 258, panel_density, 2, a.prefs.tray_density);
        cell(w, 4, 0, 2, 620, 96, L"STATIC", L"系统材质将在首次打开浮窗时检测。", SS_NOPREFIX, 257);
        routing_controls(w);
        online_controls(w);
        slabel(w, 7, 0, L"NetPilot Lite  " NP_VERSION L"\n原生 C17 / Win32 · Windows 10 / 11 x64");
        slabel(w, 7, 90, L"单文件主程序 · 无需 MSI / .NET\n不注册服务或计划任务，不接管你的代理工具。");
        slabel(w, 7, 184,
               L"网络链路连接不保证互联网可访问。\n通过热点连接也不意味着其他设备活动完全不可见。");
        settings_fonts();
        settings_scroll(w, 0);
        return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(wp);
        if (online_command(id) || routing_command(id, wp, lp))
            return 0;
        if (id >= 300 && id < 308) {
            settings_page(id - 300);
            return 0;
        }
        if (id == 270) {
            show();
            SetFocus(a.cards[0].combo);
        } else if (id == 272)
            operation(0, TRUE);
        else if (id == 273)
            hotspot();
        else if ((id == 201 || id == 202 || id == 203 || id == 242 || id == 250 || id == 254 || id == 258) &&
                 HIWORD(wp) == CBN_SELCHANGE) {
            int v = (int)SendMessageW((HWND)lp, CB_GETCURSEL, 0, 0);
            if (v < 0)
                return 0;
            if (id == 201)
                a.prefs.theme = v;
            if (id == 202)
                a.prefs.background_mode = v;
            if (id == 203)
                a.prefs.image_mode = v;
            if (id == 242) {
                a.prefs.density = v;
                layout();
            }
            if (id == 250)
                a.prefs.tray_style = v;
            if (id == 258)
                a.prefs.tray_density = v;
            if (id == 254) {
                a.prefs.tray_icon = v;
                Shell_NotifyIconW(NIM_DELETE, &a.icon);
                tray_update(TRUE);
            }
            appearance();
        } else if (id == 205 || id == 243 || id == 252) {
            BOOL value = SendMessageW((HWND)lp, BM_GETCHECK, 0, 0) == BST_CHECKED;
            if (id == 205)
                a.prefs.close_tray = value;
            if (id == 243)
                a.prefs.animations = value;
            if (id == 252)
                a.prefs.tray_round = value;
            appearance();
        } else if (id >= 210 && id <= 212)
            choose_color(id - 210);
        else if (id == 213 || id == 230)
            choose_image(id == 230);
        else if (id == 214 && !a.loading_images) {
            np_image_free(&a.background_image);
            a.prefs.image[0] = 0;
            a.prefs.background_mode = 0;
            SendMessageW(a.bg_combo, CB_SETCURSEL, 0, 0);
            appearance();
        } else if (id == 215 && !a.loading_images)
            default_appearance();
        else if (id == 217)
            local_update();
        else if (id == 218)
            MessageBoxW(w,
                        online.release.body[0]
                            ? online.release.body
                            : L"1.2.0\n• 310 DIP 紧凑托盘\n• Windows 应用选择器\n• GitHub Stable 签名更新",
                        L"更新日志", MB_OK);
        else if (id == 255) {
            static COLORREF custom[16];
            CHOOSECOLORW c = {sizeof(c)};
            c.hwndOwner = w;
            c.rgbResult = a.prefs.tray_accent;
            c.lpCustColors = custom;
            c.Flags = CC_RGBINIT | CC_FULLOPEN;
            if (ChooseColorW(&c)) {
                a.prefs.tray_accent = c.rgbResult;
                appearance();
            }
        } else if (id == 256) {
            tray_menu();
            WCHAR status[260];
            swprintf_s(status, _countof(status),
                       L"材质请求：%s\nDWM 返回 0x%08lX；系统可因节能、远程会话或透明设置进一步降级。",
                       a.acrylic ? L"Acrylic 已接受" : L"Solid 回退", (DWORD)a.acrylic_result);
            text_if_changed(GetDlgItem(a.settings_content, 257), status);
        }
        return 0;
    }
    case WM_NOTIFY: {
        NMCUSTOMDRAW *n = (NMCUSTOMDRAW *)lp;
        if (n->hdr.code != NM_CUSTOMDRAW)
            return routing_notify(&n->hdr);
        UINT_PTR id = n->hdr.idFrom;
        if (id != 204 && id != 240 && id != 241 && id != 251 && id != 253)
            return CDRF_DODEFAULT;
        if (n->dwDrawStage == CDDS_PREPAINT) {
            /* One theme paint pass, using native geometry. Hit testing,
               keyboard input and accessibility remain the native trackbar's. */
            RECT client, thumb, r = {0};
            GetClientRect(n->hdr.hwndFrom, &client);
            SendMessageW(n->hdr.hwndFrom, TBM_GETCHANNELRECT, 0, (LPARAM)&r);
            SendMessageW(n->hdr.hwndFrom, TBM_GETTHUMBRECT, 0, (LPARAM)&thumb);
            fill_color(n->hdc, client, a.colors.background);
            r.top = (r.top + r.bottom) / 2 - sp(2);
            r.bottom = r.top + sp(4);
            fill_color(n->hdc, r, a.colors.border);
            int lo = (int)SendMessageW(n->hdr.hwndFrom, TBM_GETRANGEMIN, 0, 0);
            int hi = (int)SendMessageW(n->hdr.hwndFrom, TBM_GETRANGEMAX, 0, 0);
            int value = (int)SendMessageW(n->hdr.hwndFrom, TBM_GETPOS, 0, 0);
            if (hi > lo) {
                r.right = r.left + MulDiv(r.right - r.left, value - lo, hi - lo);
                fill_color(n->hdc, r, a.colors.accent);
            }
            int diameter = thumb.right - thumb.left, center = (thumb.top + thumb.bottom) / 2;
            thumb.top = center - diameter / 2;
            thumb.bottom = thumb.top + diameter;
            int saved = SaveDC(n->hdc);
            SelectObject(n->hdc, GetStockObject(DC_BRUSH));
            SelectObject(n->hdc, GetStockObject(DC_PEN));
            SetDCBrushColor(n->hdc, a.colors.accent);
            SetDCPenColor(n->hdc, a.colors.accent);
            Ellipse(n->hdc, thumb.left, thumb.top, thumb.right, thumb.bottom);
            if (GetFocus() == n->hdr.hwndFrom) {
                InflateRect(&client, -1, -1);
                DrawFocusRect(n->hdc, &client);
            }
            RestoreDC(n->hdc, saved);
            return CDRF_SKIPDEFAULT;
        }
        return CDRF_DODEFAULT;
    }
    case WM_HSCROLL: {
        int id = GetDlgCtrlID((HWND)lp), v = (int)SendMessageW((HWND)lp, TBM_GETPOS, 0, 0);
        if (id == 204)
            a.prefs.mask = v;
        if (id == 240)
            a.prefs.card_opacity = v;
        if (id == 241)
            a.prefs.font_scale = v;
        if (id == 251)
            a.prefs.tray_opacity = v;
        if (id == 253)
            a.prefs.tray_size = v;
        if (LOWORD(wp) != TB_THUMBTRACK) {
            if (id == 241) {
                set_fonts();
                settings_fonts();
                layout();
            }
            appearance();
        } else {
            a.frame_dirty = a.backdrop_dirty = TRUE;
            if (id == 240 && a.tint.dc)
                SetPixel(a.tint.dc, 0, 0, a.colors.card);
            RedrawWindow(a.window, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN);
        }
        return 0;
    }
    case WM_SIZE:
        settings_scroll(w, a.settings_scroll);
        return 0;
    case WM_GETMINMAXINFO:
        ((MINMAXINFO *)lp)->ptMinTrackSize = (POINT){sp(700), sp(510)};
        return 0;
    case WM_DPICHANGED: {
        a.settings_dpi = HIWORD(wp);
        settings_fonts();
        RECT *r = (RECT *)lp;
        SetWindowPos(w, NULL, r->left, r->top, r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        settings_scroll(w, 0);
        return 0;
    }
    case WM_MOUSEWHEEL:
        settings_scroll(w, a.settings_scroll - GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA * sp(64));
        return 0;
    case WM_VSCROLL: {
        SCROLLINFO si = {sizeof(si), SIF_ALL};
        GetScrollInfo(w, SB_VERT, &si);
        int n = a.settings_scroll;
        switch (LOWORD(wp)) {
        case SB_TOP:
            n = 0;
            break;
        case SB_BOTTOM:
            n = si.nMax;
            break;
        case SB_LINEUP:
            n -= sp(32);
            break;
        case SB_LINEDOWN:
            n += sp(32);
            break;
        case SB_PAGEUP:
            n -= (int)si.nPage;
            break;
        case SB_PAGEDOWN:
            n += (int)si.nPage;
            break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION:
            n = si.nTrackPos;
            break;
        }
        settings_scroll(w, n);
        return 0;
    }
    case WM_DRAWITEM:
        return draw_item(lp);
    case WM_MEASUREITEM:
        ((MEASUREITEMSTRUCT *)lp)->itemHeight = sp(30);
        return TRUE;
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORLISTBOX:
        SetTextColor((HDC)wp, a.colors.foreground);
        SetBkColor((HDC)wp, a.colors.background);
        return (LRESULT)a.bg;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(w, &ps);
        FillRect(dc, &ps.rcPaint, a.bg);
        RECT r;
        GetClientRect(w, &r);
        r.left = sp(184);
        r.right = r.left + 1;
        fill_color(dc, r, a.colors.border);
        EndPaint(w, &ps);
        return 0;
    }
    case WM_CLOSE:
        DestroyWindow(w);
        return 0;
    case WM_DESTROY:
        KillTimer(a.window, 6);
        rules_list = connections_list = NULL;
        a.settings = NULL;
        a.settings_content = NULL;
        cell_count = 0;
        DeleteObject(a.settings_font);
        DeleteObject(a.settings_heading);
        a.settings_font = a.settings_heading = NULL;
        return 0;
    }
    return DefWindowProcW(w, msg, wp, lp);
}
static LRESULT CALLBACK settings_content_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_COMMAND:
    case WM_DRAWITEM:
    case WM_MEASUREITEM:
    case WM_HSCROLL:
    case WM_NOTIFY:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORLISTBOX:
    case WM_MOUSEWHEEL:
        return SendMessageW(GetParent(w), msg, wp, lp);
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(w, &ps);
        FillRect(dc, &ps.rcPaint, a.bg);
        EndPaint(w, &ps);
        return 0;
    }
    }
    return DefWindowProcW(w, msg, wp, lp);
}
static void show_settings(void) {
    if (a.settings) {
        ShowWindow(a.settings, SW_RESTORE);
        SetForegroundWindow(a.settings);
        return;
    }
    MONITORINFO m = {sizeof(m)};
    GetMonitorInfoW(MonitorFromWindow(a.window, MONITOR_DEFAULTTONEAREST), &m);
    int width = px(900), height = px(720);
    if (width > m.rcWork.right - m.rcWork.left - 24)
        width = m.rcWork.right - m.rcWork.left - 24;
    if (height > m.rcWork.bottom - m.rcWork.top - 24)
        height = m.rcWork.bottom - m.rcWork.top - 24;
    HWND w = CreateWindowExW(WS_EX_CONTROLPARENT | WS_EX_COMPOSITED, L"NetPilotLiteSettings",
                             L"NetPilot Lite · 设置", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_VSCROLL,
                             m.rcWork.left + (m.rcWork.right - m.rcWork.left - width) / 2,
                             m.rcWork.top + (m.rcWork.bottom - m.rcWork.top - height) / 2, width, height,
                             a.window, NULL, GetModuleHandleW(NULL), NULL);
    if (w) {
        settings_refresh();
        ShowWindow(w, SW_SHOW);
        SetForegroundWindow(w);
    }
}
