#include "update.h"
#include <shellapi.h>
#include <objbase.h>
#include <stdio.h>
static WCHAR health_guid[40];
static BOOL health_mode;
BOOL np_update_health_mode(void){return health_mode;}
BOOL np_update_health_argument(const WCHAR *arg) {
    GUID id;
    if (arg && wcslen(arg) == 38 && SUCCEEDED(CLSIDFromString(arg, &id))) {
        wcscpy_s(health_guid, _countof(health_guid), arg);
        health_mode=TRUE;
        return TRUE;
    }
    return FALSE;
}
void np_update_health_ready(void) {
    if (health_guid[0]) {
        WCHAR name[100];
        swprintf_s(name, _countof(name), L"Local\\NetPilotLite.Health.%s", health_guid);
        HANDLE e = OpenEventW(EVENT_MODIFY_STATE, FALSE, name);
        if (e) {
            SetEvent(e);
            CloseHandle(e);
        }
        health_guid[0] = 0;
    }
}
static BOOL launch_updater(const WCHAR *source, BOOL recover, NP_ERROR *error) {
    WCHAR target[NP_PATH], folder[NP_PATH], temp[NP_PATH], updater[NP_PATH], guid[40], args[NP_PATH * 2 + 50];
    GUID id;
    if (!GetModuleFileNameW(NULL, target, _countof(target)) || !GetTempPathW(_countof(temp), temp) ||
        FAILED(CoCreateGuid(&id))) {
        np_error(error, GetLastError(), 0, L"准备临时更新器");
        return FALSE;
    }
    StringFromGUID2(&id, guid, _countof(guid));
    swprintf_s(folder, _countof(folder), L"%sNetPilotLite-Update-%s", temp, guid);
    if (!CreateDirectoryW(folder, NULL)) {
        np_error(error, GetLastError(), 0, L"创建临时更新目录");
        return FALSE;
    }
    swprintf_s(updater, _countof(updater), L"%s\\NetPilotLite.Updater.exe", folder);
    HRSRC res = FindResourceW(NULL, MAKEINTRESOURCEW(202), RT_RCDATA);
    HGLOBAL mem = res ? LoadResource(NULL, res) : NULL;
    BYTE *data = mem ? LockResource(mem) : NULL;
    DWORD bytes = res ? SizeofResource(NULL, res) : 0;
    if (!data || !bytes || !np_write_exact(updater, data, bytes)) {
        np_error(error, GetLastError(), 0, L"提取内置更新器");
        return FALSE;
    }
    /* Verify extracted bytes and deny writes/deletion until the exact image has been mapped. */
    HANDLE lock = CreateFileW(updater, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    BYTE expected[32], actual[32];
    BOOL ok = lock != INVALID_HANDLE_VALUE && np_hash(data, bytes, expected) &&
              np_hash_file(updater, actual) && !memcmp(expected, actual, 32);
    if (recover)
        swprintf_s(args, _countof(args), L"\"%s\" --recover \"%s\"", updater, target);
    else
        swprintf_s(args, _countof(args), L"\"%s\" --apply \"%s\" \"%s\"", updater, source, target);
    STARTUPINFOW si = {0};
    PROCESS_INFORMATION pi = {0};
    si.cb = sizeof(si);
    if (ok)
        ok = CreateProcessW(updater, args, NULL, NULL, FALSE, 0, NULL, folder, &si, &pi);
    DWORD code = GetLastError();
    if (lock != INVALID_HANDLE_VALUE)
        CloseHandle(lock);
    if (ok) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    } else
        np_error(error, code, 0, L"启动内置更新器（UAC 取消时不会替换文件）");
    return ok;
}
BOOL np_update_start(HWND owner, const WCHAR *source, NP_ERROR *error) {
    NP_RELEASE release;
    WCHAR target[NP_PATH];
    DWORD current[4];
    (void)owner;
    if (!np_verify_release(source, &release, error))
        return FALSE;
    GetModuleFileNameW(NULL, target, _countof(target));
    if (!np_file_version(target, current)) {
        np_error(error, ERROR_BAD_EXE_FORMAT, 0, L"读取当前产品版本");
        return FALSE;
    }
    int cmp = 0;
    for (int i = 0; i < 4 && !cmp; ++i)
        cmp = release.version[i] > current[i] ? 1 : release.version[i] < current[i] ? -1 : 0;
    if (cmp <= 0) {
        np_error(error, ERROR_REVISION_MISMATCH, 0, L"请选择比当前版本更新的签名发布文件");
        return FALSE;
    }
    return launch_updater(source, FALSE, error);
}
BOOL np_update_recovery_pending(void) {
    WCHAR path[NP_PATH];
    GetModuleFileNameW(NULL, path, _countof(path));
    WCHAR *p = wcsrchr(path, L'\\');
    if (!p)
        return FALSE;
    wcscpy_s(p + 1, _countof(path) - (size_t)(p + 1 - path), L".NetPilotLite.update-state");
    return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}
BOOL np_update_recover_start(NP_ERROR *error) {
    return launch_updater(NULL, TRUE, error);
}
