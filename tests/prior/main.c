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
