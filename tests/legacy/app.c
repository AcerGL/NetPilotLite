#include "lite.h"
#include <shellapi.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <objbase.h>
#include <stdio.h>
#include <wchar.h>

#define ID_TARGET 100
#define ID_TOGGLE 101
#define ID_REFRESH 102
#define ID_SHOW 103
#define ID_ENABLE 104
#define ID_DISABLE 105
#define ID_EXIT 106
#define WM_TRAY (WM_APP + 1)
#define WM_DONE (WM_APP + 2)
static const WCHAR *window_class = L"NetPilotLiteTestWindow1";
static const COLORREF bg = RGB(17,24,39), fg = RGB(238,242,255), muted = RGB(156,174,199);
typedef struct APP {
    HWND window, title, subtitle, caption, combo, identity, state, toggle, refresh, wifi, note, message;
    HFONT normal_font, small_font, title_font, state_font, button_font;
    HBRUSH background;
    UINT dpi, taskbar_created;
    BOOL mock, smoke, busy, has_binding, binding_invalid, scan_ok, admin, local, tray;
    int smoke_stage, smoke_exit;
    NP_STATE current;
    NP_BINDING binding;
    NP_SNAPSHOT snapshot;
    NP_MOCK mock_data;
    NP_BACKEND backend;
    NOTIFYICONDATAW icon;
} APP;
static APP app;
/* Late-bind newer DPI helpers so the EXE also starts on early Windows 10. */
static UINT system_dpi(void) {
    typedef UINT (WINAPI *GET_DPI)(void);
    GET_DPI get = (GET_DPI)GetProcAddress(GetModuleHandleW(L"user32.dll"),"GetDpiForSystem");
    if (get) return get();
    HDC dc = GetDC(NULL); UINT value = dc ? (UINT)GetDeviceCaps(dc,LOGPIXELSX) : 96;
    if (dc) ReleaseDC(NULL,dc); return value;
}
static UINT window_dpi(HWND window) {
    typedef UINT (WINAPI *GET_DPI)(HWND);
    GET_DPI get = (GET_DPI)GetProcAddress(GetModuleHandleW(L"user32.dll"),"GetDpiForWindow");
    return get ? get(window) : system_dpi();
}
static void adjust_rect(RECT *rect, DWORD style, UINT dpi) {
    typedef BOOL (WINAPI *ADJUST)(LPRECT,DWORD,BOOL,DWORD,UINT);
    ADJUST adjust = (ADJUST)GetProcAddress(GetModuleHandleW(L"user32.dll"),"AdjustWindowRectExForDpi");
    if (adjust) adjust(rect,style,FALSE,0,dpi); else AdjustWindowRectEx(rect,style,FALSE,0);
}
static int px(int value) { return MulDiv(value, (int)app.dpi, 96); }
static void refresh_state(void);
static void start_operation(BOOL enable);
static void show_window(void) { ShowWindow(app.window, SW_RESTORE); SetForegroundWindow(app.window); }
static HFONT font(int height, int weight) {
    return CreateFontW(-px(height), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
}
static void fonts(void) {
    DeleteObject(app.normal_font); DeleteObject(app.small_font); DeleteObject(app.title_font); DeleteObject(app.state_font); DeleteObject(app.button_font);
    app.normal_font = font(15, FW_NORMAL); app.small_font = font(12, FW_NORMAL);
    app.title_font = font(30, FW_SEMIBOLD); app.state_font = font(23, FW_SEMIBOLD); app.button_font = font(20, FW_SEMIBOLD);
    HWND normal[] = {app.caption,app.combo,app.refresh,app.wifi};
    HWND small_controls[] = {app.subtitle,app.identity,app.note,app.message};
    for (size_t i = 0; i < _countof(normal); ++i) SendMessageW(normal[i], WM_SETFONT, (WPARAM)app.normal_font, TRUE);
    for (size_t i = 0; i < _countof(small_controls); ++i) SendMessageW(small_controls[i], WM_SETFONT, (WPARAM)app.small_font, TRUE);
    SendMessageW(app.title, WM_SETFONT, (WPARAM)app.title_font, TRUE);
    SendMessageW(app.state, WM_SETFONT, (WPARAM)app.state_font, TRUE);
    SendMessageW(app.toggle, WM_SETFONT, (WPARAM)app.button_font, TRUE);
    SendMessageW(app.combo, CB_SETITEMHEIGHT, (WPARAM)-1, px(30));
    SendMessageW(app.combo, CB_SETITEMHEIGHT, 0, px(30));
}
static void place(HWND w, int x, int y, int width, int height) { MoveWindow(w, px(x), px(y), px(width), px(height), TRUE); }
static void layout(void) {
    place(app.title,28,23,590,44); place(app.subtitle,29,76,580,23);
    place(app.caption,28,120,580,24); place(app.combo,28,150,604,250);
    place(app.identity,30,196,600,64); place(app.state,28,278,604,40);
    place(app.toggle,28,341,604,72); place(app.refresh,28,431,136,38);
    place(app.wifi,183,436,450,42); place(app.note,28,492,604,40); place(app.message,28,546,604,72);
}
static void error_text(const NP_ERROR *e, WCHAR *out, size_t chars) {
    WCHAR system[300] = L"";
    FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, e->win32, 0, system, _countof(system), NULL);
    swprintf_s(out, chars, L"%s\r\nWindows %lu (0x%08lX)，CM 0x%08lX：%s", e->operation, e->win32, e->win32, e->config_ret, system);
}
static void tray_update(BOOL create) {
    if (create) {
        memset(&app.icon, 0, sizeof(app.icon)); app.icon.cbSize = sizeof(app.icon); app.icon.hWnd = app.window; app.icon.uID = 1;
        app.icon.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP; app.icon.uCallbackMessage = WM_TRAY;
        app.icon.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    }
    swprintf_s(app.icon.szTip, _countof(app.icon.szTip), L"NetPilot Lite%s · %s", app.mock ? L" [模拟]" : L"", app.has_binding ? np_state_text(app.current) : L"未选择网卡");
    if (Shell_NotifyIconW(create ? NIM_ADD : NIM_MODIFY, &app.icon)) app.tray = TRUE;
}
static void refresh_state(void) {
    NP_ERROR error = {0}; WCHAR text[1200], guid[64];
    if (app.busy || SendMessageW(app.combo,CB_GETDROPPEDSTATE,0,0)) return;
    app.scan_ok = app.backend.scan(app.backend.context, &app.snapshot, &error);
    SendMessageW(app.combo, CB_RESETCONTENT, 0, 0);
    app.current = NP_UNKNOWN;
    if (!app.scan_ok) {
        error_text(&error, text, _countof(text)); SetWindowTextW(app.message, text);
        SetWindowTextW(app.wifi, L"Wi-Fi：状态暂不可读取（只读）");
    } else {
        for (size_t i = 0; i < app.snapshot.count; ++i) SendMessageW(app.combo, CB_ADDSTRING, 0, (LPARAM)app.snapshot.adapters[i].name);
        if (!app.has_binding) {
            int index = np_auto_select(&app.snapshot);
            if (index >= 0) {
                app.binding = app.snapshot.adapters[index].identity; app.has_binding = TRUE;
                if (!app.mock && !np_binding_save(&app.binding, &error)) {
                    error_text(&error, text, _countof(text)); SetWindowTextW(app.message, text);
                }
            }
        }
        const NP_ADAPTER *a = app.has_binding ? np_find(&app.snapshot, &app.binding) : NULL;
        if (a) {
            app.current = a->state;
            SendMessageW(app.combo, CB_SETCURSEL, (WPARAM)(a - app.snapshot.adapters), 0);
            StringFromGUID2(&a->identity.guid, guid, _countof(guid));
            swprintf_s(text, _countof(text), L"GUID：%s\r\n设备：%s", guid, a->identity.instance);
            SetWindowTextW(app.identity, text);
        } else SetWindowTextW(app.identity, app.binding_invalid ? L"已保存的绑定无法读取，请手动重新选择目标。" : app.has_binding ? L"已绑定设备暂未找到。不会自动改选其他网卡。" : L"请选择目标物理以太网；未唯一匹配 I219-V 时不会自动绑定。");
        SetWindowTextW(app.wifi, app.snapshot.wifi);
    }
    app.local = app.backend.safe_local_session(app.backend.context);
    SetWindowTextW(app.state, app.scan_ok ? (app.has_binding && np_find(&app.snapshot,&app.binding) ? np_state_text(app.current) : L"尚未找到目标网卡") : L"状态读取失败");
    BOOL enabled = np_enabled(app.current);
    SetWindowTextW(app.toggle, enabled ? L"禁用以太网" : L"启用以太网");
    EnableWindow(app.toggle, app.scan_ok && app.has_binding && (app.admin || app.mock) &&
        (enabled || app.current == NP_DISABLED || app.current == NP_INTERFACE_DISABLED) && (!enabled || app.local));
    if (!app.local) SetWindowTextW(app.note, L"远程桌面或会话状态无法确认：为保护远程连接，禁用操作已阻止。");
    else SetWindowTextW(app.note, L"链路连接不代表互联网可用。关闭窗口收至托盘；右键托盘可退出。");
    if (!app.admin && !app.mock) SetWindowTextW(app.message, L"需要管理员权限。请正常启动主程序，并在 Windows UAC 提示中选择“是”。");
    tray_update(FALSE); InvalidateRect(app.toggle,NULL,TRUE);
}
typedef struct JOB { NP_BINDING binding; BOOL enable; HWND window; } JOB;
static DWORD WINAPI worker(void *parameter) {
    JOB *job = parameter;
    NP_RESULT *result = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*result));
    if (result) {
        *result = np_execute(&app.backend, &job->binding, job->enable, 10000);
        if (!PostMessageW(job->window, WM_DONE, 0, (LPARAM)result)) HeapFree(GetProcessHeap(),0,result);
    } else PostMessageW(job->window, WM_DONE, 0, 0);
    HeapFree(GetProcessHeap(),0,job); return 0;
}
static void start_operation(BOOL enable) {
    if (app.busy || !app.has_binding || !app.scan_ok || (!app.admin && !app.mock)) return;
    JOB *job = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*job));
    if (!job) return;
    job->binding = app.binding; job->enable = enable; job->window = app.window;
    app.busy = TRUE; EnableWindow(app.toggle,FALSE); EnableWindow(app.combo,FALSE); EnableWindow(app.refresh,FALSE);
    SetWindowTextW(app.message, L"正在请求设备状态变更，并等待 Windows 实际状态确认…");
    HANDLE thread = CreateThread(NULL,0,worker,job,0,NULL);
    if (thread) CloseHandle(thread);
    else { HeapFree(GetProcessHeap(),0,job); PostMessageW(app.window,WM_DONE,0,0); }
}
static HMENU create_tray_menu(void) {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu,MF_STRING,ID_SHOW,L"显示 NetPilot Lite"); AppendMenuW(menu,MF_SEPARATOR,0,NULL);
    AppendMenuW(menu,MF_STRING | (app.busy || !app.has_binding || !app.scan_ok || (!app.admin && !app.mock) || np_enabled(app.current) ? MF_GRAYED : 0),ID_ENABLE,L"启用以太网");
    AppendMenuW(menu,MF_STRING | (app.busy || !app.has_binding || !app.scan_ok || (!app.admin && !app.mock) || !np_enabled(app.current) || !app.local ? MF_GRAYED : 0),ID_DISABLE,L"禁用以太网");
    AppendMenuW(menu,MF_STRING,ID_REFRESH,L"刷新当前状态"); AppendMenuW(menu,MF_SEPARATOR,0,NULL);
    AppendMenuW(menu,MF_STRING | (app.busy ? MF_GRAYED : 0),ID_EXIT,L"退出（保持当前网卡状态）");
    return menu;
}
static void tray_menu(void) {
    HMENU menu = create_tray_menu(); POINT cursor;
    GetCursorPos(&cursor); SetForegroundWindow(app.window);
    TrackPopupMenu(menu,TPM_RIGHTBUTTON,cursor.x,cursor.y,0,app.window,NULL);
    PostMessageW(app.window,WM_NULL,0,0); DestroyMenu(menu);
}
static void capture_mock(const WCHAR *path) {
    RECT r; GetClientRect(app.window,&r); HDC dc = GetDC(app.window), memory = CreateCompatibleDC(dc);
    BITMAPINFO bi = {0}; void *pixels = NULL;
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); bi.bmiHeader.biWidth = r.right; bi.bmiHeader.biHeight = -r.bottom;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    HBITMAP bitmap = CreateDIBSection(dc,&bi,DIB_RGB_COLORS,&pixels,NULL,0); HGDIOBJ previous = SelectObject(memory,bitmap);
    PrintWindow(app.window,memory,PW_CLIENTONLY);
    BITMAPFILEHEADER header = {0}; DWORD written; DWORD size = (DWORD)r.right*(DWORD)r.bottom*4;
    header.bfType = 0x4d42; header.bfOffBits = sizeof(header)+sizeof(BITMAPINFOHEADER); header.bfSize = header.bfOffBits+size;
    HANDLE file = CreateFileW(path,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
    if (file != INVALID_HANDLE_VALUE) {
        WriteFile(file,&header,sizeof(header),&written,NULL); WriteFile(file,&bi.bmiHeader,sizeof(BITMAPINFOHEADER),&written,NULL);
        WriteFile(file,pixels,size,&written,NULL); CloseHandle(file);
    }
    SelectObject(memory,previous); DeleteObject(bitmap); DeleteDC(memory); ReleaseDC(app.window,dc);
}
static HWND control(const WCHAR *cls, const WCHAR *text, DWORD style, int id) {
    return CreateWindowExW(0,cls,text,WS_CHILD|WS_VISIBLE|style,0,0,1,1,app.window,(HMENU)(INT_PTR)id,GetModuleHandleW(NULL),NULL);
}
static LRESULT CALLBACK window_proc(HWND w, UINT message, WPARAM wp, LPARAM lp) {
    switch (message) {
    case WM_CREATE: {
        app.window = w; app.dpi = window_dpi(w); if (!app.dpi) app.dpi = 96;
        BOOL dark = TRUE; DwmSetWindowAttribute(w,20,&dark,sizeof(dark));
        app.title = control(L"STATIC",L"NetPilot Lite",0,0);
        app.subtitle = control(L"STATIC",app.mock ? L"模拟预览 · 此构建无法修改真实网卡" : L"只控制选中的物理以太网适配器",0,0);
        app.caption = control(L"STATIC",L"当前网卡",0,0);
        app.combo = control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|CBS_OWNERDRAWFIXED|CBS_HASSTRINGS|WS_VSCROLL|WS_TABSTOP,ID_TARGET);
        app.identity = control(L"STATIC",L"",SS_NOPREFIX,0);
        app.state = control(L"STATIC",L"正在读取 Windows 状态…",0,0);
        app.toggle = control(L"BUTTON",L"启用以太网",BS_OWNERDRAW|WS_TABSTOP,ID_TOGGLE);
        app.refresh = control(L"BUTTON",L"刷新状态",BS_OWNERDRAW|WS_TABSTOP,ID_REFRESH);
        app.wifi = control(L"STATIC",L"Wi-Fi：正在读取…",0,0);
        app.note = control(L"STATIC",L"",0,0); app.message = control(L"STATIC",L"启动只读取状态，不自动切换网卡。",0,0);
        fonts(); layout(); tray_update(TRUE); refresh_state(); SetTimer(w,1,2000,NULL);
        if (app.smoke) SetTimer(w,2,200,NULL);
        return 0;
    }
    case WM_CTLCOLORSTATIC: case WM_CTLCOLORLISTBOX: case WM_CTLCOLOREDIT:
        SetBkColor((HDC)wp,bg); SetTextColor((HDC)wp,(HWND)lp == app.title || (HWND)lp == app.state ? fg : muted); return (LRESULT)app.background;
    case WM_ERASEBKGND: { RECT r; GetClientRect(w,&r); FillRect((HDC)wp,&r,app.background); return 1; }
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT *item = (DRAWITEMSTRUCT *)lp; RECT r = item->rcItem; WCHAR text[1024] = L"";
        FillRect(item->hDC,&r,app.background);
        BOOL disabled = (item->itemState & ODS_DISABLED) != 0;
        COLORREF color = RGB(35,47,65);
        if (item->CtlID == ID_TOGGLE) color = disabled ? RGB(48,59,76) : np_enabled(app.current) ? RGB(170,54,67) : RGB(37,99,235);
        if (item->itemState & ODS_SELECTED) color = RGB(41,77,134);
        HBRUSH brush = CreateSolidBrush(color), previous = SelectObject(item->hDC,brush);
        HPEN pen = CreatePen(PS_SOLID,1,color); HGDIOBJ oldpen = SelectObject(item->hDC,pen);
        RoundRect(item->hDC,r.left,r.top,r.right,r.bottom,px(12),px(12));
        SelectObject(item->hDC,oldpen); SelectObject(item->hDC,previous); DeleteObject(brush); DeleteObject(pen);
        if (item->CtlType == ODT_COMBOBOX && item->itemID != (UINT)-1) SendMessageW(item->hwndItem,CB_GETLBTEXT,item->itemID,(LPARAM)text);
        else GetWindowTextW(item->hwndItem,text,_countof(text));
        SetBkMode(item->hDC,TRANSPARENT); SetTextColor(item->hDC,disabled ? muted : fg);
        HGDIOBJ oldfont = SelectObject(item->hDC,item->CtlID == ID_TOGGLE ? app.button_font : app.normal_font);
        r.left += px(12); r.right -= px(12);
        DrawTextW(item->hDC,text,-1,&r,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|(item->CtlType == ODT_COMBOBOX ? DT_LEFT : DT_CENTER));
        if (item->itemState & ODS_FOCUS) { InflateRect(&r,-px(3),-px(5)); DrawFocusRect(item->hDC,&r); }
        SelectObject(item->hDC,oldfont); return TRUE;
    }
    case WM_MEASUREITEM: ((MEASUREITEMSTRUCT *)lp)->itemHeight = (UINT)px(30); return TRUE;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_TOGGLE: start_operation(!np_enabled(app.current)); break;
        case ID_ENABLE: start_operation(TRUE); break;
        case ID_DISABLE: start_operation(FALSE); break;
        case ID_SHOW: show_window(); break;
        case ID_REFRESH: refresh_state(); break;
        case 900: app.busy=TRUE; break; case 901: app.busy=FALSE; break; case ID_EXIT: if (!app.busy) DestroyWindow(w); break;
        case ID_TARGET: if (HIWORD(wp) == CBN_SELCHANGE && !app.busy) {
            LRESULT index = SendMessageW(app.combo,CB_GETCURSEL,0,0);
            if (index >= 0 && (size_t)index < app.snapshot.count) {
                app.binding = app.snapshot.adapters[index].identity; app.has_binding = TRUE; app.binding_invalid = FALSE;
                NP_ERROR error = {0}; WCHAR text[900];
                if (!app.mock && !np_binding_save(&app.binding,&error)) { error_text(&error,text,_countof(text)); SetWindowTextW(app.message,text); }
                else SetWindowTextW(app.message,L"已绑定这张物理以太网；选择网卡不会改变启用状态。");
                refresh_state();
            }
        } break;
        } return 0;
    case WM_DONE: {
        NP_RESULT *result = (NP_RESULT *)lp; WCHAR text[1000]; app.busy = FALSE;
        EnableWindow(app.combo,TRUE); EnableWindow(app.refresh,TRUE); refresh_state();
        if (!result) SetWindowTextW(app.message,L"操作未能启动：内存或线程资源不足。请刷新实际状态。");
        else {
            if (result->success) swprintf_s(text,_countof(text),L"%s：Windows 实际状态为“%s”。",result->unchanged ? L"无需更改" : L"已验证",np_state_text(result->state));
            else error_text(&result->error,text,_countof(text));
            SetWindowTextW(app.message,text); HeapFree(GetProcessHeap(),0,result);
        }
        return 0;
    }
    case WM_TIMER:
        if (wp == 1) refresh_state();
        else if (wp == 2 && app.smoke && !app.busy) {
            if (app.smoke_stage == 0) {
                HMENU menu = create_tray_menu();
                BOOL menu_valid = (GetMenuState(menu,ID_ENABLE,MF_BYCOMMAND) & MF_GRAYED) && !(GetMenuState(menu,ID_DISABLE,MF_BYCOMMAND) & MF_GRAYED);
                DestroyMenu(menu);
                ShowWindow(w,SW_MINIMIZE); BOOL hidden = !IsWindowVisible(w); show_window();
                if (!app.tray || !menu_valid || !hidden || !IsWindowVisible(w) || app.mock_data.writes) {
                    wprintf(L"GUI smoke preflight: tray=%d menu=%d minimizedHidden=%d restoredVisible=%d startupWrites=%u\n",app.tray,menu_valid,hidden,IsWindowVisible(w),app.mock_data.writes);
                    app.smoke_exit = 1; DestroyWindow(w);
                }
                else { ++app.smoke_stage; start_operation(FALSE); }
            }
            else if (app.smoke_stage == 1) {
                if (app.current != NP_DISABLED) { app.smoke_exit = 1; DestroyWindow(w); }
                else {
                    WCHAR shown[1200]; GetWindowTextW(app.identity,shown,_countof(shown));
                    wprintf(L"GUI binding display: %s\n",shown);
                    capture_mock(L"artifacts\\ui-disabled.bmp"); ++app.smoke_stage; start_operation(TRUE);
                }
            } else {
                if (!np_enabled(app.current) || app.mock_data.writes != 2) app.smoke_exit = 1;
                capture_mock(L"artifacts\\ui-enabled.bmp"); DestroyWindow(w);
            }
        } return 0;
    case WM_TRAY:
        if (lp == WM_RBUTTONUP || lp == WM_CONTEXTMENU) tray_menu();
        else if (lp == WM_LBUTTONUP || lp == WM_LBUTTONDBLCLK) show_window();
        return 0;
    case WM_SIZE: if (wp == SIZE_MINIMIZED) ShowWindow(w,SW_HIDE); return 0;
    case WM_DPICHANGED: { RECT *r = (RECT *)lp; app.dpi = HIWORD(wp); fonts(); SetWindowPos(w,NULL,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE); layout(); return 0; }
    case WM_CLOSE:
        if (app.tray) ShowWindow(w,SW_HIDE); else if (!app.busy) DestroyWindow(w);
        return 0;
    case WM_DESTROY:
        KillTimer(w,1); KillTimer(w,2); Shell_NotifyIconW(NIM_DELETE,&app.icon);
        DeleteObject(app.normal_font); DeleteObject(app.small_font); DeleteObject(app.title_font); DeleteObject(app.state_font); DeleteObject(app.button_font);
        PostQuitMessage(app.smoke_exit); return 0;
    }
    if (message == app.taskbar_created && app.taskbar_created) { tray_update(TRUE); return 0; }
    return DefWindowProcW(w,message,wp,lp);
}
int np_app_run(HINSTANCE instance, int show, BOOL mock, BOOL smoke) {
    memset(&app,0,sizeof(app)); app.mock = mock; app.smoke = smoke;
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (mock) { np_mock_init(&app.mock_data); app.backend = np_mock_backend(&app.mock_data); }
    else { app.backend = np_windows_backend(); int stored = np_binding_load(&app.binding); app.has_binding = stored != 0; app.binding_invalid = stored < 0; }
    SID_IDENTIFIER_AUTHORITY authority = SECURITY_NT_AUTHORITY; PSID admins = NULL;
    if (AllocateAndInitializeSid(&authority,2,SECURITY_BUILTIN_DOMAIN_RID,DOMAIN_ALIAS_RID_ADMINS,0,0,0,0,0,0,&admins)) {
        CheckTokenMembership(NULL,admins,&app.admin); FreeSid(admins);
    }
    app.background = CreateSolidBrush(bg); app.taskbar_created = RegisterWindowMessageW(L"TaskbarCreated");
    WNDCLASSEXW cls = {0}; cls.cbSize = sizeof(cls); cls.hInstance = instance; cls.lpfnWndProc = window_proc;
    cls.lpszClassName = window_class; cls.hCursor = LoadCursorW(NULL,IDC_ARROW); cls.hIcon = LoadIconW(NULL,IDI_APPLICATION);
    cls.hIconSm = cls.hIcon; cls.hbrBackground = app.background;
    if (!RegisterClassExW(&cls)) return 1;
    UINT dpi = system_dpi(); RECT r = {0,0,MulDiv(660,(int)dpi,96),MulDiv(636,(int)dpi,96)};
    DWORD style = WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX;
    adjust_rect(&r,style,dpi);
    HWND w = CreateWindowExW(0,window_class,mock ? L"NetPilot Lite [模拟测试]" : L"NetPilot Lite",style,
        CW_USEDEFAULT,CW_USEDEFAULT,r.right-r.left,r.bottom-r.top,NULL,NULL,instance,NULL);
    if (!w) { DeleteObject(app.background); return 1; }
    ShowWindow(w,show); UpdateWindow(w);
    MSG message;
    while (GetMessageW(&message,NULL,0,0) > 0) if (!IsDialogMessageW(w,&message)) { TranslateMessage(&message); DispatchMessageW(&message); }
    DeleteObject(app.background); return (int)message.wParam;
}
