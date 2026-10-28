static void routing_ui_tick(void) {
    static int stage, checks, failures, waits;
    if (a.busy || a.loading_images)
        return;
#define ROUTE_UI_CHECK(x)                                                                                    \
    do {                                                                                                     \
        ++checks;                                                                                            \
        if (!(x)) {                                                                                          \
            ++failures;                                                                                      \
            wprintf(L"ROUTING_UI_FAIL %d %hs\n", __LINE__, #x);                                              \
        }                                                                                                    \
    } while (0)
    if (a.routing_busy) {
        if (++waits < 40)
            return;
        ROUTE_UI_CHECK(!a.routing_busy);
        a.smoke_exit = 1;
        SendMessageW(a.window, WM_COMMAND, ID_EXIT, 0);
        return;
    }
    waits = 0;
    if (stage == 0) {
        ROUTE_UI_CHECK(a.prefs.rule_count == 0 && a.mock_data.writes == 0);
        a.dpi = 96;
        set_fonts();
        show_settings();
        settings_page(5);
        ++stage;
        return;
    }
    if (stage == 1) {
        ROUTE_UI_CHECK(a.monitor && a.monitor->count == 1);
        ROUTE_UI_CHECK(ListView_GetItemCount(connections_list) == 1);
        ROUTE_UI_CHECK(ListView_GetItemCount(rules_list) == 0);
        if (a.monitor)
            ROUTE_UI_CHECK(wcsstr(a.monitor->rows[0].note, L"未验证") != NULL);
        /* In-memory fixture only. No default example rules in the product. */
        a.prefs.rule_count = 1;
        wcscpy_s(a.prefs.rules[0].path, NP_PATH, L"C:\\Mock\\Browser.exe");
        a.prefs.rules[0].target = 2;
        a.prefs.rules[0].enabled = FALSE;
        rules_render(0);
        ROUTE_UI_CHECK(selected_rule() == 0);
        HWND combo = GetDlgItem(a.settings_content, 321);
        SendMessageW(combo, CB_SETCURSEL, 1, 0);
        SendMessageW(a.settings, WM_COMMAND, MAKEWPARAM(321, CBN_SELCHANGE), (LPARAM)combo);
        ROUTE_UI_CHECK(a.prefs.rules[0].target == 1);
        SendMessageW(a.settings, WM_COMMAND, 322, 0);
        ROUTE_UI_CHECK(a.prefs.rules[0].enabled);
        for (int i = 0; i < 5; ++i) {
            a.prefs.theme = i;
            appearance();
            ROUTE_UI_CHECK(ListView_GetTextColor(connections_list) == a.colors.foreground);
        }
        for (UINT dpi = 96; dpi <= 192; dpi += 24) {
            a.settings_dpi = dpi;
            settings_fonts();
            settings_scroll(a.settings, 0);
            ROUTE_UI_CHECK(ListView_GetColumnWidth(connections_list, 0) == sp(170));
        }
        a.settings_dpi = dpi_for(a.settings);
        settings_fonts();
        settings_scroll(a.settings, 0);
        a.prefs.theme = 2;
        appearance();
        settings_scroll(a.settings, 0);
        capture_window(a.settings, L"artifacts\\v110-routing-rules.bmp");
        settings_scroll(a.settings, 100000);
        capture_window(a.settings, L"artifacts\\v110-routing-monitor.bmp");
        ROUTE_UI_CHECK(a.settings_scroll > 0);
        SendMessageW(a.settings, WM_COMMAND, 323, 0);
        ROUTE_UI_CHECK(a.prefs.rule_count == 0 && ListView_GetItemCount(rules_list) == 0);
        SendMessageW(a.settings, WM_COMMAND, 326, 0);
        ++stage;
        return;
    }
    if (stage == 2) {
        ROUTE_UI_CHECK(a.monitor && a.monitor->count == 1);
        routing_start();
        DestroyWindow(a.settings);
        ++stage;
        return;
    }
    ROUTE_UI_CHECK(!a.settings && !a.monitor && !a.routing_busy);
    ROUTE_UI_CHECK(a.mock_data.writes == 0);
    wprintf(L"Routing GUI: %d checks, %d failures; async refresh, virtual list, 5 themes, edit/delete rules, "
            L"close during read; mock writes=%u.\n",
            checks, failures, a.mock_data.writes);
    a.smoke_exit = failures ? 1 : 0;
    KillTimer(a.window, 2);
    SendMessageW(a.window, WM_COMMAND, ID_EXIT, 0);
#undef ROUTE_UI_CHECK
}
