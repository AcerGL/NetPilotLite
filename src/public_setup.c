#include "public_setup.h"
#include "json.h"
#include "settings.h"
#include <objbase.h>
#include <stdio.h>
static BOOL field(const NP_JSON *j, int o, const char *k, WCHAR *s, size_t n) {
    return np_json_string(j, np_json_field(j, o, k), s, n);
}
static BOOL asset_name(const WCHAR *s) {
    size_t n = wcslen(s);
    if (!n || n >= 160 || s[0] == L'.')
        return FALSE;
    for (size_t i = 0; i < n; i++)
        if (!((s[i] >= L'a' && s[i] <= L'z') || (s[i] >= L'A' && s[i] <= L'Z') ||
              (s[i] >= L'0' && s[i] <= L'9') || wcschr(L"._-", s[i])))
            return FALSE;
    return TRUE;
}
BOOL np_public_parse(const char *text, size_t n, const NP_UPDATE_SETTINGS *s, NP_PUBLIC_RELEASE *r,
                     NP_ERROR *e) {
    NP_JSON j;
    ZeroMemory(r, sizeof(*r));
    ZeroMemory(e, sizeof(*e));
    if (!np_repo_valid(s->owner, s->repo) || !np_json_parse(&j, text, n))
        goto bad;
    BOOL ok = j.tokens[0].type == '{' && np_json_false(&j, np_json_field(&j, 0, "draft")) &&
              np_json_false(&j, np_json_field(&j, 0, "prerelease")) &&
              field(&j, 0, "tag_name", r->info.tag, 64) && np_semver(r->info.tag, r->info.version) &&
              r->info.tag[0] == L'v' && field(&j, 0, "published_at", r->info.published, 64);
    if (!field(&j, 0, "name", r->info.name, 256))
        wcscpy_s(r->info.name, 256, r->info.tag);
    int b = np_json_field(&j, 0, "body");
    if (b >= 0 && j.tokens[b].type == '"')
        ok = ok && field(&j, 0, "body", r->info.body, 16384);
    else if (b == -2)
        ok = FALSE;
    int a = np_json_field(&j, 0, "assets");
    if (a < 0 || j.tokens[a].type != '[')
        ok = FALSE;
    if (ok)
        for (int i = a + 1; i < j.tokens[a].next; i = j.tokens[i].next) {
            WCHAR name[160], expected[2048];
            if (!field(&j, i, "name", name, 160) || !asset_name(name) || r->count == NP_ASSET_MAX) {
                ok = FALSE;
                break;
            }
            for (unsigned k = 0; k < r->count; k++)
                if (!_wcsicmp(name, r->assets[k].name))
                    ok = FALSE;
            NP_ASSET *v = &r->assets[r->count++];
            wcscpy_s(v->name, 160, name);
            swprintf_s(expected, 2048, L"https://github.com/%s/%s/releases/download/%s/%s", s->owner, s->repo,
                       r->info.tag, name);
            ok = ok && field(&j, i, "browser_download_url", v->url, 2048) && !wcscmp(v->url, expected) &&
                 np_json_uint(&j, np_json_field(&j, i, "size"), &v->size) && v->size > 0 &&
                 v->size <= 64ULL * 1024 * 1024;
        }
    np_json_free(&j);
    const NP_ASSET *setup = np_public_asset(r, NP_SETUP_NAME),
                   *sig = np_public_asset(r, NP_SETUP_NAME L".update"),
                   *m = np_public_asset(r, NP_MANIFEST_NAME),
                   *ms = np_public_asset(r, NP_MANIFEST_NAME L".update");
    if (ok && setup && sig && m && ms && sig->size == sizeof(NP_RELEASE) && ms->size == sizeof(NP_RELEASE) &&
        m->size <= 65536) {
        r->info.size = setup->size;
        return TRUE;
    }
bad:
    np_error(e, ERROR_INVALID_DATA, 0, L"公开稳定 Release 缺少 Setup、文件清单或可信签名，或元数据无效");
    return FALSE;
}
const NP_ASSET *np_public_asset(const NP_PUBLIC_RELEASE *r, const WCHAR *name) {
    for (unsigned i = 0; i < r->count; i++)
        if (!wcscmp(r->assets[i].name, name))
            return &r->assets[i];
    return NULL;
}
BOOL np_public_check(const NP_UPDATE_SETTINGS *s, const WCHAR *version, NP_PUBLIC_RELEASE *r,
                     volatile LONG *cancel, NP_ERROR *e) {
    WCHAR url[600];
    DWORD v[4];
    ZeroMemory(e, sizeof(*e));
    if (!np_repo_valid(s->owner, s->repo) || (version && (!np_semver(version, v) || version[0] == L'v'))) {
        np_error(e, ERROR_INVALID_PARAMETER, 0, L"仓库或指定版本无效");
        return FALSE;
    }
    if (version)
        swprintf_s(url, 600, L"https://api.github.com/repos/%s/%s/releases/tags/v%s", s->owner, s->repo,
                   version);
    else
        swprintf_s(url, 600, L"https://api.github.com/repos/%s/%s/releases/latest", s->owner, s->repo);
    BYTE *bytes = NULL;
    DWORD n = 0;
    BOOL ok =
        np_public_http(url, FALSE, &bytes, &n, INVALID_HANDLE_VALUE, 1024 * 1024, cancel, NULL, NULL, e);
    if (ok)
        ok = np_public_parse((char *)bytes, n, s, r, e);
    free(bytes);
    if (ok && version && memcmp(v, r->info.version, 16)) {
        np_error(e, ERROR_REVISION_MISMATCH, 0, L"指定版本与 Release 不一致");
        ok = FALSE;
    }
    if (!ok && e->http_status == 404) {
        np_error(e, ERROR_NOT_FOUND, 0,
                 L"公开 Release 不可用：仓库仍私有、没有稳定发布或指定版本不存在；这不表示已是最新版");
        e->http_status = 404;
    }
    return ok;
}
static BOOL fetch_file(const NP_ASSET *a, const WCHAR *path, volatile LONG *cancel, NP_DOWNLOAD_PROGRESS cb,
                       void *ctx, NP_ERROR *e) {
    if (!a) {
        np_error(e, ERROR_NOT_FOUND, 0, L"Release 缺少清单指定的独立文件");
        return FALSE;
    }
    HANDLE f = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_NEW,
                           FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    BOOL ok =
        f != INVALID_HANDLE_VALUE && np_public_http(a->url, TRUE, NULL, NULL, f, a->size, cancel, cb, ctx, e);
    LARGE_INTEGER size;
    if (ok)
        ok = GetFileSizeEx(f, &size) && (ULONGLONG)size.QuadPart == a->size && FlushFileBuffers(f);
    if (f != INVALID_HANDLE_VALUE)
        CloseHandle(f);
    if (!ok) {
        DeleteFileW(path);
        if (!e->win32)
            np_error(e, ERROR_CRC, 0, L"独立文件下载长度或写入验证失败");
    }
    return ok;
}
static BOOL cache(WCHAR dir[NP_PATH]) {
    WCHAR base[NP_PATH], id[40];
    GUID g;
#ifdef NP_READONLY_BUILD
    if (np_http_fixture.active && np_http_fixture.directory[0])
        wcscpy_s(base, NP_PATH, np_http_fixture.directory);
    else
#endif
        if (!np_data_directory(base, NP_PATH))
        return FALSE;
    if (!np_install_absolute(base, FALSE) || FAILED(CoCreateGuid(&g)) || !StringFromGUID2(&g, id, 40))
        return FALSE;
    swprintf_s(dir, NP_PATH, L"%s\\SetupCache-%s", base, id);
    return CreateDirectoryW(dir, NULL);
}
BOOL np_public_download_setup(const NP_PUBLIC_RELEASE *r, WCHAR out[NP_PATH], volatile LONG *cancel,
                              NP_DOWNLOAD_PROGRESS cb, void *ctx, NP_ERROR *e) {
    WCHAR dir[NP_PATH], side[NP_PATH];
    out[0] = 0;
    ZeroMemory(e, sizeof(*e));
    if (!cache(dir)) {
        np_error(e, GetLastError(), 0, L"创建 Setup 下载目录失败");
        return FALSE;
    }
    swprintf_s(out, NP_PATH, L"%s\\%s", dir, NP_SETUP_NAME);
    swprintf_s(side, NP_PATH, L"%s.update", out);
    BOOL ok = fetch_file(np_public_asset(r, NP_SETUP_NAME L".update"), side, cancel, NULL, NULL, e) &&
              fetch_file(np_public_asset(r, NP_SETUP_NAME), out, cancel, cb, ctx, e);
    NP_RELEASE signature;
    if (ok)
        ok = np_verify_setup(out, &signature, e) && !memcmp(signature.version, r->info.version, 16);
    if (!ok) {
        np_setup_cleanup(dir);
        out[0] = 0;
        if (!e->win32)
            np_error(e, ERROR_REVISION_MISMATCH, 0, L"Setup 版本与选择的 Release 不一致");
    }
    return ok;
}
BOOL np_public_launch_setup(const WCHAR *setup, const WCHAR *version, const WCHAR *dir, NP_ERROR *e) {
    NP_RELEASE r;
    DWORD v[4];
    if (!np_semver(version, v) || version[0] == L'v' || !np_install_absolute(dir, FALSE) ||
        !np_verify_setup(setup, &r, e) || memcmp(v, r.version, 16)) {
        if (!e->win32)
            np_error(e, ERROR_INVALID_PARAMETER, 0, L"Setup 更新参数或版本错误");
        return FALSE;
    }
    HANDLE lock = CreateFileW(setup, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                              FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    WCHAR args[NP_PATH * 2 + 160];
    swprintf_s(args, _countof(args), L"\"%s\" --update --target-version %s --install-dir \"%s\"", setup,
               version, dir);
    STARTUPINFOW si = {sizeof(si)};
    PROCESS_INFORMATION pi = {0};
    BOOL ok = lock != INVALID_HANDLE_VALUE && np_verify_setup(setup, &r, e) && !memcmp(v, r.version, 16) &&
              CreateProcessW(setup, args, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
    if (lock != INVALID_HANDLE_VALUE)
        CloseHandle(lock);
    if (ok) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    } else if (!e->win32)
        np_error(e, GetLastError(), 0, L"启动已验证 Setup 更新模式失败");
    return ok;
}
typedef struct TRANSFER {
    NP_SETUP_PROGRESS cb;
    void *ctx;
    const WCHAR *file;
    unsigned done, count;
    ULONGLONG completed, total;
} TRANSFER;
static void transfer(void *arg, ULONGLONG n, ULONGLONG total) {
    (void)total;
    TRANSFER *p = arg;
    if (p->cb)
        p->cb(p->ctx, p->file, p->done, p->count, p->completed + n, p->total);
}
BOOL np_setup_stage(const NP_PUBLIC_RELEASE *r, WCHAR stage[NP_PATH], NP_MANIFEST *m, volatile LONG *cancel,
                    NP_SETUP_PROGRESS cb, void *ctx, NP_ERROR *e) {
    WCHAR path[NP_PATH], side[NP_PATH];
    stage[0] = 0;
    ZeroMemory(e, sizeof(*e));
    ZeroMemory(m, sizeof(*m));
    if (!cache(stage)) {
        np_error(e, GetLastError(), 0, L"无法创建安装暂存目录");
        return FALSE;
    }
    swprintf_s(path, NP_PATH, L"%s\\manifest.json", stage);
    swprintf_s(side, NP_PATH, L"%s.update", path);
    BOOL ok = fetch_file(np_public_asset(r, NP_MANIFEST_NAME L".update"), side, cancel, NULL, NULL, e) &&
              fetch_file(np_public_asset(r, NP_MANIFEST_NAME), path, cancel, NULL, NULL, e) &&
              np_manifest_load(stage, r->info.version, m, e);
    TRANSFER t = {cb, ctx, L"", 0, m->count + 2, 0, m->total};
    const NP_ASSET *setup = np_public_asset(r, NP_SETUP_NAME),
                   *signature = np_public_asset(r, NP_SETUP_NAME L".update");
    if (ok && setup && signature)
        t.total += setup->size + signature->size;
    else
        ok = FALSE;
    for (unsigned i = 0; ok && i < m->count; i++) {
        const NP_INSTALL_FILE *f = &m->files[i];
        const NP_ASSET *a = np_public_asset(r, f->asset);
        if (!a || a->size != f->size) {
            np_error(e, ERROR_INVALID_DATA, 0, L"清单文件与 Release 资产大小不一致");
            ok = FALSE;
            break;
        }
        swprintf_s(path, NP_PATH, L"%s\\%s", stage, f->path);
        WCHAR dir[NP_PATH];
        wcscpy_s(dir, NP_PATH, path);
        *wcsrchr(dir, L'\\') = 0;
        t.file = f->asset;
        t.done = i;
        BYTE hash[32];
        ok = np_install_mkdirs(dir) && fetch_file(a, path, cancel, transfer, &t, e) &&
             np_hash_file(path, hash) && !memcmp(hash, f->hash, 32);
        if (!ok && !e->win32)
            np_error(e, ERROR_CRC, 0, L"暂存文件 SHA-256 或目录校验失败");
        t.completed += f->size;
    }
    if (ok) {
        swprintf_s(path, NP_PATH, L"%s\\RecoverySetup.exe", stage);
        swprintf_s(side, NP_PATH, L"%s.update", path);
        t.file = NP_SETUP_NAME;
        t.done = m->count;
        ok = fetch_file(setup, path, cancel, transfer, &t, e);
        t.completed += setup->size;
        t.file = NP_SETUP_NAME L".update";
        t.done++;
        if (ok)
            ok = fetch_file(signature, side, cancel, transfer, &t, e);
        NP_RELEASE sig;
        if (ok)
            ok = np_verify_setup(path, &sig, e) && !memcmp(sig.version, m->version, 16);
    }
    if (ok)
        ok = np_manifest_verify(stage, m, e);
    if (ok && cb)
        cb(ctx, L"全部文件已验证", t.count, t.count, t.total, t.total);
    if (!ok) {
        np_setup_cleanup(stage);
        stage[0] = 0;
    }
    return ok;
}
