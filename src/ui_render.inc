/* Main content uses one retained background. Child labels sample exactly that surface. */
static void compose(void) {
    RECT viewport;
    GetClientRect(a.window, &viewport);
    int height = px(a.content_height), offset = px(a.scroll);
    if (a.backdrop.width != viewport.right || a.backdrop.height != height)
        a.backdrop_dirty = TRUE;
    if (a.frame.width != viewport.right || a.frame.height != viewport.bottom)
        a.frame_dirty = TRUE;
    if (!np_surface_size(&a.frame, viewport.right, viewport.bottom) ||
        !np_surface_size(&a.backdrop, viewport.right, height))
        return;
    if (a.backdrop_dirty) {
        RECT doc = {0, 0, viewport.right, height};
        np_background(a.backdrop.dc, &doc, &a.prefs, &a.colors, &a.background_image);
        RECT cover = a.cover;
        OffsetRect(&cover, 0, offset);
        int cw = cover.right - cover.left, ch = cover.bottom - cover.top;
        if (a.cover_cache.width != cw || a.cover_cache.height != ch)
            a.cover_dirty = TRUE;
        if (np_surface_size(&a.cover_cache, cw, ch)) {
            if (a.cover_dirty) {
                RECT cr = {0, 0, cw, ch};
                FillRect(a.cover_cache.dc, &cr, a.card);
                np_image_draw(a.cover_cache.dc, &cr, &a.cover_image, 0);
                a.cover_dirty = FALSE;
            }
            BitBlt(a.backdrop.dc, cover.left, cover.top, cw, ch, a.cover_cache.dc, 0, 0, SRCCOPY);
        }
        for (int i = 0; i < 2; ++i) {
            RECT cr = a.card_rect[i];
            OffsetRect(&cr, 0, offset);
            int saved = SaveDC(a.backdrop.dc);
            HRGN clip = CreateRoundRectRgn(cr.left, cr.top, cr.right + 1, cr.bottom + 1, px(16), px(16));
            SelectClipRgn(a.backdrop.dc, clip);
            DeleteObject(clip);
            if (a.tint.dc) {
                BLENDFUNCTION b = {AC_SRC_OVER, 0, (BYTE)(a.prefs.card_opacity * 255 / 100), 0};
                AlphaBlend(a.backdrop.dc, cr.left, cr.top, cr.right - cr.left, cr.bottom - cr.top, a.tint.dc,
                           0, 0, 1, 1, b);
            } else
                FillRect(a.backdrop.dc, &cr, a.card);
            RestoreDC(a.backdrop.dc, saved);
            HGDIOBJ p = SelectObject(a.backdrop.dc, GetStockObject(DC_PEN)),
                    b = SelectObject(a.backdrop.dc, GetStockObject(HOLLOW_BRUSH));
            SetDCPenColor(a.backdrop.dc, a.colors.border);
            RoundRect(a.backdrop.dc, cr.left, cr.top, cr.right, cr.bottom, px(16), px(16));
            SelectObject(a.backdrop.dc, b);
            SelectObject(a.backdrop.dc, p);
        }
        for (HWND c = GetWindow(a.window, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
            WCHAR cls[30], text[1024];
            GetClassNameW(c, cls, _countof(cls));
            if (wcscmp(cls, L"Static"))
                continue;
            RECT r;
            GetWindowRect(c, &r);
            MapWindowPoints(NULL, a.window, (POINT *)&r, 2);
            OffsetRect(&r, 0, offset);
            GetWindowTextW(c, text, _countof(text));
            HGDIOBJ f = SelectObject(a.backdrop.dc, (HFONT)SendMessageW(c, WM_GETFONT, 0, 0));
            COLORREF fg = a.colors.foreground;
            for (int i = 0; i < 2; ++i)
                if (c == a.cards[i].state)
                    fg = np_enabled(a.cards[i].current) ? a.colors.success : a.colors.warning;
            SetTextColor(a.backdrop.dc, fg);
            SetBkMode(a.backdrop.dc, TRANSPARENT);
            DrawTextW(a.backdrop.dc, text, -1, &r, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
            SelectObject(a.backdrop.dc, f);
        }
        a.backdrop_dirty = FALSE;
        a.frame_dirty = TRUE;
    }
    if (a.frame_dirty) {
        FillRect(a.frame.dc, &viewport, a.bg);
        BitBlt(a.frame.dc, 0, 0, viewport.right, viewport.bottom, a.backdrop.dc, 0, offset, SRCCOPY);
        a.frame_dirty = FALSE;
    }
}
static NP_COLORS flyout_colors(void) {
    NP_SETTINGS s = a.prefs;
    if (s.tray_style == 2)
        s.theme = 2;
    if (s.tray_style == 3)
        s.theme = 0;
    if (s.tray_style == 4)
        s.theme = 1;
    NP_COLORS c = np_theme(&s);
    c.accent = c.button = s.tray_accent;
    int lum = (GetRValue(c.button) * 299 + GetGValue(c.button) * 587 + GetBValue(c.button) * 114) / 1000;
    c.button_text = lum > 150 ? RGB(20, 25, 40) : RGB(255, 255, 255);
    return c;
}
static void fill_color(HDC dc, RECT r, COLORREF c) {
    SetDCBrushColor(dc, c);
    FillRect(dc, &r, (HBRUSH)GetStockObject(DC_BRUSH));
}
static LRESULT draw_item(LPARAM lp) {
    DRAWITEMSTRUCT *d = (DRAWITEMSTRUCT *)lp;
    HWND parent = GetParent(d->hwndItem);
    NP_COLORS c = parent == a.flyout ? flyout_colors() : a.colors;
    RECT r = d->rcItem;
    BOOL disabled = (d->itemState & ODS_DISABLED) != 0;
    BOOL label = d->CtlType == ODT_STATIC;
    if (label && parent == a.window) {
        compose();
        POINT point = {0, 0};
        MapWindowPoints(d->hwndItem, a.window, &point, 1);
        if (a.frame.dc)
            BitBlt(d->hDC, 0, 0, r.right, r.bottom, a.frame.dc, point.x, point.y, SRCCOPY);
        else
            fill_color(d->hDC, r, c.background);
        return TRUE;
    } else
        fill_color(d->hDC, r, c.background);
    WCHAR text[1024] = L"";
    if (d->CtlType == ODT_COMBOBOX && d->itemID != (UINT)-1)
        SendMessageW(d->hwndItem, CB_GETLBTEXT, d->itemID, (LPARAM)text);
    else
        GetWindowTextW(d->hwndItem, text, _countof(text));
    UINT dpi = (parent == a.settings || parent == a.settings_content) ? a.settings_dpi
               : parent == a.flyout                                   ? a.flyout_dpi
                                                                      : a.dpi;
    int pad = MulDiv(10, (int)dpi, 96);
    BOOL nav = parent == a.settings && d->CtlID >= 300 && d->CtlID < 308;
    BOOL primary = d->CtlID == ID_TOGGLE0 || d->CtlID == ID_TOGGLE1 || d->CtlID == 217;
    if (!label) {
        COLORREF bg = disabled ? c.border : primary ? c.button : c.card;
        if (nav)
            bg = (int)d->CtlID - 300 == a.settings_page ? c.button : c.background;
        else if (d->itemState & ODS_SELECTED)
            bg = c.accent;
        SetDCBrushColor(d->hDC, bg);
        SetDCPenColor(d->hDC, nav ? bg : c.border);
        HGDIOBJ oldb = SelectObject(d->hDC, GetStockObject(DC_BRUSH)),
                oldp = SelectObject(d->hDC, GetStockObject(DC_PEN));
        RoundRect(d->hDC, r.left, r.top, r.right, r.bottom, pad, pad);
        SelectObject(d->hDC, oldb);
        SelectObject(d->hDC, oldp);
        InflateRect(&r, -pad, 0);
        if (d->itemState & ODS_SELECTED)
            OffsetRect(&r, 0, 1);
    }
    HFONT font = (HFONT)SendMessageW(d->hwndItem, WM_GETFONT, 0, 0);
    HGDIOBJ old = SelectObject(d->hDC, font ? font : a.normal);
    SetBkMode(d->hDC, TRANSPARENT);
    COLORREF fg = label      ? c.foreground
                  : disabled ? c.secondary
                  : nav      ? ((int)d->CtlID - 300 == a.settings_page ? c.button_text : c.foreground)
                  : primary  ? c.button_text
                             : c.foreground;
    for (int i = 0; i < 2; ++i)
        if (d->hwndItem == a.cards[i].state)
            fg = np_enabled(a.cards[i].current) ? c.success : c.warning;
    SetTextColor(d->hDC, fg);
    if (d->CtlID == 299) {
        fill_color(d->hDC, r, c.card);
        RECT t = r;
        t.left += pad;
        t.top += pad;
        DrawTextW(d->hDC, L"Aa  NetPilot Lite\n外观即时预览 · 网络状态已连接", -1, &t,
                  DT_LEFT | DT_WORDBREAK);
        RECT line = {r.left + pad, r.bottom - pad * 2, r.right - pad, r.bottom - pad};
        fill_color(d->hDC, line, c.accent);
    } else
        DrawTextW(d->hDC, text, -1, &r,
                  label ? DT_LEFT | DT_WORDBREAK | DT_NOPREFIX
                        : DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX |
                              (nav || d->CtlType == ODT_COMBOBOX ? DT_LEFT : DT_CENTER));
    if (d->itemState & ODS_FOCUS) {
        RECT focus = r;
        InflateRect(&focus, -2, -2);
        DrawFocusRect(d->hDC, &focus);
    }
    SelectObject(d->hDC, old);
    return TRUE;
}
