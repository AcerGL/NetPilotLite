#include "public_setup.h"
#include "settings.h"
#include <objbase.h>
#include <stdio.h>
BOOL np_setup_pending_start(NP_ERROR *e) {
    WCHAR dir[NP_PATH], journal[NP_PATH], setup[NP_PATH], args[NP_PATH * 2 + 64];
    GetModuleFileNameW(NULL, dir, NP_PATH);
    WCHAR *slash = wcsrchr(dir, L'\\');
    if (!slash)
        return FALSE;
    *slash = 0;
    swprintf_s(journal, NP_PATH, L"%s\\.NetPilotSetup-transaction\\journal.bin", dir);
    if (GetFileAttributesW(journal) == INVALID_FILE_ATTRIBUTES)
        return FALSE;
    swprintf_s(setup, NP_PATH, L"%s\\.NetPilotSetup-transaction\\RecoverySetup.exe", dir);
    NP_RELEASE r;
    /* Run outside the transaction so recovery can remove its backup directory. */
    WCHAR base[NP_PATH], cache[NP_PATH], copy[NP_PATH], source_sig[NP_PATH], copy_sig[NP_PATH], id[40];
    GUID guid;
    if (!np_verify_setup(setup, &r, e) || !np_data_directory(base, NP_PATH) || FAILED(CoCreateGuid(&guid)) ||
        !StringFromGUID2(&guid, id, 40))
        return TRUE;
    swprintf_s(cache, NP_PATH, L"%s\\SetupCache-%s", base, id);
    swprintf_s(copy, NP_PATH, L"%s\\%s", cache, NP_SETUP_NAME);
    swprintf_s(copy_sig, NP_PATH, L"%s.update", copy);
    swprintf_s(source_sig, NP_PATH, L"%s.update", setup);
    if (!np_install_mkdirs(cache) || !CopyFileW(setup, copy, TRUE) ||
        !CopyFileW(source_sig, copy_sig, TRUE)) {
        np_error(e, GetLastError(), 0, L"无法暂存可信恢复程序；保留原备份");
        return TRUE;
    }
    wcscpy_s(setup, NP_PATH, copy);
    HANDLE lock = CreateFileW(setup, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                              FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    BOOL ok = lock != INVALID_HANDLE_VALUE && np_verify_setup(setup, &r, e);
    swprintf_s(args, _countof(args), L"\"%s\" --recover --install-dir \"%s\"", setup, dir);
    STARTUPINFOW si = {sizeof(si)};
    PROCESS_INFORMATION pi = {0};
    if (ok)
        ok = CreateProcessW(setup, args, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
    if (lock != INVALID_HANDLE_VALUE)
        CloseHandle(lock);
    if (ok) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    } else if (!e->win32)
        np_error(e, ERROR_RECOVERY_FAILURE, 0,
                 L"待恢复安装缺少可信恢复程序；保留事务目录并重新运行官方 Setup");
    return TRUE; /* journal present: caller must not start a possibly mixed application */
}
