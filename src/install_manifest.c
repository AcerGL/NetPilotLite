#include "public_setup.h"
#include "json.h"
#include <stdio.h>
BOOL np_install_relative(const WCHAR *s) {
    size_t n = s ? wcslen(s) : 0;
    if (!n || n >= 240 || s[0] == L'/' || s[n - 1] == L'/')
        return FALSE;
    WCHAR part[240];
    size_t k = 0;
    for (size_t i = 0; i <= n; i++) {
        WCHAR c = s[i];
        if (c == L'/' || !c) {
            if (!k || part[0] == L'.' || part[k - 1] == L'.')
                return FALSE;
            part[k] = 0;
            WCHAR *dot = wcschr(part, L'.');
            if (dot)
                *dot = 0;
            if (!_wcsicmp(part, L"CON") || !_wcsicmp(part, L"PRN") || !_wcsicmp(part, L"AUX") ||
                !_wcsicmp(part, L"NUL") ||
                (wcslen(part) == 4 && (!_wcsnicmp(part, L"COM", 3) || !_wcsnicmp(part, L"LPT", 3)) &&
                 part[3] >= L'1' && part[3] <= L'9'))
                return FALSE;
            k = 0;
        } else {
            if (!((c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9') ||
                  wcschr(L"._-", c)))
                return FALSE;
            part[k++] = c;
        }
    }
    return !wcscmp(s, L"NetPilotLite.exe") || !wcscmp(s, L"NetPilotLite.exe.update") ||
           !wcsncmp(s, L"bin/", 4) || !wcsncmp(s, L"dependencies/", 13) || !wcsncmp(s, L"resources/", 10);
}
BOOL np_install_absolute(const WCHAR *path, BOOL missing) {
    WCHAR full[NP_PATH], drive[4];
    DWORD n = GetFullPathNameW(path, NP_PATH, full, NULL);
    if (n < 4 || n >= NP_PATH || _wcsicmp(full, path) || full[1] != L':' || full[2] != L'\\' ||
        wcschr(full + 2, L':') || wcschr(full, L'"') || wcsstr(full, L"\\\\"))
        return FALSE;
    wcsncpy_s(drive, 4, full, 3);
    if (GetDriveTypeW(drive) != DRIVE_FIXED)
        return FALSE;
    for (WCHAR *q = full + 3;; q++)
        if (!*q || *q == L'\\') {
            if (q > full + 3 && (q[-1] == L'.' || q[-1] == L' '))
                return FALSE;
            WCHAR c = *q;
            *q = 0;
            DWORD a = GetFileAttributesW(full), code = GetLastError();
            *q = c;
            if (a == INVALID_FILE_ATTRIBUTES) {
                if (!missing || (code != ERROR_PATH_NOT_FOUND && code != ERROR_FILE_NOT_FOUND))
                    return FALSE;
            } else if (a & FILE_ATTRIBUTE_REPARSE_POINT)
                return FALSE;
            else if (c && !(a & FILE_ATTRIBUTE_DIRECTORY))
                return FALSE;
            else if (!c && !(a & FILE_ATTRIBUTE_DIRECTORY)) {
                HANDLE f = CreateFileW(full, FILE_READ_ATTRIBUTES,
                                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                                       OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, NULL);
                BY_HANDLE_FILE_INFORMATION info;
                BOOL ok = f != INVALID_HANDLE_VALUE && GetFileInformationByHandle(f, &info) &&
                          info.nNumberOfLinks == 1;
                if (f != INVALID_HANDLE_VALUE)
                    CloseHandle(f);
                if (!ok)
                    return FALSE;
            }
            if (!c)
                break;
        }
    return TRUE;
}
BOOL np_install_mkdirs(const WCHAR *path) {
    if (!np_install_absolute(path, TRUE))
        return FALSE;
    WCHAR p[NP_PATH];
    wcscpy_s(p, NP_PATH, path);
    for (WCHAR *q = p + 3;; q++)
        if (!*q || *q == L'\\') {
            WCHAR c = *q;
            *q = 0;
            BOOL ok = CreateDirectoryW(p, NULL) || GetLastError() == ERROR_ALREADY_EXISTS;
            DWORD a = GetFileAttributesW(p);
            *q = c;
            if (!ok || a == INVALID_FILE_ATTRIBUTES || (a & FILE_ATTRIBUTE_REPARSE_POINT) ||
                !(a & FILE_ATTRIBUTE_DIRECTORY))
                return FALSE;
            if (!c)
                break;
        }
    return TRUE;
}
static BOOL hash_parse(const WCHAR *text, BYTE hash[32]) {
    if (wcslen(text) != 64)
        return FALSE;
    for (int i = 0; i < 32; i++) {
        unsigned v = 0;
        for (int k = 0; k < 2; k++) {
            WCHAR c = text[i * 2 + k];
            if (c >= L'0' && c <= L'9')
                v = v * 16 + c - L'0';
            else if (c >= L'a' && c <= L'f')
                v = v * 16 + c - L'a' + 10;
            else
                return FALSE;
        }
        hash[i] = (BYTE)v;
    }
    return TRUE;
}
BOOL np_manifest_parse(const char *text, size_t n, NP_MANIFEST *m, NP_ERROR *e) {
    ZeroMemory(m, sizeof(*m));
    NP_JSON j;
    if (!np_json_parse(&j, text, n))
        goto bad;
    WCHAR arch[20];
    BOOL ok = j.tokens[0].type == '{' &&
              np_json_string(&j, np_json_field(&j, 0, "version"), m->text_version, 64) &&
              m->text_version[0] != L'v' && np_semver(m->text_version, m->version) &&
              np_json_string(&j, np_json_field(&j, 0, "architecture"), arch, 20) && !wcscmp(arch, L"x64");
    int files = np_json_field(&j, 0, "files");
    if (files < 0 || j.tokens[files].type != '[')
        ok = FALSE;
    if (ok)
        for (int i = files + 1; i < j.tokens[files].next; i = j.tokens[i].next) {
            if (m->count == NP_FILE_MAX) {
                ok = FALSE;
                break;
            }
            NP_INSTALL_FILE *f = &m->files[m->count];
            WCHAR sha[80];
            ok = np_json_string(&j, np_json_field(&j, i, "asset_name"), f->asset, 160) &&
                 np_json_string(&j, np_json_field(&j, i, "install_path"), f->path, 240) &&
                 np_install_relative(f->path) &&
                 np_json_string(&j, np_json_field(&j, i, "sha256"), sha, 80) && hash_parse(sha, f->hash) &&
                 np_json_uint(&j, np_json_field(&j, i, "size"), &f->size) && f->size &&
                 f->size <= 64ULL * 1024 * 1024;
            for (WCHAR *c = f->asset; *c; c++)
                if (!((*c >= L'a' && *c <= L'z') || (*c >= L'A' && *c <= L'Z') ||
                      (*c >= L'0' && *c <= L'9') || wcschr(L"._-", *c)))
                    ok = FALSE;
            if (!f->asset[0] || f->asset[0] == L'.')
                ok = FALSE;
            for (unsigned k = 0; k < m->count; k++) {
                const WCHAR *prior = m->files[k].path;
                size_t len = wcslen(prior), len2 = wcslen(f->path);
                if (!_wcsicmp(f->asset, m->files[k].asset) || !_wcsicmp(f->path, prior) ||
                    (len < len2 && !_wcsnicmp(prior, f->path, len) && f->path[len] == L'/') ||
                    (len2 < len && !_wcsnicmp(prior, f->path, len2) && prior[len2] == L'/'))
                    ok = FALSE;
            }
            m->total += f->size;
            if (m->total > 256ULL * 1024 * 1024)
                ok = FALSE;
            m->count++;
            if (!ok)
                break;
        }
    np_json_free(&j);
    if (ok && m->count >= 2 && !wcscmp(m->files[0].path, L"NetPilotLite.exe") &&
        !wcscmp(m->files[1].path, L"NetPilotLite.exe.update")) {
        for (unsigned i = 0; i < m->count; i++)
            for (WCHAR *c = m->files[i].path; *c; c++)
                if (*c == L'/')
                    *c = L'\\';
        return TRUE;
    }
bad:
    np_error(e, ERROR_INVALID_DATA, 0, L"文件清单无效：版本/x64、重复或危险路径、大小、哈希不符合安装规则");
    return FALSE;
}
BOOL np_manifest_load(const WCHAR *dir, const DWORD *expected, NP_MANIFEST *m, NP_ERROR *e) {
    WCHAR path[NP_PATH], side[NP_PATH];
    swprintf_s(path, NP_PATH, L"%s\\manifest.json", dir);
    swprintf_s(side, NP_PATH, L"%s.update", path);
    NP_RELEASE r;
    BYTE hash[32];
    if (!np_install_absolute(path, FALSE) || !np_install_absolute(side, FALSE) ||
        !np_read_exact(side, &r, sizeof(r)) || !np_catalog_signature(&r) || !r.size || r.size > 65536 ||
        (expected && memcmp(expected, r.version, 16))) {
        np_error(e, NP_SIGNATURE_ERROR, 0, L"文件清单可信发布签名无效");
        return FALSE;
    }
    char *text = malloc((size_t)r.size);
    BOOL ok = text && np_read_exact(path, text, (DWORD)r.size) && np_hash(text, (DWORD)r.size, hash) &&
              !memcmp(hash, r.hash, 32);
    if (ok)
        ok = np_manifest_parse(text, (size_t)r.size, m, e) && !memcmp(m->version, r.version, 16);
    free(text);
    if (!ok && !e->win32)
        np_error(e, ERROR_CRC, 0, L"文件清单内容或版本与签名不一致");
    return ok;
}
BOOL np_manifest_verify(const WCHAR *dir, const NP_MANIFEST *m, NP_ERROR *e) {
    for (unsigned i = 0; i < m->count; i++) {
        const NP_INSTALL_FILE *f = &m->files[i];
        WCHAR path[NP_PATH];
        BYTE hash[32];
        WIN32_FILE_ATTRIBUTE_DATA attr;
        swprintf_s(path, NP_PATH, L"%s\\%s", dir, f->path);
        if (!np_install_absolute(path, FALSE) || !GetFileAttributesExW(path, GetFileExInfoStandard, &attr) ||
            (((ULONGLONG)attr.nFileSizeHigh << 32) | attr.nFileSizeLow) != f->size ||
            !np_hash_file(path, hash) || memcmp(hash, f->hash, 32)) {
            np_error(e, ERROR_CRC, 0, L"安装文件大小、SHA-256 或路径验证失败");
            return FALSE;
        }
    }
    WCHAR main[NP_PATH];
    swprintf_s(main, NP_PATH, L"%s\\NetPilotLite.exe", dir);
    NP_RELEASE r;
    return np_verify_release(main, &r, e) && !memcmp(r.version, m->version, 16);
}
static BOOL remove_tree(const WCHAR *dir) {
    if (!np_install_absolute(dir, FALSE))
        return FALSE;
    WCHAR glob[NP_PATH];
    swprintf_s(glob, NP_PATH, L"%s\\*", dir);
    WIN32_FIND_DATAW d;
    HANDLE f = FindFirstFileW(glob, &d);
    BOOL ok = TRUE;
    if (f != INVALID_HANDLE_VALUE) {
        do {
            if (!wcscmp(d.cFileName, L".") || !wcscmp(d.cFileName, L".."))
                continue;
            WCHAR p[NP_PATH];
            swprintf_s(p, NP_PATH, L"%s\\%s", dir, d.cFileName);
            if (d.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
                ok = FALSE;
                continue;
            }
            if (d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                if (!remove_tree(p))
                    ok = FALSE;
            } else if (!DeleteFileW(p))
                ok = FALSE;
        } while (FindNextFileW(f, &d));
        FindClose(f);
    }
    return ok && RemoveDirectoryW(dir);
}
BOOL np_setup_cleanup(const WCHAR *dir) {
    const WCHAR *name = wcsrchr(dir, L'\\');
    if (!name)
        return FALSE;
    if (wcsncmp(name + 1, L"SetupCache-{", 12) && wcscmp(name + 1, L".NetPilotSetup-transaction"))
        return FALSE;
    return remove_tree(dir);
}
