#include "lite.h"
#include "update.h"
#include <shellapi.h>
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR command, int show) {
    (void)previous;
    (void)command;
    int argc = 0;
    WCHAR **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    BOOL health = argc == 3 && !wcscmp(argv[1], L"--update-health");
    if (argc != 1 && !health) {
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
                MessageBoxW(NULL, L"安装未完成，请重新运行已验证的 Offline Setup 进行恢复或修复。保留安装备份。\nInstallation incomplete. Rerun trusted Offline Setup.", L"NetPilot Lite", MB_ICONERROR);
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
    int result = np_app_run(instance, show, FALSE, FALSE);
#endif
    CloseHandle(mutex);
    return result;
}
