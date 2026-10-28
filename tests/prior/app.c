#include "lite.h"
#include "settings.h"
#include "visual.h"
#include "update.h"
#include <shellapi.h>
#include <commctrl.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <objbase.h>
#include <stdio.h>
#include <wchar.h>
#include <windowsx.h>
#define ID_EXIT 106 /* Compatibility: 1.0.0 normal exit, used by the offline updater. */
#define ID_SHOW 103
#define ID_REFRESH 102
#define ID_SETTINGS 110
#define ID_CAMPUS 111
#define ID_HOTSPOT 112
#define ID_COMBO0 120
#define ID_COMBO1 121
#define ID_TOGGLE0 130
#define ID_TOGGLE1 131
#define ID_ENABLE0 140
#define ID_DISABLE0 141
#define ID_ENABLE1 142
#define ID_DISABLE1 143
#define ID_THEME 160
#define WM_TRAY (WM_APP + 1)
#define WM_DONE (WM_APP + 2)
#define WM_IMAGE_DONE (WM_APP + 3)
#ifdef NP_MOCK_PRODUCT
#define NP_WINDOW_CLASS L"NetPilotLiteTestWindow1"
#else
#define NP_WINDOW_CLASS L"NetPilotLiteWindow1"
#endif
typedef struct CARD {
    HWND title, combo, identity, state, detail, toggle;
    int map[NP_MAX_ADAPTERS];
    int count;
    NP_STATE current;
} CARD;
typedef struct APP {
    HWND window, settings, title, subtitle, campus, hotspot, refresh, options, message;
    CARD cards[2];
    NP_SETTINGS prefs;
    NP_COLORS colors;
    HBRUSH bg, card;
    HFONT normal, small_font, heading, bold;
    UINT dpi, taskbar_created;
    BOOL mock, smoke, admin, tray, scan_ok, config_ok, local, quit_pending;
    int busy, loading_images, scroll, settings_scroll, content_height, smoke_stage, smoke_exit;
    BOOL first_scan;
    RECT cover, card_rect[2];
    NP_SNAPSHOT snapshot;
    NP_MOCK mock_data;
    NP_BACKEND backend;
    NP_IMAGE cover_image, background_image;
    NOTIFYICONDATAW icon;
    HWND theme_combo, bg_combo, fit_combo, mask_slider, tray_check;
} APP;
static APP a;
static const WCHAR *themes[] = {L"深色", L"浅色", L"樱彩 Princess", L"跟随系统", L"自定义"};
static void start_job(int, int, BOOL, BOOL);
static void layout(void);
static void show_settings(void);
static UINT dpi_for(HWND w) {
    typedef UINT(WINAPI * F)(HWND);
    F f = (F)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
    return f ? f(w) : 96;
}
static int px(int x) {
    return MulDiv(x, (int)a.dpi, 96);
}
static HFONT make_font(int size, int weight) {
    return CreateFontW(-px(size), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                       CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
}
static void error_text(const NP_ERROR *e, WCHAR *out, size_t n) {
    WCHAR system[260] = L"";
    FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, e->win32, 0, system,
                   _countof(system), NULL);
    swprintf_s(out, n, L"%s\r\nWindows %lu (0x%08lX)，CM 0x%08lX：%s", e->operation, e->win32, e->win32,
               e->config_ret, system);
}
static void message(const WCHAR *s) {
    SetWindowTextW(a.message, s);
}
static void save(void) {
    if (a.mock || !a.config_ok)
        return;
    NP_ERROR e;
    if (!np_settings_save(&a.prefs, NULL, &e)) {
        WCHAR text[700];
        error_text(&e, text, _countof(text));
        message(text);
    }
}
static void set_fonts(void) {
    DeleteObject(a.normal);
    DeleteObject(a.small_font);
    DeleteObject(a.heading);
    DeleteObject(a.bold);
    a.normal = make_font(14, FW_NORMAL);
    a.small_font = make_font(12, FW_NORMAL);
    a.heading = make_font(28, FW_SEMIBOLD);
    a.bold = make_font(18, FW_SEMIBOLD);
    for (HWND c = GetWindow(a.window, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT))
        SendMessageW(c, WM_SETFONT, (WPARAM)a.normal, TRUE);
    SendMessageW(a.title, WM_SETFONT, (WPARAM)a.heading, TRUE);
    SendMessageW(a.subtitle, WM_SETFONT, (WPARAM)a.small_font, TRUE);
    for (int i = 0; i < 2; ++i) {
        CARD *c = &a.cards[i];
        SendMessageW(c->title, WM_SETFONT, (WPARAM)a.bold, TRUE);
        SendMessageW(c->identity, WM_SETFONT, (WPARAM)a.small_font, TRUE);
        SendMessageW(c->detail, WM_SETFONT, (WPARAM)a.small_font, TRUE);
        SendMessageW(c->state, WM_SETFONT, (WPARAM)a.bold, TRUE);
        SendMessageW(c->combo, CB_SETITEMHEIGHT, (WPARAM)-1, px(28));
        SendMessageW(c->combo, CB_SETITEMHEIGHT, 0, px(28));
    }
}
static void appearance(void) {
    a.colors = np_theme(&a.prefs);
    DeleteObject(a.bg);
    DeleteObject(a.card);
    a.bg = CreateSolidBrush(a.colors.background);
    a.card = CreateSolidBrush(a.colors.card);
    BOOL dark = (GetRValue(a.colors.background) * 299 + GetGValue(a.colors.background) * 587 +
                 GetBValue(a.colors.background) * 114) < 128000;
    DwmSetWindowAttribute(a.window, 20, &dark, sizeof(dark));
    if (a.settings)
        DwmSetWindowAttribute(a.settings, 20, &dark, sizeof(dark));
    InvalidateRect(a.window, NULL, TRUE);
    if (a.settings)
        RedrawWindow(a.settings, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN);
    save();
}
static HWND control(HWND parent, const WCHAR *cls, const WCHAR *text, DWORD style, int id) {
    HWND w = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 1, 1, parent,
                             (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL);
    SendMessageW(w, WM_SETFONT, (WPARAM)a.normal, FALSE);
    return w;
}
static void place(HWND w, int x, int y, int width, int height) {
    MoveWindow(w, px(x), px(y - a.scroll), px(width), px(height), TRUE);
}
static void layout(void) {
    if (!a.title)
        return;
    RECT r;
    GetClientRect(a.window, &r);
    int width = MulDiv(r.right, 96, (int)a.dpi), height = MulDiv(r.bottom, 96, (int)a.dpi);
    BOOL wide = width >= 930;
    int margin = 24, gap = 24, cover_width = wide ? (width * 35 / 100) : width - 48;
    int x = wide ? margin + cover_width + gap : margin, cw = wide ? width - x - margin : width - 48,
        top = wide ? 112 : 368;
    int total = top + 2 * 242 + 122 + 104;
    a.content_height = total;
    if (a.scroll > total - height)
        a.scroll = total - height;
    if (a.scroll < 0)
        a.scroll = 0;
    SCROLLINFO si = {
        sizeof(si), SIF_RANGE | SIF_PAGE | SIF_POS, 0, total, (UINT)(height > 0 ? height : 1), a.scroll, 0};
    SetScrollInfo(a.window, SB_VERT, &si, TRUE);
    place(a.title, 24, 20, width - 48, 44);
    place(a.subtitle, 26, 70, width - 48, 28);
    a.cover = (RECT){px(margin), px(112 - a.scroll), px(margin + cover_width),
                     px((wide ? total - 32 : 344) - a.scroll)};
    for (int i = 0; i < 2; ++i) {
        CARD *c = &a.cards[i];
        int y = top + i * 242;
        a.card_rect[i] = (RECT){px(x), px(y - a.scroll), px(x + cw), px(y + 222 - a.scroll)};
        place(c->title, x + 18, y + 12, cw - 36, 26);
        place(c->combo, x + 18, y + 46, cw - 36, 220);
        place(c->identity, x + 18, y + 81, cw - 36, 21);
        place(c->state, x + 18, y + 109, cw - 36, 32);
        place(c->detail, x + 18, y + 146, cw - 36, 25);
        place(c->toggle, x + 18, y + 178, cw - 36, 32);
    }
    int y = top + 484;
    place(a.campus, x, y, (cw - gap) / 2, 42);
    place(a.hotspot, x + (cw + gap) / 2, y, (cw - gap) / 2, 42);
    place(a.refresh, x, y + 56, 130, 36);
    place(a.options, x + 146, y + 56, cw - 146, 36);
    place(a.message, x, y + 106, cw, 96);
    InvalidateRect(a.window, NULL, TRUE);
}
static void show(void) {
    ShowWindow(a.window, SW_RESTORE);
    SetForegroundWindow(a.window);
}
static const WCHAR *state_text(int i, NP_STATE state) {
    return i == 1 && state == NP_ENABLED_DISCONNECTED ? L"已启用 · 尚未连接 Wi-Fi" : np_state_text(state);
}
static void tray_update(BOOL create) {
    if (create) {
        ZeroMemory(&a.icon, sizeof(a.icon));
        a.icon.cbSize = sizeof(a.icon);
        a.icon.hWnd = a.window;
        a.icon.uID = 1;
        a.icon.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
        a.icon.uCallbackMessage = WM_TRAY;
        a.icon.hIcon = (HICON)LoadImageW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(1), IMAGE_ICON, 16, 16,
                                         LR_DEFAULTCOLOR);
    }
    swprintf_s(a.icon.szTip, _countof(a.icon.szTip), L"NetPilot Lite%s\n有线：%s\nWi-Fi：%s",
               a.mock ? L" [模拟]" : L"", state_text(0, a.cards[0].current),
               state_text(1, a.cards[1].current));
    if (Shell_NotifyIconW(create ? NIM_ADD : NIM_MODIFY, &a.icon))
        a.tray = TRUE;
}
static void notify(const WCHAR *s, BOOL success) {
    a.icon.uFlags |= NIF_INFO;
    wcscpy_s(a.icon.szInfoTitle, _countof(a.icon.szInfoTitle), L"NetPilot Lite");
    wcsncpy_s(a.icon.szInfo, _countof(a.icon.szInfo), s, _TRUNCATE);
    a.icon.dwInfoFlags = success ? NIIF_INFO : NIIF_WARNING;
    Shell_NotifyIconW(NIM_MODIFY, &a.icon);
    a.icon.uFlags &= ~NIF_INFO;
}
static BOOL can(int i, BOOL enable) {
    NP_STATE s = a.cards[i].current;
    return !a.busy && !a.quit_pending && a.scan_ok && a.prefs.has[i] && (a.admin || a.mock) &&
           (np_enabled(s) || s == NP_DISABLED || s == NP_INTERFACE_DISABLED) && (enable || a.local);
}
static void controls_state(void) {
    for (int i = 0; i < 2; ++i) {
        SetWindowTextW(a.cards[i].toggle, np_enabled(a.cards[i].current)
                                              ? (i ? L"禁用 Wi-Fi" : L"禁用以太网")
                                              : (i ? L"启用 Wi-Fi" : L"启用以太网"));
        EnableWindow(a.cards[i].toggle, can(i, !np_enabled(a.cards[i].current)));
        EnableWindow(a.cards[i].combo, !a.busy);
    }
    EnableWindow(a.refresh, !a.busy);
    EnableWindow(a.campus, can(0, TRUE));
    EnableWindow(a.hotspot, can(0, FALSE) && a.prefs.has[1]);
}
static void render_snapshot(void) {
    NP_ERROR e = {0};
    BOOL changed = FALSE;
    for (int k = 0; k < 2; ++k) {
        CARD *c = &a.cards[k];
        c->count = 0;
        SendMessageW(c->combo, CB_RESETCONTENT, 0, 0);
        c->current = NP_UNKNOWN;
        if (a.scan_ok) {
            for (size_t i = 0; i < a.snapshot.count; ++i) {
                NP_ADAPTER *d = &a.snapshot.adapters[i];
                if (k ? d->physical_wifi : d->physical_ethernet) {
                    c->map[c->count++] = (int)i;
                    SendMessageW(c->combo, CB_ADDSTRING, 0, (LPARAM)d->name);
                }
            }
            if (!a.prefs.has[k] && !a.prefs.binding_invalid) {
                int selected = k ? np_auto_wifi(&a.snapshot) : np_auto_select(&a.snapshot);
                if (selected >= 0) {
                    a.prefs.binding[k] = a.snapshot.adapters[selected].identity;
                    a.prefs.has[k] = TRUE;
                    changed = TRUE;
                }
            }
            const NP_ADAPTER *d = a.prefs.has[k] ? np_find(&a.snapshot, &a.prefs.binding[k]) : NULL;
            if (d) {
                c->current = d->state;
                for (int n = 0; n < c->count; ++n)
                    if (&a.snapshot.adapters[c->map[n]] == d)
                        SendMessageW(c->combo, CB_SETCURSEL, n, 0);
                WCHAR guid[64], text[400];
                StringFromGUID2(&d->identity.guid, guid, _countof(guid));
                swprintf_s(text, _countof(text), L"GUID %s", guid);
                SetWindowTextW(c->identity, text);
                if (k)
                    swprintf_s(text, _countof(text), L"%s%s  ·  IPv4 %s",
                               d->ssid[0]                                    ? L"SSID "
                               : d->wifi_detail_error == ERROR_ACCESS_DENIED ? L"SSID 受 Windows 位置权限限制"
                                                                             : L"SSID 暂不可读",
                               d->ssid, d->ip[0] ? d->ip : L"未获取");
                else
                    swprintf_s(text, _countof(text), L"IPv4 %s  ·  链路连接不代表互联网可用",
                               d->ip[0] ? d->ip : L"未获取");
                SetWindowTextW(c->detail, text);
            } else {
                SetWindowTextW(c->identity, a.prefs.has[k] ? L"已绑定设备暂未找到；不会自动改选其他网卡。"
                                                           : L"请选择物理网卡；无法唯一匹配时不会自动绑定。");
                SetWindowTextW(c->detail, L"仅管理本卡启用状态，不修改 DNS、Metric、网关或代理。");
            }
        }
        SetWindowTextW(c->state, state_text(k, c->current));
    }
    (void)e;
    if (changed)
        save();
    controls_state();
    tray_update(FALSE);
}
typedef struct JOB {
    HWND window;
    int kind, index;
    BOOL enable, allow;
    NP_BINDING binding[2];
} JOB;
typedef struct DONE {
    int kind;
    BOOL scan_ok, local;
    NP_SNAPSHOT snapshot;
    NP_RESULT result;
    NP_ERROR error;
} DONE;
static DWORD WINAPI worker(void *param) {
    JOB *j = param;
    DONE *d = calloc(1, sizeof(*d));
    if (d) {
        d->kind = j->kind;
        if (j->kind == 2)
            d->result = j->index == 2
                            ? np_hotspot(&a.backend, &j->binding[0], &j->binding[1], j->allow, 10000)
                            : np_execute(&a.backend, &j->binding[j->index], j->enable, 10000);
        d->scan_ok = a.backend.scan(a.backend.context, &d->snapshot, &d->error);
        d->local = a.backend.safe_local_session(a.backend.context);
        if (!PostMessageW(j->window, WM_DONE, 0, (LPARAM)d))
            free(d);
    } else
        PostMessageW(j->window, WM_DONE, 0, 0);
    free(j);
    return 0;
}
static void start_job(int kind, int index, BOOL enable, BOOL allow) {
    if (a.busy || a.quit_pending)
        return;
    if (kind == 2 && index < 2 && !can(index, enable))
        return;
    JOB *j = calloc(1, sizeof(*j));
    if (!j)
        return;
    j->window = a.window;
    j->kind = kind;
    j->index = index;
    j->enable = enable;
    j->allow = allow;
    memcpy(j->binding, a.prefs.binding, sizeof(j->binding));
    a.busy = kind;
    controls_state();
    if (kind == 2)
        message(L"正在请求设备变更，并等待 Windows 实际状态验证…");
    HANDLE t = CreateThread(NULL, 0, worker, j, 0, NULL);
    if (t)
        CloseHandle(t);
    else {
        free(j);
        a.busy = 0;
        message(L"无法启动工作线程，请重试。");
        controls_state();
    }
}
static void operation(int index, BOOL enable) {
    if (!can(index, enable))
        return;
    if (!enable && !a.mock && a.cards[index].current == NP_ENABLED_CONNECTED &&
        a.cards[1 - index].current != NP_ENABLED_CONNECTED) {
        if (MessageBoxW(a.window,
                        L"另一张绑定网卡尚未确认连接。禁用当前网卡可能立即断网。仍要禁用这张网卡吗？",
                        L"确认禁用选中的网卡", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
            return;
    }
    start_job(2, index, enable, FALSE);
}
static void hotspot(void) {
    if (a.busy || !a.local || !a.prefs.has[0] || !a.prefs.has[1])
        return;
    BOOL allow = FALSE;
    if (!np_enabled(a.cards[1].current)) {
        if (!a.mock &&
            MessageBoxW(a.window, L"Wi-Fi 当前未启用。先启用它吗？只有确认 Wi-Fi 已连接后才会禁用有线网卡。",
                        L"手机热点模式", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
            return;
        allow = TRUE;
    }
    start_job(2, 2, FALSE, allow);
}
static HMENU tray_menu_create(void) {
    HMENU menu = CreatePopupMenu(), theme = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, ID_SHOW, L"显示 NetPilot Lite");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    for (int i = 0; i < 2; ++i) {
        AppendMenuW(menu, MF_STRING | (can(i, TRUE) ? 0 : MF_GRAYED), i ? ID_ENABLE1 : ID_ENABLE0,
                    i ? L"启用 Wi-Fi" : L"启用以太网");
        AppendMenuW(menu, MF_STRING | (can(i, FALSE) ? 0 : MF_GRAYED), i ? ID_DISABLE1 : ID_DISABLE0,
                    i ? L"禁用 Wi-Fi" : L"禁用以太网");
    }
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING | (can(0, TRUE) ? 0 : MF_GRAYED), ID_CAMPUS, L"校园网模式");
    AppendMenuW(menu, MF_STRING | (can(0, FALSE) && a.prefs.has[1] ? 0 : MF_GRAYED), ID_HOTSPOT,
                L"手机热点模式");
    for (int i = 0; i < 5; ++i)
        AppendMenuW(theme, MF_STRING | (a.prefs.theme == i ? MF_CHECKED : 0), ID_THEME + i, themes[i]);
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)theme, L"主题");
    AppendMenuW(menu, MF_STRING, ID_SETTINGS, L"设置 / 关于 / 更新");
    AppendMenuW(menu, MF_STRING, ID_REFRESH, L"刷新状态");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING | (a.busy == 2 ? MF_GRAYED : 0), ID_EXIT, L"退出（保持网卡当前状态）");
    return menu;
}
static void tray_menu(void) {
    HMENU m = tray_menu_create();
    POINT p;
    GetCursorPos(&p);
    SetForegroundWindow(a.window);
    TrackPopupMenu(m, TPM_RIGHTBUTTON, p.x, p.y, 0, a.window, NULL);
    PostMessageW(a.window, WM_NULL, 0, 0);
    DestroyMenu(m);
}
static BOOL browse(HWND owner, WCHAR *file, const WCHAR *filter) {
    OPENFILENAMEW o = {0};
    o.lStructSize = sizeof(o);
    o.hwndOwner = owner;
    o.lpstrFilter = filter;
    o.lpstrFile = file;
    o.nMaxFile = NP_PATH;
    o.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    return GetOpenFileNameW(&o);
}
static void choose_color(int which) {
    static COLORREF custom[16];
    COLORREF *color = which == 0   ? &a.prefs.custom_accent
                      : which == 1 ? &a.prefs.custom_background
                                   : &a.prefs.custom_card;
    CHOOSECOLORW c = {0};
    c.lStructSize = sizeof(c);
    c.hwndOwner = a.settings;
    c.rgbResult = *color;
    c.lpCustColors = custom;
    c.Flags = CC_FULLOPEN | CC_RGBINIT;
    if (ChooseColorW(&c)) {
        *color = c.rgbResult;
        a.prefs.theme = 4;
        SendMessageW(a.theme_combo, CB_SETCURSEL, 4, 0);
        appearance();
    }
}
typedef struct IMAGE_JOB {
    HWND window;
    BOOL cover, select, copy, ok;
    WCHAR path[NP_PATH];
    NP_IMAGE image;
} IMAGE_JOB;
static DWORD WINAPI image_worker(void *param) {
    IMAGE_JOB *j = param;
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (j->cover)
        j->ok = np_image_resource(200, &j->image);
    else {
        BOOL copied = FALSE;
        if (j->copy) {
            WCHAR dir[NP_PATH], dest[NP_PATH], guid[40];
            GUID id;
            if (np_data_directory(dir, _countof(dir)) && SUCCEEDED(CoCreateGuid(&id))) {
                StringFromGUID2(&id, guid, _countof(guid));
                swprintf_s(dest, _countof(dest), L"%s\\background-%s.img", dir, guid);
                if (CopyFileW(j->path, dest, TRUE)) {
                    wcscpy_s(j->path, _countof(j->path), dest);
                    copied = TRUE;
                }
            }
        }
        if (!j->copy || copied)
            j->ok = np_image_file(j->path, &j->image);
        if (copied && !j->ok)
            DeleteFileW(j->path);
    }
    CoUninitialize();
    if (!PostMessageW(j->window, WM_IMAGE_DONE, 0, (LPARAM)j)) {
        np_image_free(&j->image);
        free(j);
    }
    return 0;
}
static BOOL load_image(BOOL cover, const WCHAR *path, BOOL selected) {
    IMAGE_JOB *j = calloc(1, sizeof(*j));
    if (!j)
        return FALSE;
    j->window = a.window;
    j->cover = cover;
    j->select = selected;
    j->copy = selected && !a.mock;
    if (path)
        wcscpy_s(j->path, _countof(j->path), path);
    HANDLE t = CreateThread(NULL, 0, image_worker, j, 0, NULL);
    if (!t) {
        free(j);
        return FALSE;
    }
    ++a.loading_images;
    CloseHandle(t);
    return TRUE;
}
static void choose_background(void) {
    if (a.loading_images) {
        message(L"正在加载图片，请稍候。");
        return;
    }
    WCHAR file[NP_PATH] = L"";
    if (!browse(a.settings, file, L"图片 (*.png;*.jpg;*.jpeg;*.bmp)\0*.png;*.jpg;*.jpeg;*.bmp\0\0"))
        return;
    if (load_image(FALSE, file, TRUE))
        message(L"正在后台读取背景图片…");
    else
        message(L"无法启动图片加载线程。");
}
static void local_update(void) {
    if (a.busy == 2) {
        MessageBoxW(a.settings, L"请先等待网卡操作完成。", L"更新", MB_OK);
        return;
    }
    WCHAR file[NP_PATH] = L"";
    if (!browse(a.settings, file, L"NetPilot Lite EXE\0NetPilotLite.exe\0\0"))
        return;
    NP_RELEASE release;
    NP_ERROR e = {0};
    WCHAR text[700];
    if (!np_verify_release(file, &release, &e)) {
        error_text(&e, text, _countof(text));
        MessageBoxW(a.settings, text, L"更新文件验证失败", MB_OK | MB_ICONERROR);
        return;
    }
    swprintf_s(text, _countof(text),
               L"已验证签名、SHA-256 和产品版本 %lu.%lu.%lu。\n将退出当前程序并覆盖当前路径的 "
               L"NetPilotLite.exe。\n保留用户配置，不改变任何网卡状态。继续吗？",
               release.version[0], release.version[1], release.version[2]);
    if (MessageBoxW(a.settings, text, L"本地原地更新", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
        return;
    save();
    if (!np_update_start(a.settings, file, &e)) {
        error_text(&e, text, _countof(text));
        MessageBoxW(a.settings, text, L"更新未启动", MB_OK | MB_ICONERROR);
    } else
        message(L"更新器已启动，正在等待本实例正常退出…");
}
static LRESULT draw_item(LPARAM lp) {
    DRAWITEMSTRUCT *d = (DRAWITEMSTRUCT *)lp;
    RECT r = d->rcItem;
    BOOL disabled = (d->itemState & ODS_DISABLED) != 0;
    COLORREF color = disabled ? a.colors.border : a.colors.button;
    HBRUSH b = CreateSolidBrush(color);
    FillRect(d->hDC, &r, b);
    DeleteObject(b);
    WCHAR text[512] = L"";
    if (d->CtlType == ODT_COMBOBOX && d->itemID != (UINT)-1)
        SendMessageW(d->hwndItem, CB_GETLBTEXT, d->itemID, (LPARAM)text);
    else
        GetWindowTextW(d->hwndItem, text, _countof(text));
    HGDIOBJ old = SelectObject(d->hDC, a.normal);
    SetBkMode(d->hDC, TRANSPARENT);
    SetTextColor(d->hDC, disabled ? a.colors.secondary : a.colors.button_text);
    r.left += px(10);
    r.right -= px(10);
    DrawTextW(d->hDC, text, -1, &r,
              DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS |
                  (d->CtlType == ODT_COMBOBOX ? DT_LEFT : DT_CENTER));
    if (d->itemState & ODS_FOCUS)
        DrawFocusRect(d->hDC, &r);
    SelectObject(d->hDC, old);
    return TRUE;
}
static void settings_scroll(HWND w, int next) {
    RECT r;
    GetClientRect(w, &r);
    int content = px(640), max = content - r.bottom;
    if (max < 0)
        max = 0;
    if (next < 0)
        next = 0;
    if (next > max)
        next = max;
    int delta = a.settings_scroll - next;
    a.settings_scroll = next;
    SCROLLINFO si = {sizeof(si), SIF_RANGE | SIF_PAGE | SIF_POS, 0, content - 1, (UINT)r.bottom, next, 0};
    SetScrollInfo(w, SB_VERT, &si, TRUE);
    if (delta)
        ScrollWindowEx(w, 0, delta, NULL, NULL, NULL, NULL, SW_SCROLLCHILDREN | SW_INVALIDATE | SW_ERASE);
}
static LRESULT CALLBACK settings_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        a.settings = w;
        a.settings_scroll = 0;
        int x = 24, y = 20;
        HWND h;
#define POS(handle, xx, yy, ww, hh) MoveWindow(handle, px(xx), px(yy), px(ww), px(hh), TRUE)
#define LABEL(text, yy)                                                                                      \
    h = control(w, L"STATIC", text, SS_NOPREFIX, 0);                                                         \
    POS(h, 24, yy, 590, 26)
#define BUTTON(text, id, xx, yy, ww)                                                                         \
    h = control(w, L"BUTTON", text, BS_OWNERDRAW | WS_TABSTOP, id);                                          \
    POS(h, xx, yy, ww, 34)
        LABEL(L"外观与背景", y);
        a.theme_combo = control(w, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP, 201);
        POS(a.theme_combo, x, 54, 250, 220);
        for (int i = 0; i < 5; ++i)
            SendMessageW(a.theme_combo, CB_ADDSTRING, 0, (LPARAM)themes[i]);
        SendMessageW(a.theme_combo, CB_SETCURSEL, a.prefs.theme, 0);
        BUTTON(L"自定义强调色", 210, 24, 98, 176);
        BUTTON(L"背景色", 211, 214, 98, 176);
        BUTTON(L"卡片色", 212, 404, 98, 176);
        a.bg_combo = control(w, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP, 202);
        POS(a.bg_combo, 24, 148, 250, 180);
        const WCHAR *bg[] = {L"纯色背景", L"渐变背景", L"自定义图片"};
        for (int i = 0; i < 3; ++i)
            SendMessageW(a.bg_combo, CB_ADDSTRING, 0, (LPARAM)bg[i]);
        SendMessageW(a.bg_combo, CB_SETCURSEL, a.prefs.background_mode, 0);
        a.fit_combo = control(w, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP, 203);
        POS(a.fit_combo, 298, 148, 282, 180);
        const WCHAR *fit[] = {L"Fit · 完整显示", L"Fill · 填满裁剪", L"Center · 居中原尺寸"};
        for (int i = 0; i < 3; ++i)
            SendMessageW(a.fit_combo, CB_ADDSTRING, 0, (LPARAM)fit[i]);
        SendMessageW(a.fit_combo, CB_SETCURSEL, a.prefs.image_mode, 0);
        BUTTON(L"选择图片…", 213, 24, 194, 176);
        BUTTON(L"清除背景图片", 214, 214, 194, 176);
        BUTTON(L"恢复外观默认值", 215, 404, 194, 176);
        LABEL(L"图片遮罩强度（左侧 0% → 右侧 100%）", 246);
        a.mask_slider = control(w, TRACKBAR_CLASSW, L"", TBS_AUTOTICKS | WS_TABSTOP, 204);
        POS(a.mask_slider, 24, 276, 556, 32);
        SendMessageW(a.mask_slider, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
        SendMessageW(a.mask_slider, TBM_SETPOS, TRUE, a.prefs.mask);
        a.tray_check = control(w, L"BUTTON", L"关闭窗口时收至系统托盘", BS_AUTOCHECKBOX | WS_TABSTOP, 205);
        POS(a.tray_check, 24, 320, 556, 32);
        SendMessageW(a.tray_check, BM_SETCHECK, a.prefs.close_tray ? BST_CHECKED : BST_UNCHECKED, 0);
        LABEL(L"关于 · 更新", 374);
        LABEL(L"NetPilot Lite  " NP_VERSION L"  ·  原生 C17 / Win32 · x64", 410);
        BUTTON(L"检查更新", 216, 24, 454, 176);
        BUTTON(L"从本地文件更新…", 217, 214, 454, 176);
        BUTTON(L"查看更新日志", 218, 404, 454, 176);
        LABEL(L"本地更新需同时提供 EXE 与它的 .exe.update 签名清单。", 506);
        LABEL(L"配置保存在 %LOCALAPPDATA%\\NetPilotLite，不随 EXE 覆盖。", 540);
        BUTTON(L"完成", 219, 404, 584, 176);
#undef POS
#undef LABEL
#undef BUTTON
        return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(wp);
        if (id >= 213 && id <= 215 && a.loading_images) {
            message(L"正在加载图片，请稍候。");
            return 0;
        }
        if (id == 201 && HIWORD(wp) == CBN_SELCHANGE) {
            a.prefs.theme = (int)SendMessageW(a.theme_combo, CB_GETCURSEL, 0, 0);
            appearance();
        } else if (id == 202 && HIWORD(wp) == CBN_SELCHANGE) {
            a.prefs.background_mode = (int)SendMessageW(a.bg_combo, CB_GETCURSEL, 0, 0);
            appearance();
        } else if (id == 203 && HIWORD(wp) == CBN_SELCHANGE) {
            a.prefs.image_mode = (int)SendMessageW(a.fit_combo, CB_GETCURSEL, 0, 0);
            appearance();
        } else if (id == 205) {
            a.prefs.close_tray = SendMessageW(a.tray_check, BM_GETCHECK, 0, 0) == BST_CHECKED;
            save();
        } else if (id >= 210 && id <= 212)
            choose_color(id - 210);
        else if (id == 213)
            choose_background();
        else if (id == 214) {
            np_image_free(&a.background_image);
            a.prefs.image[0] = 0;
            a.prefs.background_mode = 0;
            SendMessageW(a.bg_combo, CB_SETCURSEL, 0, 0);
            appearance();
        } else if (id == 215) {
            NP_SETTINGS defaults;
            np_settings_defaults(&defaults);
            a.prefs.theme = defaults.theme;
            a.prefs.background_mode = 0;
            a.prefs.image_mode = 0;
            a.prefs.mask = 35;
            a.prefs.custom_accent = defaults.custom_accent;
            a.prefs.custom_background = defaults.custom_background;
            a.prefs.custom_card = defaults.custom_card;
            DestroyWindow(w);
            appearance();
            show_settings();
        } else if (id == 216)
            MessageBoxW(w, L"此版本支持本地签名文件更新。在线检查尚未接入；未进行任何联网下载。", L"检查更新",
                        MB_OK | MB_ICONINFORMATION);
        else if (id == 217)
            local_update();
        else if (id == 218)
            MessageBoxW(w,
                        L"1.0.1\n• 原地签名更新、启动确认与失败恢复\n• 响应式窗口、樱彩主题与原图封面\n• "
                        L"物理 Wi-Fi 控制及安全热点模式\n• 配置迁移、持久化与独立托盘操作",
                        L"更新日志", MB_OK);
        else if (id == 219)
            DestroyWindow(w);
        return 0;
    }
    case WM_SIZE:
        settings_scroll(w, a.settings_scroll);
        return 0;
    case WM_MOUSEWHEEL:
        settings_scroll(w, a.settings_scroll - GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA * px(80));
        return 0;
    case WM_VSCROLL: {
        SCROLLINFO si = {sizeof(si), SIF_ALL};
        GetScrollInfo(w, SB_VERT, &si);
        int next = a.settings_scroll;
        switch (LOWORD(wp)) {
        case SB_LINEUP:
            next -= px(32);
            break;
        case SB_LINEDOWN:
            next += px(32);
            break;
        case SB_PAGEUP:
            next -= (int)si.nPage;
            break;
        case SB_PAGEDOWN:
            next += (int)si.nPage;
            break;
        case SB_THUMBTRACK:
            next = si.nTrackPos;
            break;
        case SB_TOP:
            next = 0;
            break;
        case SB_BOTTOM:
            next = px(640);
            break;
        }
        settings_scroll(w, next);
        return 0;
    }
    case WM_HSCROLL:
        a.prefs.mask = (int)SendMessageW(a.mask_slider, TBM_GETPOS, 0, 0);
        InvalidateRect(a.window, NULL, TRUE);
        if (LOWORD(wp) != TB_THUMBTRACK)
            save();
        return 0;
    case WM_DRAWITEM:
        return draw_item(lp);
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORLISTBOX:
        SetBkColor((HDC)wp, a.colors.card);
        SetTextColor((HDC)wp, a.colors.foreground);
        return (LRESULT)a.card;
    case WM_ERASEBKGND: {
        RECT r;
        GetClientRect(w, &r);
        FillRect((HDC)wp, &r, a.card);
        return 1;
    }
    case WM_CLOSE:
        DestroyWindow(w);
        return 0;
    case WM_DESTROY:
        a.settings = NULL;
        return 0;
    }
    return DefWindowProcW(w, msg, wp, lp);
}
static void show_settings(void) {
    if (a.settings) {
        SetForegroundWindow(a.settings);
        return;
    }
    RECT r = {0, 0, px(608), px(640)};
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VSCROLL;
    AdjustWindowRectEx(&r, style, FALSE, 0);
    MONITORINFO monitor = {sizeof(monitor)};
    GetMonitorInfoW(MonitorFromWindow(a.window, MONITOR_DEFAULTTONEAREST), &monitor);
    int height = r.bottom - r.top, available = monitor.rcWork.bottom - monitor.rcWork.top - px(24);
    if (height > available)
        height = available;
    HWND w = CreateWindowExW(WS_EX_CONTROLPARENT, L"NetPilotLiteSettings", L"设置 · 关于 · 更新", style,
                             CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, height, a.window, NULL,
                             GetModuleHandleW(NULL), NULL);
    ShowWindow(w, SW_SHOW);
}
static void capture(const WCHAR *path) {
    RECT r;
    GetClientRect(a.window, &r);
    HDC dc = GetDC(a.window), mem = CreateCompatibleDC(dc);
    BITMAPINFO bi = {0};
    void *pixels;
    bi.bmiHeader.biSize = 40;
    bi.bmiHeader.biWidth = r.right;
    bi.bmiHeader.biHeight = -r.bottom;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    HBITMAP bm = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &pixels, NULL, 0);
    HGDIOBJ old = SelectObject(mem, bm);
    PrintWindow(a.window, mem, PW_CLIENTONLY);
    BITMAPFILEHEADER h = {0};
    h.bfType = 0x4d42;
    h.bfOffBits = sizeof(h) + 40;
    h.bfSize = h.bfOffBits + (DWORD)r.right * r.bottom * 4;
    FILE *f;
    _wfopen_s(&f, path, L"wb");
    if (f) {
        fwrite(&h, 1, sizeof(h), f);
        fwrite(&bi.bmiHeader, 1, 40, f);
        fwrite(pixels, 1, (size_t)r.right * r.bottom * 4, f);
        fclose(f);
    }
    SelectObject(mem, old);
    DeleteObject(bm);
    DeleteDC(mem);
    ReleaseDC(a.window, dc);
}
static void smoke_tick(void) {
    if (a.busy || a.loading_images)
        return;
    if (a.smoke_stage == 0) {
        if (!a.tray || a.mock_data.writes || !a.prefs.has[0] || !a.prefs.has[1])
            a.smoke_exit = 1;
        ShowWindow(a.window, SW_MINIMIZE);
        if (IsWindowVisible(a.window))
            a.smoke_exit = 1;
        show();
        ++a.smoke_stage;
        operation(0, FALSE);
    } else if (a.smoke_stage == 1) {
        if (a.cards[0].current != NP_DISABLED)
            a.smoke_exit = 1;
        capture(L"artifacts\\ui-disabled.bmp");
        ++a.smoke_stage;
        operation(0, TRUE);
    } else if (a.smoke_stage == 2) {
        if (!np_enabled(a.cards[0].current) || a.mock_data.writes != 2)
            a.smoke_exit = 1;
        capture(L"artifacts\\ui-enabled.bmp");
        ++a.smoke_stage;
    } else if (a.smoke_stage < 11) {
        int k = a.smoke_stage - 3;
        UINT dpis[] = {96, 120, 144, 192};
        a.dpi = dpis[k / 2];
        set_fonts();
        SetWindowPos(a.window, NULL, 20, 20, px(k % 2 ? 650 : 1160), px(850), SWP_NOZORDER);
        layout();
        WCHAR file[100];
        swprintf_s(file, _countof(file), L"artifacts\\ui-%u-%s.bmp", a.dpi, k % 2 ? L"narrow" : L"wide");
        capture(file);
        if (a.card_rect[0].bottom > a.card_rect[1].top)
            a.smoke_exit = 1;
        wprintf(L"Layout %u%% %s: cards do not overlap, scroll range=%d\n", a.dpi * 100 / 96,
                k % 2 ? L"narrow" : L"wide", a.content_height);
        ++a.smoke_stage;
    } else if (a.smoke_stage < 16) {
        int theme = a.smoke_stage - 11;
        a.prefs.theme = theme;
        appearance();
        WCHAR file[100];
        swprintf_s(file, _countof(file), L"artifacts\\ui-theme-%d.bmp", theme);
        capture(file);
        if (theme == 2) {
            show_settings();
            if (!a.settings || !IsWindowVisible(a.settings) ||
                SendMessageW(a.theme_combo, CB_GETCOUNT, 0, 0) != 5)
                a.smoke_exit = 1;
            SetWindowPos(a.settings, NULL, 30, 30, px(640), px(360), SWP_NOZORDER);
            SCROLLINFO si = {sizeof(si), SIF_ALL};
            GetScrollInfo(a.settings, SB_VERT, &si);
            if (si.nMax <= (int)si.nPage)
                a.smoke_exit = 1;
            SendMessageW(a.settings, WM_VSCROLL, SB_BOTTOM, 0);
            if (!a.settings_scroll)
                a.smoke_exit = 1;
            DestroyWindow(a.settings);
        }
        ++a.smoke_stage;
    } else if (a.smoke_stage < 19) {
        int mode = a.smoke_stage - 16;
        if (mode == 0 && !np_image_resource(200, &a.background_image))
            a.smoke_exit = 1;
        a.prefs.background_mode = 2;
        a.prefs.image_mode = mode;
        appearance();
        WCHAR file[100];
        swprintf_s(file, _countof(file), L"artifacts\\ui-background-%d.bmp", mode);
        capture(file);
        ++a.smoke_stage;
    } else {
        NP_IMAGE missing = {0};
        if (np_image_file(L"Z:\\NetPilotLite-nonexistent-image.png", &missing))
            a.smoke_exit = 1;
        np_image_free(&missing);
        wprintf(L"GUI smoke: tray=%d, two bindings, startup writes=0, verified toggles=%u, five themes, "
                L"settings, three image modes, exit=%d\n",
                a.tray, a.mock_data.writes, a.smoke_exit);
        DestroyWindow(a.window);
    }
}
static LRESULT CALLBACK window_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        a.window = w;
        a.dpi = dpi_for(w);
        a.normal = make_font(14, FW_NORMAL);
        appearance();
        a.title = control(w, L"STATIC", L"NetPilot Lite", 0, 0);
        a.subtitle = control(w, L"STATIC",
                             a.mock ? L"模拟预览 · 真实网卡修改入口已编译移除"
                                    : L"有线与 Wi-Fi，分别掌控 · 启动不会切换网络",
                             0, 0);
        for (int i = 0; i < 2; ++i) {
            CARD *c = &a.cards[i];
            c->title = control(w, L"STATIC", i ? L"Wi-Fi · 手机热点" : L"Ethernet · 校园网", 0, 0);
            c->combo =
                control(w, L"COMBOBOX", L"",
                        CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_TABSTOP | WS_VSCROLL,
                        ID_COMBO0 + i);
            c->identity = control(w, L"STATIC", L"正在枚举物理设备…", SS_NOPREFIX, 0);
            c->state = control(w, L"STATIC", L"正在读取状态…", 0, 0);
            c->detail = control(w, L"STATIC", L"", SS_NOPREFIX, 0);
            c->toggle = control(w, L"BUTTON", i ? L"启用 Wi-Fi" : L"启用以太网", BS_OWNERDRAW | WS_TABSTOP,
                                ID_TOGGLE0 + i);
        }
        a.campus = control(w, L"BUTTON", L"校园网模式", BS_OWNERDRAW | WS_TABSTOP, ID_CAMPUS);
        a.hotspot = control(w, L"BUTTON", L"手机热点模式", BS_OWNERDRAW | WS_TABSTOP, ID_HOTSPOT);
        a.refresh = control(w, L"BUTTON", L"刷新状态", BS_OWNERDRAW | WS_TABSTOP, ID_REFRESH);
        a.options = control(w, L"BUTTON", L"设置 · 关于 · 更新", BS_OWNERDRAW | WS_TABSTOP, ID_SETTINGS);
        a.message = control(w, L"STATIC",
                            a.config_ok ? L"仅切换选中的物理网卡；链路连接不保证互联网可用。"
                                        : L"配置未能安全读取。请选择正确网卡后重试；原文件保留。",
                            SS_NOPREFIX, 0);
        set_fonts();
        load_image(TRUE, NULL, FALSE);
        if (a.prefs.image[0])
            load_image(FALSE, a.prefs.image, FALSE);
        layout();
        tray_update(TRUE);
        SetTimer(w, 1, 5000, NULL);
        start_job(1, 0, FALSE, FALSE);
        if (a.smoke)
            SetTimer(w, 2, 250, NULL);
        return 0;
    }
    case WM_IMAGE_DONE: {
        IMAGE_JOB *j = (IMAGE_JOB *)lp;
        --a.loading_images;
        if (j) {
            if (j->ok) {
                NP_IMAGE *dest = j->cover ? &a.cover_image : &a.background_image;
                np_image_free(dest);
                *dest = j->image;
                if (j->select) {
                    wcscpy_s(a.prefs.image, _countof(a.prefs.image), j->path);
                    a.prefs.background_mode = 2;
                    if (a.settings)
                        SendMessageW(a.bg_combo, CB_SETCURSEL, 2, 0);
                    message(L"背景图片已保存并应用。");
                    appearance();
                }
                InvalidateRect(w, NULL, TRUE);
            } else
                message(j->select ? L"背景图片无法解码、复制失败或超过 1600 万像素；旧背景保持不变。"
                                  : L"图片暂不可用，已使用主题底色；原配置保留。");
            free(j);
        }
        if (a.first_scan && a.config_ok && !a.loading_images)
            np_update_health_ready();
        if (a.quit_pending && !a.busy && !a.loading_images)
            DestroyWindow(w);
        return 0;
    }
    case WM_DONE: {
        DONE *d = (DONE *)lp;
        a.busy = 0;
        if (d) {
            a.first_scan = TRUE;
            a.scan_ok = d->scan_ok;
            a.local = d->local;
            a.snapshot = d->snapshot;
            render_snapshot();
            WCHAR text[800];
            if (d->kind == 2) {
                if (d->result.success)
                    swprintf_s(text, _countof(text), L"%s：Windows 实际状态为“%s”。",
                               d->result.unchanged ? L"无需更改" : L"已验证", np_state_text(d->result.state));
                else
                    error_text(&d->result.error, text, _countof(text));
                message(text);
                notify(text, d->result.success);
            } else if (!d->scan_ok) {
                error_text(&d->error, text, _countof(text));
                message(text);
            } else if (!a.local)
                message(L"检测到远程桌面或会话状态不确定，所有禁用操作已阻止。");
            if (a.config_ok && !a.loading_images)
                np_update_health_ready();
            free(d);
        } else {
            message(L"工作线程内存不足，请重新刷新状态。");
            controls_state();
        }
        if (a.quit_pending && !a.loading_images)
            DestroyWindow(w);
        return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(wp);
        if (id == ID_EXIT) {
            if (a.busy == 1 || (!a.busy && a.loading_images))
                a.quit_pending = TRUE;
            else if (!a.busy)
                DestroyWindow(w);
        } else if (id == ID_SHOW)
            show();
        else if (id == ID_REFRESH)
            start_job(1, 0, FALSE, FALSE);
        else if (id == ID_SETTINGS)
            show_settings();
        else if (id == ID_CAMPUS)
            operation(0, TRUE);
        else if (id == ID_HOTSPOT)
            hotspot();
        else if (id == ID_TOGGLE0 || id == ID_TOGGLE1) {
            int i = id - ID_TOGGLE0;
            operation(i, !np_enabled(a.cards[i].current));
        } else if (id >= ID_ENABLE0 && id <= ID_DISABLE1)
            operation((id - ID_ENABLE0) / 2, ((id - ID_ENABLE0) % 2) == 0);
        else if (id >= ID_THEME && id < ID_THEME + 5) {
            a.prefs.theme = id - ID_THEME;
            appearance();
        } else if ((id == ID_COMBO0 || id == ID_COMBO1) && HIWORD(wp) == CBN_SELCHANGE && !a.busy) {
            int i = id - ID_COMBO0;
            LRESULT index = SendMessageW(a.cards[i].combo, CB_GETCURSEL, 0, 0);
            if (index >= 0 && index < a.cards[i].count) {
                a.prefs.binding[i] = a.snapshot.adapters[a.cards[i].map[index]].identity;
                a.prefs.has[i] = TRUE;
                a.prefs.binding_invalid =
                    FALSE; /* Unknown future schemas remain read-only until upgraded software is used. */
                a.config_ok = TRUE;
                save();
                render_snapshot();
                message(L"已绑定选中的物理网卡。绑定操作不会改变网卡状态。");
            }
        }
        return 0;
    }
    case WM_TIMER:
        if (wp == 1) {
            if (!SendMessageW(a.cards[0].combo, CB_GETDROPPEDSTATE, 0, 0) &&
                !SendMessageW(a.cards[1].combo, CB_GETDROPPEDSTATE, 0, 0))
                start_job(1, 0, FALSE, FALSE);
        } else if (wp == 2 && a.smoke)
            smoke_tick();
        return 0;
    case WM_DEVICECHANGE:
    case WM_POWERBROADCAST:
        start_job(1, 0, FALSE, FALSE);
        return TRUE;
    case WM_SETTINGCHANGE:
        if (a.prefs.theme == 3)
            appearance();
        return 0;
    case WM_TRAY:
        if (lp == WM_RBUTTONUP || lp == WM_CONTEXTMENU)
            tray_menu();
        else if (lp == WM_LBUTTONUP || lp == WM_LBUTTONDBLCLK)
            show();
        return 0;
    case WM_SIZE:
        if (wp == SIZE_MINIMIZED && a.tray)
            ShowWindow(w, SW_HIDE);
        else
            layout();
        return 0;
    case WM_GETMINMAXINFO:
        ((MINMAXINFO *)lp)->ptMinTrackSize = (POINT){px(580), px(480)};
        return 0;
    case WM_VSCROLL: {
        SCROLLINFO si = {sizeof(si), SIF_ALL};
        GetScrollInfo(w, SB_VERT, &si);
        int next = a.scroll;
        switch (LOWORD(wp)) {
        case SB_LINEUP:
            next -= 32;
            break;
        case SB_LINEDOWN:
            next += 32;
            break;
        case SB_PAGEUP:
            next -= (int)si.nPage;
            break;
        case SB_PAGEDOWN:
            next += (int)si.nPage;
            break;
        case SB_THUMBTRACK:
            next = si.nTrackPos;
            break;
        }
        a.scroll = next;
        layout();
        return 0;
    }
    case WM_MOUSEWHEEL:
        a.scroll -= GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA * 80;
        layout();
        return 0;
    case WM_DPICHANGED: {
        RECT *r = (RECT *)lp;
        a.dpi = HIWORD(wp);
        if (a.settings)
            DestroyWindow(a.settings);
        set_fonts();
        SetWindowPos(w, NULL, r->left, r->top, r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        layout();
        return 0;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORLISTBOX: {
        SetBkColor((HDC)wp, a.colors.card);
        COLORREF color = a.colors.foreground;
        for (int i = 0; i < 2; ++i)
            if ((HWND)lp == a.cards[i].state)
                color = np_enabled(a.cards[i].current) ? a.colors.success
                        : a.cards[i].current == NP_UNKNOWN || a.cards[i].current == NP_DEVICE_ERROR
                            ? a.colors.error
                            : a.colors.warning;
        SetTextColor((HDC)wp, color);
        return (LRESULT)a.card;
    }
    case WM_DRAWITEM:
        return draw_item(lp);
    case WM_MEASUREITEM:
        ((MEASUREITEMSTRUCT *)lp)->itemHeight = px(28);
        return TRUE;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(w, &ps);
        RECT r;
        GetClientRect(w, &r);
        HDC mem = CreateCompatibleDC(dc);
        HBITMAP bm = CreateCompatibleBitmap(dc, r.right ? r.right : 1, r.bottom ? r.bottom : 1);
        HGDIOBJ old = SelectObject(mem, bm);
        np_background(mem, &r, &a.prefs, &a.colors, &a.background_image);
        FillRect(mem, &a.cover, a.card);
        np_image_draw(mem, &a.cover, &a.cover_image, 0);
        for (int i = 0; i < 2; ++i) {
            FillRect(mem, &a.card_rect[i], a.card);
            HBRUSH b = CreateSolidBrush(a.colors.border);
            FrameRect(mem, &a.card_rect[i], b);
            DeleteObject(b);
        }
        BitBlt(dc, 0, 0, r.right, r.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old);
        DeleteObject(bm);
        DeleteDC(mem);
        EndPaint(w, &ps);
        return 0;
    }
    case WM_CLOSE:
        if (a.prefs.close_tray && a.tray)
            ShowWindow(w, SW_HIDE);
        else
            SendMessageW(w, WM_COMMAND, ID_EXIT, 0);
        return 0;
    case WM_QUERYENDSESSION:
        return a.busy == 2 ? FALSE : TRUE;
    case WM_DESTROY: {
        KillTimer(w, 1);
        KillTimer(w, 2);
        if (a.settings)
            DestroyWindow(a.settings);
        Shell_NotifyIconW(NIM_DELETE, &a.icon);
        if (a.icon.hIcon)
            DestroyIcon(a.icon.hIcon);
        WINDOWPLACEMENT pos = {sizeof(pos)};
        if (GetWindowPlacement(w, &pos)) {
            a.prefs.window = pos.rcNormalPosition;
            a.prefs.maximized = pos.showCmd == SW_SHOWMAXIMIZED;
        }
        save();
        PostQuitMessage(a.smoke_exit);
        return 0;
    }
    }
    if (msg == a.taskbar_created && a.taskbar_created) {
        tray_update(TRUE);
        return 0;
    }
    return DefWindowProcW(w, msg, wp, lp);
}
int np_app_run(HINSTANCE instance, int show_cmd, BOOL mock, BOOL smoke) {
    ZeroMemory(&a, sizeof(a));
    a.mock = mock;
    a.smoke = smoke;
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32);
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX cc = {sizeof(cc), ICC_STANDARD_CLASSES | ICC_BAR_CLASSES};
    InitCommonControlsEx(&cc);
    NP_ERROR error = {0};
    if (mock) {
        np_settings_defaults(&a.prefs);
        a.config_ok = TRUE;
        np_mock_init(&a.mock_data);
        a.backend = np_mock_backend(&a.mock_data);
    } else {
        a.config_ok = np_settings_load(&a.prefs, NULL, &error);
        a.backend = np_windows_backend();
    }
    SID_IDENTIFIER_AUTHORITY auth = SECURITY_NT_AUTHORITY;
    PSID admins;
    if (AllocateAndInitializeSid(&auth, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0,
                                 0, 0, &admins)) {
        CheckTokenMembership(NULL, admins, &a.admin);
        FreeSid(admins);
    }
    a.dpi = 96;
    a.colors = np_theme(&a.prefs);
    a.bg = CreateSolidBrush(a.colors.background);
    a.card = CreateSolidBrush(a.colors.card);
    a.taskbar_created = RegisterWindowMessageW(L"TaskbarCreated");
    WNDCLASSEXW cls = {0};
    cls.cbSize = sizeof(cls);
    cls.hInstance = instance;
    cls.lpfnWndProc = window_proc;
    cls.lpszClassName = NP_WINDOW_CLASS;
    cls.hCursor = LoadCursorW(NULL, IDC_ARROW);
    cls.hIcon = (HICON)LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON, 48, 48, 0);
    cls.hIconSm = (HICON)LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON, 24, 24, 0);
    if (!RegisterClassExW(&cls))
        return 1;
    cls.lpszClassName = L"NetPilotLiteSettings";
    cls.lpfnWndProc = settings_proc;
    RegisterClassExW(&cls);
    RECT r = a.prefs.window;
    if (r.right - r.left < 580 || r.bottom - r.top < 480 || !MonitorFromRect(&r, MONITOR_DEFAULTTONULL))
        r = (RECT){80, 80, 1260, 980};
    HWND w = CreateWindowExW(WS_EX_CONTROLPARENT, NP_WINDOW_CLASS,
                             mock ? L"NetPilot Lite [模拟测试]" : L"NetPilot Lite",
                             WS_OVERLAPPEDWINDOW | WS_VSCROLL, r.left, r.top, r.right - r.left,
                             r.bottom - r.top, NULL, NULL, instance, NULL);
    if (!w)
        return 1;
    ShowWindow(w, a.prefs.maximized ? SW_MAXIMIZE : show_cmd);
    UpdateWindow(w);
    MSG msg = {0};
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (a.settings && IsDialogMessageW(a.settings, &msg))
            continue;
        if (!IsDialogMessageW(w, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    np_image_free(&a.cover_image);
    np_image_free(&a.background_image);
    DeleteObject(a.normal);
    DeleteObject(a.small_font);
    DeleteObject(a.heading);
    DeleteObject(a.bold);
    DeleteObject(a.bg);
    DeleteObject(a.card);
    DestroyIcon(cls.hIcon);
    DestroyIcon(cls.hIconSm);
    CoUninitialize();
    return (int)msg.wParam;
}
