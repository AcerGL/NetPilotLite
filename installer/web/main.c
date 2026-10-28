/* Native online bootstrapper. Worker owns all download/verification/installer wait operations. */
#include "online.h"
#include <commctrl.h>
#include <shellapi.h>
#include <stdio.h>
static HWND window, status, progress, start_button, cancel_button;
static HANDLE worker;
static volatile LONG cancelled;
static BOOL chinese, installing;
static ULONGLONG start_time;
#define TEXT2(zh,en) (chinese ? L##zh : L##en)
#define MSG_PROGRESS (WM_APP+1)
#define MSG_DONE (WM_APP+2)
#define MSG_INSTALL (WM_APP+3)
static void progress_cb(void *context, ULONGLONG done, ULONGLONG total) {
    (void)context;
    ULONGLONG elapsed = GetTickCount64() - start_time;
    WCHAR *text = calloc(512, sizeof(WCHAR));
    if (!text) return;
    swprintf_s(text, 512, TEXT2("下载 %llu%% · %.2f / %.2f MiB · %.2f MiB/s", "Download %llu%% · %.2f / %.2f MiB · %.2f MiB/s"),
               total ? done * 100 / total : 0, (double)done/1048576, (double)total/1048576,
               elapsed ? (double)done*1000/1048576/(double)elapsed : 0);
    if (!PostMessageW(window, MSG_PROGRESS, (WPARAM)(total ? done*100/total : 0), (LPARAM)text)) free(text);
}
static DWORD WINAPI run(void *unused) {
    (void)unused;
    NP_UPDATE_SETTINGS s = {0};
    wcscpy_s(s.owner, 100, NP_GITHUB_OWNER); wcscpy_s(s.repo, 100, NP_GITHUB_REPO);
    wcscpy_s(s.channel, 16, L"Stable"); s.installer = TRUE;
    NP_ONLINE_RELEASE *r = calloc(1, sizeof(*r));
    NP_ERROR e = {0}; WCHAR file[NP_PATH] = L"";
    BOOL ok = r && np_online_check(&s, r, &cancelled, &e);
    /* Retry entry is explicit and bounded; never silently retry UAC or installation. */
    if (ok) { start_time = GetTickCount64(); ok = np_online_download(r, &s, file, &cancelled, progress_cb, NULL, &e); }
    if (ok && InterlockedCompareExchange(&cancelled, 0, 0)) { ok = FALSE; e.win32 = ERROR_CANCELLED; }
    if (ok) {
        PostMessageW(window, MSG_INSTALL, 0, 0);
        HANDLE process = NULL;
        ok = np_installer_launch(file, &process, &e);
        if (ok) {
            WaitForSingleObject(process, INFINITE); /* worker wait: installer controls cancellation */
            DWORD code = 1; GetExitCodeProcess(process, &code); CloseHandle(process);
            ok = code == 0;
            if (!ok) np_error(&e, code, 0, L"Offline Setup failed/cancelled; see its installation log");
        }
    }
    if (file[0]) np_online_cleanup(file);
    free(r);
    WCHAR *message = calloc(512, sizeof(WCHAR));
    if (message) {
        if (ok) wcscpy_s(message, 512, TEXT2("离线安装器已完成。", "Offline Setup completed."));
        else swprintf_s(message, 512, TEXT2("未完成 · 错误 %lu (0x%08lX)\r\n%s\r\n可重试，或从 Releases 手动下载 Offline Setup。", "Not completed · error %lu (0x%08lX)\r\n%s\r\nRetry or manually download Offline Setup from Releases."), e.win32, e.win32, e.operation);
    }
    if (!PostMessageW(window, MSG_DONE, ok, (LPARAM)message)) free(message);
    return 0;
}
static LRESULT CALLBACK proc(HWND w, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
    case WM_CREATE: {
        HFONT font = GetStockObject(DEFAULT_GUI_FONT);
        CreateWindowW(L"STATIC", L"NetPilot Lite", WS_CHILD|WS_VISIBLE, 24,20,470,36,w,0,0,0);
        status = CreateWindowW(L"STATIC", TEXT2("通过 GitHub 获取可信离线安装包。\r\n不修改网卡、代理或个人设置。", "Download the trusted offline installer from GitHub.\r\nNetwork adapters, proxies and settings are preserved."), WS_CHILD|WS_VISIBLE,24,64,480,104,w,0,0,0);
        progress = CreateWindowW(PROGRESS_CLASSW,0,WS_CHILD|WS_VISIBLE,24,178,480,20,w,0,0,0);
        start_button = CreateWindowW(L"BUTTON",TEXT2("下载并安装 / 重试", "Download and install / Retry"),WS_CHILD|WS_VISIBLE|WS_TABSTOP,24,214,240,38,w,(HMENU)1,0,0);
        cancel_button = CreateWindowW(L"BUTTON",TEXT2("取消", "Cancel"),WS_CHILD|WS_VISIBLE|WS_TABSTOP,280,214,104,38,w,(HMENU)2,0,0);
        CreateWindowW(L"BUTTON",TEXT2("手动下载", "Releases"),WS_CHILD|WS_VISIBLE|WS_TABSTOP,400,214,104,38,w,(HMENU)3,0,0);
        for (HWND c=GetWindow(w,GW_CHILD);c;c=GetWindow(c,GW_HWNDNEXT)) SendMessageW(c,WM_SETFONT,(WPARAM)font,TRUE);
        WCHAR dir[NP_PATH], message[NP_PATH+150];
        if (np_installed_directory(dir)) {
            WCHAR exe[NP_PATH]; DWORD v[4] = {0};
            swprintf_s(exe, NP_PATH, L"%s\\NetPilotLite.exe", dir); np_file_version(exe,v);
            swprintf_s(message,_countof(message),TEXT2("已安装 %lu.%lu.%lu\r\n%s\r\n离线安装器将核验原路径并确认升级或修复。", "Installed %lu.%lu.%lu\r\n%s\r\nOffline Setup verifies the original directory and confirms upgrade/repair."),v[0],v[1],v[2],dir);
            SetWindowTextW(status,message);
        }
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wp)==1 && !worker) {
            InterlockedExchange(&cancelled,0); installing=FALSE;
            EnableWindow(start_button,FALSE); EnableWindow(cancel_button,TRUE);
            SetWindowTextW(status,TEXT2("正在检查 GitHub 稳定版…", "Checking GitHub stable release..."));
            worker=CreateThread(NULL,0,run,NULL,0,NULL);
            if (!worker) { EnableWindow(start_button,TRUE); SetWindowTextW(status,L"Could not start download worker."); }
        } else if (LOWORD(wp)==2 && worker && !installing) {
            InterlockedExchange(&cancelled,1);
            SetWindowTextW(status,TEXT2("正在取消；等待当前 HTTPS 请求返回（最长约 20 秒）…", "Cancelling; waiting for the current HTTPS request (up to about 20 seconds)..."));
        } else if (LOWORD(wp)==3) ShellExecuteW(w,L"open",L"https://github.com/" NP_GITHUB_OWNER L"/" NP_GITHUB_REPO L"/releases",NULL,NULL,SW_SHOWNORMAL);
        return 0;
    case MSG_PROGRESS:
        if (!InterlockedCompareExchange(&cancelled,0,0)) { SetWindowTextW(status,(WCHAR*)lp); SendMessageW(progress,PBM_SETPOS,wp,0); }
        free((void*)lp); return 0;
    case MSG_INSTALL:
        installing=TRUE; EnableWindow(cancel_button,FALSE);
        SetWindowTextW(status,TEXT2("签名已验证。请在 UAC 和离线安装器中继续；可在那里取消安装。", "Signature verified. Continue in UAC and Offline Setup; cancellation is available there.")); return 0;
    case MSG_DONE:
        if (worker) CloseHandle(worker); worker=NULL; installing=FALSE;
        EnableWindow(start_button,TRUE); EnableWindow(cancel_button,FALSE);
        SetWindowTextW(status,lp ? (WCHAR*)lp : L"Out of memory"); free((void*)lp); return 0;
    case WM_CLOSE:
        if (worker) {
            if (!installing) InterlockedExchange(&cancelled,1);
            SetWindowTextW(status,TEXT2("请等待下载取消，或先关闭离线安装器。", "Wait for download cancellation, or close Offline Setup first."));
            return 0;
        }
        DestroyWindow(w); return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(w,m,wp,lp);
}
int WINAPI wWinMain(HINSTANCE h,HINSTANCE prev,PWSTR cmd,int show) {
    (void)prev; (void)cmd;
    chinese=PRIMARYLANGID(GetUserDefaultUILanguage())==LANG_CHINESE;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    INITCOMMONCONTROLSEX ic={sizeof(ic),ICC_PROGRESS_CLASS}; InitCommonControlsEx(&ic);
    WNDCLASSW c={0}; c.lpfnWndProc=proc; c.hInstance=h; c.lpszClassName=L"NetPilotLite.WebSetup.1";
    c.hIcon=LoadIconW(h,MAKEINTRESOURCEW(1)); c.hCursor=LoadCursorW(NULL,IDC_ARROW);
    c.hbrBackground=CreateSolidBrush(RGB(238,233,250)); RegisterClassW(&c);
    window=CreateWindowW(c.lpszClassName,L"NetPilot Lite · Web Setup",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,CW_USEDEFAULT,CW_USEDEFAULT,550,310,NULL,NULL,h,NULL);
    if (!window) return 1;
    ShowWindow(window,show);
    MSG msg; while(GetMessageW(&msg,NULL,0,0)>0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    DeleteObject(c.hbrBackground); return (int)msg.wParam;
}
