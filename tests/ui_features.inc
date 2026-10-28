static void feature_ui_tick(void) {
    static int stage, checks, failures, waits;
    static DWORD initial_gdi;
#define FUI(x)                                                                                               \
    do {                                                                                                     \
        ++checks;                                                                                            \
        if (!(x)) {                                                                                          \
            ++failures;                                                                                      \
            wprintf(L"FEATURE_UI_FAIL %d %hs\n", __LINE__, #x);                                              \
        }                                                                                                    \
    } while (0)
    if (a.busy || a.loading_images || a.routing_busy || a.async_jobs) {
        if (++waits < 120)
            return;
        FUI(FALSE);
        a.smoke_exit = 1;
        SendMessageW(a.window, WM_COMMAND, ID_EXIT, 0);
        return;
    }
    waits = 0;
    if (stage == 0) {
        FUI(a.mock_data.writes == 0);
        show_settings();
        settings_page(4);
        a.prefs.animations = FALSE;
        tray_menu();
        FUI(flyout_width() == 310);
        FUI(flyout_content_height() == 272);
        RECT outer;
        GetWindowRect(a.flyout, &outer);
        FUI(abs((outer.right - outer.left) - fp(310)) <= 2);
        SCROLLINFO si = {sizeof(si), SIF_ALL};
        GetScrollInfo(a.flyout, SB_VERT, &si);
        FUI(si.nMax - (int)si.nPage + 1 <= 0);
        capture_window(a.flyout, L"artifacts\\v120-tray-compact.bmp");
        for (int density = 0; density < 2; ++density)
            for (int scale = 100; scale <= 140; scale += 20)
                for (UINT dpi = 96; dpi <= 192; dpi += 24) {
                    a.prefs.tray_density = density;
                    a.prefs.font_scale = scale;
                    a.flyout_dpi = dpi;
                    RECT work = {-1280, 0, 0, 720}, anchor = {-20, 710, 0, 720};
                    RECT b = flyout_bounds(work, anchor, fp(flyout_width()), fp(flyout_content_height()) + 2);
                    FUI(b.left >= work.left && b.right <= work.right && b.top >= work.top &&
                        b.bottom <= work.bottom);
                }
        a.prefs.tray_density = 1;
        a.prefs.font_scale = 100;
        a.flyout_dpi = dpi_for(a.flyout);
        flyout_style();
        capture_window(a.flyout, L"artifacts\\v120-tray-comfortable.bmp");
        ShowWindow(a.flyout, SW_HIDE);
        a.prefs.tray_density = 0;
        settings_page(5);
        picker_show(0);
        ++stage;
        return;
    }
    if (stage == 1) {
        FUI(picker.data && picker.count == 2);
        FUI(picker_active == 0);
        FUI(a.async_jobs == 0);
        SetWindowTextW(picker.search, L"Store");
        FUI(picker.count == 1);
        FUI(picker.data->rows[picker.map[0]].package);
        NP_APP_ENTRY *r = &picker.data->rows[picker.map[0]];
        FUI(picker_add(r));
        FUI(!picker_add(r));
        FUI(a.prefs.rule_count == 1 && !a.prefs.rules[0].enabled && !a.prefs.rules[0].path[0]);
        SetWindowTextW(picker.search, L"");
        FUI(picker.count == 2);
        FUI(picker_add(&picker.data->rows[0]));
        FUI(!picker_add(&picker.data->rows[0]));
        RedrawWindow(picker.window, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
        capture_window(picker.window, L"artifacts\\v120-app-picker.bmp");
        initial_gdi = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
        picker_show(1);
        picker_show(0);
        picker_show(1);
        FUI(picker_active == 1);
        ++stage;
        return;
    }
    if (stage == 2) {
        FUI(picker.tab == 1 && picker.count == 2);
        picker_show(2);
        FUI(!IsWindowVisible(picker.list));
        picker_show(0);
        DestroyWindow(picker.window);
        ++stage;
        return;
    }
    if (stage == 3) {
        FUI(!picker.window && !picker.data && !picker.icons && picker_active == 0);
        FUI(GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) <= initial_gdi + 2);
        settings_page(6);
        FUI(!IsWindowEnabled(GetDlgItem(a.settings_content, 605)));
        online_start(FALSE);
        FUI(online.busy);
        DestroyWindow(a.settings);
        ++stage;
        return;
    }
    FUI(!online.busy && !a.async_jobs);
    FUI(wcsstr(online.status, L"模拟") != NULL);
    show_settings();
    settings_page(6);
    RedrawWindow(a.settings, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    capture_window(a.settings, L"artifacts\\v120-update.bmp");
    FUI(a.mock_data.writes == 0);
    wprintf(L"Feature GUI: %d checks, %d failures; DPI/layout simulation, picker async/filter/dedup/close, "
            L"mock update close; writes=%u.\n",
            checks, failures, a.mock_data.writes);
    a.smoke_exit = failures ? 1 : 0;
    KillTimer(a.window, 2);
    SendMessageW(a.window, WM_COMMAND, ID_EXIT, 0);
#undef FUI
}
