static int ui_checks, ui_failures;
#define UI_CHECK(expr)                                                                                       \
    do {                                                                                                     \
        ++ui_checks;                                                                                         \
        if (!(expr)) {                                                                                       \
            ++ui_failures;                                                                                   \
            wprintf(L"UI_FAIL %d %hs\n", __LINE__, #expr);                                                   \
        }                                                                                                    \
    } while (0)
static void capture_surface(HWND w, NP_SURFACE *s) {
    RECT r;
    GetClientRect(w, &r);
    np_surface_size(s, r.right, r.bottom);
    HDC dc = GetDC(w);
    BitBlt(s->dc, 0, 0, r.right, r.bottom, dc, 0, 0, SRCCOPY);
    ReleaseDC(w, dc);
    GdiFlush();
}
static BOOL same_rgb(NP_SURFACE *x, NP_SURFACE *y) {
    if (x->width != y->width || x->height != y->height)
        return FALSE;
    DWORD *a1 = x->pixels, *b1 = y->pixels;
    for (int i = 0; i < x->width * x->height; ++i)
        if ((a1[i] & 0xffffff) != (b1[i] & 0xffffff))
            return FALSE;
    return TRUE;
}
static void ui_acceptance_tick(void) {
    if (a.busy || a.loading_images)
        return;
    KillTimer(a.window, 2);
    UI_CHECK(a.mock_data.writes == 0);
    a.dpi = 96;
    set_fonts();
    SetWindowPos(a.window, NULL, 40, 40, 820, 680, SWP_NOZORDER);
    layout();
    NP_SURFACE before = {0}, after = {0};
    for (int mode = 0; mode < 3; ++mode) {
        a.prefs.background_mode = mode;
        if (mode == 2)
            np_image_resource(200, &a.background_image);
        appearance();
        for (int i = 0; i < 24; ++i) {
            SendMessageW(a.window, WM_VSCROLL, i % 2 ? SB_TOP : SB_BOTTOM, 0);
            RedrawWindow(a.window, NULL, NULL, RDW_UPDATENOW | RDW_ALLCHILDREN);
            capture_surface(a.window, &before);
            a.backdrop_dirty = a.frame_dirty = TRUE;
            RedrawWindow(a.window, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
            capture_surface(a.window, &after);
            UI_CHECK(same_rgb(&before, &after));
        }
    }
    np_surface_free(&before);
    np_surface_free(&after);
    a.prefs.background_mode = 0;
    a.prefs.theme = 2;
    appearance();
    show_settings();
    settings_page(3);
    capture_window(a.settings, L"artifacts\\v110-settings.bmp");
    RECT channel = {0};
    SendMessageW(a.mask_slider, TBM_GETCHANNELRECT, 0, (LPARAM)&channel);
    HDC slider_dc = GetDC(a.mask_slider);
    int channel_y = (channel.top + channel.bottom) / 2;
    wprintf(L"Slider channel=(%ld,%ld,%ld,%ld) colors=%08lX/%08lX expected=%08lX/%08lX\n", channel.left,
            channel.top, channel.right, channel.bottom,
            GetPixel(slider_dc, channel.left + (channel.right - channel.left) / 4, channel_y),
            GetPixel(slider_dc, channel.right - 3, channel_y), a.colors.accent, a.colors.border);
    UI_CHECK(GetPixel(slider_dc, channel.left + (channel.right - channel.left) / 4, channel_y) ==
             a.colors.accent);
    UI_CHECK(GetPixel(slider_dc, channel.right - 3, channel_y) == a.colors.border);
    ReleaseDC(a.mask_slider, slider_dc);
    UI_CHECK(SendMessageW(a.theme_combo, CB_GETCOUNT, 0, 0) == 5);
    settings_scroll(a.settings, 100000);
    UI_CHECK(a.settings_scroll > 0);
    capture_window(a.settings, L"artifacts\\v110-settings-scrolled.bmp");
    for (int i = 0; i < 8; ++i) {
        settings_page(i);
        UI_CHECK(a.settings_page == i);
    }
    a.prefs.animations = FALSE;
    tray_menu();
    UI_CHECK(a.flyout != NULL);
    wprintf(L"DWM Acrylic requested=%d HRESULT=0x%08lX (acceptance by DWM, not a visual guarantee)\n",
            a.acrylic, (DWORD)a.acrylic_result);
    capture_window(a.flyout, L"artifacts\\v110-tray.bmp");
    for (int i = 0; i < 5; ++i) {
        a.prefs.tray_style = i;
        flyout_style();
        UI_CHECK(i != 1 || !a.acrylic);
    }
    SendMessageW(a.flyout, WM_KEYDOWN, VK_ESCAPE, 0);
    UI_CHECK(!IsWindowVisible(a.flyout));
    tray_menu();
    SendMessageW(a.flyout, WM_ACTIVATE, WA_INACTIVE, 0);
    UI_CHECK(!IsWindowVisible(a.flyout));
    RECT monitors[] = {{0, 0, 1920, 1040}, {-1920, -100, 0, 980}, {0, -900, 1280, 0}, {0, 0, 640, 400}};
    for (int i = 0; i < 4; ++i)
        for (int dpi = 96; dpi <= 192; dpi += 24) {
            RECT m = monitors[i], anchor = {m.left + 12, m.bottom - 20, m.left + 36, m.bottom};
            RECT r = flyout_bounds(m, anchor, MulDiv(flyout_width(), dpi, 96), MulDiv(flyout_content_height()+2, dpi, 96));
            UI_CHECK(r.left >= m.left && r.top >= m.top && r.right <= m.right && r.bottom <= m.bottom);
        }
    /* Native double-buffered list views allocate their paint buffers lazily.
       Draw each page before measuring; compare the same page/theme at both ends. */
    for (int i = 0; i < 8; ++i) {
        settings_page(i);
        RedrawWindow(a.settings, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    }
    a.prefs.theme = 0;
    appearance();
    settings_page(0);
    a.prefs.tray_style = 0;
    flyout_style();
    RedrawWindow(a.settings, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    DWORD gdi_before = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    for (int i = 0; i < 150; ++i) {
        a.prefs.theme = i % 5;
        appearance();
        settings_page(i % 8);
        settings_scroll(a.settings, (i % 2) * 100000);
        a.prefs.tray_style = i % 5;
        flyout_style();
        if ((i + 1) % 50 == 0)
            wprintf(L"UI GDI checkpoint=%d objects=%lu\n", i + 1,
                    GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS));
    }
    a.prefs.theme = 0;
    appearance();
    settings_page(0);
    a.prefs.tray_style = 0;
    flyout_style();
    RedrawWindow(a.settings, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    DWORD gdi_after = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    UI_CHECK(gdi_after <= gdi_before + 2);
    UI_CHECK(a.mock_data.writes == 0);
    wprintf(L"UI acceptance: %d checks, %d failures; 150 theme/settings/tray cycles; GDI %lu -> %lu; real "
            L"network writes=compiled out, mock writes=%u\n",
            ui_checks, ui_failures, gdi_before, gdi_after, a.mock_data.writes);
    a.smoke_exit = ui_failures ? 1 : 0;
    DestroyWindow(a.window);
}
#undef UI_CHECK
static void ui_soak_tick(void) {
    static int step;
    static DWORD initial_gdi;
    static double started, cpu;
    if (a.busy || a.loading_images)
        return;
    if (!step) {
        a.dpi = 96;
        set_fonts();
        SetWindowPos(a.window, NULL, 30, 30, 820, 680, SWP_NOZORDER);
        show_settings();
        tray_menu();
        ShowWindow(a.flyout, SW_HIDE);
        for (int t = 0; t < 5; ++t) {
            a.prefs.theme = t;
            appearance();
            for (int p = 0; p < 8; ++p) {
                settings_page(p);
                RedrawWindow(a.settings, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
            }
        }
        a.prefs.theme = 0;
        appearance();
        settings_page(0);
        RedrawWindow(a.window, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
        RedrawWindow(a.settings, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
        initial_gdi = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
        started = perf_ms();
        cpu = perf_cpu();
        SetTimer(a.window, 2, 200, NULL);
    }
    SendMessageW(a.window, WM_VSCROLL, step % 2 ? SB_TOP : SB_BOTTOM, 0);
    if (step % 10 == 0) {
        a.prefs.theme = step / 10 % 5;
        appearance();
        settings_page(step / 10 % 8);
    }
    if (step && step % 100 == 0)
        wprintf(L"SOAK checkpoint=%d GDI=%lu\n", step, GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS));
    if (++step >= 600) {
        a.prefs.theme = 0;
        appearance();
        settings_page(0);
        RedrawWindow(a.settings, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
        RedrawWindow(a.window, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
        DWORD final = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
        wprintf(L"SOAK elapsed_s=%.2f cpu_ms=%.2f iterations=%d GDI=%lu->%lu mock_writes=%u\n",
                (perf_ms() - started) / 1000, perf_cpu() - cpu, step, initial_gdi, final, a.mock_data.writes);
        a.smoke_exit = (final > initial_gdi + 2 || a.mock_data.writes) ? 1 : 0;
        DestroyWindow(a.window);
    }
}
