#include "lite.h"
#include "settings.h"
#include "visual.h"
#include "update.h"
#include "apps.h"
#include "private_update.h"
#include "public_setup.h"
#include <shlwapi.h>
#include <richedit.h>
#include <shellapi.h>
#include <commctrl.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <objbase.h>
#include <stdio.h>
#include <wchar.h>
#include <windowsx.h>
#include <ws2ipdef.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <uxtheme.h>
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
#define WM_NETWORK_CHANGED (WM_APP + 4)
#define WM_ROUTING_DONE (WM_APP + 5)
#define WM_PICKER_DONE (WM_APP + 6)
#define WM_ONLINE_DONE (WM_APP + 7)
#define WM_ONLINE_PROGRESS (WM_APP + 8)
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
    NP_SURFACE frame, backdrop, cover_cache, tint;
    BOOL frame_dirty, backdrop_dirty, cover_dirty, laying_out, rescan_pending;
    HDWP positions;
    HANDLE interface_notify, address_notify;
    HWND flyout, settings_content;
    int flyout_scroll;
    UINT settings_dpi, flyout_dpi;
    HFONT settings_font, settings_heading, flyout_font, flyout_heading;
    int settings_page;
    BOOL acrylic;
    HRESULT acrylic_result;
    WCHAR status[800];
    NP_MONITOR *monitor;
    BOOL routing_busy;
    int async_jobs;
} APP;
static APP a;
static const WCHAR *themes[] = {L"深色", L"浅色", L"樱彩 Princess", L"跟随系统", L"自定义"};
static void start_job(int, int, BOOL, BOOL);
static void layout(void);
static void show_settings(void);
static void settings_refresh(void);
static void flyout_update(void);
static void flyout_style(void);
static void tray_update(BOOL);
static void compose(void);
static void routing_start(void);
static void routing_done(NP_MONITOR *);
static void routing_theme(void);
static void routing_columns(void);
static void text_if_changed(HWND w, const WCHAR *text) {
    WCHAR prior[1024];
    GetWindowTextW(w, prior, _countof(prior));
    if (wcscmp(prior, text)) {
        if (GetParent(w) == a.window)
            a.backdrop_dirty = a.frame_dirty = TRUE;
        SetWindowTextW(w, text);
    }
}
static void enable_if_changed(HWND w, BOOL enabled) {
    if (!!IsWindowEnabled(w) != !!enabled)
        EnableWindow(w, enabled);
}
static UINT dpi_for(HWND w) {
    typedef UINT(WINAPI * F)(HWND);
    F f = (F)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
    return f ? f(w) : 96;
}
static int px(int x) {
    return MulDiv(x, (int)a.dpi, 96);
}
static HFONT make_font(int size, int weight) {
    return CreateFontW(-MulDiv(px(size), a.prefs.font_scale, 100), 0, 0, 0, weight, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                       DEFAULT_PITCH, L"Segoe UI");
}
static void error_text(const NP_ERROR *e, WCHAR *out, size_t n) {
    WCHAR system[260] = L"";
    FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, e->win32, 0, system,
                   _countof(system), NULL);
    swprintf_s(out, n, L"%s\r\nWindows %lu (0x%08lX)，CM 0x%08lX：%s", e->operation, e->win32, e->win32,
               e->config_ret, system);
}
static void message(const WCHAR *s) {
    wcsncpy_s(a.status, _countof(a.status), s, _TRUNCATE);
    text_if_changed(a.message, s);
    flyout_update();
}
static void save_now(void) {
    if (a.mock || !a.config_ok)
        return;
    NP_ERROR e;
    if (!np_settings_save(&a.prefs, NULL, &e)) {
        WCHAR text[700];
        error_text(&e, text, _countof(text));
        message(text);
    }
}
static void save(void) {
    if (!a.mock && a.config_ok && a.window)
        SetTimer(a.window, 5, 300, NULL);
}
static void set_fonts(void) {
    a.frame_dirty = a.backdrop_dirty = TRUE;
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
    a.frame_dirty = a.backdrop_dirty = a.cover_dirty = TRUE;
    if (np_surface_size(&a.tint, 1, 1))
        SetPixel(a.tint.dc, 0, 0, a.colors.card);
    RedrawWindow(a.window, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN);
    flyout_style();
    settings_refresh();
    routing_theme();
    if (a.settings)
        RedrawWindow(a.settings, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN);
    save();
}
static HWND control(HWND parent, const WCHAR *cls, const WCHAR *text, DWORD style, int id) {
    HWND w = CreateWindowExW(0, cls, text,
                             WS_CHILD | WS_VISIBLE | style | (!wcscmp(cls, L"STATIC") ? SS_OWNERDRAW : 0), 0,
                             0, 1, 1, parent, (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL);
    SendMessageW(w, WM_SETFONT, (WPARAM)a.normal, FALSE);
    return w;
}
static void place(HWND w, int x, int y, int width, int height) {
    RECT prior;
    GetWindowRect(w, &prior);
    MapWindowPoints(NULL, a.window, (POINT *)&prior, 2);
    int xx = px(x), yy = px(y - a.scroll), ww = px(width), hh = px(height);
    if (prior.left == xx && prior.top == yy && prior.right - prior.left == ww &&
        (prior.bottom - prior.top == hh || w == a.cards[0].combo || w == a.cards[1].combo))
        return;
    UINT flags = SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW | SWP_NOCOPYBITS;
    if (a.positions)
        a.positions = DeferWindowPos(a.positions, w, NULL, xx, yy, ww, hh, flags);
    else
        SetWindowPos(w, NULL, xx, yy, ww, hh, flags);
}
static void layout(void) {
    if (!a.title || a.laying_out)
        return;
    a.laying_out = TRUE;
    a.frame_dirty = TRUE;
    a.positions = BeginDeferWindowPos(24);
    RECT r;
    GetClientRect(a.window, &r);
    int width = MulDiv(r.right, 96, (int)a.dpi), height = MulDiv(r.bottom, 96, (int)a.dpi);
    BOOL wide = width >= 930;
    int margin = 24, gap = a.prefs.density ? 12 : 24, cover_width = wide ? (width * 35 / 100) : width - 48;
    int x = wide ? margin + cover_width + gap : margin, cw = wide ? width - x - margin : width - 48,
        top = wide ? 112 : 368;
    int total = top + 2 * 242 + 122 + 104;
    a.content_height = total;
    if (a.scroll > total - height)
        a.scroll = total - height;
    if (a.scroll < 0)
        a.scroll = 0;
    SCROLLINFO si = {sizeof(si), SIF_RANGE | SIF_PAGE | SIF_POS,  0,
                     total - 1,  (UINT)(height > 0 ? height : 1), a.scroll,
                     0};
    SetScrollInfo(a.window, SB_VERT, &si, FALSE);
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
    if (a.positions)
        EndDeferWindowPos(a.positions);
    a.positions = NULL;
    a.laying_out = FALSE;
    a.frame_dirty = TRUE;
    RedrawWindow(a.window, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN);
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
        if (a.icon.hIcon)
            DestroyIcon(a.icon.hIcon);
        ZeroMemory(&a.icon, sizeof(a.icon));
        a.icon.cbSize = sizeof(a.icon);
        a.icon.hWnd = a.window;
        a.icon.uID = 1;
        a.icon.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
        a.icon.uCallbackMessage = WM_TRAY;
        if (a.icon.hIcon)
            DestroyIcon(a.icon.hIcon);
        a.icon.hIcon =
            (HICON)LoadImageW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(1), IMAGE_ICON,
                              a.prefs.tray_icon ? 24 : 16, a.prefs.tray_icon ? 24 : 16, LR_DEFAULTCOLOR);
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
    return !a.busy && !a.quit_pending && a.scan_ok && a.prefs.has[i] &&
           (np_enabled(s) || s == NP_DISABLED || s == NP_INTERFACE_DISABLED) && (enable || a.local);
}
static void controls_state(void) {
    for (int i = 0; i < 2; ++i) {
        text_if_changed(a.cards[i].toggle, np_enabled(a.cards[i].current)
                                               ? (i ? L"禁用 Wi-Fi" : L"禁用以太网")
                                               : (i ? L"启用 Wi-Fi" : L"启用以太网"));
        enable_if_changed(a.cards[i].toggle, can(i, !np_enabled(a.cards[i].current)));
        enable_if_changed(a.cards[i].combo, !a.busy);
    }
    enable_if_changed(a.refresh, !a.busy);
    enable_if_changed(a.campus, can(0, TRUE));
    enable_if_changed(a.hotspot, can(0, FALSE) && a.prefs.has[1]);
    flyout_update();
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
                text_if_changed(c->identity, text);
                if (k)
                    swprintf_s(text, _countof(text), L"%s%s  ·  IPv4 %s",
                               d->ssid[0]                                    ? L"SSID "
                               : d->wifi_detail_error == ERROR_ACCESS_DENIED ? L"SSID 受 Windows 位置权限限制"
                                                                             : L"SSID 暂不可读",
                               d->ssid, d->ip[0] ? d->ip : L"未获取");
                else
                    swprintf_s(text, _countof(text), L"IPv4 %s  ·  链路连接不代表互联网可用",
                               d->ip[0] ? d->ip : L"未获取");
                text_if_changed(c->detail, text);
            } else {
                text_if_changed(c->identity, a.prefs.has[k]
                                                 ? L"已绑定设备暂未找到；不会自动改选其他网卡。"
                                                 : L"请选择物理网卡；无法唯一匹配时不会自动绑定。");
                text_if_changed(c->detail, L"仅管理本卡启用状态，不修改 DNS、Metric、网关或代理。");
            }
        }
        text_if_changed(c->state, state_text(k, c->current));
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
    if (kind == 2)
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
static void tray_menu(void);
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
    if (j->cover && !j->path[0])
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
static void choose_image(BOOL cover) {
    if (a.loading_images) {
        message(L"正在加载图片，请稍候。");
        return;
    }
    WCHAR file[NP_PATH] = L"";
    if (!browse(a.settings, file, L"图片 (*.png;*.jpg;*.jpeg;*.bmp)\0*.png;*.jpg;*.jpeg;*.bmp\0\0"))
        return;
    if (load_image(cover, file, TRUE))
        message(L"正在后台读取背景图片…");
    else
        message(L"无法启动图片加载线程。");
}
static void local_update(void) {
    WCHAR url[512];if(!np_repo_valid(a.prefs.update.owner,a.prefs.update.repo))return;
    swprintf_s(url,512,L"https://github.com/%s/%s/releases",a.prefs.update.owner,a.prefs.update.repo);
    ShellExecuteW(a.settings,L"open",url,NULL,NULL,SW_SHOWNORMAL);
}
#include "ui_render.inc"
#include "ui_tray.inc"
#include "ui_settings.inc"
static void capture_window(HWND window, const WCHAR *path) {
    RECT r;
    GetClientRect(window, &r);
    HDC dc = GetDC(window), mem = CreateCompatibleDC(dc);
    BITMAPINFO bi = {0};
    void *pixels;
    bi.bmiHeader.biSize = 40;
    bi.bmiHeader.biWidth = r.right;
    bi.bmiHeader.biHeight = -r.bottom;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    HBITMAP bm = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &pixels, NULL, 0);
    HGDIOBJ old = SelectObject(mem, bm);
    PrintWindow(window, mem, PW_CLIENTONLY);
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
    ReleaseDC(window, dc);
}
static void capture(const WCHAR *path) {
    capture_window(a.window, path);
}
#ifdef NP_READONLY_BUILD
#include "../tests/ui_perf.inc"
#include "../tests/ui_acceptance.inc"
#include "../tests/ui_routing_tests.inc"
#include "../tests/ui_features.inc"
#endif
static void smoke_tick(void) {
#ifdef NP_READONLY_BUILD
    if (a.smoke == 2) {
        perf_tick();
        return;
    }
    if (a.smoke == 3) {
        ui_acceptance_tick();
        return;
    }
    if (a.smoke == 4) {
        ui_soak_tick();
        return;
    }
    if (a.smoke == 6) {
        feature_ui_tick();
        return;
    }
    if (a.smoke == 5) {
        routing_ui_tick();
        return;
    }
#endif
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
            settings_page(3);
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
static VOID CALLBACK interface_changed(PVOID ctx, PMIB_IPINTERFACE_ROW row, MIB_NOTIFICATION_TYPE type) {
    (void)row;
    (void)type;
    PostMessageW((HWND)ctx, WM_NETWORK_CHANGED, 0, 0);
}
static VOID CALLBACK address_changed(PVOID ctx, PMIB_UNICASTIPADDRESS_ROW row, MIB_NOTIFICATION_TYPE type) {
    (void)row;
    (void)type;
    PostMessageW((HWND)ctx, WM_NETWORK_CHANGED, 0, 0);
}
static LRESULT CALLBACK window_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_ONLINE_PROGRESS:
        if (online.busy)
            SendMessageW(GetDlgItem(a.settings_content, 613), PBM_SETPOS, wp, 0);
        return 0;
    case WM_ONLINE_DONE:
        online_done((ONLINE_JOB *)lp);
        if (a.quit_pending && !a.async_jobs && !a.busy && !a.loading_images && !a.routing_busy)
            DestroyWindow(w);
        return 0;
    case WM_PICKER_DONE:
        picker_done(wp, (NP_APP_LIST *)lp);
        if (a.quit_pending && !a.async_jobs && !a.busy && !a.loading_images && !a.routing_busy)
            DestroyWindow(w);
        return 0;
    case WM_ROUTING_DONE:
        routing_done((NP_MONITOR *)lp);
        if (a.quit_pending && !a.busy && !a.loading_images && !a.async_jobs)
            DestroyWindow(w);
        return 0;
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
        GetWindowTextW(a.message, a.status, _countof(a.status));
        set_fonts();
        load_image(TRUE, a.prefs.cover[0] ? a.prefs.cover : NULL, FALSE);
        if (a.prefs.image[0])
            load_image(FALSE, a.prefs.image, FALSE);
        layout();
        tray_update(TRUE);
        SetTimer(w, 1, 30000, NULL);
        if (!a.mock) {
            NotifyIpInterfaceChange(AF_UNSPEC, interface_changed, w, FALSE, &a.interface_notify);
            NotifyUnicastIpAddressChange(AF_UNSPEC, address_changed, w, FALSE, &a.address_notify);
        }
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
                    wcscpy_s(j->cover ? a.prefs.cover : a.prefs.image, NP_PATH, j->path);
                    if (!j->cover)
                        a.prefs.background_mode = 2;
                    if (a.settings)
                        SendMessageW(a.bg_combo, CB_SETCURSEL, 2, 0);
                    message(L"背景图片已保存并应用。");
                    appearance();
                }
                a.cover_dirty = a.backdrop_dirty = a.frame_dirty = TRUE;
                RedrawWindow(w, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN);
            } else
                message(j->select ? L"背景图片无法解码、复制失败或超过 1600 万像素；旧背景保持不变。"
                                  : L"图片暂不可用，已使用主题底色；原配置保留。");
            free(j);
        }
        if (a.first_scan && a.config_ok && !a.loading_images)
            np_update_health_ready();
        if (a.quit_pending && !a.busy && !a.loading_images && !a.routing_busy && !a.async_jobs)
            DestroyWindow(w);
        return 0;
    }
    case WM_DONE: {
        DONE *d = (DONE *)lp;
        a.busy = 0;
        if (d) {
            BOOL data_changed = !a.first_scan || a.scan_ok != d->scan_ok ||
                                memcmp(&a.snapshot, &d->snapshot, sizeof(a.snapshot)) != 0;
            if (!online.started) {
                online.started = TRUE;
                if (!a.mock && !np_update_health_mode()) {
                    NP_ERROR e = {0};
                    if (!np_startup_initialize(&e)) error_text(&e, online.status, 1000);
                    if (a.prefs.update.startup) SetTimer(w, 7, 1500, NULL);
                }
            }
            a.first_scan = TRUE;
            a.scan_ok = d->scan_ok;
            a.local = d->local;
            a.snapshot = d->snapshot;
            if (data_changed || d->kind == 2)
                render_snapshot();
            else
                controls_state();
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
        if (a.quit_pending && !a.loading_images && !a.routing_busy && !a.async_jobs)
            DestroyWindow(w);
        else if (a.rescan_pending) {
            a.rescan_pending = FALSE;
            SetTimer(w, 4, 400, NULL);
        }
        return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(wp);
        if (id == ID_EXIT) {
            InterlockedExchange(&online.cancel, 1);
            if (a.busy == 1 || (!a.busy && (a.loading_images || a.routing_busy || a.async_jobs)))
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
        if (wp == 7) {
            online_boot_tick();
        } else if (wp == 6) {
            if (a.settings && a.settings_page == 5)
                routing_start();
            else
                KillTimer(w, 6);
        } else if (wp == 5) {
            KillTimer(w, 5);
            save_now();
        } else if (wp == 1) {
            if (!SendMessageW(a.cards[0].combo, CB_GETDROPPEDSTATE, 0, 0) &&
                !SendMessageW(a.cards[1].combo, CB_GETDROPPEDSTATE, 0, 0))
                start_job(1, 0, FALSE, FALSE);
        } else if (wp == 4) {
            KillTimer(w, 4);
            if (a.busy)
                a.rescan_pending = TRUE;
            else
                start_job(1, 0, FALSE, FALSE);
        } else if (wp == 2 && a.smoke)
            smoke_tick();
        return 0;
    case WM_NETWORK_CHANGED:
    case WM_DEVICECHANGE:
    case WM_POWERBROADCAST:
        SetTimer(w, 4, 400, NULL);
        return TRUE;
    case WM_SETTINGCHANGE:
        if (a.prefs.theme == 3)
            appearance();
        return 0;
    case WM_TRAY:
        if (lp == WM_RBUTTONUP || lp == WM_CONTEXTMENU || lp == WM_LBUTTONUP)
            tray_menu();
        else if (lp == WM_LBUTTONDBLCLK)
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
        case SB_TOP:
            next = 0;
            break;
        case SB_BOTTOM:
            next = si.nMax;
            break;
        case SB_THUMBPOSITION:
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
        compose();
        RECT r = ps.rcPaint;
        if (a.frame.dc)
            BitBlt(dc, r.left, r.top, r.right - r.left, r.bottom - r.top, a.frame.dc, r.left, r.top, SRCCOPY);
        else
            FillRect(dc, &r, a.bg);
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
        KillTimer(w, 4);
        if (a.interface_notify)
            CancelMibChangeNotify2(a.interface_notify);
        if (a.address_notify)
            CancelMibChangeNotify2(a.address_notify);
        if (a.flyout)
            DestroyWindow(a.flyout);
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
        KillTimer(w, 5);
        save_now();
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
    BufferedPaintInit();
    LoadLibraryW(L"Msftedit.dll");
    INITCOMMONCONTROLSEX cc = {sizeof(cc), ICC_STANDARD_CLASSES | ICC_BAR_CLASSES | ICC_LISTVIEW_CLASSES |
                                               ICC_TAB_CLASSES};
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
    cls.lpszClassName = L"NetPilotLiteSettingsContent";
    cls.lpfnWndProc = settings_content_proc;
    RegisterClassExW(&cls);
    cls.lpszClassName = L"NetPilotLiteAppPicker";
    cls.lpfnWndProc = picker_proc;
    RegisterClassExW(&cls);
    cls.lpszClassName = L"NetPilotLiteFlyout";
    cls.lpfnWndProc = flyout_proc;
    cls.style = CS_DROPSHADOW;
    RegisterClassExW(&cls);
    RECT r = a.prefs.window;
    if (r.right - r.left < 580 || r.bottom - r.top < 480 || !MonitorFromRect(&r, MONITOR_DEFAULTTONULL))
        r = (RECT){80, 80, 1260, 980};
    HWND w = CreateWindowExW(WS_EX_CONTROLPARENT, NP_WINDOW_CLASS,
                             mock ? L"NetPilot Lite [模拟测试]" : L"NetPilot Lite",
                             WS_OVERLAPPEDWINDOW | WS_VSCROLL | WS_CLIPCHILDREN, r.left, r.top,
                             r.right - r.left, r.bottom - r.top, NULL, NULL, instance, NULL);
    if (!w)
        return 1;
    ShowWindow(w, show_cmd == SW_HIDE ? SW_HIDE : a.prefs.maximized ? SW_MAXIMIZE : show_cmd);
    UpdateWindow(w);
    MSG msg = {0};
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (a.flyout && IsWindowVisible(a.flyout)) {
            if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) {
                ShowWindow(a.flyout, SW_HIDE);
                continue;
            }
            if (IsDialogMessageW(a.flyout, &msg))
                continue;
        }
        if (picker.window && IsDialogMessageW(picker.window, &msg))
            continue;
        if (a.settings && IsDialogMessageW(a.settings, &msg))
            continue;
        if (!IsDialogMessageW(w, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    np_surface_free(&a.frame);
    np_surface_free(&a.backdrop);
    np_surface_free(&a.cover_cache);
    np_surface_free(&a.tint);
    np_image_free(&a.cover_image);
    np_image_free(&a.background_image);
    free(a.monitor);
    a.monitor = NULL;
    DeleteObject(a.normal);
    DeleteObject(a.small_font);
    DeleteObject(a.heading);
    DeleteObject(a.bold);
    DeleteObject(a.bg);
    DeleteObject(a.card);
    DestroyIcon(cls.hIcon);
    DestroyIcon(cls.hIconSm);
    CoUninitialize();
    BufferedPaintUnInit();
    return (int)msg.wParam;
}
