/* One native setup: public Release -> signed manifest -> independent files -> transaction. */
#define COBJMACROS
#include "public_setup.h"
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <sddl.h>
#include <stdio.h>
typedef struct OPTIONS {
    BOOL update, apply, recover;
    WCHAR version[64], dir[NP_PATH], stage[NP_PATH], sid[192];
} OPTIONS;
static OPTIONS options;
static HWND window, status, progress, start_button, cancel_button, target, browse_button;
static HANDLE worker;
static volatile LONG cancelled;
static BOOL committing, finished;
static ULONGLONG started, last_progress;
static HBRUSH background;
static HFONT font;
#define MSG_PROGRESS (WM_APP + 1)
#define MSG_DONE (WM_APP + 2)
#define MSG_COMMIT (WM_APP + 3)
static BOOL current_sid(WCHAR out[192]) {
    HANDLE token;
    BYTE buf[512];
    DWORD n;
    PWSTR text = NULL;
    BOOL ok = OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token);
    if (!ok)
        return FALSE;
    ok = GetTokenInformation(token, TokenUser, buf, sizeof(buf), &n) &&
         ConvertSidToStringSidW(((TOKEN_USER *)buf)->User.Sid, &text);
    if (ok) {
        ok = wcslen(text) < 192;
        if (ok)
            wcscpy_s(out, 192, text);
        LocalFree(text);
    }
    CloseHandle(token);
    return ok;
}
static BOOL writable(const WCHAR *dir) {
    WCHAR parent[NP_PATH];
    wcscpy_s(parent, NP_PATH, dir);
    while (GetFileAttributesW(parent) == INVALID_FILE_ATTRIBUTES) {
        WCHAR *q = wcsrchr(parent, L'\\');
        if (!q || q - parent < 3)
            return FALSE;
        *q = 0;
    }
    HANDLE f = CreateFileW(parent, FILE_ADD_FILE | FILE_DELETE_CHILD,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                           FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    if (f == INVALID_HANDLE_VALUE)
        return FALSE;
    CloseHandle(f);
    return TRUE;
}
static BOOL elevate(const OPTIONS *o, NP_ERROR *e) {
    WCHAR exe[NP_PATH], sid[192], args[NP_PATH * 2 + 500];
    if (!GetModuleFileNameW(NULL, exe, NP_PATH) || !current_sid(sid))
        return FALSE;
    if (o->recover)
        swprintf_s(args, _countof(args), L"--recover --install-dir \"%s\" --user-sid %s", o->dir, sid);
    else
        swprintf_s(args, _countof(args),
                   L"--apply %s --target-version %s --install-dir \"%s\" --stage \"%s\" --user-sid %s",
                   o->update ? L"--update" : L"", o->version, o->dir, o->stage, sid);
    SHELLEXECUTEINFOW se = {sizeof(se)};
    se.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
    se.lpVerb = L"runas";
    se.lpFile = exe;
    se.lpParameters = args;
    se.nShow = SW_HIDE;
    if (!ShellExecuteExW(&se)) {
        np_error(e, GetLastError(), 0, L"UAC 已取消或无法获取目录写入权限；原程序未关闭");
        return FALSE;
    }
    WaitForSingleObject(se.hProcess, INFINITE);
    DWORD code = ERROR_INSTALL_FAILURE;
    GetExitCodeProcess(se.hProcess, &code);
    CloseHandle(se.hProcess);
    if (code)
        np_error(e, code, 0, L"提升权限后的安装未完成，旧版已尽可能恢复；可重试");
    return code == 0;
}
static void progress_cb(void *ctx, const WCHAR *file, unsigned done, unsigned count, ULONGLONG bytes,
                        ULONGLONG total) {
    (void)ctx;
    ULONGLONG now = GetTickCount64(), elapsed = now - started;
    if (now - last_progress < 100 && bytes < total)
        return;
    last_progress = now;
    double speed = elapsed ? (double)bytes * 1000 / (double)elapsed : 0,
           remaining = speed > 0 ? (double)(total - bytes) / speed : 0;
    WCHAR *s = calloc(700, sizeof(WCHAR));
    if (!s)
        return;
    swprintf_s(s, 700,
               L"当前文件：%s\n已完成 %u / %u 个文件 · 总进度 %llu%%\n%.2f / %.2f MiB · %.2f MiB/s · "
               L"预计剩余 %.0f 秒",
               file, done, count, total ? bytes * 100 / total : 0, (double)bytes / 1048576,
               (double)total / 1048576, speed / 1048576, remaining);
    if (!PostMessageW(window, MSG_PROGRESS, (WPARAM)(total ? bytes * 100 / total : 0), (LPARAM)s))
        free(s);
}
static DWORD WINAPI run(void *unused) {
    (void)unused;
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    NP_ERROR e = {0};
    OPTIONS job = options;
    BOOL ok = FALSE;
    NP_PUBLIC_RELEASE *r = calloc(1, sizeof(*r));
    NP_MANIFEST *m = calloc(1, sizeof(*m));
    if (job.recover) {
        PostMessageW(window, MSG_COMMIT, 0, 0);
        ok = writable(job.dir) || IsUserAnAdmin() ? np_setup_recover(job.dir, TRUE, &e) : elevate(&job, &e);
    } else if (r && m) {
        NP_UPDATE_SETTINGS s = {0};
        wcscpy_s(s.owner, 100, NP_GITHUB_OWNER);
        wcscpy_s(s.repo, 100, NP_GITHUB_REPO);
        wcscpy_s(s.channel, 16, L"Stable");
        WCHAR pending[NP_PATH];
        swprintf_s(pending, NP_PATH, L"%s\\.NetPilotSetup-transaction\\journal.bin", job.dir);
        ok = TRUE;
        if (GetFileAttributesW(pending) != INVALID_FILE_ATTRIBUTES) {
            OPTIONS recovery = job;
            recovery.recover = TRUE;
            PostMessageW(window, MSG_COMMIT, 0, 0);
            ok = writable(job.dir) || IsUserAnAdmin() ? np_setup_recover(job.dir, TRUE, &e)
                                                      : elevate(&recovery, &e);
        }
        if (ok)
            ok = np_public_check(&s, job.version[0] ? job.version : NULL, r, &cancelled, &e);
        if (ok) {
            started = GetTickCount64();
            ok = np_setup_stage(r, job.stage, m, &cancelled, progress_cb, NULL, &e);
            wcscpy_s(job.version, 64, m->text_version);
        }
        if (ok && InterlockedCompareExchange(&cancelled, 0, 0)) {
            np_error(&e, ERROR_CANCELLED, 0, L"安装已取消；未关闭或修改旧程序");
            ok = FALSE;
        }
        if (ok) {
            PostMessageW(window, MSG_COMMIT, 0, 0);
            ok = writable(job.dir) || IsUserAnAdmin()
                     ? np_setup_install(job.stage, job.dir, job.update, TRUE, &e)
                     : elevate(&job, &e);
        }
        if (job.stage[0])
            np_setup_cleanup(job.stage);
    }
    free(r);
    free(m);
    CoUninitialize();
    WCHAR *message = calloc(900, sizeof(WCHAR));
    if (message) {
        if (ok)
            wcscpy_s(message, 900,
                     L"安装 / 升级成功，新版 NetPilot 已启动。\n原目录、用户配置与网卡状态已保留。");
        else
            swprintf_s(
                message, 900,
                L"未完成：Windows %lu (0x%08lX)\n%s\n可重试。若提示恢复未完成，请保留原目录的事务记录。",
                e.win32, e.win32, e.operation);
    }
    if (!PostMessageW(window, MSG_DONE, ok, (LPARAM)message))
        free(message);
    return 0;
}
static BOOL parse(OPTIONS *o, int argc, WCHAR **argv) {
    ZeroMemory(o, sizeof(*o));
    unsigned seen = 0;
    for (int i = 1; i < argc; i++) {
        unsigned bit = 0;
        WCHAR *dest = NULL;
        size_t cap = 0;
        if (!wcscmp(argv[i], L"--update")) {
            bit = 1;
            o->update = TRUE;
        } else if (!wcscmp(argv[i], L"--apply")) {
            bit = 2;
            o->apply = TRUE;
        } else if (!wcscmp(argv[i], L"--recover")) {
            bit = 4;
            o->recover = TRUE;
        } else if (!wcscmp(argv[i], L"--target-version")) {
            bit = 8;
            dest = o->version;
            cap = 64;
        } else if (!wcscmp(argv[i], L"--install-dir")) {
            bit = 16;
            dest = o->dir;
            cap = NP_PATH;
        } else if (!wcscmp(argv[i], L"--stage")) {
            bit = 32;
            dest = o->stage;
            cap = NP_PATH;
        } else if (!wcscmp(argv[i], L"--user-sid")) {
            bit = 64;
            dest = o->sid;
            cap = 192;
        } else
            return FALSE;
        if (seen & bit)
            return FALSE;
        seen |= bit;
        if (dest) {
            if (++i >= argc || wcslen(argv[i]) >= cap)
                return FALSE;
            wcscpy_s(dest, cap, argv[i]);
        }
    }
    DWORD v[4];
    if (o->version[0] && (!np_semver(o->version, v) || o->version[0] == L'v'))
        return FALSE;
    if (o->dir[0] && !np_install_absolute(o->dir, TRUE))
        return FALSE;
    if (o->update && (!o->version[0] || !o->dir[0]))
        return FALSE;
    if (o->recover && (o->update || o->apply || o->stage[0] || o->version[0] || !o->dir[0]))
        return FALSE;
    if (o->apply &&
        (!o->version[0] || !o->dir[0] || !o->stage[0] || !o->sid[0] || !np_install_absolute(o->stage, FALSE)))
        return FALSE;
    if (!o->apply && o->stage[0])
        return FALSE;
    if (o->sid[0]) {
        WCHAR sid[192];
        if ((!o->apply && !o->recover) || !IsUserAnAdmin() || !current_sid(sid) || wcscmp(sid, o->sid))
            return FALSE;
    }
    return TRUE;
}
static void pick_directory(void) {
    BROWSEINFOW bi = {0};
    bi.hwndOwner = window;
    bi.lpszTitle = L"选择 NetPilot Lite 安装目录";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE id = SHBrowseForFolderW(&bi);
    if (id) {
        WCHAR path[MAX_PATH];
        if (SHGetPathFromIDListW(id, path))
            SetWindowTextW(target, path);
        CoTaskMemFree(id);
    }
}
static LRESULT CALLBACK proc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        CreateWindowW(L"STATIC", L"NetPilot Lite · Princess Setup", WS_CHILD | WS_VISIBLE, 24, 20, 610, 30, w,
                      NULL, NULL, NULL);
        CreateWindowW(L"STATIC", L"安装目录", WS_CHILD | WS_VISIBLE, 24, 64, 90, 24, w, NULL, NULL, NULL);
        target = CreateWindowW(L"EDIT", options.dir, WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 24,
                               94, 490, 30, w, (HMENU)4, NULL, NULL);
        browse_button = CreateWindowW(L"BUTTON", L"选择…", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 526, 94, 90,
                                      30, w, (HMENU)3, NULL, NULL);
        status = CreateWindowW(
            L"STATIC",
            L"从公开 GitHub Release 获取已签名文件清单。\n自动下载每个运行文件并安装，无需 ZIP 或手动解压。",
            WS_CHILD | WS_VISIBLE | SS_NOPREFIX, 24, 145, 592, 112, w, NULL, NULL, NULL);
        progress =
            CreateWindowW(PROGRESS_CLASSW, L"", WS_CHILD | WS_VISIBLE, 24, 274, 592, 18, w, NULL, NULL, NULL);
        start_button = CreateWindowW(L"BUTTON", L"开始安装 / 重试", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 24,
                                     316, 360, 42, w, (HMENU)1, NULL, NULL);
        cancel_button = CreateWindowW(L"BUTTON", L"取消", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 400, 316, 216,
                                      42, w, (HMENU)2, NULL, NULL);
        for (HWND c = GetWindow(w, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT))
            SendMessageW(c, WM_SETFONT, (WPARAM)font, TRUE);
        if (options.update || options.recover) {
            EnableWindow(target, FALSE);
            EnableWindow(browse_button, FALSE);
            PostMessageW(w, WM_COMMAND, 1, 0);
        }
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == 3 && !worker)
            pick_directory();
        if (LOWORD(wp) == 1 && !worker) {
            if (finished) {
                DestroyWindow(w);
                return 0;
            }
            GetWindowTextW(target, options.dir, NP_PATH);
            if (!np_install_absolute(options.dir, TRUE)) {
                SetWindowTextW(status, L"请选择有效的本地绝对路径；不支持链接、网络盘或驱动器根目录。");
                return 0;
            }
            if (!options.recover) {
                WCHAR main[NP_PATH];
                swprintf_s(main, NP_PATH, L"%s\\NetPilotLite.exe", options.dir);
                BOOL exists = GetFileAttributesW(main) != INVALID_FILE_ATTRIBUTES;
                if (options.update && !exists) {
                    SetWindowTextW(status,
                                   L"指定原安装目录不存在 NetPilotLite.exe；已拒绝将更新变成全新安装。");
                    return 0;
                }
                options.update = exists;
            }
            InterlockedExchange(&cancelled, 0);
            committing = FALSE;
            EnableWindow(start_button, FALSE);
            EnableWindow(target, FALSE);
            EnableWindow(browse_button, FALSE);
            EnableWindow(cancel_button, TRUE);
            SetWindowTextW(status, L"正在连接 GitHub，验证稳定版本与签名清单…");
            worker = CreateThread(NULL, 0, run, NULL, 0, NULL);
            if (!worker) {
                EnableWindow(start_button, TRUE);
                SetWindowTextW(status, L"无法创建安装工作线程，可重试。");
            }
        }
        if (LOWORD(wp) == 2) {
            if (worker && !committing) {
                InterlockedExchange(&cancelled, 1);
                SetWindowTextW(status, L"正在取消下载，等待当前 HTTPS 请求返回…");
            } else if (!worker)
                DestroyWindow(w);
        }
        return 0;
    case MSG_PROGRESS:
        if (!cancelled) {
            SetWindowTextW(status, (WCHAR *)lp);
            SendMessageW(progress, PBM_SETPOS, wp, 0);
        }
        free((void *)lp);
        return 0;
    case MSG_COMMIT:
        committing = TRUE;
        EnableWindow(cancel_button, FALSE);
        SetWindowTextW(status, L"全部文件已验证，正在正常关闭旧实例并安装。\n必要时 Windows 会请求 "
                               L"UAC；随后验证新版启动，失败自动回滚。");
        return 0;
    case MSG_DONE:
        CloseHandle(worker);
        worker = NULL;
        committing = FALSE;
        finished = (BOOL)wp;
        SetWindowTextW(status, lp ? (WCHAR *)lp : L"内存不足");
        free((void *)lp);
        EnableWindow(start_button, TRUE);
        EnableWindow(cancel_button, TRUE);
        SetWindowTextW(start_button, finished ? L"完成" : L"重试");
        if (!finished && !options.update) {
            EnableWindow(target, TRUE);
            EnableWindow(browse_button, TRUE);
        }
        return 0;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
        SetTextColor((HDC)wp, RGB(63, 70, 109));
        SetBkColor((HDC)wp, RGB(249, 242, 252));
        return (LRESULT)background;
    case WM_ERASEBKGND: {
        RECT r;
        GetClientRect(w, &r);
        FillRect((HDC)wp, &r, background);
        return 1;
    }
    case WM_CLOSE:
        if (worker) {
            if (!committing)
                InterlockedExchange(&cancelled, 1);
            return 0;
        }
        DestroyWindow(w);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(w, msg, wp, lp);
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE prev, PWSTR cmd, int show) {
    (void)prev;
    (void)cmd;
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32);
    int argc;
    WCHAR **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv || !parse(&options, argc, argv)) {
        if (argv)
            LocalFree(argv);
        MessageBoxW(NULL, L"安装参数无效，或提升权限时切换了 Windows 账户。未修改安装文件。",
                    L"NetPilot Lite Setup", MB_ICONERROR);
        return ERROR_INVALID_PARAMETER;
    }
    LocalFree(argv);
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (options.apply || (options.recover && options.sid[0])) {
        NP_ERROR e = {0};
        BOOL ok;
        if (options.recover)
            ok = np_setup_recover(options.dir, TRUE, &e);
        else {
            NP_MANIFEST *m = calloc(1, sizeof(*m));
            DWORD v[4];
            np_semver(options.version, v);
            ok = m && np_manifest_load(options.stage, v, m, &e) &&
                 np_setup_install(options.stage, options.dir, options.update, TRUE, &e);
            free(m);
        }
        CoUninitialize();
        return ok ? 0 : (int)(e.win32 ? e.win32 : ERROR_INSTALL_FAILURE);
    }
    if (!options.dir[0]) {
        if (!np_installed_directory(options.dir)) {
            PWSTR local = NULL;
            if (SUCCEEDED(SHGetKnownFolderPath(&FOLDERID_LocalAppData, 0, NULL, &local))) {
                swprintf_s(options.dir, NP_PATH, L"%s\\Programs\\NetPilotLite", local);
                CoTaskMemFree(local);
            }
        }
    }
    INITCOMMONCONTROLSEX controls = {sizeof(controls), ICC_PROGRESS_CLASS};
    InitCommonControlsEx(&controls);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    background = CreateSolidBrush(RGB(249, 242, 252));
    font = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY,
                       0, L"Segoe UI");
    WNDCLASSW cls = {0};
    cls.lpfnWndProc = proc;
    cls.hInstance = instance;
    cls.lpszClassName = L"NetPilotLiteSetup";
    cls.hCursor = LoadCursorW(NULL, IDC_ARROW);
    cls.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
    RegisterClassW(&cls);
    window = CreateWindowW(cls.lpszClassName, L"NetPilot Lite · 安装与更新",
                           WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, CW_USEDEFAULT,
                           CW_USEDEFAULT, 660, 425, NULL, NULL, instance, NULL);
    if (!window)
        return 1;
    ShowWindow(window, show);
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (!IsDialogMessageW(window, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    DeleteObject(font);
    DeleteObject(background);
    CoUninitialize();
    return finished ? 0 : ERROR_CANCELLED;
}
