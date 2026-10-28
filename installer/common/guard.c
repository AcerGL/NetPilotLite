/* Fixed-purpose installation guard; no network APIs, shell commands or adapter control. */
#include "update.h"
#include <tlhelp32.h>
#include <stdio.h>
#include <objbase.h>
#define EXPORT __declspec(dllexport) DWORD __stdcall
static BOOL safe_path(const WCHAR *path) {
    WCHAR full[NP_PATH], part[NP_PATH];
    DWORD n = GetFullPathNameW(path, NP_PATH, full, NULL);
    if (!n || n >= NP_PATH || _wcsicmp(path, full) || n < 4 || full[1] != L':' || full[2] != L'\\' ||
        wcschr(full + 2, L':')) return FALSE;
    wcscpy_s(part, NP_PATH, full);
    for (WCHAR *s = part + 3;; ++s) {
        if (*s && *s != L'\\') continue;
        WCHAR saved = *s;
        *s = 0;
        DWORD a = GetFileAttributesW(part);
        *s = saved;
        if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_REPARSE_POINT)) return FALSE;
        if (a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY)) {
            *s = 0;
            HANDLE f = CreateFileW(part, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, NULL);
            *s = saved;
            BY_HANDLE_FILE_INFORMATION i;
            BOOL ok = f != INVALID_HANDLE_VALUE && GetFileInformationByHandle(f, &i) && i.nNumberOfLinks == 1;
            if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
            if (!ok) return FALSE;
        }
        if (!saved) break;
    }
    WCHAR root[] = {full[0], L':', L'\\', 0};
    return GetDriveTypeW(root) == DRIVE_FIXED;
}
static DWORD verify(const WCHAR *path, NP_RELEASE *r) {
    NP_ERROR e = {0};
    if (!safe_path(path)) return ERROR_ACCESS_DENIED;
    if (!np_verify_release(path, r, &e)) return e.win32 ? e.win32 : ERROR_INVALID_DATA;
    return 0;
}
EXPORT NpVerify(const WCHAR *path) { NP_RELEASE r; return verify(path, &r); }
EXPORT NpPath(const WCHAR *path) { return safe_path(path) ? 0 : ERROR_ACCESS_DENIED; }
EXPORT NpEqual(const WCHAR *path, const WCHAR *candidate) {
    NP_RELEASE a, b;
    DWORD e = verify(path, &a);
    if (e) return e;
    if ((e = verify(candidate, &b))) return e;
    return memcmp(&a, &b, sizeof(a)) ? ERROR_CRC : 0;
}
EXPORT NpCompare(const WCHAR *old, const WCHAR *next) {
    NP_RELEASE a, b;
    DWORD e = verify(old, &a);
    if (e) return e;
    if ((e = verify(next, &b))) return e;
    for (int i = 0; i < 4; ++i) {
        if (a.version[i] > b.version[i]) return ERROR_REVISION_MISMATCH;
        if (a.version[i] < b.version[i]) return 0;
    }
    return ERROR_ALREADY_EXISTS; /* explicit same-version repair confirmation */
}
static BOOL CALLBACK quit_window(HWND w, LPARAM pid) {
    DWORD owner;
    WCHAR cls[80];
    GetWindowThreadProcessId(w, &owner);
    GetClassNameW(w, cls, 80);
    if (owner == (DWORD)pid && (!wcscmp(cls, L"NetPilotLiteWindow1") || !wcscmp(cls, L"NetPilotLiteTestWindow1")))
        PostMessageW(w, WM_COMMAND, 106, 0); /* app drains its asynchronous jobs and saves settings */
    return TRUE;
}
EXPORT NpClose(const WCHAR *path) {
    if (!safe_path(path)) return ERROR_ACCESS_DENIED;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return GetLastError();
    PROCESSENTRY32W pe = {sizeof(pe)};
    DWORD result = 0;
    if (Process32FirstW(snap, &pe)) do {
        if (_wcsicmp(pe.szExeFile, L"NetPilotLite.exe")) continue;
        HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pe.th32ProcessID);
        if (!proc) { result = ERROR_ACCESS_DENIED; break; }
        WCHAR image[NP_PATH]; DWORD n = NP_PATH;
        if (!QueryFullProcessImageNameW(proc, 0, image, &n)) result = ERROR_ACCESS_DENIED;
        else if (_wcsicmp(image, path)) result = ERROR_BUSY; /* another location/session must be closed manually */
        else {
            EnumWindows(quit_window, pe.th32ProcessID);
            if (WaitForSingleObject(proc, 20000) != WAIT_OBJECT_0) result = ERROR_BUSY;
        }
        CloseHandle(proc);
        if (result) break;
    } while (Process32NextW(snap, &pe));
    CloseHandle(snap);
    return result;
}
static BOOL paths(const WCHAR *dir, WCHAR exe[NP_PATH], WCHAR side[NP_PATH]) {
    return safe_path(dir) && swprintf_s(exe, NP_PATH, L"%s\\NetPilotLite.exe", dir) > 0 &&
           swprintf_s(side, NP_PATH, L"%s.update", exe) > 0;
}
static BOOL flush_file(const WCHAR *path) {
    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
                              FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_WRITE_THROUGH, NULL);
    if (file == INVALID_HANDLE_VALUE) return FALSE;
    BOOL ok = FlushFileBuffers(file);
    DWORD error = GetLastError();
    CloseHandle(file); SetLastError(error);
    return ok;
}
EXPORT NpJournal(const WCHAR *dir) {
    WCHAR file[NP_PATH];
    if (!safe_path(dir) || swprintf_s(file, NP_PATH, L"%s\\.NetPilotLite-install-pending", dir) < 0) return ERROR_ACCESS_DENIED;
    const char journal[] = "NetPilot Lite installer transaction v1; rerun trusted installer to recover";
    return np_write_exact(file, journal, sizeof(journal)) ? 0 : GetLastError();
}
EXPORT NpBackup(const WCHAR *dir) {
    WCHAR exe[NP_PATH], side[NP_PATH], backup[NP_PATH], out[NP_PATH], sig[NP_PATH];
    NP_RELEASE r;
    if (!paths(dir, exe, side) || !safe_path(exe) || !safe_path(side)) return ERROR_ACCESS_DENIED;
    DWORD e = verify(exe, &r);
    if (e) return e;
    swprintf_s(backup, NP_PATH, L"%s\\.NetPilotLite-install-backup", dir);
    if (!CreateDirectoryW(backup, NULL)) return GetLastError();
    swprintf_s(out, NP_PATH, L"%s\\NetPilotLite.exe", backup);
    swprintf_s(sig, NP_PATH, L"%s.update", out);
    if (!CopyFileW(exe, out, TRUE) || !CopyFileW(side, sig, TRUE)) return GetLastError();
    if (!flush_file(out) || !flush_file(sig)) return GetLastError();
    return verify(out, &r);
}
EXPORT NpRestore(const WCHAR *dir) {
    WCHAR exe[NP_PATH], side[NP_PATH], backup[NP_PATH], sig[NP_PATH], stage[NP_PATH];
    NP_RELEASE r;
    if (!paths(dir, exe, side) || !safe_path(exe) || !safe_path(side)) return ERROR_ACCESS_DENIED;
    swprintf_s(backup, NP_PATH, L"%s\\.NetPilotLite-install-backup\\NetPilotLite.exe", dir);
    swprintf_s(sig, NP_PATH, L"%s.update", backup);
    DWORD e = verify(backup, &r);
    if (e) return e;
    /* Recovery never closes other installations. A live target's image lock makes replacement fail safely. */
    swprintf_s(stage, NP_PATH, L"%s\\.NetPilotLite-install-restore", dir);
    if (!CopyFileW(backup, stage, TRUE)) return GetLastError();
    if (!flush_file(stage)) return GetLastError();
    if (!MoveFileExW(stage, exe, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) ||
        !CopyFileW(sig, side, FALSE)) return GetLastError();
    return verify(exe, &r);
}
EXPORT NpCleanup(const WCHAR *dir) {
    WCHAR backup[NP_PATH], exe[NP_PATH], side[NP_PATH];
    swprintf_s(backup, NP_PATH, L"%s\\.NetPilotLite-install-backup", dir);
    if (!paths(backup, exe, side)) return ERROR_ACCESS_DENIED;
    if (!DeleteFileW(exe) && GetLastError() != ERROR_FILE_NOT_FOUND) return GetLastError();
    if (!DeleteFileW(side) && GetLastError() != ERROR_FILE_NOT_FOUND) return GetLastError();
    if (!RemoveDirectoryW(backup) && GetLastError() != ERROR_PATH_NOT_FOUND && GetLastError() != ERROR_FILE_NOT_FOUND) return GetLastError();
    return 0;
}
EXPORT NpHealth(const WCHAR *path) {
    NP_RELEASE r;
    DWORD e = verify(path, &r);
    if (e) return e;
    HANDLE lock = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (lock == INVALID_HANDLE_VALUE) return GetLastError();
    if ((e = verify(path, &r))) { CloseHandle(lock); return e; }
    GUID id; WCHAR guid[40], eventname[100], args[NP_PATH + 100];
    if (FAILED(CoCreateGuid(&id))) { CloseHandle(lock); return ERROR_GEN_FAILURE; }
    StringFromGUID2(&id, guid, 40);
    swprintf_s(eventname, 100, L"Local\\NetPilotLite.Health.%s", guid);
    HANDLE event = CreateEventW(NULL, TRUE, FALSE, eventname);
    swprintf_s(args, _countof(args), L"\"%s\" --update-health %s", path, guid);
    STARTUPINFOW si = {sizeof(si)}; PROCESS_INFORMATION pi = {0};
    BOOL ok = event && CreateProcessW(path, args, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
    e = ok ? 0 : GetLastError();
    CloseHandle(lock);
    if (ok) {
        HANDLE waits[] = {event, pi.hProcess};
        if (WaitForMultipleObjects(2, waits, FALSE, 30000) != WAIT_OBJECT_0) e = ERROR_TIMEOUT;
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
        DWORD close_error = NpClose(path); /* health probe must release files before commit/rollback */
        if (!e) e = close_error;
    }
    if (event) CloseHandle(event);
    return e;
}
