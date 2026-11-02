#include "private_update.h"
#include "settings.h"
#include <wincrypt.h>
#include <objbase.h>
#include <stdio.h>
static BOOL data_path(const WCHAR *root, const WCHAR *name, WCHAR out[NP_PATH]) {
    WCHAR dir[NP_PATH];
    if (!root) {
        if (!np_data_directory(dir, NP_PATH))
            return FALSE;
        root = dir;
    }
    DWORD a = GetFileAttributesW(root);
    if (a == INVALID_FILE_ATTRIBUTES || (a & FILE_ATTRIBUTE_REPARSE_POINT))
        return FALSE;
    return swprintf_s(out, NP_PATH, L"%s\\%s", root, name) > 0;
}
BOOL np_token_valid(const WCHAR *s) {
    size_t n = s ? wcslen(s) : 0;
    if (n < 32 || n >= NP_TOKEN_CHARS || wcsncmp(s, L"github_pat_", 11))
        return FALSE;
    for (size_t i = 0; i < n; i++)
        if (!((s[i] >= L'a' && s[i] <= L'z') || (s[i] >= L'A' && s[i] <= L'Z') ||
              (s[i] >= L'0' && s[i] <= L'9') || s[i] == L'_'))
            return FALSE;
    return TRUE;
}
BOOL np_auth_store(const WCHAR *root, const NP_AUTH *auth, NP_ERROR *e) {
    WCHAR path[NP_PATH], tmp[NP_PATH];
    DATA_BLOB in = {(DWORD)sizeof(*auth), (BYTE *)auth}, out = {0};
    BOOL ok = np_token_valid(auth->token) && np_repo_valid(auth->owner, auth->repo) &&
              data_path(root, L"github-auth.dpapi", path);
    GUID id;
    WCHAR guid[40];
    ok = ok && SUCCEEDED(CoCreateGuid(&id)) && StringFromGUID2(&id, guid, 40);
    if (ok)
        swprintf_s(tmp, NP_PATH, L"%s.%s.tmp", path, guid);
    if (ok)
        ok = CryptProtectData(&in, L"NetPilot GitHub repository read authorization", NULL, NULL, NULL,
                              CRYPTPROTECT_UI_FORBIDDEN, &out);
    if (ok) {
        ok = np_write_exact(tmp, out.pbData, out.cbData) &&
             MoveFileExW(tmp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
        if (!ok)
            DeleteFileW(tmp);
    }
    if (out.pbData) {
        SecureZeroMemory(out.pbData, out.cbData);
        LocalFree(out.pbData);
    }
    if (!ok)
        np_error(e, GetLastError() ? GetLastError() : ERROR_INVALID_DATA, 0,
                 L"保存 CurrentUser 加密授权失败；旧授权未主动删除");
    return ok;
}
BOOL np_auth_load(const WCHAR *root, const NP_UPDATE_SETTINGS *s, NP_AUTH *auth, NP_ERROR *e) {
    WCHAR path[NP_PATH];
    BYTE bytes[8192];
    DWORD got = 0;
    DATA_BLOB out = {0};
    BOOL ok = FALSE;
    ZeroMemory(auth, sizeof(*auth));
    if (!data_path(root, L"github-auth.dpapi", path))
        goto done;
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                           FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    if (f == INVALID_HANDLE_VALUE)
        goto done;
    BY_HANDLE_FILE_INFORMATION info;
    LARGE_INTEGER size;
    ok = GetFileInformationByHandle(f, &info) && !(info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
         GetFileSizeEx(f, &size) && size.QuadPart > 0 && size.QuadPart <= sizeof(bytes) &&
         ReadFile(f, bytes, sizeof(bytes), &got, NULL);
    CloseHandle(f);
    DATA_BLOB in = {got, bytes};
    ok = ok && CryptUnprotectData(&in, NULL, NULL, NULL, NULL, CRYPTPROTECT_UI_FORBIDDEN, &out) &&
         out.cbData == sizeof(*auth);
    if (ok) {
        memcpy(auth, out.pbData, sizeof(*auth));
        ok = auth->magic == 0x4e504154 && auth->owner[99] == 0 && auth->repo[99] == 0 &&
             auth->token[NP_TOKEN_CHARS - 1] == 0 && !_wcsicmp(auth->owner, s->owner) &&
             !_wcsicmp(auth->repo, s->repo) && np_token_valid(auth->token);
    }
done:
    if (out.pbData) {
        SecureZeroMemory(out.pbData, out.cbData);
        LocalFree(out.pbData);
    }
    SecureZeroMemory(bytes, sizeof(bytes));
    if (!ok) {
        SecureZeroMemory(auth, sizeof(*auth));
        np_error(e, ERROR_LOGON_FAILURE, 0,
                 L"需要首次授权或重新授权：请在更新设置中配置本仓库 Contents: Read 的 Fine-grained Token");
    }
    return ok;
}
BOOL np_auth_remove(const WCHAR *root, NP_ERROR *e) {
    WCHAR path[NP_PATH];
    BOOL ok = data_path(root, L"github-auth.dpapi", path) &&
              (DeleteFileW(path) || GetLastError() == ERROR_FILE_NOT_FOUND);
    if (!ok)
        np_error(e, GetLastError(), 0, L"删除本机授权失败");
    return ok;
}
/* Dynamic NT query; fail closed if Windows cannot provide a stable boot identifier.
   BootIdentifier survives app restarts; hybrid resume is the same kernel boot cycle. */
BOOL np_boot_id(WCHAR out[40]) {
    typedef LONG(WINAPI * QUERY)(ULONG, void *, ULONG, ULONG *);
    struct {
        GUID id;
        ULONG firmware;
        ULONGLONG flags;
    } boot = {0};
    QUERY q = (QUERY)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQuerySystemInformation");
    return q && q(90, &boot, sizeof(boot), NULL) >= 0 && StringFromGUID2(&boot.id, out, 40) > 0;
}
BOOL np_boot_due(const WCHAR *root, const WCHAR *boot) {
    WCHAR p[NP_PATH], last[40];
    if (!boot || wcslen(boot) != 38 || !data_path(root, L"update-state.ini", p))
        return FALSE;
    GetPrivateProfileStringW(L"Boot", L"Completed", L"", last, 40, p);
    return wcscmp(last, boot) != 0;
}
BOOL np_boot_complete(const WCHAR *root, const WCHAR *boot) {
    WCHAR p[NP_PATH];
    return data_path(root, L"update-state.ini", p) &&
           WritePrivateProfileStringW(L"Boot", L"Completed", boot, p);
}
BOOL np_boot_attempt(const WCHAR *root, const WCHAR *boot, ULONGLONG now, unsigned *attempt) {
    WCHAR p[NP_PATH], last[40], ticks[40];
    if (!np_boot_due(root, boot) || !data_path(root, L"update-state.ini", p))
        return FALSE;
    GetPrivateProfileStringW(L"Boot", L"AttemptBoot", L"", last, 40, p);
    unsigned n = !wcscmp(last, boot) ? GetPrivateProfileIntW(L"Boot", L"Attempts", 0, p) : 0;
    if (attempt)
        *attempt = n;
    GetPrivateProfileStringW(L"Boot", L"LastTick", L"0", ticks, 40, p);
    ULONGLONG previous = _wcstoui64(ticks, NULL, 10);
    if (n >= 3 || (n && now >= previous && now - previous < (n == 1 ? 30000 : 90000)))
        return FALSE;
    swprintf_s(ticks, 40, L"%llu", now);
    BOOL ok = WritePrivateProfileStringW(L"Boot", L"AttemptBoot", boot, p) &&
              WritePrivateProfileStringW(L"Boot", L"LastTick", ticks, p);
    swprintf_s(last, 40, L"%u", n + 1);
    ok = ok && WritePrivateProfileStringW(L"Boot", L"Attempts", last, p);
    if (ok && attempt)
        *attempt = n + 1;
    return ok;
}
BOOL np_startup_initialize(NP_ERROR *e) {
    WCHAR p[NP_PATH];
    if (!data_path(NULL, L"update-state.ini", p))
        return FALSE;
    if (GetPrivateProfileIntW(L"Boot", L"StartupInitialized", 0, p))
        return TRUE;
    return np_autorun(TRUE, e) && WritePrivateProfileStringW(L"Boot", L"StartupInitialized", L"1", p);
}
BOOL np_ignore_version(const WCHAR *root, const WCHAR *tag) {
    WCHAR p[NP_PATH];
    DWORD v[4];
    return np_semver(tag, v) && data_path(root, L"update-state.ini", p) &&
           WritePrivateProfileStringW(L"Update", L"Ignored", tag, p);
}
BOOL np_ignored(const WCHAR *root, const DWORD version[4]) {
    WCHAR p[NP_PATH], tag[64];
    DWORD v[4];
    if (!data_path(root, L"update-state.ini", p))
        return FALSE;
    GetPrivateProfileStringW(L"Update", L"Ignored", L"", tag, 64, p);
    return np_semver(tag, v) && np_version_compare(version, v) <= 0;
}
static const WCHAR runkey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
BOOL np_autorun(BOOL enabled, NP_ERROR *e) {
#ifdef NP_READONLY_BUILD
    (void)enabled;
    np_error(e, ERROR_NOT_SUPPORTED, 0, L"模拟测试不注册登录启动");
    return FALSE;
#else
    HKEY key;
    LONG status = RegCreateKeyExW(HKEY_CURRENT_USER, runkey, 0, NULL, 0, KEY_SET_VALUE, NULL, &key, NULL);
    if (!status) {
        if (enabled) {
            WCHAR exe[NP_PATH], value[NP_PATH + 30];
            GetModuleFileNameW(NULL, exe, NP_PATH);
            swprintf_s(value, _countof(value), L"\"%s\" --startup", exe);
            status = RegSetValueExW(key, L"NetPilotLite", 0, REG_SZ, (BYTE *)value,
                                    (DWORD)(wcslen(value) + 1) * 2);
        } else {
            status = RegDeleteValueW(key, L"NetPilotLite");
            if (status == ERROR_FILE_NOT_FOUND)
                status = 0;
        }
        RegCloseKey(key);
    }
    if (status)
        np_error(e, status, 0, L"更新当前用户登录启动设置失败");
    return !status;
#endif
}
BOOL np_autorun_enabled(void) {
    WCHAR value[NP_PATH + 30];
    DWORD n = sizeof(value);
    return RegGetValueW(HKEY_CURRENT_USER, runkey, L"NetPilotLite", RRF_RT_REG_SZ, NULL, value, &n) ==
           ERROR_SUCCESS;
}
