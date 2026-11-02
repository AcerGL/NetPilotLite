#include "lite.h"
#include "update.h"
#include <shellapi.h>
#include <shlobj.h>
#include <objbase.h>
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR command, int show) {
    (void)previous;
    (void)command;
    int argc = 0;
    WCHAR **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
#ifndef NP_READONLY_BUILD
    if (argc == 5 && !wcscmp(argv[1], L"--adapter-action")) {
        NP_BINDING binding = {0};
        binding.version = NP_BINDING_VERSION;
        BOOL valid = IsUserAnAdmin() && SUCCEEDED(CLSIDFromString(argv[2], &binding.guid)) &&
                     wcslen(argv[3]) < NP_ID_LEN &&
                     (!wcscmp(argv[4], L"enable") || !wcscmp(argv[4], L"disable"));
        int code = ERROR_ACCESS_DENIED;
        if (valid) {
            wcscpy_s(binding.instance, NP_ID_LEN, argv[3]);
            NP_BACKEND backend = np_windows_backend();
            NP_RESULT r = np_execute(&backend, &binding, !wcscmp(argv[4], L"enable"), 10000);
            code = r.success ? 0 : (int)(r.error.win32 ? r.error.win32 : ERROR_GEN_FAILURE);
        }
        LocalFree(argv);
        return code;
    }
#endif
    BOOL startup = argc == 2 && !wcscmp(argv[1], L"--startup");
    BOOL health = argc == 3 && !wcscmp(argv[1], L"--update-health");
    if (argc != 1 && !health && !startup) {
        LocalFree(argv);
        return ERROR_INVALID_PARAMETER;
    }
    if (health && !np_update_health_argument(argv[2])) {
        LocalFree(argv);
        return ERROR_INVALID_PARAMETER;
    }
    LocalFree(argv);
    if (!health) {
        WCHAR pending[NP_PATH];
        GetModuleFileNameW(NULL, pending, NP_PATH);
        WCHAR *slash = wcsrchr(pending, L'\\');
        if (slash) {
            wcscpy_s(slash + 1, NP_PATH - (slash + 1 - pending), L".NetPilotLite-install-pending");
            if (GetFileAttributesW(pending) != INVALID_FILE_ATTRIBUTES) {
                MessageBoxW(
                    NULL,
                    L"安装未完成，请重新运行已验证的 Offline Setup "
                    L"进行恢复或修复。保留安装备份。\nInstallation incomplete. Rerun trusted Offline Setup.",
                    L"NetPilot Lite", MB_ICONERROR);
                return ERROR_INSTALL_FAILURE;
            }
        }
    }
    /* The updater owns the journal during health-confirmed startup. An ordinary launch recovers it. */
    if (!health && np_update_recovery_pending()) {
        NP_ERROR e = {0};
        if (np_update_recover_start(&e))
            return 0;
        MessageBoxW(NULL, L"检测到未完成的更新，但恢复器无法启动。请保留 .NetPilotLite.previous 与更新记录。",
                    L"NetPilot Lite", MB_OK | MB_ICONERROR);
        return 1;
    }
#ifdef NP_MOCK_PRODUCT
    HANDLE mutex = CreateMutexW(NULL, FALSE, L"Local\\NetPilotLite.UpdateTest.1");
#else
    HANDLE mutex = CreateMutexW(NULL, FALSE, L"Local\\NetPilotLite.UserInterface.1");
#endif
    if (!mutex)
        return 1;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
#ifdef NP_MOCK_PRODUCT
        HWND window = FindWindowW(L"NetPilotLiteTestWindow1", NULL);
#else
        HWND window = FindWindowW(L"NetPilotLiteWindow1", NULL);
#endif
        if (window) {
            ShowWindow(window, SW_RESTORE);
            SetForegroundWindow(window);
        }
        CloseHandle(mutex);
        return 0;
    }
#ifdef NP_MOCK_PRODUCT
    int result = np_app_run(instance, show, TRUE, FALSE);
#else
    int result = np_app_run(instance, startup ? SW_HIDE : show, FALSE, FALSE);
#endif
    CloseHandle(mutex);
    return result;
}
