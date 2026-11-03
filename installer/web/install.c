/* Manifest-driven file transaction. No network or adapter modification functions. */
#define COBJMACROS
#include "public_setup.h"
#include <shlobj.h>
#include <objbase.h>
#include <stdio.h>
typedef struct INSTALL_JOURNAL {
    DWORD magic, phase, count, touched;
    BOOL update, existed[NP_FILE_MAX], old_manifest;
    BOOL integration, run_exists, link_exists;
    DWORD run_type, run_size;
    BYTE run_value[4096];
} INSTALL_JOURNAL;
static const WCHAR run_key[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
#ifdef NP_SETUP_TEST
static const WCHAR run_name[] = L"NetPilotLite.SetupTest";
static const WCHAR link_name[] = L"NetPilotLite.SetupTest.lnk";
#else
static const WCHAR run_name[] = L"NetPilotLite";
static const WCHAR link_name[] = L"NetPilot Lite.lnk";
#endif
static BOOL join(WCHAR out[NP_PATH], const WCHAR *dir, const WCHAR *name) {
    return swprintf_s(out, NP_PATH, L"%s\\%s", dir, name) > 0;
}
static BOOL copy(const WCHAR *from, const WCHAR *to) {
    if (!np_install_absolute(from, FALSE) || !np_install_absolute(to, TRUE))
        return FALSE;
    WCHAR dir[NP_PATH];
    wcscpy_s(dir, NP_PATH, to);
    *wcsrchr(dir, L'\\') = 0;
    return np_install_mkdirs(dir) && CopyFileW(from, to, TRUE);
}
static BOOL journal_write(const WCHAR *txn, INSTALL_JOURNAL *j) {
    WCHAR tmp[NP_PATH], p[NP_PATH];
    join(tmp, txn, L"journal.tmp");
    join(p, txn, L"journal.bin");
    if (!np_install_absolute(tmp, TRUE) || !np_install_absolute(p, TRUE))
        return FALSE;
    DeleteFileW(tmp);
    return np_write_exact(tmp, j, sizeof(*j)) &&
           MoveFileExW(tmp, p, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
}
typedef struct CLOSE {
    const WCHAR *path;
    BOOL ok;
} CLOSE;
static BOOL CALLBACK close_window(HWND w, LPARAM lp) {
    CLOSE *c = (CLOSE *)lp;
    WCHAR cls[80], path[NP_PATH];
    DWORD pid, n = NP_PATH;
    GetClassNameW(w, cls, 80);
#ifdef NP_SETUP_TEST
    if (wcscmp(cls, L"NetPilotLiteTestWindow1"))
        return TRUE;
#else
    if (wcscmp(cls, L"NetPilotLiteWindow1"))
        return TRUE;
#endif
    GetWindowThreadProcessId(w, &pid);
    HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid);
    if (!p) {
        c->ok = FALSE;
        return TRUE;
    }
    if (QueryFullProcessImageNameW(p, 0, path, &n) && !_wcsicmp(path, c->path)) {
        DWORD_PTR result;
        if (!SendMessageTimeoutW(w, WM_COMMAND, 106, 0, SMTO_ABORTIFHUNG, 3000, &result) ||
            WaitForSingleObject(p, 20000) != WAIT_OBJECT_0)
            c->ok = FALSE;
    }
    CloseHandle(p);
    return TRUE;
}
static BOOL close_app(const WCHAR *dir) {
    WCHAR p[NP_PATH];
    join(p, dir, L"NetPilotLite.exe");
    CLOSE c = {p, TRUE};
    EnumWindows(close_window, (LPARAM)&c);
    return c.ok;
}
static BOOL start_app(const WCHAR *dir, BOOL health) {
    WCHAR path[NP_PATH], args[NP_PATH + 100], id[40], event_name[100];
    GUID g;
    HANDLE event = NULL;
    join(path, dir, L"NetPilotLite.exe");
    NP_RELEASE r;
    NP_ERROR e = {0};
    HANDLE lock = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                              FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    if (lock == INVALID_HANDLE_VALUE || !np_verify_release(path, &r, &e)) {
        if (lock != INVALID_HANDLE_VALUE)
            CloseHandle(lock);
        return FALSE;
    }
    if (health) {
        CoCreateGuid(&g);
        StringFromGUID2(&g, id, 40);
        swprintf_s(event_name, 100, L"Local\\NetPilotLite.Health.%s", id);
        event = CreateEventW(NULL, TRUE, FALSE, event_name);
        if (!event || GetLastError() == ERROR_ALREADY_EXISTS) {
            if (event)
                CloseHandle(event);
            CloseHandle(lock);
            return FALSE;
        }
        swprintf_s(args, _countof(args), L"\"%s\" --update-health %s", path, id);
    } else
        swprintf_s(args, _countof(args), L"\"%s\"", path);
    STARTUPINFOW si = {sizeof(si)};
    PROCESS_INFORMATION pi = {0};
    BOOL ok = CreateProcessW(path, args, NULL, NULL, FALSE, 0, NULL, dir, &si, &pi);
    CloseHandle(lock);
    if (ok) {
        CloseHandle(pi.hThread);
        if (health) {
            HANDLE waits[] = {pi.hProcess, event};
            ok = WaitForMultipleObjects(2, waits, FALSE, 30000) == WAIT_OBJECT_0 + 1 &&
                 WaitForSingleObject(pi.hProcess, 1200) == WAIT_TIMEOUT;
        }
        CloseHandle(pi.hProcess);
    }
    if (event)
        CloseHandle(event);
    return ok;
}
static BOOL shortcut_path(WCHAR out[NP_PATH]) {
    PWSTR dir = NULL;
    BOOL ok = SUCCEEDED(SHGetKnownFolderPath(&FOLDERID_Programs, KF_FLAG_CREATE, NULL, &dir)) &&
              join(out, dir, link_name);
    CoTaskMemFree(dir);
    return ok;
}
static BOOL save_integration(const WCHAR *txn, INSTALL_JOURNAL *j) {
    HKEY k;
    LONG status = RegOpenKeyExW(HKEY_CURRENT_USER, run_key, 0, KEY_QUERY_VALUE, &k);
    j->run_size = sizeof(j->run_value);
    if (status == ERROR_SUCCESS) {
        status = RegQueryValueExW(k, run_name, NULL, &j->run_type, j->run_value, &j->run_size);
        RegCloseKey(k);
        j->run_exists = status == ERROR_SUCCESS;
    }
    if (status != ERROR_SUCCESS && status != ERROR_FILE_NOT_FOUND)
        return FALSE;
    WCHAR path[NP_PATH], backup[NP_PATH];
    if (!shortcut_path(path) || !join(backup, txn, L"shortcut.previous"))
        return FALSE;
    DWORD attr = GetFileAttributesW(path);
    j->link_exists = attr != INVALID_FILE_ATTRIBUTES;
    return !j->link_exists || copy(path, backup);
}
static BOOL integration(const WCHAR *dir, BOOL autorun) {
    WCHAR exe[NP_PATH], args[NP_PATH + 32], link[NP_PATH];
    join(exe, dir, L"NetPilotLite.exe");
    swprintf_s(args, _countof(args), L"\"%s\" --startup", exe);
    HKEY k;
    LONG code = 0;
    if (autorun) {
        code = RegCreateKeyExW(HKEY_CURRENT_USER, run_key, 0, NULL, 0, KEY_SET_VALUE, NULL, &k, NULL);
        if (code)
            return FALSE;
        code = RegSetValueExW(k, run_name, 0, REG_SZ, (BYTE *)args, (DWORD)(wcslen(args) + 1) * 2);
        RegCloseKey(k);
    }
    if (code || !shortcut_path(link) || !np_install_absolute(link, TRUE))
        return FALSE;
    IShellLinkW *sl = NULL;
    IPersistFile *pf = NULL;
    BOOL ok = SUCCEEDED(
        CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, &IID_IShellLinkW, (void **)&sl));
    if (ok)
        ok = SUCCEEDED(IShellLinkW_SetPath(sl, exe)) && SUCCEEDED(IShellLinkW_SetWorkingDirectory(sl, dir)) &&
             SUCCEEDED(IShellLinkW_SetDescription(sl, L"NetPilot Lite")) &&
             SUCCEEDED(IShellLinkW_QueryInterface(sl, &IID_IPersistFile, (void **)&pf)) &&
             SUCCEEDED(IPersistFile_Save(pf, link, TRUE));
    if (pf)
        IPersistFile_Release(pf);
    if (sl)
        IShellLinkW_Release(sl);
    return ok;
}
static BOOL undo_integration(const WCHAR *txn, const INSTALL_JOURNAL *j) {
    if (!j->integration)
        return TRUE;
    HKEY k;
    LONG c = RegCreateKeyExW(HKEY_CURRENT_USER, run_key, 0, NULL, 0, KEY_SET_VALUE, NULL, &k, NULL);
    if (c)
        return FALSE;
    if (j->run_exists)
        c = RegSetValueExW(k, run_name, 0, j->run_type, j->run_value, j->run_size);
    else {
        c = RegDeleteValueW(k, run_name);
        if (c == ERROR_FILE_NOT_FOUND)
            c = 0;
    }
    RegCloseKey(k);
    WCHAR link[NP_PATH], backup[NP_PATH];
    if (c || !shortcut_path(link) || !np_install_absolute(link, TRUE))
        return FALSE;
    if (!DeleteFileW(link) && GetLastError() != ERROR_FILE_NOT_FOUND)
        return FALSE;
    join(backup, txn, L"shortcut.previous");
    return !j->link_exists || copy(backup, link);
}
static BOOL matches_file(const WCHAR *path, const NP_INSTALL_FILE *f) {
    BYTE hash[32];
    WIN32_FILE_ATTRIBUTE_DATA a;
    return np_install_absolute(path, FALSE) && GetFileAttributesExW(path, GetFileExInfoStandard, &a) &&
           (((ULONGLONG)a.nFileSizeHigh << 32) | a.nFileSizeLow) == f->size && np_hash_file(path, hash) &&
           !memcmp(hash, f->hash, 32);
}
static BOOL restore(const WCHAR *dir, const WCHAR *txn, INSTALL_JOURNAL *j, const NP_MANIFEST *m,
                    NP_ERROR *e) {
    if (!close_app(dir)) {
        np_error(e, ERROR_BUSY, 0, L"新版仍在运行；保留恢复记录，暂不覆盖");
        return FALSE;
    }
    WCHAR old[NP_PATH], p[NP_PATH], source[NP_PATH];
    join(old, txn, L"old");
    if (j->update && j->phase >= 2) {
        NP_RELEASE r;
        join(p, old, L"NetPilotLite.exe");
        if (!np_verify_release(p, &r, e))
            return FALSE;
    }
    for (unsigned i = 0; i < j->touched; i++) {
        join(p, dir, m->files[i].path);
        join(source, old, m->files[i].path);
        if (!np_install_absolute(p, TRUE))
            return FALSE;
        if (j->existed[i]) {
            if (!np_install_absolute(source, FALSE) || !CopyFileW(source, p, FALSE))
                return FALSE;
        } else if (GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES) {
            if (!matches_file(p, &m->files[i]) || !DeleteFileW(p))
                return FALSE;
        }
    }
    if (!undo_integration(txn, j))
        return FALSE;
    /* Previous metadata is never overwritten before health and integration succeed. */
    WCHAR meta[NP_PATH], prior_meta[NP_PATH];
    join(meta, dir, L".NetPilot-install");
    join(prior_meta, txn, L"prior");
    const WCHAR *metadata[] = {L"manifest.json", L"manifest.json.update"};
    for (unsigned k = 0; k < 2; k++) {
        join(p, meta, metadata[k]);
        if (!np_install_absolute(p, TRUE))
            return FALSE;
        if (j->old_manifest) {
            join(source, prior_meta, metadata[k]);
            if (!CopyFileW(source, p, FALSE))
                return FALSE;
        } else if (!DeleteFileW(p) && GetLastError() != ERROR_FILE_NOT_FOUND &&
                   GetLastError() != ERROR_PATH_NOT_FOUND)
            return FALSE;
    }
    if (!j->old_manifest)
        RemoveDirectoryW(meta);
    if (j->update) {
        if (j->old_manifest) {
            NP_MANIFEST *prior = calloc(1, sizeof(*prior));
            BOOL ok =
                prior && np_manifest_load(prior_meta, NULL, prior, e) && np_manifest_verify(dir, prior, e);
            free(prior);
            if (!ok)
                return FALSE;
        }
        if (!start_app(dir, TRUE))
            return FALSE;
    }
    j->phase = 4;
    if (!journal_write(txn, j))
        return FALSE;
    join(p, txn, L"journal.bin");
    DeleteFileW(p);
    np_setup_cleanup(txn);
    return TRUE;
}
BOOL np_setup_recover(const WCHAR *dir, BOOL launch, NP_ERROR *e) {
    (void)launch;
    ZeroMemory(e, sizeof(*e));
    WCHAR txn[NP_PATH], p[NP_PATH];
    join(txn, dir, L".NetPilotSetup-transaction");
    join(p, txn, L"journal.bin");
    INSTALL_JOURNAL j = {0};
    WCHAR lockpath[NP_PATH];
    join(lockpath, dir, L".NetPilotSetup-lock");
    HANDLE lock = np_install_absolute(lockpath, TRUE)
                      ? CreateFileW(lockpath, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_ALWAYS,
                                    FILE_FLAG_OPEN_REPARSE_POINT, NULL)
                      : INVALID_HANDLE_VALUE;
    if (lock == INVALID_HANDLE_VALUE) {
        np_error(e, ERROR_BUSY, 0, L"另一个 Setup 正在安装或恢复；请等待完成");
        return FALSE;
    }
    NP_MANIFEST *m = calloc(1, sizeof(*m));
    BOOL ok = m && np_install_absolute(dir, FALSE) && np_read_exact(p, &j, sizeof(j)) &&
              j.magic == 0x3153504e && j.phase >= 1 && j.phase <= 4 && j.count <= NP_FILE_MAX &&
              j.touched <= j.count && j.run_size <= sizeof(j.run_value) &&
              np_manifest_load(txn, NULL, m, e) && m->count == j.count;
    if (ok && j.phase == 3) {
        ok = np_manifest_verify(dir, m, e);
        if (ok) {
            DeleteFileW(p);
            np_setup_cleanup(txn);
            ok = start_app(dir, FALSE);
        }
    } else if (ok)
        ok = restore(dir, txn, &j, m, e);
    CloseHandle(lock);
    free(m);
    if (!ok && !e->win32)
        np_error(e, ERROR_RECOVERY_FAILURE, 0, L"恢复未完成；保留事务备份，不运行未验证文件");
    return ok;
}
BOOL np_setup_install(const WCHAR *stage, const WCHAR *dir, BOOL update, BOOL integrate, NP_ERROR *e) {
    ZeroMemory(e, sizeof(*e));
    NP_MANIFEST *m = calloc(1, sizeof(*m)), *prior = calloc(1, sizeof(*prior));
    INSTALL_JOURNAL j = {0};
    WCHAR txn[NP_PATH] = L"", newdir[NP_PATH], olddir[NP_PATH], p[NP_PATH], q[NP_PATH], main[NP_PATH],
          lockpath[NP_PATH];
    HANDLE lock = INVALID_HANDLE_VALUE;
    BOOL ok = FALSE, have_journal = FALSE, owned_txn = FALSE;
    if (!m || !prior || !np_install_absolute(stage, FALSE) || !np_install_absolute(dir, TRUE) ||
        wcslen(dir) < 8 || !np_manifest_load(stage, NULL, m, e) || !np_manifest_verify(stage, m, e))
        goto done;
    NP_RELEASE recovery;
    join(p, stage, L"RecoverySetup.exe");
    if (!np_verify_setup(p, &recovery, e) || memcmp(recovery.version, m->version, 16))
        goto done;
    if (!np_install_mkdirs(dir))
        goto done;
    join(txn, dir, L".NetPilotSetup-transaction");
    join(lockpath, dir, L".NetPilotSetup-lock");
    if (!np_install_absolute(lockpath, TRUE))
        goto done;
    lock = CreateFileW(lockpath, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_ALWAYS,
                       FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    if (lock == INVALID_HANDLE_VALUE)
        goto done;
    join(p, txn, L"journal.bin");
    if (GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES) {
        np_error(e, ERROR_RECOVERY_FAILURE, 0, L"先恢复上一次未完成的 Setup 事务，再重试更新");
        goto done;
    }
    if (GetFileAttributesW(txn) != INVALID_FILE_ATTRIBUTES && !np_setup_cleanup(txn))
        goto done;
    join(main, dir, L"NetPilotLite.exe");
    BOOL exists = GetFileAttributesW(main) != INVALID_FILE_ATTRIBUTES;
    NP_RELEASE oldmain;
    if (update != exists) {
        np_error(e, ERROR_INVALID_PARAMETER, 0, L"安装模式与目标目录状态不一致；请选择原安装目录");
        goto done;
    }
    if (update &&
        (!np_verify_release(main, &oldmain, e) || np_version_compare(m->version, oldmain.version) <= 0)) {
        if (!e->win32)
            np_error(e, ERROR_REVISION_MISMATCH, 0, L"目标版本必须高于已安装版本");
        goto done;
    }
    /* Previously installed managed files are signed; unknown collisions are not overwritten. */
    WCHAR installed_meta[NP_PATH];
    join(installed_meta, dir, L".NetPilot-install");
    BOOL prior_present = GetFileAttributesW(installed_meta) != INVALID_FILE_ATTRIBUTES;
    if (prior_present &&
        (!np_manifest_load(installed_meta, NULL, prior, e) || !np_manifest_verify(dir, prior, e)))
        goto done;
    if (!np_install_mkdirs(txn))
        goto done;
    owned_txn = TRUE;
    join(newdir, txn, L"new");
    join(olddir, txn, L"old");
    if (!np_install_mkdirs(newdir) || !np_install_mkdirs(olddir))
        goto done;
    const WCHAR *fixed[] = {L"manifest.json", L"manifest.json.update", L"RecoverySetup.exe",
                            L"RecoverySetup.exe.update"};
    for (unsigned i = 0; i < 4; i++) {
        join(p, stage, fixed[i]);
        join(q, txn, fixed[i]);
        if (!copy(p, q))
            goto done;
    }
    j.magic = 0x3153504e;
    j.phase = 1;
    j.count = m->count;
    j.update = update;
    j.old_manifest = prior_present;
    if (prior_present) {
        WCHAR dest[NP_PATH];
        join(dest, txn, L"prior");
        if (!np_install_mkdirs(dest))
            goto done;
        for (unsigned k = 0; k < 2; k++) {
            join(p, installed_meta, fixed[k]);
            join(q, dest, fixed[k]);
            if (!copy(p, q))
                goto done;
        }
    }
    for (unsigned i = 0; i < m->count; i++) {
        join(p, stage, m->files[i].path);
        join(q, newdir, m->files[i].path);
        if (!copy(p, q) || !matches_file(q, &m->files[i]))
            goto done;
        join(p, dir, m->files[i].path);
        if (!np_install_absolute(p, TRUE))
            goto done;
        j.existed[i] = GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES;
        if (j.existed[i] && i >= 2) {
            BOOL managed = FALSE;
            for (unsigned k = 0; k < prior->count; k++)
                if (!_wcsicmp(prior->files[k].path, m->files[i].path) && matches_file(p, &prior->files[k]))
                    managed = TRUE;
            if (!managed) {
                np_error(e, ERROR_ALREADY_EXISTS, 0,
                         L"目标存在不属于旧签名清单的同名文件；保留用户文件并停止");
                goto done;
            }
        }
    }
    if (integrate && !save_integration(txn, &j))
        goto done;
    if (!journal_write(txn, &j))
        goto done;
    have_journal = TRUE;
#ifdef NP_SETUP_TEST
    WCHAR fault[40] = L"";
    GetEnvironmentVariableW(L"NP_SETUP_TEST_FAULT", fault, 40);
    if (!wcscmp(fault, L"prepared")) {
        free(m);
        free(prior);
        CloseHandle(lock);
        np_error(e, ERROR_PROCESS_ABORTED, 0, L"Injected prepared interruption");
        return FALSE;
    }
#endif
    if (!close_app(dir)) {
        np_error(e, ERROR_BUSY, 0, L"NetPilot 尚未正常退出；未替换程序文件");
        goto done;
    }
    for (unsigned i = 0; i < m->count; i++)
        if (j.existed[i]) {
            join(p, dir, m->files[i].path);
            join(q, olddir, m->files[i].path);
            if (!copy(p, q))
                goto done;
        }
    j.phase = 2;
    if (!journal_write(txn, &j))
        goto done;
    for (unsigned i = 0; i < m->count; i++) {
        j.touched = i + 1;
        if (!journal_write(txn, &j))
            goto done;
        join(p, newdir, m->files[i].path);
        join(q, dir, m->files[i].path);
        WCHAR parent[NP_PATH];
        wcscpy_s(parent, NP_PATH, q);
        *wcsrchr(parent, L'\\') = 0;
        if (!np_install_mkdirs(parent) || !np_install_absolute(q, TRUE) ||
            !MoveFileExW(p, q, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            goto done;
#ifdef NP_SETUP_TEST
        if (i == 0 && !wcscmp(fault, L"replaced")) {
            free(m);
            free(prior);
            CloseHandle(lock);
            np_error(e, ERROR_PROCESS_ABORTED, 0, L"Injected replacement interruption");
            return FALSE;
        }
#endif
    }
    if (!np_manifest_verify(dir, m, e) || !start_app(dir, TRUE)) {
        if (!e->win32)
            np_error(e, ERROR_APP_INIT_FAILURE, 0, L"新版启动确认失败，正在恢复旧版");
        goto done;
    }
    if (integrate) {
        j.integration = TRUE;
        if (!journal_write(txn, &j) || !integration(dir, !update || j.run_exists))
            goto done;
    }
    /* Preserve old signed metadata until new startup has been acknowledged. */
    if (!np_install_mkdirs(installed_meta))
        goto done;
    for (unsigned k = 0; k < 2; k++) {
        join(p, txn, fixed[k]);
        join(q, installed_meta, fixed[k]);
        if (!np_install_absolute(q, TRUE) || !CopyFileW(p, q, FALSE))
            goto done;
    }
    j.phase = 3;
    if (!journal_write(txn, &j))
        goto done;
    ok = TRUE;
done:
    if (!ok) {
        if (!e->win32)
            np_error(e, GetLastError() ? GetLastError() : ERROR_INSTALL_FAILURE, 0,
                     L"安装事务失败，正在回滚");
        if (have_journal) {
            NP_ERROR recover = {0};
            if (!restore(dir, txn, &j, m, &recover))
                np_error(e, ERROR_RECOVERY_FAILURE, 0, L"回滚尚未完成，保留原目录恢复记录");
        } else if (owned_txn)
            np_setup_cleanup(txn);
    } else {
        join(p, txn, L"journal.bin");
        DeleteFileW(p);
        np_setup_cleanup(txn);
    }
    if (lock != INVALID_HANDLE_VALUE)
        CloseHandle(lock);
    free(m);
    free(prior);
    return ok;
}
