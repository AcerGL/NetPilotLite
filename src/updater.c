/* Fixed-purpose offline updater. No shell commands, downloads, services or network APIs. */
#include "update.h"
#include <shellapi.h>
#include <objbase.h>
#include <stdio.h>
#include <wchar.h>
#include <sddl.h>
typedef struct JOURNAL {
    DWORD magic, phase, baseline;
    NP_RELEASE old_release, new_release;
    BYTE old_hash[32];
} JOURNAL;
static const BYTE baseline_hash[32] = {0xd6, 0x71, 0xcf, 0x66, 0x48, 0x16, 0x4f, 0x34, 0x3c, 0x08, 0xdc,
                                       0xab, 0xa2, 0x0f, 0xf1, 0x68, 0x98, 0xf0, 0x52, 0xd3, 0xdc, 0xb1,
                                       0x7a, 0x9d, 0xf4, 0xe3, 0x55, 0xd3, 0x4f, 0x42, 0xf5, 0xff};
typedef struct FILES {
    WCHAR dir[NP_PATH], target[NP_PATH], stage[NP_PATH], backup[NP_PATH], journal[NP_PATH], temp[NP_PATH],
        side[NP_PATH], lock[NP_PATH], log[NP_PATH];
} FILES;
static void log_step(const FILES *p, const WCHAR *stage, DWORD code) {
    HANDLE f = CreateFileW(p->log, FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    if (f != INVALID_HANDLE_VALUE) {
        WCHAR line[300];
        SYSTEMTIME t;
        GetSystemTime(&t);
        int n = swprintf_s(line, _countof(line), L"%04u-%02u-%02uT%02u:%02u:%02uZ stage=%s win32=%lu\r\n",
                           t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, stage, code);
        DWORD done;
        WriteFile(f, line, (DWORD)n * 2, &done, NULL);
        FlushFileBuffers(f);
        CloseHandle(f);
    }
}
static BOOL safe_path(const WCHAR *path, BOOL allow_missing) {
    WCHAR full[NP_PATH];
    DWORD n = GetFullPathNameW(path, _countof(full), full, NULL);
    if (n < 4 || n >= _countof(full) || full[1] != L':' || full[2] != L'\\' || wcsstr(full, L"\\\\") ||
        wcschr(full + 2, L':'))
        return FALSE;
    /* Reject every reparse ancestor; the updater never traverses a junction into another destination. */
    for (WCHAR *q = full + 3;; ++q)
        if (!*q || *q == L'\\') {
            WCHAR c = *q;
            *q = 0;
            DWORD attr = GetFileAttributesW(full);
            *q = c;
            if (attr == INVALID_FILE_ATTRIBUTES) {
                if (!c && allow_missing &&
                    (GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND))
                    return TRUE;
                return FALSE;
            }
            if (attr & FILE_ATTRIBUTE_REPARSE_POINT)
                return FALSE;
            if (!c) {
                if (!(attr & FILE_ATTRIBUTE_DIRECTORY)) {
                    HANDLE f = CreateFileW(full, FILE_READ_ATTRIBUTES,
                                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                                           OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, NULL);
                    BY_HANDLE_FILE_INFORMATION info;
                    BOOL single = f != INVALID_HANDLE_VALUE && GetFileInformationByHandle(f, &info) &&
                                  info.nNumberOfLinks == 1;
                    if (f != INVALID_HANDLE_VALUE)
                        CloseHandle(f);
                    if (!single)
                        return FALSE;
                }
                break;
            }
        }
    return TRUE;
}
static BOOL paths(const WCHAR *target, FILES *p) {
    ZeroMemory(p, sizeof(*p));
    DWORD n = GetFullPathNameW(target, _countof(p->target), p->target, NULL);
    if (!n || n >= _countof(p->target) || !safe_path(p->target, TRUE))
        return FALSE;
    WCHAR *name = wcsrchr(p->target, L'\\');
    if (!name || _wcsicmp(name + 1, L"NetPilotLite.exe"))
        return FALSE;
    wcscpy_s(p->dir, _countof(p->dir), p->target);
    p->dir[name - p->target] = 0;
#define CHILD(field, name) swprintf_s(p->field, _countof(p->field), L"%s\\%s", p->dir, L##name)
    CHILD(stage, ".NetPilotLite.new");
    CHILD(backup, ".NetPilotLite.previous");
    CHILD(journal, ".NetPilotLite.update-state");
    CHILD(temp, ".NetPilotLite.update-state.tmp");
    CHILD(side, "NetPilotLite.exe.update");
    CHILD(lock, ".NetPilotLite.update-lock");
    CHILD(log, "NetPilotLite-update.log");
#undef CHILD
    const WCHAR *all[] = {p->stage, p->backup, p->journal, p->temp, p->side, p->lock, p->log};
    for (size_t i = 0; i < _countof(all); ++i)
        if (!safe_path(all[i], TRUE))
            return FALSE;
    return TRUE;
}
static BOOL write_journal(const FILES *p, const JOURNAL *j) {
    DeleteFileW(p->temp);
    if (!np_write_exact(p->temp, j, sizeof(*j)))
        return FALSE;
    return MoveFileExW(p->temp, p->journal, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
}
static BOOL old_matches(const WCHAR *file, const JOURNAL *j) {
    BYTE h[32];
    if (j->baseline)
        return !memcmp(j->old_hash, baseline_hash, 32) && np_hash_file(file, h) &&
               !memcmp(h, baseline_hash, 32);
    return np_release_signature(&j->old_release) && np_release_matches(file, &j->old_release);
}
typedef struct CLOSE_CONTEXT {
    const WCHAR *path;
    BOOL found, refused, only_find;
} CLOSE_CONTEXT;
static BOOL CALLBACK close_window(HWND w, LPARAM lp) {
    CLOSE_CONTEXT *c = (CLOSE_CONTEXT *)lp;
    WCHAR cls[80], image[NP_PATH];
    DWORD pid, n = _countof(image);
    GetClassNameW(w, cls, _countof(cls));
#ifdef NP_UPDATE_TEST
    if (wcscmp(cls, L"NetPilotLiteTestWindow1"))
        return TRUE;
#else
    if (wcscmp(cls, L"NetPilotLiteWindow1"))
        return TRUE;
#endif
    GetWindowThreadProcessId(w, &pid);
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid);
    if (!process) {
        c->refused = TRUE;
        return TRUE;
    }
    if (QueryFullProcessImageNameW(process, 0, image, &n) && !_wcsicmp(image, c->path)) {
        DWORD_PTR result = 0;
        c->found = TRUE;
        /* ID_EXIT=106 is the normal exit command in the original 1.0.0 UI too. */
        if (!c->only_find && (!SendMessageTimeoutW(w, WM_COMMAND, 106, 0, SMTO_ABORTIFHUNG, 3000, &result) ||
                              WaitForSingleObject(process, 20000) != WAIT_OBJECT_0))
            c->refused = TRUE;
    }
    CloseHandle(process);
    return TRUE;
}
static BOOL close_target(const WCHAR *path) {
    CLOSE_CONTEXT c = {path, FALSE, FALSE, FALSE};
    EnumWindows(close_window, (LPARAM)&c);
    return !c.refused;
}
static BOOL running_target(const WCHAR *path) {
    CLOSE_CONTEXT c = {path, FALSE, FALSE, TRUE};
    EnumWindows(close_window, (LPARAM)&c);
    return c.found;
}
static BOOL launch(const WCHAR *target, BOOL health, PROCESS_INFORMATION *pi, HANDLE *event) {
    WCHAR command[NP_PATH + 150], dir[NP_PATH], guid[40], eventname[100];
    GUID id;
    STARTUPINFOW si = {0};
    si.cb = sizeof(si);
    ZeroMemory(pi, sizeof(*pi));
    *event = NULL;
    wcscpy_s(dir, _countof(dir), target);
    *wcsrchr(dir, L'\\') = 0;
    if (health) {
        if (FAILED(CoCreateGuid(&id)))
            return FALSE;
        StringFromGUID2(&id, guid, _countof(guid));
        swprintf_s(eventname, _countof(eventname), L"Local\\NetPilotLite.Health.%s", guid);
        *event = CreateEventW(NULL, TRUE, FALSE, eventname);
        if (!*event || GetLastError() == ERROR_ALREADY_EXISTS) {
            if (*event)
                CloseHandle(*event);
            *event = NULL;
            return FALSE;
        }
        swprintf_s(command, _countof(command), L"\"%s\" --update-health %s", target, guid);
    } else
        swprintf_s(command, _countof(command), L"\"%s\"", target);
    if (!CreateProcessW(target, command, NULL, NULL, FALSE, CREATE_UNICODE_ENVIRONMENT, NULL, dir, &si, pi)) {
        if (*event)
            CloseHandle(*event);
        *event = NULL;
        return FALSE;
    }
    CloseHandle(pi->hThread);
    return TRUE;
}
static BOOL rollback(const FILES *p, JOURNAL *j, BOOL restart) {
    if (!old_matches(p->target, j)) {
        if (!old_matches(p->backup, j) || !close_target(p->target))
            return FALSE;
        if (!MoveFileExW(p->backup, p->target, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            return FALSE;
    }
    if (!j->baseline) {
        WCHAR side_tmp[NP_PATH];
        swprintf_s(side_tmp, _countof(side_tmp), L"%s.restore", p->side);
        DeleteFileW(side_tmp);
        if (!np_write_exact(side_tmp, &j->old_release, sizeof(j->old_release)) ||
            !MoveFileExW(side_tmp, p->side, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            return FALSE;
    } else
        DeleteFileW(p->side);
    DeleteFileW(p->stage);
    DeleteFileW(p->journal);
    log_step(p, L"rolled-back", 0);
    if (restart) {
        if (running_target(p->target))
            return TRUE;
        PROCESS_INFORMATION pi = {0};
        HANDLE event = NULL;
        HANDLE image = CreateFileW(p->target, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        if (image == INVALID_HANDLE_VALUE)
            return FALSE;
        BOOL ok = old_matches(p->target, j) && launch(p->target, FALSE, &pi, &event);
        CloseHandle(image);
        if (!ok)
            return FALSE;
        WaitForInputIdle(pi.hProcess, 10000);
        BOOL alive = WaitForSingleObject(pi.hProcess, 250) == WAIT_TIMEOUT || running_target(p->target);
        CloseHandle(pi.hProcess);
        if (!alive) {
            SetLastError(ERROR_APP_INIT_FAILURE);
            return FALSE;
        }
    }
    return TRUE;
}
int np_updater_apply(const WCHAR *source, const WCHAR *target, BOOL recovery) {
    FILES p;
    JOURNAL j = {0};
    NP_ERROR e = {0};
    DWORD failure = 0;
    BOOL replaced = FALSE;
#ifdef NP_UPDATE_TEST
    WCHAR inject[32] = L"";
    GetEnvironmentVariableW(L"NP_UPDATE_TEST_FAULT", inject, _countof(inject));
#endif
    if (!paths(target, &p))
        return ERROR_BAD_PATHNAME;
    HANDLE lock = CreateFileW(p.lock, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_ALWAYS,
                              FILE_FLAG_DELETE_ON_CLOSE, NULL);
    if (lock == INVALID_HANDLE_VALUE)
        return GetLastError();
    log_step(&p, recovery ? L"recovery-begin" : L"verify-begin", 0);
    if (GetFileAttributesW(p.journal) != INVALID_FILE_ATTRIBUTES) {
        if (!np_read_exact(p.journal, &j, sizeof(j)) || j.magic != 0x3155504e ||
            !np_release_signature(&j.new_release) || (!j.baseline && !np_release_signature(&j.old_release))) {
            failure = NP_SIGNATURE_ERROR;
            goto done;
        }
        if (j.phase == 3 && np_release_matches(p.target, &j.new_release)) {
            DeleteFileW(p.temp);
            if (!np_write_exact(p.temp, &j.new_release, sizeof(j.new_release)) ||
                !MoveFileExW(p.temp, p.side, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                failure = GetLastError();
                goto done;
            }
            if (GetFileAttributesW(p.backup) != INVALID_FILE_ATTRIBUTES && !DeleteFileW(p.backup)) {
                failure = GetLastError();
                goto done;
            }
            DeleteFileW(p.journal);
            DeleteFileW(p.stage);
            if (recovery) {
                if (running_target(p.target))
                    goto done;
                PROCESS_INFORMATION pi = {0};
                HANDLE event = NULL;
                HANDLE image =
                    CreateFileW(p.target, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
                BOOL ok = image != INVALID_HANDLE_VALUE && np_release_matches(p.target, &j.new_release) &&
                          launch(p.target, FALSE, &pi, &event);
                if (image != INVALID_HANDLE_VALUE)
                    CloseHandle(image);
                if (!ok) {
                    failure = GetLastError();
                    goto done;
                }
                WaitForInputIdle(pi.hProcess, 10000);
                BOOL alive =
                    WaitForSingleObject(pi.hProcess, 250) == WAIT_TIMEOUT || running_target(p.target);
                CloseHandle(pi.hProcess);
                if (!alive)
                    failure = ERROR_APP_INIT_FAILURE;
                goto done;
            }
        } else {
            if (!rollback(&p, &j, recovery)) {
                failure = ERROR_RECOVERY_FAILURE;
                goto done;
            }
            if (recovery)
                goto done;
        }
    } else if (recovery)
        goto done;
    ZeroMemory(&j, sizeof(j));
    j.magic = 0x3155504e;
    if (!source || !safe_path(source, FALSE) || !np_verify_release(source, &j.new_release, &e)) {
        failure = e.win32 ? e.win32 : ERROR_INVALID_DATA;
        goto done;
    }
    if (!_wcsicmp(source, p.target)) {
        failure = ERROR_INVALID_PARAMETER;
        goto done;
    }
    if (!np_verify_release(p.target, &j.old_release, &e)) {
        if (!np_hash_file(p.target, j.old_hash) || memcmp(j.old_hash, baseline_hash, 32)) {
            failure = NP_SIGNATURE_ERROR;
            goto done;
        }
        j.baseline = 1;
    }
    DWORD current[4];
    if (!np_file_version(p.target, current)) {
        failure = ERROR_BAD_EXE_FORMAT;
        goto done;
    }
    int comparison = 0;
    for (int k = 0; k < 4 && !comparison; ++k)
        comparison = j.new_release.version[k] > current[k]   ? 1
                     : j.new_release.version[k] < current[k] ? -1
                                                             : 0;
    if (comparison <= 0) {
        failure = ERROR_REVISION_MISMATCH;
        goto done;
    }
    if (GetFileAttributesW(p.backup) != INVALID_FILE_ATTRIBUTES) {
        failure = ERROR_ALREADY_EXISTS;
        goto done;
    }
#ifdef NP_UPDATE_TEST
    if (!wcscmp(inject, L"cancel")) {
        failure = ERROR_CANCELLED;
        goto done;
    }
#endif
    /* Stage and verify before asking the old process to exit. Never trust CopyFile's exit alone. */
    DeleteFileW(p.stage);
    if (!CopyFileW(source, p.stage, TRUE) || !np_release_matches(p.stage, &j.new_release)) {
        failure = GetLastError();
        if (!failure)
            failure = ERROR_CRC;
        goto done;
    }
    HANDLE stage = CreateFileW(p.stage, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                               FILE_FLAG_WRITE_THROUGH, NULL);
    if (stage == INVALID_HANDLE_VALUE || !FlushFileBuffers(stage)) {
        failure = GetLastError();
        if (stage != INVALID_HANDLE_VALUE)
            CloseHandle(stage);
        goto done;
    }
    CloseHandle(stage);
    j.phase = 1;
    if (!write_journal(&p, &j)) {
        failure = GetLastError();
        goto done;
    }
    if (!close_target(p.target)) {
        failure = ERROR_BUSY;
        goto done;
    }
    log_step(&p, L"old-process-exited", 0);
#ifdef NP_UPDATE_TEST
    if (!wcscmp(inject, L"prepared")) {
        CloseHandle(lock);
        return 90;
    }
#endif
    if (!ReplaceFileW(p.target, p.stage, p.backup, 0, NULL, NULL)) {
        failure = GetLastError(); /* ReplaceFile may have partially renamed files: journal recovery checks
                                     actual hashes. */
        goto done;
    }
    replaced = TRUE;
    j.phase = 2;
    if (!write_journal(&p, &j)) {
        failure = GetLastError();
        goto done;
    }
#ifdef NP_UPDATE_TEST
    if (!wcscmp(inject, L"replaced")) {
        CloseHandle(lock);
        return 91;
    }
#endif
    if (!np_release_matches(p.target, &j.new_release)) {
        failure = ERROR_CRC;
        goto done;
    }
    /* Keep the image open against write/delete until CreateProcess has mapped it. */
    HANDLE image = CreateFileW(p.target, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    PROCESS_INFORMATION pi;
    HANDLE health;
    if (image == INVALID_HANDLE_VALUE) {
        failure = GetLastError();
        goto done;
    }
    BOOL started = launch(p.target, TRUE, &pi, &health);
    CloseHandle(image);
    if (!started) {
        failure = GetLastError();
        goto done;
    }
    HANDLE handles[] = {pi.hProcess, health};
    DWORD wait = WaitForMultipleObjects(2, handles, FALSE, 30000);
    BOOL healthy = wait == WAIT_OBJECT_0 + 1 && WaitForSingleObject(pi.hProcess, 1200) == WAIT_TIMEOUT;
    CloseHandle(health);
    CloseHandle(pi.hProcess);
    if (!healthy) {
        failure = ERROR_APP_INIT_FAILURE;
        goto done;
    }
    /* Sidecar is installed only after successful startup; the journal carries both signed records. */
    DeleteFileW(p.temp);
    if (!np_write_exact(p.temp, &j.new_release, sizeof(j.new_release)) ||
        !MoveFileExW(p.temp, p.side, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        failure = GetLastError();
        goto done;
    }
    j.phase = 3;
    if (!write_journal(&p, &j)) {
        failure = GetLastError();
        goto done;
    }
#ifdef NP_UPDATE_TEST
    if (!wcscmp(inject, L"committed")) {
        CloseHandle(lock);
        return 92;
    }
#endif
    log_step(&p, L"committed", 0);
    if (DeleteFileW(p.backup))
        DeleteFileW(p.journal);
    else
        log_step(&p, L"committed-cleanup-pending", GetLastError());
done:
    if (failure) {
        log_step(&p, L"failed", failure);
        if (j.magic == 0x3155504e && j.phase && j.phase < 3) {
            BOOL restored = rollback(&p, &j, replaced);
            log_step(&p, restored ? L"rollback-complete" : L"rollback-pending",
                     restored ? 0 : GetLastError());
            if (!restored)
                failure = ERROR_RECOVERY_FAILURE;
        }
    }
    CloseHandle(lock);
    return (int)failure;
}
#ifndef NP_UPDATE_LIBRARY
static BOOL current_sid(WCHAR out[192]) {
    HANDLE token;
    BYTE bytes[512];
    DWORD size = 0;
    PWSTR sid = NULL;
    BOOL ok = FALSE;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return FALSE;
    if (GetTokenInformation(token, TokenUser, bytes, sizeof(bytes), &size) &&
        ConvertSidToStringSidW(((TOKEN_USER *)bytes)->User.Sid, &sid)) {
        ok = wcslen(sid) < 192;
        if (ok)
            wcscpy_s(out, 192, sid);
        LocalFree(sid);
    }
    CloseHandle(token);
    return ok;
}
static BOOL directory_writable(const WCHAR *target) {
    WCHAR dir[NP_PATH];
    if (wcslen(target) >= NP_PATH)
        return FALSE;
    wcscpy_s(dir, NP_PATH, target);
    WCHAR *slash = wcsrchr(dir, L'\\');
    if (!slash)
        return FALSE;
    *slash = 0;
    HANDLE f = CreateFileW(dir, FILE_ADD_FILE | FILE_DELETE_CHILD,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                           FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    if (f == INVALID_HANDLE_VALUE)
        return FALSE;
    BY_HANDLE_FILE_INFORMATION info;
    BOOL ok = GetFileInformationByHandle(f, &info) && !(info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT);
    CloseHandle(f);
    return ok;
}
static BOOL is_admin(void) {
    SID_IDENTIFIER_AUTHORITY auth = SECURITY_NT_AUTHORITY;
    PSID group = NULL;
    BOOL admin = FALSE;
    if (AllocateAndInitializeSid(&auth, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0,
                                 0, 0, &group)) {
        CheckTokenMembership(NULL, group, &admin);
        FreeSid(group);
    }
    return admin;
}
static BOOL elevate_arguments(int n, WCHAR **args, DWORD *result) {
    FILES p;
    NP_RELEASE release;
    NP_ERROR e;
    BYTE hash[32];
    BOOL recovery = n == 3 && !wcscmp(args[1], L"--recover");
    const WCHAR *target = recovery ? args[2] : args[3];
    if (!paths(target, &p)) {
        *result = ERROR_BAD_PATHNAME;
        return FALSE;
    }
    BOOL trusted;
    if (recovery) {
        JOURNAL j;
        trusted =
            np_read_exact(p.journal, &j, sizeof(j)) && j.magic == 0x3155504e &&
            np_release_signature(&j.new_release) &&
            (j.baseline ? !memcmp(j.old_hash, baseline_hash, 32) : np_release_signature(&j.old_release));
    } else
        trusted = np_verify_release(args[2], &release, &e) &&
                  (np_verify_release(target, &release, &e) ||
                   (np_hash_file(target, hash) && !memcmp(hash, baseline_hash, 32)));
    if (!trusted) {
        *result = NP_SIGNATURE_ERROR;
        return FALSE;
    }
    WCHAR self[NP_PATH], parameters[NP_PATH * 2 + 300], sid[192];
    if (!current_sid(sid)) {
        *result = ERROR_ACCESS_DENIED;
        return FALSE;
    }
    GetModuleFileNameW(NULL, self, _countof(self));
    if (recovery)
        swprintf_s(parameters, _countof(parameters), L"--recover \"%s\" --elevated-for %s", p.target, sid);
    else
        swprintf_s(parameters, _countof(parameters), L"--apply \"%s\" \"%s\" --elevated-for %s", args[2],
                   p.target, sid);
    SHELLEXECUTEINFOW se = {0};
    se.cbSize = sizeof(se);
    se.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
    se.lpVerb = L"runas";
    se.lpFile = self;
    se.lpParameters = parameters;
    se.nShow = SW_HIDE;
    if (!ShellExecuteExW(&se)) {
        *result = GetLastError();
        return FALSE;
    }
    WaitForSingleObject(se.hProcess, INFINITE);
    GetExitCodeProcess(se.hProcess, result);
    CloseHandle(se.hProcess);
    return TRUE;
}
int WINAPI wWinMain(HINSTANCE i, HINSTANCE prev, PWSTR cmd, int show) {
    (void)i;
    (void)prev;
    (void)cmd;
    (void)show;
    int n;
    WCHAR **args = CommandLineToArgvW(GetCommandLineW(), &n);
    int result = ERROR_INVALID_PARAMETER;
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (args && (n == 5 || n == 6) && !wcscmp(args[n - 2], L"--elevated-for")) {
        WCHAR sid[192];
        if (!is_admin() || !current_sid(sid) || wcscmp(sid, args[n - 1])) {
            MessageBoxW(NULL,
                        L"更新已取消：请使用当前 Windows 账户批准 "
                        L"UAC。使用其他管理员账户会改变配置和加密授权所属用户，因此未替换任何文件。",
                        L"NetPilot Lite", MB_ICONWARNING);
            LocalFree(args);
            return ERROR_ACCESS_DENIED;
        }
        n -= 2;
    }
    BOOL apply = args && n == 4 && !wcscmp(args[1], L"--apply"),
         recovery = args && n == 3 && !wcscmp(args[1], L"--recover");
    if ((apply || recovery) && !is_admin() && !directory_writable(recovery ? args[2] : args[3])) {
        DWORD code;
        BOOL child = elevate_arguments(n, args, &code);
        LocalFree(args);
        if (!child) {
            WCHAR text[220];
            swprintf_s(text, _countof(text), L"%s\nWindows 错误 %lu。程序未退出，更新文件未替换。",
                       code == ERROR_CANCELLED ? L"管理员权限请求已取消。" : L"更新验证或权限请求失败。",
                       code);
            MessageBoxW(NULL, text, L"NetPilot Lite 更新", MB_OK | MB_ICONWARNING);
        }
        return (int)code;
    }
    if (apply)
        result = np_updater_apply(args[2], args[3], FALSE);
    else if (recovery)
        result = np_updater_apply(NULL, args[2], TRUE);
    if (result) {
        WCHAR text[400];
        swprintf_s(
            text, _countof(text),
            L"更新未完成。Windows 错误 %d "
            L"(0x%08X)。\n旧文件已尽可能恢复；若仍有恢复记录，请保留备份并使用恢复命令。\n详细阶段见原目录 "
            L"NetPilotLite-update.log。",
            result, result);
        MessageBoxW(NULL, text, L"NetPilot Lite 更新", MB_OK | MB_ICONERROR);
    }
    LocalFree(args);
    return result;
}
#endif
